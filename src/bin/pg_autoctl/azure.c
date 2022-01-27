/*
 * src/bin/pg_autoctl/azure.c
 *     Implementation of a CLI which lets you call `az` cli commands to prepare
 *     a pg_auto_failover demo or QA environment.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <termios.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "postgres_fe.h"
#include "pqexpbuffer.h"
#include "snprintf.h"

#include "azure.h"
#include "cli_common.h"
#include "cli_do_root.h"
#include "cli_root.h"
#include "commandline.h"
#include "config.h"
#include "env_utils.h"
#include "log.h"
#include "parsing.h"
#include "pgsql.h"
#include "pidfile.h"
#include "signals.h"
#include "string_utils.h"

#include "runprogram.h"

char azureCLI[MAXPGPATH] = { 0 };

static int azure_run_command(Program *program);
static pid_t azure_start_command(Program *program);
static bool azure_wait_for_commands(int count, pid_t pidArray[]);

static bool run_ssh(const char *username, const char *ip);

static bool run_ssh_command(const char *username,
							const char *ip,
							bool tty,
							const char *command);

static bool start_ssh_command(const char *username,
							  const char *ip,
							  const char *command);

static bool azure_git_toplevel(char *srcDir, size_t size);

static bool start_rsync_command(const char *username,
								const char *ip,
								const char *srcDir);

static bool azure_rsync_vms(AzureRegionResources *azRegion);

static bool azure_fetch_resource_list(const char *group,
									  AzureRegionResources *azRegion);

static bool azure_fetch_vm_addresses(const char *group, const char *vm,
									 AzureVMipAddresses *addresses);


/* log_program_output logs the output of the given program. */
static void
log_program_output(Program *prog, int outLogLevel, int errorLogLevel)
{
	if (prog->stdOut != NULL)
	{
		char *outLines[BUFSIZE];
		int lineCount = splitLines(prog->stdOut, outLines, BUFSIZE);
		int lineNumber = 0;

		for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
		{
			log_level(outLogLevel, "%s", outLines[lineNumber]);
		}
	}

	if (prog->stdErr != NULL)
	{
		char *errorLines[BUFSIZE];
		int lineCount = splitLines(prog->stdErr, errorLines, BUFSIZE);
		int lineNumber = 0;

		for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
		{
			log_level(errorLogLevel, "%s", errorLines[lineNumber]);
		}
	}
}


/*
 * azure_run_command runs a command line using the azure CLI command, and when
 * dryRun is true instead of running the command it only shows the command it
 * would run as the output of the pg_autoctl command.
 */
static int
azure_run_command(Program *program)
{
	int returnCode;
	char command[BUFSIZE] = { 0 };

	(void) snprintf_program_command_line(program, command, sizeof(command));

	if (dryRun)
	{
		appendPQExpBuffer(azureScript, "\n%s", command);

		/* fake successful execution */
		return 0;
	}

	log_debug("%s", command);

	(void) execute_subprogram(program);

	returnCode = program->returnCode;

	if (returnCode != 0)
	{
		(void) log_program_output(program, LOG_INFO, LOG_ERROR);
	}

	free_program(program);

	return returnCode;
}


/*
 * azure_start_command starts a command in the background, as a subprocess of
 * the current process, and returns the sub-process pid as soon as the
 * sub-process is started. It's the responsibility of the caller to then
 * implement waitpid() on the returned pid.
 *
 * This allows running several commands in parallel, as in the shell sequence:
 *
 *   $ az vm create &
 *   $ az vm create &
 *   $ az vm create &
 *   $ wait
 */
static pid_t
azure_start_command(Program *program)
{
	pid_t fpid;
	char command[BUFSIZE] = { 0 };

	(void) snprintf_program_command_line(program, command, sizeof(command));

	if (dryRun)
	{
		appendPQExpBuffer(azureScript, "\n%s &", command);

		/* fake successful execution */
		return 0;
	}

	log_debug("%s", command);

	/* Flush stdio channels just before fork, to avoid double-output problems */
	fflush(stdout);
	fflush(stderr);

	/* time to create the node_active sub-process */
	fpid = fork();

	switch (fpid)
	{
		case -1:
		{
			log_error("Failed to fork a process for command: %s", command);
			return -1;
		}

		case 0:
		{
			/* child process runs the command */
			int returnCode;

			/* initialize the semaphore used for locking log output */
			if (!semaphore_init(&log_semaphore))
			{
				exit(EXIT_CODE_INTERNAL_ERROR);
			}

			/* set our logging facility to use our semaphore as a lock */
			(void) log_set_udata(&log_semaphore);
			(void) log_set_lock(&semaphore_log_lock_function);

			(void) execute_subprogram(program);
			returnCode = program->returnCode;

			log_debug("Command %s exited with return code %d",
					  program->args[0],
					  returnCode);

			if (returnCode != 0)
			{
				(void) log_program_output(program, LOG_INFO, LOG_ERROR);
				free_program(program);

				/* the parent will have to use exit status */
				(void) semaphore_finish(&log_semaphore);
				exit(EXIT_CODE_INTERNAL_ERROR);
			}

			free_program(program);
			(void) semaphore_finish(&log_semaphore);
			exit(EXIT_CODE_QUIT);
		}

		default:
		{
			/* fork succeeded, in parent */
			return fpid;
		}
	}
}


/*
 * azure_wait_for_commands waits until all processes with pids from the array
 * are done.
 */
static bool
azure_wait_for_commands(int count, pid_t pidArray[])
{
	int subprocessCount = count;
	bool allReturnCodeAreZero = true;

	while (subprocessCount > 0)
	{
		pid_t pid;
		int status;

		/* ignore errors */
		pid = waitpid(-1, &status, WNOHANG);

		switch (pid)
		{
			case -1:
			{
				if (errno == ECHILD)
				{
					/* no more childrens */
					return subprocessCount == 0;
				}

				pg_usleep(100 * 1000); /* 100 ms */
				break;
			}

			case 0:
			{
				/*
				 * We're using WNOHANG, 0 means there are no stopped or
				 * exited children, it's all good. It's the expected case
				 * when everything is running smoothly, so enjoy and sleep
				 * for awhile.
				 */
				pg_usleep(100 * 1000); /* 100 ms */
				break;
			}

			default:
			{
				/*
				 * One of the az vm create sub-commands has finished, find
				 * which and if it went all okay.
				 */
				int returnCode = WEXITSTATUS(status);

				/* find which VM is done now */
				for (int index = 0; index < count; index++)
				{
					if (pidArray[index] == pid)
					{
						if (returnCode == 0)
						{
							log_debug("Process %d exited successfully",
									  pid);
						}
						else
						{
							log_error("Process %d exited with code %d",
									  pid, returnCode);

							allReturnCodeAreZero = false;
						}
					}
				}

				--subprocessCount;
				break;
			}
		}
	}

	return allReturnCodeAreZero;
}


/*
 * azure_psleep runs count parallel sleep process at the same time.
 */
bool
azure_psleep(int count, bool force)
{
	char sleep[MAXPGPATH] = { 0 };
	pid_t pidArray[26] = { 0 };

	bool saveDryRun = dryRun;

	if (!search_path_first("sleep", sleep, LOG_ERROR))
	{
		log_fatal("Failed to find program sleep in PATH");
		return false;
	}

	if (force)
	{
		dryRun = false;
	}

	for (int i = 0; i < count; i++)
	{
		char *args[3];
		int argsIndex = 0;

		Program program = { 0 };

		args[argsIndex++] = sleep;
		args[argsIndex++] = "5";
		args[argsIndex++] = NULL;

		(void) initialize_program(&program, args, false);

		pidArray[i] = azure_start_command(&program);
	}

	if (force)
	{
		dryRun = saveDryRun;
	}

	if (!azure_wait_for_commands(count, pidArray))
	{
		log_fatal("Failed to sleep concurrently with %d processes", count);
		return false;
	}

	return true;
}


/*
 * azure_get_remote_ip gets the local IP address by using the command `curl
 * ifconfig.me`
 */
bool
azure_get_remote_ip(char *ipAddress, size_t ipAddressSize)
{
	Program program;
	char curl[MAXPGPATH] = { 0 };

	if (!search_path_first("curl", curl, LOG_ERROR))
	{
		log_fatal("Failed to find program curl in PATH");
		return false;
	}

	program = run_program(curl, "ifconfig.me", NULL);

	if (program.returnCode != 0)
	{
		(void) log_program_output(&program, LOG_INFO, LOG_ERROR);
		free_program(&program);
		return false;
	}
	else
	{
		/* we expect a single line of output, no end-of-line */
		strlcpy(ipAddress, program.stdOut, ipAddressSize);
		free_program(&program);

		return true;
	}
}


/*
 * azure_create_group creates a new resource group on Azure.
 */
bool
azure_create_group(const char *name, const char *location)
{
	char *args[16];
	int argsIndex = 0;

	Program program = { 0 };

	args[argsIndex++] = azureCLI;
	args[argsIndex++] = "group";
	args[argsIndex++] = "create";
	args[argsIndex++] = "--name";
	args[argsIndex++] = (char *) name;
	args[argsIndex++] = "--location";
	args[argsIndex++] = (char *) location;
	args[argsIndex++] = NULL;

	(void) initialize_program(&program, args, false);

	log_info("Creating group \"%s\" in location \"%s\"", name, location);

	return azure_run_command(&program) == 0;
}


/*
 * azure_create_vnet creates a new vnet on Azure.
 */
bool
azure_create_vnet(const char *group, const char *name, const char *prefix)
{
	char *args[16];
	int argsIndex = 0;

	Program program = { 0 };

	args[argsIndex++] = azureCLI;
	args[argsIndex++] = "network";
	args[argsIndex++] = "vnet";
	args[argsIndex++] = "create";
	args[argsIndex++] = "--resource-group";
	args[argsIndex++] = (char *) group;
	args[argsIndex++] = "--name";
	args[argsIndex++] = (char *) name;
	args[argsIndex++] = "--address-prefix";
	args[argsIndex++] = (char *) prefix;
	args[argsIndex++] = NULL;

	(void) initialize_program(&program, args, false);

	log_info("Creating network vnet \"%s\" using address prefix \"%s\"",
			 name, prefix);

	return azure_run_command(&program) == 0;
}


/*
 * azure_create_vnet creates a new vnet on Azure.
 */
bool
azure_create_nsg(const char *group, const char *name)
{
	char *args[16];
	int argsIndex = 0;

	Program program = { 0 };

	args[argsIndex++] = azureCLI;
	args[argsIndex++] = "network";
	args[argsIndex++] = "nsg";
	args[argsIndex++] = "create";
	args[argsIndex++] = "--resource-group";
	args[argsIndex++] = (char *) group;
	args[argsIndex++] = "--name";
	args[argsIndex++] = (char *) name;
	args[argsIndex++] = NULL;

	(void) initialize_program(&program, args, false);

	log_info("Creating network nsg \"%s\"", name);

	return azure_run_command(&program) == 0;
}


/*
 * azure_create_vnet creates a new network security rule.
 */
bool
azure_create_nsg_rule(const char *group,
					  const char *nsgName,
					  const char *name,
					  const char *ipAddress)
{
	char *args[38];
	int argsIndex = 0;

	Program program = { 0 };

	args[argsIndex++] = azureCLI;
	args[argsIndex++] = "network";
	args[argsIndex++] = "nsg";
	args[argsIndex++] = "rule";
	args[argsIndex++] = "create";
	args[argsIndex++] = "--resource-group";
	args[argsIndex++] = (char *) group;
	args[argsIndex++] = "--nsg-name";
	args[argsIndex++] = (char *) nsgName;
	args[argsIndex++] = "--name";
	args[argsIndex++] = (char *) name;
	args[argsIndex++] = "--access";
	args[argsIndex++] = "allow";
	args[argsIndex++] = "--protocol";
	args[argsIndex++] = "Tcp";
	args[argsIndex++] = "--direction";
	args[argsIndex++] = "Inbound";
	args[argsIndex++] = "--priority";
	args[argsIndex++] = "100";
	args[argsIndex++] = "--source-address-prefixes";
	args[argsIndex++] = (char *) ipAddress;
	args[argsIndex++] = "--source-port-range";
	args[argsIndex++] = dryRun ? "\"*\"" : "*";
	args[argsIndex++] = "--destination-address-prefix";
	args[argsIndex++] = dryRun ? "\"*\"" : "*";
	args[argsIndex++] = "--destination-port-ranges";
	args[argsIndex++] = "22";
	args[argsIndex++] = "5432";
	args[argsIndex++] = NULL;

	(void) initialize_program(&program, args, false);

	log_info("Creating network nsg rules \"%s\" for our IP address \"%s\" "
			 "for ports 22 and 5432", name, ipAddress);

	return azure_run_command(&program) == 0;
}


/*
 * azure_create_subnet creates a new subnet on Azure.
 */
bool
azure_create_subnet(const char *group,
					const char *vnet,
					const char *name,
					const char *prefixes,
					const char *nsg)
{
	char *args[16];
	int argsIndex = 0;

	Program program = { 0 };

	args[argsIndex++] = azureCLI;
	args[argsIndex++] = "network";
	args[argsIndex++] = "vnet";
	args[argsIndex++] = "subnet";
	args[argsIndex++] = "create";
	args[argsIndex++] = "--resource-group";
	args[argsIndex++] = (char *) group;
	args[argsIndex++] = "--vnet-name";
	args[argsIndex++] = (char *) vnet;
	args[argsIndex++] = "--name";
	args[argsIndex++] = (char *) name;
	args[argsIndex++] = "--address-prefixes";
	args[argsIndex++] = (char *) prefixes;
	args[argsIndex++] = "--network-security-group";
	args[argsIndex++] = (char *) nsg;
	args[argsIndex++] = NULL;

	(void) initialize_program(&program, args, false);

	log_info("Creating network subnet \"%s\" using address prefix \"%s\"",
			 name, prefixes);

	return azure_run_command(&program) == 0;
}


/*
 * az_group_delete runs the command az group delete.
 */
bool
az_group_delete(const char *group)
{
	char *args[16];
	int argsIndex = 0;

	Program program = { 0 };

	args[argsIndex++] = azureCLI;
	args[argsIndex++] = "group";
	args[argsIndex++] = "delete";
	args[argsIndex++] = "--name";
	args[argsIndex++] = (char *) group;
	args[argsIndex++] = "--yes";
	args[argsIndex++] = NULL;

	(void) initialize_program(&program, args, false);

	log_info("Deleting azure resource group \"%s\"", group);

	return azure_run_command(&program) == 0;
}


/*
 * azure_prepare_node_name is a utility function that prepares a node name to
 * use for a VM in our pg_auto_failover deployment in a target Azure region.
 *
 * In the resource group "ha-demo-dim-paris" when creating a monitor (index 0),
 * an app VM (index 27), and 2 pg nodes VMs we would have the following names:
 *
 *   -  [0] ha-demo-dim-paris-monitor
 *   -  [1] ha-demo-dim-paris-a
 *   -  [2] ha-demo-dim-paris-b
 *   - [27] ha-demo-dim-paris-app
 */
static void
azure_prepare_node(AzureRegionResources *azRegion, int index)
{
	char vmsuffix[] = "abcdefghijklmnopqrstuvwxyz";

	if (index == 0)
	{
		sformat(azRegion->vmArray[index].name,
				sizeof(azRegion->vmArray[index].name),
				"%s-monitor",
				azRegion->group);
	}
	else if (index == MAX_VMS_PER_REGION - 1)
	{
		sformat(azRegion->vmArray[index].name,
				sizeof(azRegion->vmArray[index].name),
				"%s-app",
				azRegion->group);
	}
	else
	{
		sformat(azRegion->vmArray[index].name,
				sizeof(azRegion->vmArray[index].name),
				"%s-%c",
				azRegion->group,
				vmsuffix[index - 1]);
	}
}


/*
 * azure_node_index_from_name is the complement to azure_prepare_node.
 * Given a VM name such as ha-demo-dim-paris-monitor or ha-demo-dim-paris-a,
 * the function returns respectively 0 and 1, which is the array index where we
 * want to find information about the VM (name, IP addresses, etc) in an array
 * of VMs.
 */
static int
azure_node_index_from_name(const char *group, const char *name)
{
	int groupNameLen = strlen(group);
	char *ptr;

	if (strncmp(name, group, groupNameLen) != 0 ||
		strlen(name) < (groupNameLen + 1))
	{
		log_error("VM name \"%s\" does not start with group name \"%s\"",
				  name, group);
		return -1;
	}

	/* skip group name and dash: ha-demo-dim-paris- */
	ptr = (char *) name + groupNameLen + 1;

	/*
	 * ha-demo-dim-paris-monitor is always index 0
	 * ha-demo-dim-paris-app     is always index 27 (last in the array)
	 * ha-demo-dim-paris-a       is index 1
	 * ha-demo-dim-paris-b       is index 2
	 * ...
	 * ha-demo-dim-paris-z       is index 26
	 */
	if (strcmp(ptr, "monitor") == 0)
	{
		return 0;
	}
	else if (strcmp(ptr, "app") == 0)
	{
		return MAX_VMS_PER_REGION - 1;
	}
	else
	{
		if (strlen(ptr) != 1)
		{
			log_error("Failed to parse VM index from name \"%s\"", name);
			return -1;
		}

		/* 'a' is 1, 'b' is 2, etc */
		return *ptr - 'a' + 1;
	}
}


/*
 * azure_create_vm creates a Virtual Machine in our azure resource group.
 */
bool
azure_create_vm(AzureRegionResources *azRegion,
				const char *name,
				const char *image,
				const char *username)
{
	char *args[26];
	int argsIndex = 0;

	Program program = { 0 };

	char publicIpAddressName[BUFSIZE] = { 0 };

	sformat(publicIpAddressName, BUFSIZE, "%s-ip", name);

	args[argsIndex++] = azureCLI;
	args[argsIndex++] = "vm";
	args[argsIndex++] = "create";
	args[argsIndex++] = "--resource-group";
	args[argsIndex++] = (char *) azRegion->group;
	args[argsIndex++] = "--name";
	args[argsIndex++] = (char *) name;
	args[argsIndex++] = "--vnet-name";
	args[argsIndex++] = (char *) azRegion->vnet;
	args[argsIndex++] = "--subnet";
	args[argsIndex++] = (char *) azRegion->subnet;
	args[argsIndex++] = "--nsg";
	args[argsIndex++] = (char *) azRegion->nsg;
	args[argsIndex++] = "--public-ip-address";
	args[argsIndex++] = (char *) publicIpAddressName;
	args[argsIndex++] = "--image";
	args[argsIndex++] = (char *) image;
	args[argsIndex++] = "--admin-username";
	args[argsIndex++] = (char *) username;
	args[argsIndex++] = "--generate-ssh-keys";
	args[argsIndex++] = NULL;

	(void) initialize_program(&program, args, false);

	log_info("Creating %s virtual machine \"%s\" with user \"%s\"",
			 image, name, username);

	return azure_start_command(&program);
}


/*
 * azure_create_vms creates several azure virtual machine in parallel and waits
 * until all the commands have finished.
 */
bool
azure_create_vms(AzureRegionResources *azRegion,
				 const char *image,
				 const char *username)
{
	int pending = 0;
	pid_t pidArray[MAX_VMS_PER_REGION] = { 0 };

	/* we read from left to right, have the smaller number on the left */
	if (26 < azRegion->nodes)
	{
		log_error("pg_autoctl only supports up to 26 VMs per region");
		return false;
	}

	log_info("Creating Virtual Machines for %s%d Postgres nodes, in parallel",
			 azRegion->monitor ? "a monitor and " : " ",
			 azRegion->nodes);

	/* index == 0 for the monitor, then 1..count for the other nodes */
	for (int index = 0; index <= azRegion->nodes; index++)
	{
		/* skip index 0 when we're not creating a monitor */
		if (index == 0 && !azRegion->monitor)
		{
			continue;
		}

		/* skip VMs that already exist, unless --script is used */
		if (!dryRun &&
			!IS_EMPTY_STRING_BUFFER(azRegion->vmArray[index].name) &&
			!IS_EMPTY_STRING_BUFFER(azRegion->vmArray[index].public) &&
			!IS_EMPTY_STRING_BUFFER(azRegion->vmArray[index].private))
		{
			log_info("Skipping creation of VM \"%s\", "
					 "which already exists with public IP address %s",
					 azRegion->vmArray[index].name,
					 azRegion->vmArray[index].public);
			continue;
		}

		(void) azure_prepare_node(azRegion, index);

		pidArray[index] = azure_create_vm(azRegion,
										  azRegion->vmArray[index].name,
										  image,
										  username);
		++pending;
	}

	/* also create the application node VM when asked to */
	if (azRegion->appNodes > 0)
	{
		int index = MAX_VMS_PER_REGION - 1;

		if (!dryRun &&
			!IS_EMPTY_STRING_BUFFER(azRegion->vmArray[index].name) &&
			!IS_EMPTY_STRING_BUFFER(azRegion->vmArray[index].public) &&
			!IS_EMPTY_STRING_BUFFER(azRegion->vmArray[index].private))
		{
			log_info("Skipping creation of VM \"%s\", "
					 "which already exists with public IP address %s",
					 azRegion->vmArray[index].name,
					 azRegion->vmArray[index].public);
		}
		else
		{
			(void) azure_prepare_node(azRegion, index);

			pidArray[index] = azure_create_vm(azRegion,
											  azRegion->vmArray[index].name,
											  image,
											  username);
			++pending;
		}
	}

	/* now wait for the child processes to be done */
	if (dryRun && pending > 0)
	{
		appendPQExpBuffer(azureScript, "\nwait");
	}
	else
	{
		if (!azure_wait_for_commands(pending, pidArray))
		{
			log_fatal("Failed to create all %d azure VMs, "
					  "see above for details",
					  pending);
			return false;
		}
	}

	return true;
}


/*
 * azure_git_toplevel calls `git rev-parse --show-toplevel` and uses the result
 * as the directory to rsync to our VMs when provisionning from sources.
 */
static bool
azure_git_toplevel(char *srcDir, size_t size)
{
	Program program;
	char git[MAXPGPATH] = { 0 };

	if (!search_path_first("git", git, LOG_ERROR))
	{
		log_fatal("Failed to find program git in PATH");
		return false;
	}

	program = run_program(git, "rev-parse", "--show-toplevel", NULL);

	if (program.returnCode != 0)
	{
		(void) log_program_output(&program, LOG_INFO, LOG_ERROR);
		free_program(&program);
		return false;
	}
	else
	{
		char *outLines[BUFSIZE];

		/* git rev-parse --show-toplevel outputs a single line */
		splitLines(program.stdOut, outLines, BUFSIZE);
		strlcpy(srcDir, outLines[0], size);

		free_program(&program);

		return true;
	}
}


/*
 * start_rsync_command is used to sync our local source directory with a remote
 * place on a target VM.
 */
static bool
start_rsync_command(const char *username,
					const char *ip,
					const char *srcDir)
{
	char *args[16];
	int argsIndex = 0;

	Program program = { 0 };

	char ssh[MAXPGPATH] = { 0 };
	char essh[MAXPGPATH] = { 0 };
	char rsync[MAXPGPATH] = { 0 };
	char sourceDir[MAXPGPATH] = { 0 };
	char rsync_remote[MAXPGPATH] = { 0 };

	if (!search_path_first("rsync", rsync, LOG_ERROR))
	{
		log_fatal("Failed to find program rsync in PATH");
		return false;
	}

	if (!search_path_first("ssh", ssh, LOG_ERROR))
	{
		log_fatal("Failed to find program ssh in PATH");
		return false;
	}

	/* use our usual ssh options even when using it through rsync */
	sformat(essh, sizeof(essh),
			"%s -o '%s' -o '%s' -o '%s'",
			ssh,
			"StrictHostKeyChecking=no",
			"UserKnownHostsFile /dev/null",
			"LogLevel=quiet");

	/* we need the rsync remote as one string */
	sformat(rsync_remote, sizeof(rsync_remote),
			"%s@%s:/home/%s/pg_auto_failover/",
			username, ip, username);

	/* we need to ensure that the source directory terminates with a "/" */
	if (strcmp(strrchr(srcDir, '/'), "/") != 0)
	{
		sformat(sourceDir, sizeof(sourceDir), "%s/", srcDir);
	}
	else
	{
		strlcpy(sourceDir, srcDir, sizeof(sourceDir));
	}

	args[argsIndex++] = rsync;
	args[argsIndex++] = "-a";
	args[argsIndex++] = "-e";
	args[argsIndex++] = essh;
	args[argsIndex++] = "--exclude='.git'";
	args[argsIndex++] = "--exclude='*.o'";
	args[argsIndex++] = "--exclude='*.deps'";
	args[argsIndex++] = "--exclude='./src/bin/pg_autoctl/pg_autoctl'";
	args[argsIndex++] = sourceDir;
	args[argsIndex++] = rsync_remote;
	args[argsIndex++] = NULL;

	(void) initialize_program(&program, args, false);

	return azure_start_command(&program);
}


/*
 * azure_rsync_vms runs the rsync command for target VMs in parallel.
 */
static bool
azure_rsync_vms(AzureRegionResources *azRegion)
{
	int pending = 0;
	pid_t pidArray[MAX_VMS_PER_REGION] = { 0 };

	char srcDir[MAXPGPATH] = { 0 };

	if (!azure_git_toplevel(srcDir, sizeof(srcDir)))
	{
		/* errors have already been logged */
		return false;
	}

	log_info("Syncing local directory \"%s\" to %d Azure VMs",
			 srcDir,
			 azRegion->nodes +
			 azRegion->monitor +
			 azRegion->appNodes);

	/* index == 0 for the monitor, then 1..count for the other nodes */
	for (int index = 0; index <= azRegion->nodes; index++)
	{
		/* skip index 0 when we're not creating a monitor */
		if (index == 0 && !azRegion->monitor)
		{
			continue;
		}

		(void) azure_prepare_node(azRegion, index);

		pidArray[index] =
			start_rsync_command("ha-admin",
								azRegion->vmArray[index].public,
								srcDir);

		++pending;
	}

	/* also provision the application node VM when asked to */
	if (azRegion->appNodes > 0)
	{
		int index = MAX_VMS_PER_REGION - 1;

		(void) azure_prepare_node(azRegion, index);

		pidArray[index] =
			start_rsync_command("ha-admin",
								azRegion->vmArray[index].public,
								srcDir);

		++pending;
	}

	/* now wait for the child processes to be done */
	if (dryRun)
	{
		appendPQExpBuffer(azureScript, "\nwait");
	}
	else
	{
		if (!azure_wait_for_commands(pending, pidArray))
		{
			log_fatal("Failed to provision all %d azure VMs, "
					  "see above for details",
					  pending);
			return false;
		}
	}

	return true;
}


/*
 * azure_build_pg_autoctl runs `make all` then `make install` on all the target
 * VMs in parallel, using an ssh command line.
 */
static bool
azure_build_pg_autoctl(AzureRegionResources *azRegion)
{
	int pending = 0;
	pid_t pidArray[MAX_VMS_PER_REGION] = { 0 };

	char *buildCommand =
		"make PG_CONFIG=/usr/lib/postgresql/11/bin/pg_config "
		"-C pg_auto_failover -s clean all "
		" && "
		"sudo make PG_CONFIG=/usr/lib/postgresql/11/bin/pg_config "
		"BINDIR=/usr/local/bin -C pg_auto_failover install";

	log_info("Building pg_auto_failover from sources on %d Azure VMs",
			 azRegion->nodes +
			 azRegion->monitor +
			 azRegion->appNodes);

	log_info("%s", buildCommand);

	/* index == 0 for the monitor, then 1..count for the other nodes */
	for (int index = 0; index <= azRegion->nodes; index++)
	{
		/* skip index 0 when we're not creating a monitor */
		if (index == 0 && !azRegion->monitor)
		{
			continue;
		}

		(void) azure_prepare_node(azRegion, index);

		pidArray[index] =
			start_ssh_command("ha-admin",
							  azRegion->vmArray[index].public,
							  buildCommand);
		++pending;
	}

	/* also provision the application node VM when asked to */
	if (azRegion->appNodes > 0)
	{
		int index = MAX_VMS_PER_REGION - 1;

		(void) azure_prepare_node(azRegion, index);

		pidArray[index] =
			start_ssh_command("ha-admin",
							  azRegion->vmArray[index].public,
							  buildCommand);
		++pending;
	}

	/* now wait for the child processes to be done */
	if (dryRun)
	{
		appendPQExpBuffer(azureScript, "\nwait");
	}
	else
	{
		if (!azure_wait_for_commands(pending, pidArray))
		{
			log_fatal("Failed to provision all %d azure VMs, "
					  "see above for details",
					  pending);
			return false;
		}
	}

	return true;
}


/*
 * azure_prepare_target_versions prepares the environment variables that we
 * need to grasp for provisioning our target Azure VMs. We use the following
 * environment variables:
 *
 *   AZ_PG_VERSION ?= 13
 *   AZ_PGAF_DEB_VERSION ?= 1.5
 *   AZ_PGAF_DEB_REVISION ?= 1.5.2-1
 */
bool
azure_prepare_target_versions(KeyVal *env)
{
	char *keywords[] = {
		"AZ_PG_VERSION", "AZ_PGAF_DEB_VERSION", "AZ_PGAF_DEB_REVISION"
	};

	/* set our static set of 3 variables from the environment */
	env->count = 3;

	/* default values */
	sformat(env->values[0], MAXCONNINFO, "13");   /* AZ_PG_VERSION */
	sformat(env->values[1], MAXCONNINFO, "1.6"); /* AZ_PGAF_DEB_VERSION */
	sformat(env->values[2], MAXCONNINFO, "1.6.4-1"); /* AZ_PGAF_DEB_REVISION */

	for (int i = 0; i < 3; i++)
	{
		/* install the environment variable name as the keyword */
		strlcpy(env->keywords[i], keywords[i], MAXCONNINFO);

		/* pick values from the environment when they exist */
		if (env_exists(env->keywords[i]))
		{
			if (!get_env_copy(env->keywords[i], env->values[i], MAXCONNINFO))
			{
				/* errors have already been logged */
				return false;
			}
		}
	}

	return true;
}


/*
 * azure_prepare_debian_command prepares the debian command to install our
 * target pg_auto_failover package on the Azure VMs.
 *
 *   sudo apt-get install -q -y                 \
 *      postgresql-13-auto-failover-1.5=1.5.2-1 \
 *      pg-auto-failover-cli-1.5=1.5.2-1
 *
 * We are using environment variables to fill in the actual version numbers,
 * and we hard-code some defaults in case the environment has not be provided
 * for.
 */
static bool
azure_prepare_debian_install_command(char *command, size_t size)
{
	/* re-use our generic data structure from Postgres URI parsing */
	KeyVal env = { 0 };

	if (!azure_prepare_target_versions(&env))
	{
		/* errors have already been logged */
		return false;
	}

	sformat(command, size,
			"sudo apt-get install -q -y "
			" postgresql-%s-auto-failover-%s=%s"
			" pg-auto-failover-cli-%s=%s",
			env.values[0],      /* AZ_PG_VERSION */
			env.values[1],      /* AZ_PGAF_DEB_VERSION */
			env.values[2],      /* AZ_PGAF_DEB_REVISION */
			env.values[1],      /* AZ_PGAF_DEB_VERSION */
			env.values[2]);     /* AZ_PGAF_DEB_REVISION */

	return true;
}


/*
 * azure_prepare_debian_install_postgres_command prepares the debian command to
 * install our target Postgres version when building from sources.
 *
 *   sudo apt-get build-dep -q -y postgresql-11
 */
static bool
azure_prepare_debian_install_postgres_command(char *command, size_t size)
{
	/* re-use our generic data structure from Postgres URI parsing */
	KeyVal env = { 0 };

	if (!azure_prepare_target_versions(&env))
	{
		/* errors have already been logged */
		return false;
	}

	sformat(command, size,
			"sudo apt-get build-dep -q -y postgresql-%s",

	        /* AZ_PG_VERSION */
			env.values[0]);

	return true;
}


/*
 * azure_prepare_debian_build_dep_postgres_command_command prepares the debian
 * command to install our target Postgres version when building from sources.
 *
 * As we don't have deb-src for pg_auto_failover packages, we do the list
 * manually, and we add also rsync to be able to push sources from the local
 * git repository.
 *
 *   sudo apt-get install -q -y \
 *      postgresql-server-dev-all libkrb5-dev postgresql-11 rsync
 */
static bool
azure_prepare_debian_build_dep_postgres_command(char *command, size_t size)
{
	/* re-use our generic data structure from Postgres URI parsing */
	KeyVal env = { 0 };

	if (!azure_prepare_target_versions(&env))
	{
		/* errors have already been logged */
		return false;
	}

	sformat(command, size,
			"sudo apt-get install -q -y "
			"postgresql-server-dev-all "
			"postgresql-%s "
			"libkrb5-dev "
			"rsync ",

	        /* AZ_PG_VERSION */
			env.values[0]);

	return true;
}


/*
 * azure_provision_vm runs the command `az vm run-command invoke` with our
 * provisioning script.
 */
bool
azure_provision_vm(const char *group, const char *name, bool fromSource)
{
	char *args[26];
	int argsIndex = 0;

	Program program = { 0 };

	char aptGetInstall[BUFSIZE] = { 0 };
	char aptGetInstallPostgres[BUFSIZE] = { 0 };
	char aptGetBuildDepPostgres[BUFSIZE] = { 0 };

	const char *scriptsFromPackage[] =
	{
		"curl https://install.citusdata.com/community/deb.sh | sudo bash",
		"sudo apt-get install -q -y postgresql-common",
		"echo 'create_main_cluster = false' "
		"| sudo tee -a /etc/postgresql-common/createcluster.conf",
		aptGetInstall,
		"sudo usermod -a -G postgres ha-admin",
		NULL
	};

	const char *scriptsFromSource[] =
	{
		"curl https://install.citusdata.com/community/deb.sh | sudo bash",
		"sudo apt-get install -q -y postgresql-common",
		"echo 'create_main_cluster = false' "
		"| sudo tee -a /etc/postgresql-common/createcluster.conf",
		aptGetInstallPostgres,
		aptGetBuildDepPostgres,
		"sudo usermod -a -G postgres ha-admin",
		NULL
	};

	char **scripts =
		fromSource ? (char **) scriptsFromSource : (char **) scriptsFromPackage;

	char *quotedScripts[10][BUFSIZE] = { 0 };

	if (!azure_prepare_debian_install_command(aptGetInstall, BUFSIZE))
	{
		/* errors have already been logged */
		return false;
	}

	if (!azure_prepare_debian_install_postgres_command(aptGetInstallPostgres,
													   BUFSIZE))
	{
		/* errors have already been logged */
		return false;
	}

	if (!azure_prepare_debian_build_dep_postgres_command(aptGetBuildDepPostgres,
														 BUFSIZE))
	{
		/* errors have already been logged */
		return false;
	}

	args[argsIndex++] = azureCLI;
	args[argsIndex++] = "vm";
	args[argsIndex++] = "run-command";
	args[argsIndex++] = "invoke";
	args[argsIndex++] = "--resource-group";
	args[argsIndex++] = (char *) group;
	args[argsIndex++] = "--name";
	args[argsIndex++] = (char *) name;
	args[argsIndex++] = "--command-id";
	args[argsIndex++] = "RunShellScript";
	args[argsIndex++] = "--scripts";

	if (dryRun)
	{
		for (int i = 0; scripts[i] != NULL; i++)
		{
			sformat((char *) quotedScripts[i], BUFSIZE, "\"%s\"", scripts[i]);
			args[argsIndex++] = (char *) quotedScripts[i];
		}
	}
	else
	{
		for (int i = 0; scripts[i] != NULL; i++)
		{
			args[argsIndex++] = (char *) scripts[i];
		}
	}

	args[argsIndex++] = NULL;

	(void) initialize_program(&program, args, false);

	log_info("Provisioning Virtual Machine \"%s\"", name);

	return azure_start_command(&program);
}


/*
 * azure_provision_vms provisions several azure virtual machine in parallel and
 * waits until all the commands have finished.
 */
bool
azure_provision_vms(AzureRegionResources *azRegion, bool fromSource)
{
	int pending = 0;
	pid_t pidArray[MAX_VMS_PER_REGION] = { 0 };

	char aptGetInstall[BUFSIZE] = { 0 };

	/* we read from left to right, have the smaller number on the left */
	if (26 < azRegion->nodes)
	{
		log_error("pg_autoctl only supports up to 26 VMs per region");
		return false;
	}

	log_info("Provisioning %d Virtual Machines in parallel",
			 azRegion->nodes +
			 azRegion->monitor +
			 azRegion->appNodes);

	if (!azure_prepare_debian_install_command(aptGetInstall, BUFSIZE))
	{
		/* errors have already been logged */
		return false;
	}

	log_info("Using: %s", aptGetInstall);

	/* index == 0 for the monitor, then 1..count for the other nodes */
	for (int index = 0; index <= azRegion->nodes; index++)
	{
		/* skip index 0 when we're not creating a monitor */
		if (index == 0 && azRegion->monitor == 0)
		{
			continue;
		}

		(void) azure_prepare_node(azRegion, index);

		pidArray[index] = azure_provision_vm(azRegion->group,
											 azRegion->vmArray[index].name,
											 fromSource);

		++pending;
	}

	/* also provision the application node VM when asked to */
	if (azRegion->appNodes > 0)
	{
		int index = MAX_VMS_PER_REGION - 1;

		(void) azure_prepare_node(azRegion, index);

		pidArray[index] = azure_provision_vm(azRegion->group,
											 azRegion->vmArray[index].name,
											 fromSource);
		++pending;
	}

	/* now wait for the child processes to be done */
	if (dryRun)
	{
		appendPQExpBuffer(azureScript, "\nwait");
	}
	else
	{
		if (!azure_wait_for_commands(pending, pidArray))
		{
			log_fatal("Failed to provision all %d azure VMs, "
					  "see above for details",
					  pending);
			return false;
		}
	}

	return true;
}


/*
 * azure_resource_list runs the command azure resource list.
 *
 *  az resource list --output table --query  "[?resourceGroup=='ha-demo-dim-paris'].{ name: name, flavor: kind, resourceType: type, region: location }"
 */
bool
azure_resource_list(const char *group)
{
	char *args[16];
	int argsIndex = 0;
	bool success = true;

	Program program = { 0 };

	char query[BUFSIZE] = { 0 };

	char command[BUFSIZE] = { 0 };

	sformat(query, BUFSIZE,
			"[?resourceGroup=='%s']"
			".{ name: name, flavor: kind, resourceType: type, region: location }",
			group);

	args[argsIndex++] = azureCLI;
	args[argsIndex++] = "resource";
	args[argsIndex++] = "list";
	args[argsIndex++] = "--output";
	args[argsIndex++] = "table";
	args[argsIndex++] = "--query";
	args[argsIndex++] = (char *) query;
	args[argsIndex++] = NULL;

	(void) initialize_program(&program, args, false);

	(void) snprintf_program_command_line(&program, command, sizeof(command));

	log_info("%s", command);

	(void) execute_subprogram(&program);
	success = program.returnCode == 0;

	if (success)
	{
		fformat(stdout, "%s", program.stdOut);
	}
	else
	{
		(void) log_program_output(&program, LOG_INFO, LOG_ERROR);
	}
	free_program(&program);

	return success;
}


/*
 * azure_fetch_resource_list fetches existing resource names for a short list
 * of known objects in a target azure resource group.
 */
static bool
azure_fetch_resource_list(const char *group, AzureRegionResources *azRegion)
{
	char *args[16];
	int argsIndex = 0;
	bool success = true;

	Program program = { 0 };

	char query[BUFSIZE] = { 0 };

	char command[BUFSIZE] = { 0 };

	sformat(query, BUFSIZE,
			"[?resourceGroup=='%s'].{ name: name, resourceType: type }",
			group);

	args[argsIndex++] = azureCLI;
	args[argsIndex++] = "resource";
	args[argsIndex++] = "list";
	args[argsIndex++] = "--output";
	args[argsIndex++] = "json";
	args[argsIndex++] = "--query";
	args[argsIndex++] = (char *) query;
	args[argsIndex++] = NULL;

	(void) initialize_program(&program, args, false);

	(void) snprintf_program_command_line(&program, command, sizeof(command));

	log_info("Fetching resources that might already exist from a previous run");
	log_info("%s", command);

	(void) execute_subprogram(&program);
	success = program.returnCode == 0;

	if (success)
	{
		/* parson insists on having fresh heap allocated memory, apparently */
		char *jsonString = strdup(program.stdOut);
		JSON_Value *js = json_parse_string(jsonString);
		JSON_Array *jsArray = json_value_get_array(js);
		int count = json_array_get_count(jsArray);

		if (js == NULL)
		{
			log_error("Failed to parse JSON string: %s", program.stdOut);
			return false;
		}

		log_info("Found %d Azure resources already created in group \"%s\"",
				 count, group);

		for (int index = 0; index < count; index++)
		{
			JSON_Object *jsObj = json_array_get_object(jsArray, index);

			char *name = (char *) json_object_get_string(jsObj, "name");
			char *type = (char *) json_object_get_string(jsObj, "resourceType");

			if (streq(type, "Microsoft.Network/virtualNetworks"))
			{
				strlcpy(azRegion->vnet, name, sizeof(azRegion->vnet));

				log_info("Found existing vnet \"%s\"", azRegion->vnet);
			}
			else if (streq(type, "Microsoft.Network/networkSecurityGroups"))
			{
				strlcpy(azRegion->nsg, name, sizeof(azRegion->nsg));

				log_info("Found existing nsg \"%s\"", azRegion->nsg);
			}
			else if (streq(type, "Microsoft.Compute/virtualMachines"))
			{
				int index = azure_node_index_from_name(group, name);

				strlcpy(azRegion->vmArray[index].name, name, NAMEDATALEN);

				log_info("Found existing VM \"%s\"", name);
			}
			else
			{
				/* ignore the resource Type listed */
				log_debug("Unknown resource type: \"%s\" with name \"%s\"",
						  type, name);
			}
		}

		free(jsonString);
	}
	else
	{
		(void) log_program_output(&program, LOG_INFO, LOG_ERROR);
	}
	free_program(&program);

	return success;
}


/*
 * azure_show_ip_addresses shows public and private IP addresses for our list
 * of nodes created in a specific resource group.
 *
 *   az vm list-ip-addresses -g ha-demo-dim-paris --query '[] [] . { name: virtualMachine.name, "public address": virtualMachine.network.publicIpAddresses[0].ipAddress, "private address": virtualMachine.network.privateIpAddresses[0] }' -o table
 */
bool
azure_show_ip_addresses(const char *group)
{
	char *args[16];
	int argsIndex = 0;
	bool success = true;

	Program program = { 0 };

	char query[BUFSIZE] = { 0 };

	char command[BUFSIZE] = { 0 };

	sformat(query, BUFSIZE,
			"[] [] . { name: virtualMachine.name, "
			"\"public address\": "
			"virtualMachine.network.publicIpAddresses[0].ipAddress, "
			"\"private address\": "
			"virtualMachine.network.privateIpAddresses[0] }");

	args[argsIndex++] = azureCLI;
	args[argsIndex++] = "vm";
	args[argsIndex++] = "list-ip-addresses";
	args[argsIndex++] = "--resource-group";
	args[argsIndex++] = (char *) group;
	args[argsIndex++] = "--query";
	args[argsIndex++] = (char *) query;
	args[argsIndex++] = "-o";
	args[argsIndex++] = "table";
	args[argsIndex++] = NULL;

	(void) initialize_program(&program, args, false);

	(void) snprintf_program_command_line(&program, command, sizeof(command));

	log_info("%s", command);

	(void) execute_subprogram(&program);
	success = program.returnCode == 0;

	if (success)
	{
		fformat(stdout, "%s", program.stdOut);
	}
	else
	{
		(void) log_program_output(&program, LOG_INFO, LOG_ERROR);
	}
	free_program(&program);

	return success;
}


/*
 * azure_fetch_ip_addresses fetches IP address (both public and private) for
 * VMs created in an Azure resource group, and fill-in the given array.
 */
bool
azure_fetch_ip_addresses(const char *group, AzureVMipAddresses *vmArray)
{
	char *args[16];
	int argsIndex = 0;
	bool success = true;

	Program program = { 0 };

	char query[BUFSIZE] = { 0 };

	char command[BUFSIZE] = { 0 };

	sformat(query, BUFSIZE,
			"[] [] . { name: virtualMachine.name, "
			"\"public address\": "
			"virtualMachine.network.publicIpAddresses[0].ipAddress, "
			"\"private address\": "
			"virtualMachine.network.privateIpAddresses[0] }");

	args[argsIndex++] = azureCLI;
	args[argsIndex++] = "vm";
	args[argsIndex++] = "list-ip-addresses";
	args[argsIndex++] = "--resource-group";
	args[argsIndex++] = (char *) group;
	args[argsIndex++] = "--query";
	args[argsIndex++] = (char *) query;
	args[argsIndex++] = "-o";
	args[argsIndex++] = "json";
	args[argsIndex++] = NULL;

	(void) initialize_program(&program, args, false);

	(void) snprintf_program_command_line(&program, command, sizeof(command));

	if (dryRun)
	{
		appendPQExpBuffer(azureScript, "\n%s", command);

		return true;
	}

	log_info("%s", command);

	(void) execute_subprogram(&program);
	success = program.returnCode == 0;

	if (success)
	{
		JSON_Value *js = json_parse_string(program.stdOut);
		JSON_Array *jsArray = json_value_get_array(js);
		int count = json_array_get_count(jsArray);

		for (int index = 0; index < count; index++)
		{
			JSON_Object *jsObj = json_array_get_object(jsArray, index);
			char *str = NULL;
			int vmIndex = -1;

			str = (char *) json_object_get_string(jsObj, "name");

			vmIndex = azure_node_index_from_name(group, str);

			if (vmIndex == -1)
			{
				/* errors have already been logged */
				return false;
			}

			strlcpy(vmArray[vmIndex].name, str, NAMEDATALEN);

			str = (char *) json_object_get_string(jsObj, "private address");
			strlcpy(vmArray[vmIndex].private, str, BUFSIZE);

			str = (char *) json_object_get_string(jsObj, "public address");
			strlcpy(vmArray[vmIndex].public, str, BUFSIZE);

			log_debug(
				"Parsed VM %d as \"%s\" with public IP %s and private IP %s",
				vmIndex,
				vmArray[vmIndex].name,
				vmArray[vmIndex].public,
				vmArray[vmIndex].private);
		}
	}
	else
	{
		(void) log_program_output(&program, LOG_INFO, LOG_ERROR);
	}
	free_program(&program);

	return success;
}


/*
 * run_ssh runs the ssh command to the specified IP address as the given
 * username, sharing the current terminal tty.
 */
static bool
run_ssh(const char *username, const char *ip)
{
	char *args[16];
	int argsIndex = 0;

	Program program = { 0 };

	char ssh[MAXPGPATH] = { 0 };
	char command[BUFSIZE] = { 0 };

	if (!search_path_first("ssh", ssh, LOG_ERROR))
	{
		log_fatal("Failed to find program ssh in PATH");
		return false;
	}

	args[argsIndex++] = ssh;
	args[argsIndex++] = "-o";
	args[argsIndex++] = "StrictHostKeyChecking=no";
	args[argsIndex++] = "-o";
	args[argsIndex++] = "UserKnownHostsFile /dev/null";
	args[argsIndex++] = "-o";
	args[argsIndex++] = "LogLevel=quiet";
	args[argsIndex++] = "-l";
	args[argsIndex++] = (char *) username;
	args[argsIndex++] = (char *) ip;
	args[argsIndex++] = NULL;

	(void) initialize_program(&program, args, false);

	program.capture = false;    /* don't capture output */
	program.tty = true;         /* allow sharing the parent's tty */

	(void) snprintf_program_command_line(&program, command, sizeof(command));

	log_info("%s", command);

	(void) execute_subprogram(&program);

	return true;
}


/*
 * run_ssh_command runs the given command on the remote machine given by ip
 * address, as the given username.
 */
static bool
run_ssh_command(const char *username,
				const char *ip,
				bool tty,
				const char *command)
{
	char *args[16];
	int argsIndex = 0;

	Program program = { 0 };

	char ssh[MAXPGPATH] = { 0 };
	char ssh_command[BUFSIZE] = { 0 };

	if (!search_path_first("ssh", ssh, LOG_ERROR))
	{
		log_fatal("Failed to find program ssh in PATH");
		return false;
	}

	args[argsIndex++] = ssh;

	if (tty)
	{
		args[argsIndex++] = "-t";
	}

	args[argsIndex++] = "-o";
	args[argsIndex++] = "StrictHostKeyChecking=no";
	args[argsIndex++] = "-o";
	args[argsIndex++] = "UserKnownHostsFile /dev/null";
	args[argsIndex++] = "-o";
	args[argsIndex++] = "LogLevel=quiet";
	args[argsIndex++] = "-l";
	args[argsIndex++] = (char *) username;
	args[argsIndex++] = (char *) ip;
	args[argsIndex++] = "--";
	args[argsIndex++] = (char *) command;
	args[argsIndex++] = NULL;

	(void) initialize_program(&program, args, false);

	program.capture = false;    /* don't capture output */
	program.tty = true;         /* allow sharing the parent's tty */

	(void) snprintf_program_command_line(&program, ssh_command, BUFSIZE);

	if (dryRun)
	{
		appendPQExpBuffer(azureScript, "\n%s", ssh_command);

		return true;
	}

	log_info("%s", ssh_command);

	(void) execute_subprogram(&program);

	return true;
}


/*
 * start_ssh_command starts the given command on the remote machine given by ip
 * address, as the given username.
 */
static bool
start_ssh_command(const char *username,
				  const char *ip,
				  const char *command)
{
	char *args[16];
	int argsIndex = 0;

	Program program = { 0 };

	char ssh[MAXPGPATH] = { 0 };
	char ssh_command[BUFSIZE] = { 0 };

	if (!search_path_first("ssh", ssh, LOG_ERROR))
	{
		log_fatal("Failed to find program ssh in PATH");
		return false;
	}

	args[argsIndex++] = ssh;
	args[argsIndex++] = "-o";
	args[argsIndex++] = "StrictHostKeyChecking=no";
	args[argsIndex++] = "-o";
	args[argsIndex++] = "UserKnownHostsFile /dev/null";
	args[argsIndex++] = "-o";
	args[argsIndex++] = "LogLevel=quiet";
	args[argsIndex++] = "-l";
	args[argsIndex++] = (char *) username;
	args[argsIndex++] = (char *) ip;
	args[argsIndex++] = "--";
	args[argsIndex++] = (char *) command;
	args[argsIndex++] = NULL;

	(void) initialize_program(&program, args, false);

	(void) snprintf_program_command_line(&program, ssh_command, BUFSIZE);

	if (dryRun)
	{
		appendPQExpBuffer(azureScript, "\n%s", ssh_command);

		return true;
	}

	return azure_start_command(&program);
}


/*
 * azure_fetch_vm_addresses fetches a given VM addresses.
 */
static bool
azure_fetch_vm_addresses(const char *group, const char *vm,
						 AzureVMipAddresses *addresses)
{
	char groupName[BUFSIZE] = { 0 };
	char vmName[BUFSIZE] = { 0 };
	int vmIndex = -1;

	AzureVMipAddresses vmAddresses[MAX_VMS_PER_REGION] = { 0 };

	/* if the vmName is already complete, just use it already */
	if (strstr(vm, group) == NULL)
	{
		sformat(vmName, sizeof(vmName), "%s-%s", group, vm);
	}
	else
	{
		sformat(vmName, sizeof(vmName), "%s", vm);
	}

	vmIndex = azure_node_index_from_name(group, vmName);

	if (vmIndex == -1)
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * It takes as much time fetching all the IP addresses at once compared to
	 * fetching a single IP address, so we always fetch them all internally.
	 */
	if (!azure_fetch_ip_addresses(group, vmAddresses))
	{
		/* errors have already been logged */
		return false;
	}

	if (IS_EMPTY_STRING_BUFFER(vmAddresses[vmIndex].name))
	{
		log_error(
			"Failed to find Virtual Machine \"%s\" in resource group \"%s\"",
			vmName, groupName);
		return false;
	}

	/* copy the structure wholesale to the target address */
	*addresses = vmAddresses[vmIndex];

	return true;
}


/*
 * azure_vm_ssh runs an ssh command to the given VM public IP address.
 */
bool
azure_vm_ssh(const char *group, const char *vm)
{
	AzureVMipAddresses addresses = { 0 };

	if (!azure_fetch_vm_addresses(group, vm, &addresses))
	{
		/* errors have already been logged */
		return false;
	}

	return run_ssh("ha-admin", addresses.public);
}


/*
 * azure_vm_ssh runs an ssh command to the given VM public IP address.
 */
bool
azure_vm_ssh_command(const char *group,
					 const char *vm,
					 bool tty,
					 const char *command)
{
	AzureVMipAddresses addresses = { 0 };

	if (!azure_fetch_vm_addresses(group, vm, &addresses))
	{
		/* errors have already been logged */
		return false;
	}

	return run_ssh_command("ha-admin", addresses.public, tty, command);
}


/*
 * azure_create_region creates a region on Azure and prepares it for
 * pg_auto_failover demo/QA activities.
 *
 * We need to create a vnet, a subnet, a network security group with a rule
 * that opens ports 22 (ssh) and 5432 (Postgres) for direct access from the
 * current IP address of the "client" machine where this pg_autoctl command is
 * being run.
 */
bool
azure_create_region(AzureRegionResources *azRegion)
{
	AzureRegionResources azRegionFound = { 0 };

	/*
	 * Fetch Azure objects that might have already been created in the target
	 * resource group, we're going to re-use them, allowing the command to be
	 * run several times in a row and then "fix itself", or at least continue
	 * from where it failed.
	 */
	if (!dryRun)
	{
		if (!azure_fetch_resource_list(azRegion->group, &azRegionFound))
		{
			/* errors have already been logged */
			return false;
		}
	}

	/*
	 * First create the resource group in the target location.
	 */
	if (!azure_create_group(azRegion->group, azRegion->location))
	{
		/* errors have already been logged */
		return false;
	}

	/* never skip a step when --script is used */
	if (dryRun || IS_EMPTY_STRING_BUFFER(azRegionFound.vnet))
	{
		if (!azure_create_vnet(azRegion->group,
							   azRegion->vnet,
							   azRegion->vnetPrefix))
		{
			/* errors have already been logged */
			return false;
		}
	}
	else
	{
		log_info("Skipping creation of vnet \"%s\" which already exist",
				 azRegion->vnet);
	}

	/*
	 * Get our IP address as seen by the outside world.
	 */
	if (!azure_get_remote_ip(azRegion->ipAddress, sizeof(azRegion->ipAddress)))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * Create the network security group.
	 */
	if (dryRun || IS_EMPTY_STRING_BUFFER(azRegionFound.nsg))
	{
		if (!azure_create_nsg(azRegion->group, azRegion->nsg))
		{
			/* errors have already been logged */
			return false;
		}
	}
	else
	{
		log_info("Skipping creation of nsg \"%s\" which already exist",
				 azRegion->nsg);
	}

	/*
	 * Create the network security rules for SSH and Postgres protocols.
	 *
	 * Some objects won't show up in the list from azure_fetch_resource_list
	 * and it would be quite surprising that we find everything but those, so
	 * we skip their creation even though we don't see them in azRegionFound.
	 */
	if (dryRun || IS_EMPTY_STRING_BUFFER(azRegionFound.nsg))
	{
		if (!azure_create_nsg_rule(azRegion->group,
								   azRegion->nsg,
								   azRegion->rule,
								   azRegion->ipAddress))
		{
			/* errors have already been logged */
			return false;
		}
	}
	else
	{
		log_info("Skipping creation of nsg rule \"%s\", "
				 "because nsg \"%s\" already exists",
				 azRegion->rule,
				 azRegion->nsg);
	}

	/*
	 * Create the network subnet using previous network security group.
	 */
	if (dryRun || IS_EMPTY_STRING_BUFFER(azRegionFound.vnet))
	{
		if (!azure_create_subnet(azRegion->group,
								 azRegion->vnet,
								 azRegion->subnet,
								 azRegion->subnetPrefix,
								 azRegion->nsg))
		{
			/* errors have already been logged */
			return false;
		}
	}
	else
	{
		log_info("Skipping creation of subnet \"%s\" for prefix \"%s\", "
				 "because vnet \"%s\" already exists",
				 azRegion->subnet,
				 azRegion->subnetPrefix,
				 azRegion->vnet);
	}

	/*
	 * Now is time to create the virtual machines.
	 */
	return azure_provision_nodes(azRegion);
}


/*
 * azure_drop_region runs the command az group delete --name ... --yes
 */
bool
azure_drop_region(AzureRegionResources *azRegion)
{
	return az_group_delete(azRegion->group);
}


/*
 * azure_provision_nodes creates the pg_autoctl VM nodes that we need, and
 * provision them with our provisioning script.
 */
bool
azure_provision_nodes(AzureRegionResources *azRegion)
{
	if (!azure_fetch_ip_addresses(azRegion->group, azRegion->vmArray))
	{
		/* errors have already been logged */
		return false;
	}

	if (azRegion->monitor > 0 || azRegion->nodes > 0)
	{
		/*
		 * Here we run the following commands:
		 *
		 *   $ az vm create --name a &
		 *   $ az vm create --name b &
		 *   $ wait
		 *
		 *   $ az vm run-command invoke --name a --scripts ... &
		 *   $ az vm run-command invoke --name b --scripts ... &
		 *   $ wait
		 *
		 * We could optimize our code so that we run the provisioning scripts
		 * for a VM as soon as it's been created, without having to wait until
		 * the other VMs are created. Two things to keep in mind, though:
		 *
		 * - overall, being cleverer here might not be a win as we're going to
		 *   have to wait until all the VMs are provisioned anyway
		 *
		 * - in dry-run mode (--script), we still want to produce the more
		 *   naive script as shown above, for lack of known advanced control
		 *   structures in the target shell (we don't require a specific one).
		 */
		if (!azure_create_vms(azRegion, "debian", "ha-admin"))
		{
			/* errors have already been logged */
			return false;
		}

		if (!azure_provision_vms(azRegion, azRegion->fromSource))
		{
			/* errors have already been logged */
			return false;
		}

		/*
		 * When provisioning from sources, after the OS related steps in
		 * azure_provision_vms, we still need to upload our local sources (this
		 * requires rsync to have been installed in the previous step), and to
		 * build our software from same sources.
		 */
		if (azRegion->fromSource)
		{
			if (!azure_rsync_vms(azRegion))
			{
				/* errors have already been logged */
				return false;
			}

			return azure_build_pg_autoctl(azRegion);
		}
	}

	return true;
}


/*
 * azure_deploy_monitor deploys pg_autoctl on a monitor node, running both the
 * pg_autoctl create monitor command and then the systemd integration commands.
 */
bool
azure_deploy_monitor(AzureRegionResources *azRegion)
{
	KeyVal env = { 0 };
	char create_monitor[BUFSIZE] = { 0 };

	char *systemd =
		"pg_autoctl -q show systemd --pgdata /home/ha-admin/monitor "
		"> pgautofailover.service; "
		"sudo mv pgautofailover.service /etc/systemd/system; "
		"sudo systemctl daemon-reload; "
		"sudo systemctl enable pgautofailover; "
		"sudo systemctl start pgautofailover";

	bool tty = false;
	char *host = azRegion->vmArray[0].public;

	if (!azure_prepare_target_versions(&env))
	{
		/* errors have already been logged */
		return false;
	}

	/* build pg_autoctl create monitor command with target Postgres version  */
	sformat(create_monitor, sizeof(create_monitor),
			"pg_autoctl create monitor "
			"--auth trust "
			"--ssl-self-signed "
			"--pgdata /home/ha-admin/monitor "
			"--pgctl /usr/lib/postgresql/%s/bin/pg_ctl",

	        /* AZ_PG_VERSION */
			env.values[0]);

	if (azRegion->monitor == 0)
	{
		/* no monitor to deploy, we're done already */
		return true;
	}

	/* the monitor is always at index 0 in the vmArray */
	if (!run_ssh_command("ha-admin", host, tty, create_monitor))
	{
		/* errors have already been logged */
		return false;
	}

	if (!run_ssh_command("ha-admin", host, tty, systemd))
	{
		/* errors have already been logged */
		return false;
	}

	return true;
}


/*
 * azure_deploy_postgres deploys pg_autoctl on a Postgres node, running both
 * the pg_autoctl create postgres command and then the systemd integration
 * commands.
 */
bool
azure_deploy_postgres(AzureRegionResources *azRegion, int vmIndex)
{
	KeyVal env = { 0 };
	char create_postgres[BUFSIZE] = { 0 };

	char *systemd =
		"pg_autoctl -q show systemd --pgdata /home/ha-admin/pgdata "
		"> pgautofailover.service; "
		"sudo mv pgautofailover.service /etc/systemd/system; "
		"sudo systemctl daemon-reload; "
		"sudo systemctl enable pgautofailover; "
		"sudo systemctl start pgautofailover";

	bool tty = false;
	char *host = azRegion->vmArray[vmIndex].public;

	if (!azure_prepare_target_versions(&env))
	{
		/* errors have already been logged */
		return false;
	}

	/* build pg_autoctl create monitor command with target Postgres version  */
	sformat(create_postgres, sizeof(create_postgres),
			"pg_autoctl create postgres "
			"--pgctl /usr/lib/postgresql/%s/bin/pg_ctl "
			"--pgdata /home/ha-admin/pgdata "
			"--auth trust "
			"--ssl-self-signed "
			"--username ha-admin "
			"--dbname appdb "
			"--hostname %s "
			"--name %s-%c "
			"--monitor "
			"'postgres://autoctl_node@%s/pg_auto_failover?sslmode=require'",

	        /* AZ_PG_VERSION */
			env.values[0],
			azRegion->vmArray[vmIndex].private,
			azRegion->region,
			'a' + vmIndex - 1,
			azRegion->vmArray[0].private);

	/* the monitor is always at index 0 in the vmArray */
	if (!run_ssh_command("ha-admin", host, tty, create_postgres))
	{
		/* errors have already been logged */
		return false;
	}

	if (!run_ssh_command("ha-admin", host, tty, systemd))
	{
		/* errors have already been logged */
		return false;
	}

	return true;
}


/*
 * azure_create_nodes run the pg_autoctl commands that create our nodes, and
 * then register them with systemd on the remote VMs.
 */
bool
azure_create_nodes(AzureRegionResources *azRegion)
{
	bool success = true;

	if (!azure_fetch_ip_addresses(azRegion->group, azRegion->vmArray))
	{
		/* errors have already been logged */
		return false;
	}

	if (azRegion->monitor > 0)
	{
		success = success && azure_deploy_monitor(azRegion);
	}

	/*
	 * Now prepare all the other nodes, one at a time, so that we have a the
	 * primary, etc. It could also be all at once, but one at a time is good
	 * for a tutorial.
	 */
	for (int vmIndex = 1; vmIndex <= azRegion->nodes; vmIndex++)
	{
		success = success && azure_deploy_postgres(azRegion, vmIndex);
	}

	return success;
}


/*
 * azure_deploy_vm deploys a vm given by name ("monitor", "a", ...).
 */
bool
azure_deploy_vm(AzureRegionResources *azRegion, const char *vmName)
{
	int vmIndex = -1;

	if (!azure_fetch_ip_addresses(azRegion->group, azRegion->vmArray))
	{
		/* errors have already been logged */
		return false;
	}

	vmIndex = azure_node_index_from_name(azRegion->group, vmName);

	switch (vmIndex)
	{
		case -1:
		{
			/* errors have already been logged */
			return false;
		}

		case 0:
		{
			return azure_deploy_monitor(azRegion);
		}

		default:
		{
			return azure_deploy_postgres(azRegion, vmIndex);
		}
	}
}


/*
 * azure_ls lists the azure resources we created in a specific resource group.
 */
bool
azure_ls(AzureRegionResources *azRegion)
{
	return azure_resource_list(azRegion->group);
}


/*
 * azure_show_ips shows the azure ip addresses for the VMs we created in a
 * specific resource group.
 */
bool
azure_show_ips(AzureRegionResources *azRegion)
{
	return azure_show_ip_addresses(azRegion->group);
}


/*
 * azure_ssh runs the ssh -l ha-admin <public ip address> command for given
 * node in given azure group, identified as usual with a prefix and a name.
 */
bool
azure_ssh(AzureRegionResources *azRegion, const char *vm)
{
	/* return azure_vm_ssh_command(groupName, vm, true, "watch date -R"); */
	return azure_vm_ssh(azRegion->group, vm);
}


/*
 * azure_ssh_command runs the ssh -l ha-admin <public ip address> <command> for
 * given node in given azure group, identified as usual with a prefix and a
 * name.
 */
bool
azure_ssh_command(AzureRegionResources *azRegion,
				  const char *vm, bool tty, const char *command)
{
	return azure_vm_ssh_command(azRegion->group, vm, tty, command);
}


/*
 * azure_sync_source_dir runs rsync in parallel to all the created VMs.
 */
bool
azure_sync_source_dir(AzureRegionResources *azRegion)
{
	if (!azure_fetch_ip_addresses(azRegion->group, azRegion->vmArray))
	{
		/* errors have already been logged */
		return false;
	}

	if (!azure_rsync_vms(azRegion))
	{
		/* errors have already been logged */
		return false;
	}

	return azure_build_pg_autoctl(azRegion);
}
