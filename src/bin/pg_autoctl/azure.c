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
#include "pidfile.h"
#include "signals.h"
#include "string_utils.h"

#include "runprogram.h"

char azureCLI[MAXPGPATH] = { 0 };

static void azure_prepare_region(const char *prefix, const char *region,
								 bool monitor, int nodes,
								 AzureRegionResources *azRegion);

static int azure_run_command(Program *program);
static pid_t azure_start_command(Program *program);
static bool azure_wait_for_commands(int count, pid_t pidArray[]);


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
 * run_az_command runs a command line using the azure CLI command, and when
 * azureScript is true instead of running the command it only shows the command
 * it would run as the output of the pg_autoctl command.
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

	IntString semIdString = intToString(log_semaphore.semId);

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

	/* we want to use the same logs semaphore in the sub-processes */
	setenv(PG_AUTOCTL_LOG_SEMAPHORE, semIdString.strValue, 1);

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

	if (!search_path_first("sleep", sleep))
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

		Program program;

		args[argsIndex++] = sleep;
		args[argsIndex++] = "5";
		args[argsIndex++] = NULL;

		program = initialize_program(args, false);

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

	if (!search_path_first("curl", curl))
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

	Program program;

	args[argsIndex++] = azureCLI;
	args[argsIndex++] = "group";
	args[argsIndex++] = "create";
	args[argsIndex++] = "--name";
	args[argsIndex++] = (char *) name;
	args[argsIndex++] = "--location";
	args[argsIndex++] = (char *) location;
	args[argsIndex++] = NULL;

	program = initialize_program(args, false);

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

	Program program;

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

	program = initialize_program(args, false);

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

	Program program;

	args[argsIndex++] = azureCLI;
	args[argsIndex++] = "network";
	args[argsIndex++] = "nsg";
	args[argsIndex++] = "create";
	args[argsIndex++] = "--resource-group";
	args[argsIndex++] = (char *) group;
	args[argsIndex++] = "--name";
	args[argsIndex++] = (char *) name;
	args[argsIndex++] = NULL;

	program = initialize_program(args, false);

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
	char *args[28];
	int argsIndex = 0;

	Program program;

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

	program = initialize_program(args, false);

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

	Program program;

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

	program = initialize_program(args, false);

	log_info("Creating network subnet \"%s\" using address prefix \"%s\"",
			 name, prefixes);

	return azure_run_command(&program) == 0;
}


/*
 * azure_prepare_network_names prepares the names we use for the different
 * Azure network objects that we need: vnet, nsg, nsgrule, subnet.
 */
static void
azure_prepare_region(const char *prefix, const char *region,
					 bool monitor, int nodes,
					 AzureRegionResources *azRegion)
{
	/*
	 * First create the resource group in the target location.
	 */
	strlcpy(azRegion->prefix, prefix, sizeof(azRegion->prefix));
	strlcpy(azRegion->region, region, sizeof(azRegion->region));
	sformat(azRegion->group, sizeof(azRegion->group), "%s-%s", prefix, region);

	/*
	 * Prepare our Azure object names from the group objects: vnet, subnet,
	 * nsg, nsg rule.
	 */
	sformat(azRegion->vnet, sizeof(azRegion->vnet), "%s-net", azRegion->group);
	sformat(azRegion->nsg, sizeof(azRegion->nsg), "%s-nsg", azRegion->group);

	sformat(azRegion->rule, sizeof(azRegion->rule),
			"%s-ssh-and-pg", azRegion->group);

	sformat(azRegion->subnet, sizeof(azRegion->subnet),
			"%s-subnet", azRegion->group);

	azRegion->monitor = monitor;
	azRegion->nodes = nodes;
}


/*
 * azure_prepare_node_name is a utility function that prepares a node name to
 * use for a VM in our pg_auto_failover deployment in a target Azure region.
 *
 * In the resource group "ha-demo-dim-paris" when creating a monitor (index 0)
 * and 2 VMs we would have the following names:
 *
 *   - ha-demo-dim-paris-monitor
 *   - ha-demo-dim-paris-a
 *   - ha-demo-dim-paris-a
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

	if (strcmp(ptr, "monitor") == 0)
	{
		return 0;
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

	Program program;

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

	program = initialize_program(args, false);

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

		/* skip VMs that already exist */
		if (!IS_EMPTY_STRING_BUFFER(azRegion->vmArray[index].name) &&
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
 * azure_provision_vm runs the command `az vm run-command invoke` with our
 * provisioning script.
 */
bool
azure_provision_vm(const char *group, const char *name)
{
	char *args[26];
	int argsIndex = 0;

	Program program;

	const char *scripts[] =
	{
		"curl https://install.citusdata.com/community/deb.sh | sudo bash",
		"sudo apt-get install -q -y postgresql-common",
		"echo 'create_main_cluster = false' "
		"| sudo tee -a /etc/postgresql-common/createcluster.conf",
		"sudo apt-get install -q -y postgresql-11-auto-failover-1.4",
		"sudo usermod -a -G postgres ha-admin",
		NULL
	};
	char *quotedScripts[10][BUFSIZE] = { 0 };

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

	program = initialize_program(args, false);

	log_info("Provisioning Virtual Machine \"%s\"", name);

	return azure_start_command(&program);
}


/*
 * azure_provision_vms provisions several azure virtual machine in parallel and
 * waits until all the commands have finished.
 */
bool
azure_provision_vms(AzureRegionResources *azRegion)
{
	pid_t pidArray[MAX_VMS_PER_REGION] = { 0 };

	/* we read from left to right, have the smaller number on the left */
	if (26 < azRegion->nodes)
	{
		log_error("pg_autoctl only supports up to 26 VMs per region");
		return false;
	}

	log_info("Provisioning %d Virtual Machines in parallel",
			 azRegion->monitor ? azRegion->nodes + 1 : azRegion->nodes);

	/* index == 0 for the monitor, then 1..count for the other nodes */
	for (int index = 0; index <= azRegion->nodes; index++)
	{
		/* skip index 0 when we're not creating a monitor */
		if (index == 0 && !azRegion->monitor)
		{
			continue;
		}

		(void) azure_prepare_node(azRegion, index);

		pidArray[index] = azure_provision_vm(azRegion->group,
											 azRegion->vmArray[index].name);
	}

	/* now wait for the child processes to be done */
	if (dryRun)
	{
		appendPQExpBuffer(azureScript, "\nwait");
	}
	else
	{
		if (!azure_wait_for_commands(azRegion->nodes, pidArray))
		{
			log_fatal("Failed to provision all %d azure VMs, "
					  "see above for details",
					  azRegion->nodes);
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

	Program program;

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

	program = initialize_program(args, false);

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

	Program program;

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

	program = initialize_program(args, false);

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
static bool
azure_fetch_ip_addresses(const char *group, AzureVMipAddresses *vmArray)
{
	char *args[16];
	int argsIndex = 0;
	bool success = true;

	Program program;

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

	program = initialize_program(args, false);

	(void) snprintf_program_command_line(&program, command, sizeof(command));

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

	Program program;

	char ssh[MAXPGPATH] = { 0 };
	char command[BUFSIZE] = { 0 };

	if (!search_path_first("ssh", ssh))
	{
		log_fatal("Failed to find program ssh in PATH");
		return false;
	}

	args[argsIndex++] = ssh;
	args[argsIndex++] = "-o";
	args[argsIndex++] = "StrictHostKeyChecking=no";
	args[argsIndex++] = "-o";
	args[argsIndex++] = "UserKnownHostsFile /dev/null";
	args[argsIndex++] = "-l";
	args[argsIndex++] = (char *) username;
	args[argsIndex++] = (char *) ip;
	args[argsIndex++] = NULL;

	program = initialize_program(args, false);

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

	Program program;

	char ssh[MAXPGPATH] = { 0 };
	char ssh_command[BUFSIZE] = { 0 };

	if (!search_path_first("ssh", ssh))
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
	args[argsIndex++] = "-l";
	args[argsIndex++] = (char *) username;
	args[argsIndex++] = (char *) ip;
	args[argsIndex++] = "--";
	args[argsIndex++] = (char *) command;
	args[argsIndex++] = NULL;

	program = initialize_program(args, false);

	program.capture = false;    /* don't capture output */
	program.tty = true;         /* allow sharing the parent's tty */

	(void) snprintf_program_command_line(&program, ssh_command, BUFSIZE);

	log_info("%s", ssh_command);

	(void) execute_subprogram(&program);

	return true;
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

	sformat(vmName, sizeof(vmName), "%s-%s", group, vm);

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
azure_create_region(const char *prefix,
					const char *region,
					const char *location,
					int cidr,
					bool monitor,
					int nodes)
{
	AzureRegionResources azRegion = { 0 };

	char vnetPrefix[BUFSIZE] = { 0 };
	char subnetPrefix[BUFSIZE] = { 0 };
	char ipAddress[BUFSIZE] = { 0 };

	(void) azure_prepare_region(prefix, region, nodes, monitor, &azRegion);

	/*
	 * First create the resource group in the target location.
	 */
	if (!azure_create_group(azRegion.group, location))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * Prepare vnet and subnet IP addresses prefixes.
	 */
	sformat(vnetPrefix, sizeof(vnetPrefix), "10.%d.0.0/16", cidr);
	sformat(subnetPrefix, sizeof(subnetPrefix), "10.%d.%d.0/24", cidr, cidr);

	if (!azure_create_vnet(azRegion.group, azRegion.vnet, vnetPrefix))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * Get our IP address as seen by the outside world.
	 */
	if (!azure_get_remote_ip(ipAddress, sizeof(ipAddress)))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * Create the network security group.
	 */
	if (!azure_create_nsg(azRegion.group, azRegion.nsg))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * Create the network security rules for SSH and Postgres protocols.
	 */
	if (!azure_create_nsg_rule(azRegion.group,
							   azRegion.nsg,
							   azRegion.rule,
							   ipAddress))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * Create the network subnet using previous network security group.
	 */
	if (!azure_create_subnet(azRegion.group,
							 azRegion.vnet,
							 azRegion.subnet,
							 subnetPrefix,
							 azRegion.nsg))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * Now is time to create the virtual machines.
	 */
	return azure_create_nodes(prefix, region, monitor, nodes);
}


/*
 * azure_create_nodes creates the pg_autoctl VM nodes that we need, and
 * provision them with our provisioning script.
 */
bool
azure_create_nodes(const char *prefix,
				   const char *region,
				   bool monitor,
				   int nodes)
{
	AzureRegionResources azRegion = { 0 };

	(void) azure_prepare_region(prefix, region, monitor, nodes, &azRegion);

	if (!azure_fetch_ip_addresses(azRegion.group, azRegion.vmArray))
	{
		/* errors have already been logged */
		return false;
	}

	if (monitor || nodes > 0)
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
		if (!azure_create_vms(&azRegion, "debian", "ha-admin"))
		{
			/* errors have already been logged */
			return false;
		}

		if (!azure_provision_vms(&azRegion))
		{
			/* errors have already been logged */
			return false;
		}
	}

	return true;
}


/*
 * azure_create_service creates the pg_autoctl services on the target nodes
 */
bool
azure_create_service(const char *prefix,
					 const char *name,
					 bool monitor,
					 int nodes)
{
	return true;
}


/*
 * azure_ls lists the azure resources we created in a specific resource group.
 */
bool
azure_ls(const char *prefix, const char *name)
{
	char groupName[BUFSIZE] = { 0 };

	sformat(groupName, sizeof(groupName), "%s-%s", prefix, name);

	return azure_resource_list(groupName);
}


/*
 * azure_show_ips shows the azure ip addresses for the VMs we created in a
 * specific resource group.
 */
bool
azure_show_ips(const char *prefix, const char *name)
{
	char groupName[BUFSIZE] = { 0 };

	sformat(groupName, sizeof(groupName), "%s-%s", prefix, name);

	return azure_show_ip_addresses(groupName);
}


/*
 * azure_ssh runs the ssh -l ha-admin <public ip address> command for given
 * node in given azure group, identified as usual with a prefix and a name.
 */
bool
azure_ssh(const char *prefix, const char *name, const char *vm)
{
	char groupName[BUFSIZE] = { 0 };

	sformat(groupName, sizeof(groupName), "%s-%s", prefix, name);

	/* return azure_vm_ssh_command(groupName, vm, true, "watch date -R"); */
	return azure_vm_ssh(groupName, vm);
}


/*
 * azure_ssh_command runs the ssh -l ha-admin <public ip address> <command> for
 * given node in given azure group, identified as usual with a prefix and a
 * name.
 */
bool
azure_ssh_command(const char *prefix, const char *name, const char *vm,
				  bool tty, const char *command)
{
	char groupName[BUFSIZE] = { 0 };

	sformat(groupName, sizeof(groupName), "%s-%s", prefix, name);

	return azure_vm_ssh_command(groupName, vm, tty, command);
}
