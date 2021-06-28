/*
 * src/bin/pg_autoctl/cli_do_tmux_azure.c
 *
 *     Implementation of commands that create a tmux session to connect to a
 *     set of Azure VMs where we run pg_autoctl nodes for QA and testing.
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
#include "azure_config.h"
#include "cli_common.h"
#include "cli_do_root.h"
#include "cli_do_tmux.h"
#include "cli_root.h"
#include "commandline.h"
#include "config.h"
#include "env_utils.h"
#include "log.h"
#include "parsing.h"
#include "pidfile.h"
#include "signals.h"
#include "string_utils.h"

#include "runprogram.h"

static void tmux_azure_new_session(PQExpBuffer script,
								   AzureRegionResources *azRegion);

static void tmux_azure_deploy(PQExpBuffer script,
							  AzureRegionResources *azRegion,
							  const char *vmName);

static void tmux_azure_ssh(PQExpBuffer script, AzureRegionResources *azRegion,
						   const char *vmName);

static void tmux_azure_systemctl_status(PQExpBuffer script,
										AzureRegionResources *azRegion);

static void prepare_tmux_azure_script(AzureRegionResources *azRegion,
									  PQExpBuffer script);


/*
 * tmux_azure_new_session appends a new-session command to the given tmux
 * script buffer, using the azure group name for the tmux session name.
 */
static void
tmux_azure_new_session(PQExpBuffer script, AzureRegionResources *azRegion)
{
	tmux_add_command(script, "new-session -s %s", azRegion->group);
}


/*
 * tmux_azure_deploy_postgres appends a pg_autoctl do azure deploy command for
 * the given vmName to the given script buffer.
 */
static void
tmux_azure_deploy(PQExpBuffer script, AzureRegionResources *azRegion,
				  const char *vmName)
{
	tmux_add_send_keys_command(script,
							   "%s do azure deploy %s",
							   pg_autoctl_argv0,
							   vmName);
}


/*
 * tmux_azure_ssh appends a pg_autoctl do azure ssh command for the given
 * vmName to the given script buffer.
 */
static void
tmux_azure_ssh(PQExpBuffer script, AzureRegionResources *azRegion,
			   const char *vmName)
{
	tmux_add_send_keys_command(script,
							   "%s do azure ssh %s",
							   pg_autoctl_argv0,
							   vmName);
}


/*
 * tmux_azure_ssh appends a pg_autoctl do azure ssh command for the given
 * vmName to the given script buffer.
 */
static void
tmux_azure_systemctl_status(PQExpBuffer script,
							AzureRegionResources *azRegion)
{
	tmux_add_send_keys_command(script, "systemctl status pgautofailover");
}


/*
 * tmux_add_environment appends the export VAR=value commands that we need to
 * set the environment for pg_autoctl do azure deploy in the shell windows.
 */
static void
tmux_azure_add_environment(PQExpBuffer script, KeyVal *env)
{
	for (int i = 0; i < env->count; i++)
	{
		tmux_add_send_keys_command(script,
								   "export %s=%s",
								   env->keywords[i],
								   env->values[i]);
	}
}


/*
 * prepare_tmux_script prepares a script for a tmux session with the given
 * azure region resources.
 */
static void
prepare_tmux_azure_script(AzureRegionResources *azRegion, PQExpBuffer script)
{
	KeyVal env = { 0 };

	/* fetch environment and defaults for versions */
	if (!azure_prepare_target_versions(&env))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	tmux_add_command(script, "set-option -g default-shell /bin/bash");

	tmux_azure_new_session(script, azRegion);

	/* deploy VMs each in a new tmux window */
	for (int vmIndex = 0; vmIndex <= azRegion->nodes; vmIndex++)
	{
		const char *vmName = azRegion->vmArray[vmIndex].name;

		/* after the first VM, create new tmux windows for each VM */
		if (vmIndex > 0)
		{
			tmux_add_command(script, "split-window -v");
			tmux_add_command(script, "select-layout even-vertical");
		}

		tmux_azure_add_environment(script, &env);
		tmux_azure_deploy(script, azRegion, vmName);
		tmux_azure_ssh(script, azRegion, vmName);
		tmux_azure_systemctl_status(script, azRegion);
	}

	/* add a window for pg_autoctl show state */
	tmux_add_command(script, "split-window -v");
	tmux_add_command(script, "select-layout even-vertical");

	tmux_add_send_keys_command(script,
							   "%s do azure show state --watch",
							   pg_autoctl_argv0);

	/* add a window for interactive pg_autoctl commands */
	tmux_add_command(script, "split-window -v");
	tmux_add_command(script, "select-layout even-vertical");
	tmux_add_send_keys_command(script,
							   "%s do azure show ips",
							   pg_autoctl_argv0);
}


/*
 * cli_do_azure_tmux_session starts a new tmux session for the given azure
 * region and resources, or attach an existing session that might be running in
 * the background already.
 */
bool
tmux_azure_start_or_attach_session(AzureRegionResources *azRegion)
{
	char tmux[MAXPGPATH] = { 0 };

	PQExpBuffer script;
	char scriptName[MAXPGPATH] = { 0 };

	if (setenv("PG_AUTOCTL_DEBUG", "1", 1) != 0)
	{
		log_error("Failed to set environment PG_AUTOCTL_DEBUG: %m");
		return false;
	}

	if (!search_path_first("tmux", tmux, LOG_ERROR))
	{
		log_fatal("Failed to find program tmux in PATH");
		return false;
	}

	/* we might just re-use a pre-existing tmux session */
	if (!dryRun && tmux_has_session(tmux, azRegion->group))
	{
		return tmux_attach_session(tmux, azRegion->group);
	}

	/*
	 * Okay, so we have to create the session now. And for that we need the IP
	 * addresses of the target VMs.
	 */
	if (!azure_fetch_ip_addresses(azRegion->group, azRegion->vmArray))
	{
		/* errors have already been logged */
		return false;
	}

	script = createPQExpBuffer();
	if (script == NULL)
	{
		log_error("Failed to allocate memory");
		return false;
	}

	/* prepare the tmux script */
	(void) prepare_tmux_azure_script(azRegion, script);

	/*
	 * Start a tmux session from the script.
	 */
	if (dryRun)
	{
		fformat(stdout, "%s", script->data);
		destroyPQExpBuffer(script);
	}
	else
	{
		/* write the tmux script to file */
		sformat(scriptName, sizeof(scriptName), "%s.tmux", azRegion->group);
		log_info("Writing tmux session script \"%s\"", scriptName);

		if (!write_file(script->data, script->len, scriptName))
		{
			log_fatal("Failed to write tmux script at \"%s\"", scriptName);
			destroyPQExpBuffer(script);
			return false;
		}

		if (!tmux_start_server(scriptName, NULL))
		{
			log_fatal("Failed to start the tmux session, see above for details");
			destroyPQExpBuffer(script);
			return false;
		}
	}

	return true;
}


/*
 * tmux_azure_kill_session kills a tmux session for the given QA setup, when
 * the tmux session already exists.
 */
bool
tmux_azure_kill_session(AzureRegionResources *azRegion)
{
	return tmux_kill_session_by_name(azRegion->group);
}
