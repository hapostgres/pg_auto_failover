/*
 * src/bin/pg_autoctl/cli_archiver.c
 *   See cli_archiver.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <getopt.h>
#include <inttypes.h>

#include "postgres_fe.h"

#include "cli_archiver.h"
#include "cli_common.h"
#include "commandline.h"
#include "defaults.h"
#include "file_utils.h"
#include "keeper.h"
#include "keeper_config.h"
#include "log.h"
#include "monitor.h"
#include "parson.h"
#include "service_archiver_serve.h"
#include "signals.h"
#include "state.h"
#include "string_utils.h"
#include "supervisor.h"
#include "system_utils.h"

static int cli_archiver_serve_getopts(int argc, char **argv);
static void cli_archiver_serve(int argc, char **argv);

static int cli_archiver_formation_getopts(int argc, char **argv, bool requireName);
static void cli_archiver_formation_add(int argc, char **argv);
static void cli_archiver_formation_remove(int argc, char **argv);
static void cli_archiver_formation_list(int argc, char **argv);

static int cli_archiver_show_basebackup_wal_getopts(int argc, char **argv);
static void cli_archiver_show_basebackup(int argc, char **argv);
static void cli_archiver_show_wal(int argc, char **argv);

static int cli_archiver_show_state_getopts(int argc, char **argv);
static void cli_archiver_show_state(int argc, char **argv);

/* set by --port; 0 means "use PG_AUTOCTL_ARCHIVER_SERVE_PORT" */
static int archiverServePortOption = 0;

/*
 * Shared by add/remove/list: --name is required for add/remove, unused
 * for list (which lists every archiver attached to --formation);
 * --formation is required by all three, matching pgautofailover.get_
 * archivers()'s own single-formation shape (no "every formation" mode).
 */
typedef struct ArchiverFormationOptions
{
	char monitorPguri[MAXCONNINFO];
	char archiverName[NAMEDATALEN];
	char formation[NAMEDATALEN];
} ArchiverFormationOptions;

static ArchiverFormationOptions archiverFormationOptions = { 0 };

/*
 * Shared by show basebackup/show wal: both listings are per (formation,
 * group), no "every group" mode -- see list_basebackups/list_archiver_wal's
 * own SQL (pgautofailover.sql), neither accepts a NULL groupid.
 */
typedef struct ArchiverShowOptions
{
	char monitorPguri[MAXCONNINFO];
	char formation[NAMEDATALEN];
	int groupId;
} ArchiverShowOptions;

static ArchiverShowOptions archiverShowOptions = { 0 };


static int
cli_archiver_serve_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "port", required_argument, NULL, 'p' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	while ((c = getopt_long(argc, argv, "D:p:Vvqh",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(options.pgSetup.pgdata, optarg, MAXPGPATH);
				log_trace("--pgdata %s", options.pgSetup.pgdata);
				break;
			}

			case 'p':
			{
				if (!stringToInt(optarg, &archiverServePortOption) ||
					archiverServePortOption <= 0 ||
					archiverServePortOption > 65535)
				{
					log_fatal("Failed to parse --port value \"%s\"", optarg);
					exit(EXIT_CODE_BAD_ARGS);
				}
				break;
			}

			case 'V':
			{
				keeper_cli_print_version(argc, argv);
				break;
			}

			case 'v':
			{
				++verboseCount;
				switch (verboseCount)
				{
					case 1:
					{
						log_set_level(LOG_INFO);
						break;
					}

					case 2:
					{
						log_set_level(LOG_DEBUG);
						break;
					}

					default:
					{
						log_set_level(LOG_TRACE);
						break;
					}
				}
				break;
			}

			case 'q':
			{
				log_set_level(LOG_ERROR);
				break;
			}

			case 'h':
			{
				commandline_help(stderr);
				exit(EXIT_CODE_QUIT);
				break;
			}

			default:
			{
				commandline_help(stderr);
				exit(EXIT_CODE_BAD_ARGS);
				break;
			}
		}
	}

	(void) prepare_keeper_options(&options);

	keeperOptions = options;

	return optind;
}


/*
 * cli_archiver_serve implements `pg_autoctl archiver serve`: loads the
 * archiver's own config/state (already written by `pg_autoctl create
 * archiver`) and calls supervisor_start() with a single Service entry
 * (service_archiver_walsender_start(), service_archiver_serve.c) -- the
 * exact same generic supervision `pg_autoctl run` uses for pg_walsender,
 * reused here rather than a bespoke loop, so this standalone command gets
 * identical restart-on-death behavior for free. Deliberately no monitor
 * connection here: service_archiver_walsender_start() itself never talks
 * to the monitor (see service_archiver_serve.c's own header comment), so
 * this command can start and keep pg_walsender serving already-captured
 * data even while the monitor is unreachable.
 */
static void
cli_archiver_serve(int argc, char **argv)
{
	Keeper keeper = { 0 };

	keeper.config = keeperOptions;

	/*
	 * An archiver's pgdata is its local WAL-cache root, never a real
	 * Postgres instance (see service_archiver.c's own header comment) --
	 * both flags must tolerate that, matching cli_create_archiver's own
	 * choice not to run pg_setup_init's real-instance checks at all.
	 */
	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	if (!keeper_config_read_file(&(keeper.config),
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		log_fatal("Failed to read the archiver configuration file \"%s\", "
				  "see above for details", keeper.config.pathnames.config);
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (strcmp(keeper.config.nodeKind, "archiver") != 0)
	{
		log_fatal("\"%s\" is not an archiver's configuration file "
				  "(pg_autoctl.nodekind is \"%s\", expected \"archiver\")",
				  keeper.config.pathnames.config, keeper.config.nodeKind);
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (keeper.config.archiverId <= 0)
	{
		log_fatal("This archiver's configuration file has no archiver_id "
				  "recorded -- it may predate `pg_autoctl archiver serve` "
				  "support; re-create the archiver with `pg_autoctl create "
				  "archiver` to pick it up");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_load_state(&keeper))
	{
		log_fatal("Failed to read the archiver state file \"%s\", "
				  "see above for details", keeper.config.pathnames.state);
		exit(EXIT_CODE_BAD_STATE);
	}

	if (archiverServePortOption > 0)
	{
		service_archiver_serve_set_port(archiverServePortOption);
	}

	(void) set_ps_title("pg_autoctl: archiver serve");

	Service subprocesses[] = {
		{
			SERVICE_NAME_ARCHIVER_SERVE,
			RP_PERMANENT,
			-1,
			&service_archiver_walsender_start,
			(void *) &keeper
		}
	};

	int subprocessesCount = sizeof(subprocesses) / sizeof(subprocesses[0]);

	if (!supervisor_start(subprocesses, subprocessesCount,
						  keeper.config.pathnames.pid))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


CommandLine archiver_serve_command =
	make_command(
		"serve",
		"Start serving this archiver's captured WAL and base backups",
		" [ --pgdata --port ] ",
		"  --pgdata          path to the archiver's local data/cache directory\n"
		"  --port            port for pg_walsender to listen on "
		"(default: 6543)\n",
		cli_archiver_serve_getopts,
		cli_archiver_serve);


/*
 * cli_archiver_formation_getopts parses --monitor --name --formation,
 * shared by add/remove/list: the same self-contained "connect straight to
 * --monitor" shape create_basebackup_policy_command already uses
 * (cli_basebackup_policy.c) rather than resolving the monitor URL through
 * a local config file, since none of these commands have a --pgdata of
 * their own to read one from either. requireName is false only for list,
 * which lists every archiver attached to --formation rather than one by
 * name.
 */
static int
cli_archiver_formation_getopts(int argc, char **argv, bool requireName)
{
	int c, option_index = 0, errors = 0;

	static struct option long_options[] = {
		{ "monitor", required_argument, NULL, 'm' },
		{ "name", required_argument, NULL, 'n' },
		{ "formation", required_argument, NULL, 'f' },
		{ "json", no_argument, NULL, 'J' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	while ((c = getopt_long(argc, argv, "m:n:f:JVvqh",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'm':
			{
				strlcpy(archiverFormationOptions.monitorPguri, optarg,
						MAXCONNINFO);
				log_trace("--monitor %s", archiverFormationOptions.monitorPguri);
				break;
			}

			case 'n':
			{
				strlcpy(archiverFormationOptions.archiverName, optarg,
						NAMEDATALEN);
				log_trace("--name %s", archiverFormationOptions.archiverName);
				break;
			}

			case 'f':
			{
				strlcpy(archiverFormationOptions.formation, optarg,
						NAMEDATALEN);
				log_trace("--formation %s", archiverFormationOptions.formation);
				break;
			}

			case 'J':
			{
				outputJSON = true;
				log_trace("--json");
				break;
			}

			case 'V':
			{
				keeper_cli_print_version(argc, argv);
				break;
			}

			case 'v':
			{
				log_set_level(LOG_INFO);
				break;
			}

			case 'q':
			{
				log_set_level(LOG_ERROR);
				break;
			}

			case 'h':
			{
				commandline_help(stderr);
				exit(EXIT_CODE_QUIT);
				break;
			}

			default:
			{
				++errors;
				break;
			}
		}
	}

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (IS_EMPTY_STRING_BUFFER(archiverFormationOptions.monitorPguri))
	{
		log_fatal("Failed to get value for --monitor");
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (requireName && IS_EMPTY_STRING_BUFFER(archiverFormationOptions.archiverName))
	{
		log_fatal("Failed to get value for --name");
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (IS_EMPTY_STRING_BUFFER(archiverFormationOptions.formation))
	{
		log_fatal("Failed to get value for --formation");
		exit(EXIT_CODE_BAD_ARGS);
	}

	return optind;
}


static int
cli_archiver_formation_add_getopts(int argc, char **argv)
{
	return cli_archiver_formation_getopts(argc, argv, true);
}


static int
cli_archiver_formation_remove_getopts(int argc, char **argv)
{
	return cli_archiver_formation_getopts(argc, argv, true);
}


static int
cli_archiver_formation_list_getopts(int argc, char **argv)
{
	return cli_archiver_formation_getopts(argc, argv, false);
}


/*
 * cli_archiver_formation_add implements `pg_autoctl archiver formation
 * add`: attach an already-registered archiver to one more formation,
 * exactly what `create archiver --formation` does at create time, callable
 * again afterwards for a formation that didn't exist (or wasn't ready)
 * yet -- see monitor_archiver_add_formation_by_name's own comment
 * (monitor.c) for why a zero-row result here is reported back as "no
 * group yet" rather than folded into the generic failure path.
 */
static void
cli_archiver_formation_add(int argc, char **argv)
{
	Monitor monitor = { 0 };

	if (!monitor_init(&monitor, archiverFormationOptions.monitorPguri))
	{
		/* errors already logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	int64_t archiverNodeId = 0;

	if (!monitor_archiver_add_formation_by_name(&monitor,
												archiverFormationOptions.archiverName,
												archiverFormationOptions.formation,
												&archiverNodeId))
	{
		log_fatal("Failed to attach archiver \"%s\" to formation \"%s\", "
				  "see above for details",
				  archiverFormationOptions.archiverName,
				  archiverFormationOptions.formation);
		exit(EXIT_CODE_MONITOR);
	}

	if (archiverNodeId == 0)
	{
		log_fatal("Formation \"%s\" has no group registered yet; "
				  "register its nodes first, or retry once it does",
				  archiverFormationOptions.formation);
		exit(EXIT_CODE_MONITOR);
	}

	log_info("Attached archiver \"%s\" to formation \"%s\", "
			 "ARCHIVING node id %" PRId64,
			 archiverFormationOptions.archiverName,
			 archiverFormationOptions.formation,
			 archiverNodeId);
}


/*
 * cli_archiver_formation_remove implements `pg_autoctl archiver formation
 * remove`: detach an archiver from a formation, dropping its ARCHIVING
 * node row (and thus its replication slot) in every group of that
 * formation -- see pgautofailover.archiver_remove_formation()'s own
 * comment (pgautofailover.sql) for the exact mechanism.
 */
static void
cli_archiver_formation_remove(int argc, char **argv)
{
	Monitor monitor = { 0 };

	if (!monitor_init(&monitor, archiverFormationOptions.monitorPguri))
	{
		/* errors already logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!monitor_archiver_remove_formation_by_name(&monitor,
												   archiverFormationOptions.archiverName,
												   archiverFormationOptions.formation))
	{
		log_fatal("Failed to detach archiver \"%s\" from formation \"%s\", "
				  "see above for details",
				  archiverFormationOptions.archiverName,
				  archiverFormationOptions.formation);
		exit(EXIT_CODE_MONITOR);
	}

	log_info("Detached archiver \"%s\" from formation \"%s\"",
			 archiverFormationOptions.archiverName,
			 archiverFormationOptions.formation);
}


/*
 * cli_archiver_formation_list implements `pg_autoctl archiver formation
 * list`: every archiver currently attached to --formation, with its FSM
 * state and storage stats -- the same pgautofailover.get_archivers() data
 * `pg_autoctl watch`'s own archivers section already renders
 * interactively (watch.c), exposed here as a plain one-shot listing.
 */
static void
cli_archiver_formation_list(int argc, char **argv)
{
	Monitor monitor = { 0 };

	if (!monitor_init(&monitor, archiverFormationOptions.monitorPguri))
	{
		/* errors already logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	ArchiverInfoArray archiversArray = { 0 };

	if (!monitor_get_archivers(&monitor, archiverFormationOptions.formation,
							   &archiversArray))
	{
		log_fatal("Failed to list archivers for formation \"%s\", see "
				  "above for details", archiverFormationOptions.formation);
		exit(EXIT_CODE_MONITOR);
	}

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_array();
		JSON_Array *jsArray = json_value_get_array(js);

		for (int i = 0; i < archiversArray.count; i++)
		{
			ArchiverInfo *archiver = &(archiversArray.archivers[i]);

			JSON_Value *jsArchiver = json_value_init_object();
			JSON_Object *jsObj = json_value_get_object(jsArchiver);

			json_object_set_string(jsObj, "name", archiver->archiverName);
			json_object_set_string(jsObj, "hostname", archiver->hostname);
			json_object_set_string(jsObj, "region", archiver->region);
			json_object_set_string(jsObj, "state",
								   archiver->hasNode
								   ? NodeStateToString(archiver->reportedState)
								   : "?");
			json_object_set_number(jsObj, "used_bytes",
								   (double) archiver->usedBytes);
			json_object_set_number(jsObj, "free_bytes",
								   (double) archiver->freeBytes);

			json_array_append_value(jsArray, jsArchiver);
		}

		(void) cli_pprint_json(js);

		return;
	}

	fformat(stdout, "%20s | %20s | %10s | %12s | %10s | %10s\n",
			"Name", "Hostname", "Region", "State", "Used", "Free");
	fformat(stdout, "%20s-+-%20s-+-%10s-+-%12s-+-%10s-+-%10s\n",
			"--------------------", "--------------------", "----------",
			"------------", "----------", "----------");

	for (int i = 0; i < archiversArray.count; i++)
	{
		ArchiverInfo *archiver = &(archiversArray.archivers[i]);

		char usedStr[NAMEDATALEN] = "?";
		char freeStr[NAMEDATALEN] = "?";

		if (archiver->hasStorageStats)
		{
			pretty_print_bytes(usedStr, sizeof(usedStr), archiver->usedBytes);
			pretty_print_bytes(freeStr, sizeof(freeStr), archiver->freeBytes);
		}

		const char *stateStr =
			archiver->hasNode
			? NodeStateToString(archiver->reportedState)
			: "?";

		fformat(stdout, "%20s | %20s | %10s | %12s | %10s | %10s\n",
				archiver->archiverName, archiver->hostname, archiver->region,
				stateStr, usedStr, freeStr);
	}
}


CommandLine archiver_formation_add_command =
	make_command(
		"add",
		"Attach an existing archiver to one more formation",
		" --monitor --name --formation ",
		"  --monitor    monitor uri to connect to\n"
		"  --name       name of the archiver to attach\n"
		"  --formation  formation to attach it to\n",
		cli_archiver_formation_add_getopts,
		cli_archiver_formation_add);

CommandLine archiver_formation_remove_command =
	make_command(
		"remove",
		"Detach an archiver from a formation",
		" --monitor --name --formation ",
		"  --monitor    monitor uri to connect to\n"
		"  --name       name of the archiver to detach\n"
		"  --formation  formation to detach it from\n",
		cli_archiver_formation_remove_getopts,
		cli_archiver_formation_remove);

CommandLine archiver_formation_list_command =
	make_command(
		"list",
		"List the archivers attached to a formation",
		" --monitor --formation [ --json ] ",
		"  --monitor    monitor uri to connect to\n"
		"  --formation  formation to list archivers for\n"
		"  --json       output data in the JSON format\n",
		cli_archiver_formation_list_getopts,
		cli_archiver_formation_list);

CommandLine *archiver_formation_subcommands[] = {
	&archiver_formation_add_command,
	&archiver_formation_remove_command,
	&archiver_formation_list_command,
	NULL
};

CommandLine archiver_formation_commands =
	make_command_set("formation",
					 "Manage the formations an archiver is attached to", NULL, NULL,
					 NULL, archiver_formation_subcommands);

/*
 * cli_archiver_show_basebackup_wal_getopts parses --monitor --formation
 * --group [--json], shared by `show basebackup` and `show wal`: both are
 * per (formation, group) listings, same self-contained "connect straight
 * to --monitor" shape as the formation subcommands above.
 */
static int
cli_archiver_show_basebackup_wal_getopts(int argc, char **argv)
{
	int c, option_index = 0, errors = 0;

	archiverShowOptions.groupId = -1;

	static struct option long_options[] = {
		{ "monitor", required_argument, NULL, 'm' },
		{ "formation", required_argument, NULL, 'f' },
		{ "group", required_argument, NULL, 'g' },
		{ "json", no_argument, NULL, 'J' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	while ((c = getopt_long(argc, argv, "m:f:g:JVvqh",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'm':
			{
				strlcpy(archiverShowOptions.monitorPguri, optarg, MAXCONNINFO);
				log_trace("--monitor %s", archiverShowOptions.monitorPguri);
				break;
			}

			case 'f':
			{
				strlcpy(archiverShowOptions.formation, optarg, NAMEDATALEN);
				log_trace("--formation %s", archiverShowOptions.formation);
				break;
			}

			case 'g':
			{
				if (!stringToInt(optarg, &archiverShowOptions.groupId))
				{
					log_fatal("Failed to parse --group value \"%s\"", optarg);
					exit(EXIT_CODE_BAD_ARGS);
				}
				break;
			}

			case 'J':
			{
				outputJSON = true;
				log_trace("--json");
				break;
			}

			case 'V':
			{
				keeper_cli_print_version(argc, argv);
				break;
			}

			case 'v':
			{
				log_set_level(LOG_INFO);
				break;
			}

			case 'q':
			{
				log_set_level(LOG_ERROR);
				break;
			}

			case 'h':
			{
				commandline_help(stderr);
				exit(EXIT_CODE_QUIT);
				break;
			}

			default:
			{
				++errors;
				break;
			}
		}
	}

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (IS_EMPTY_STRING_BUFFER(archiverShowOptions.monitorPguri))
	{
		log_fatal("Failed to get value for --monitor");
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (IS_EMPTY_STRING_BUFFER(archiverShowOptions.formation))
	{
		log_fatal("Failed to get value for --formation");
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (archiverShowOptions.groupId < 0)
	{
		log_fatal("Failed to get value for --group");
		exit(EXIT_CODE_BAD_ARGS);
	}

	return optind;
}


/*
 * cli_archiver_show_basebackup implements `pg_autoctl archiver show
 * basebackup`: every complete base backup for (formation, group), newest
 * first -- the same pgautofailover.list_basebackups() data service_
 * archiver_basebackup.c's own retention pass already walks internally,
 * exposed here as a plain one-shot listing.
 */
static void
cli_archiver_show_basebackup(int argc, char **argv)
{
	Monitor monitor = { 0 };

	if (!monitor_init(&monitor, archiverShowOptions.monitorPguri))
	{
		/* errors already logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	BasebackupInfoArray backupsArray = { 0 };

	if (!monitor_list_basebackups(&monitor,
								  archiverShowOptions.formation,
								  archiverShowOptions.groupId,
								  &backupsArray))
	{
		log_fatal("Failed to list base backups for \"%s\"/%d, see above "
				  "for details", archiverShowOptions.formation,
				  archiverShowOptions.groupId);
		exit(EXIT_CODE_MONITOR);
	}

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_array();
		JSON_Array *jsArray = json_value_get_array(js);

		for (int i = 0; i < backupsArray.count; i++)
		{
			BasebackupInfo *backup = &(backupsArray.backups[i]);

			JSON_Value *jsBackup = json_value_init_object();
			JSON_Object *jsObj = json_value_get_object(jsBackup);

			json_object_set_number(jsObj, "id", (double) backup->basebackupId);
			json_object_set_string(jsObj, "label", backup->label);
			json_object_set_string(jsObj, "location", backup->storageLocation);
			json_object_set_number(jsObj, "started_at",
								   (double) backup->startedAtEpoch);

			json_array_append_value(jsArray, jsBackup);
		}

		(void) cli_pprint_json(js);

		return;
	}

	fformat(stdout, "%12s | %30s | %s\n", "Started At", "Label", "Location");
	fformat(stdout, "%12s-+-%30s-+-%s\n",
			"------------", "------------------------------",
			"------------------------------");

	for (int i = 0; i < backupsArray.count; i++)
	{
		BasebackupInfo *backup = &(backupsArray.backups[i]);
		char startedAt[BUFSIZE] = { 0 };

		sformat(startedAt, sizeof(startedAt), "%" PRId64, backup->startedAtEpoch);

		fformat(stdout, "%12s | %30s | %s\n",
				startedAt, backup->label, backup->storageLocation);
	}
}


/*
 * cli_archiver_show_wal implements `pg_autoctl archiver show wal`: every
 * captured WAL segment for (formation, group), newest first, grouped
 * across every archiver holding it -- see pgautofailover.list_archiver_
 * wal()'s own comment (pgautofailover.sql) for why this is one row per
 * segment rather than one per (segment, archiver).
 */
static void
cli_archiver_show_wal(int argc, char **argv)
{
	Monitor monitor = { 0 };

	if (!monitor_init(&monitor, archiverShowOptions.monitorPguri))
	{
		/* errors already logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	ArchiverWalInfoArray walArray = { 0 };

	if (!monitor_list_archiver_wal(&monitor,
								   archiverShowOptions.formation,
								   archiverShowOptions.groupId,
								   &walArray))
	{
		log_fatal("Failed to list captured WAL for \"%s\"/%d, see above "
				  "for details", archiverShowOptions.formation,
				  archiverShowOptions.groupId);
		exit(EXIT_CODE_MONITOR);
	}

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_array();
		JSON_Array *jsArray = json_value_get_array(js);

		for (int i = 0; i < walArray.count; i++)
		{
			ArchiverWalInfo *wal = &(walArray.wal[i]);

			JSON_Value *jsWal = json_value_init_object();
			JSON_Object *jsObj = json_value_get_object(jsWal);

			json_object_set_string(jsObj, "filename", wal->walFileName);
			json_object_set_string(jsObj, "lsn", wal->lsn);
			json_object_set_number(jsObj, "archiver_count",
								   (double) wal->archiverCount);
			json_object_set_string(jsObj, "archivers", wal->archivers);
			json_object_set_number(jsObj, "received_at",
								   (double) wal->receivedAtEpoch);

			json_array_append_value(jsArray, jsWal);
		}

		(void) cli_pprint_json(js);

		return;
	}

	fformat(stdout, "%24s | %11s | %5s | %s\n",
			"Filename", "LSN", "Count", "Archivers");
	fformat(stdout, "%24s-+-%11s-+-%5s-+-%s\n",
			"------------------------", "-----------", "-----",
			"------------------------------");

	for (int i = 0; i < walArray.count; i++)
	{
		ArchiverWalInfo *wal = &(walArray.wal[i]);

		fformat(stdout, "%24s | %11s | %5" PRId64 " | %s\n",
				wal->walFileName, wal->lsn, wal->archiverCount, wal->archivers);
	}
}


/*
 * cli_archiver_show_state_getopts parses `pg_autoctl archiver show
 * state`'s own options: --pgdata [--json], the same local-config shape
 * `archiver serve` reads from -- this command is inherently about "this
 * node", not a --monitor-only lookup, matching `pg_autoctl show state`'s
 * own local-first convention (cli_show.c).
 */
static int
cli_archiver_show_state_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0, errors = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "json", no_argument, NULL, 'J' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	while ((c = getopt_long(argc, argv, "D:JVvqh",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(options.pgSetup.pgdata, optarg, MAXPGPATH);
				log_trace("--pgdata %s", options.pgSetup.pgdata);
				break;
			}

			case 'J':
			{
				outputJSON = true;
				log_trace("--json");
				break;
			}

			case 'V':
			{
				keeper_cli_print_version(argc, argv);
				break;
			}

			case 'v':
			{
				log_set_level(LOG_INFO);
				break;
			}

			case 'q':
			{
				log_set_level(LOG_ERROR);
				break;
			}

			case 'h':
			{
				commandline_help(stderr);
				exit(EXIT_CODE_QUIT);
				break;
			}

			default:
			{
				++errors;
				break;
			}
		}
	}

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	(void) prepare_keeper_options(&options);

	keeperOptions = options;

	return optind;
}


/*
 * cli_print_archiver_state loads config's own archiver identity (archiver
 * Id/name/hostname/region, already read from its config file by the
 * caller) and monitor's current view of every formation/group membership
 * it holds (monitor_list_archiver_memberships(), the same call the
 * archiver's own reconciler makes at startup and periodically thereafter,
 * service_archiver_reconciler.c), then prints a human-friendly summary --
 * shared between `pg_autoctl archiver show state` and `pg_autoctl show
 * state` (cli_show.c), which delegates to this when run against an
 * archiver's own configuration file rather than an ordinary keeper's.
 */
void
cli_print_archiver_state(Monitor *monitor, KeeperConfig *config)
{
	ArchiverMembershipArray membershipsArray = { 0 };

	if (!monitor_list_archiver_memberships(monitor, config->archiverId,
										   &membershipsArray))
	{
		log_fatal("Failed to list this archiver's formation memberships, "
				  "see above for details");
		exit(EXIT_CODE_MONITOR);
	}

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();
		JSON_Object *jsObj = json_value_get_object(js);

		json_object_set_string(jsObj, "name", config->name);
		json_object_set_string(jsObj, "hostname", config->hostname);
		json_object_set_number(jsObj, "archiver_id", (double) config->archiverId);

		JSON_Value *jsMemberships = json_value_init_array();
		JSON_Array *jsArray = json_value_get_array(jsMemberships);

		for (int i = 0; i < membershipsArray.count; i++)
		{
			ArchiverMembership *membership = &(membershipsArray.memberships[i]);

			JSON_Value *jsMembership = json_value_init_object();
			JSON_Object *jsMembershipObj = json_value_get_object(jsMembership);

			json_object_set_string(jsMembershipObj, "formation",
								   membership->formation);
			json_object_set_number(jsMembershipObj, "group", membership->groupId);
			json_object_set_number(jsMembershipObj, "node_id",
								   (double) membership->nodeId);
			json_object_set_string(jsMembershipObj, "reported_state",
								   NodeStateToString(membership->reportedState));
			json_object_set_string(jsMembershipObj, "goal_state",
								   NodeStateToString(membership->goalState));

			json_array_append_value(jsArray, jsMembership);
		}

		json_object_set_value(jsObj, "memberships", jsMemberships);

		(void) cli_pprint_json(js);

		return;
	}

	fformat(stdout, "Archiver \"%s\" (%s), archiver id %" PRId64 "\n",
			config->name, config->hostname, config->archiverId);
	fformat(stdout, "\n");

	if (membershipsArray.count == 0)
	{
		fformat(stdout, "This archiver is not attached to any formation.\n");
		return;
	}

	fformat(stdout, "%20s | %5s | %10s | %14s | %14s\n",
			"Formation", "Group", "Node Id", "Reported State", "Goal State");
	fformat(stdout, "%20s-+-%5s-+-%10s-+-%14s-+-%14s\n",
			"--------------------", "-----", "----------",
			"--------------", "--------------");

	for (int i = 0; i < membershipsArray.count; i++)
	{
		ArchiverMembership *membership = &(membershipsArray.memberships[i]);

		fformat(stdout, "%20s | %5d | %10" PRId64 " | %14s | %14s\n",
				membership->formation, membership->groupId, membership->nodeId,
				NodeStateToString(membership->reportedState),
				NodeStateToString(membership->goalState));
	}
}


/*
 * cli_archiver_show_state implements `pg_autoctl archiver show state`:
 * loads this archiver's own local config, connects to the monitor it
 * names, and prints this archiver's identity and every formation/group
 * membership it currently holds -- see cli_print_archiver_state's own
 * comment for the shared rendering, also reused by `pg_autoctl show
 * state` (cli_show.c) when run against an archiver's config file.
 */
static void
cli_archiver_show_state(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	if (!keeper_config_read_file(&config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		log_fatal("Failed to read the archiver configuration file \"%s\", "
				  "see above for details", config.pathnames.config);
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (strcmp(config.nodeKind, "archiver") != 0)
	{
		log_fatal("\"%s\" is not an archiver's configuration file "
				  "(pg_autoctl.nodekind is \"%s\", expected \"archiver\")",
				  config.pathnames.config, config.nodeKind);
		exit(EXIT_CODE_BAD_CONFIG);
	}

	Monitor monitor = { 0 };

	if (!monitor_init(&monitor, config.monitor_pguri))
	{
		/* errors already logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	(void) cli_print_archiver_state(&monitor, &config);
}


CommandLine archiver_show_basebackup_command =
	make_command(
		"basebackup",
		"List base backups for a formation/group",
		" --monitor --formation --group [ --json ] ",
		"  --monitor    monitor uri to connect to\n"
		"  --formation  formation to list base backups for\n"
		"  --group      group to list base backups for\n"
		"  --json       output data in the JSON format\n",
		cli_archiver_show_basebackup_wal_getopts,
		cli_archiver_show_basebackup);

CommandLine archiver_show_wal_command =
	make_command(
		"wal",
		"List captured WAL segments for a formation/group",
		" --monitor --formation --group [ --json ] ",
		"  --monitor    monitor uri to connect to\n"
		"  --formation  formation to list captured WAL for\n"
		"  --group      group to list captured WAL for\n"
		"  --json       output data in the JSON format\n",
		cli_archiver_show_basebackup_wal_getopts,
		cli_archiver_show_wal);

CommandLine archiver_show_state_command =
	make_command(
		"state",
		"Show this archiver's own identity and formation memberships",
		" [ --pgdata ] [ --json ] ",
		"  --pgdata  path to the archiver's local data/cache directory\n"
		"  --json    output data in the JSON format\n",
		cli_archiver_show_state_getopts,
		cli_archiver_show_state);

CommandLine *archiver_show_subcommands[] = {
	&archiver_show_basebackup_command,
	&archiver_show_wal_command,
	&archiver_show_state_command,
	NULL
};

CommandLine archiver_show_commands =
	make_command_set("show",
					 "Show base backups, captured WAL, and state for an archiver", NULL,
					 NULL,
					 NULL, archiver_show_subcommands);

CommandLine *archiver_subcommands[] = {
	&archiver_serve_command,
	&archiver_formation_commands,
	&archiver_show_commands,
	NULL
};

CommandLine archiver_commands =
	make_command_set("archiver",
					 "Manage a pg_auto_failover archiver node", NULL, NULL,
					 NULL, archiver_subcommands);
