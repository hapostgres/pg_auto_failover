/*
 * src/bin/pg_autoctl/cli_basebackup_policy.c
 *   See cli_basebackup_policy.h.
 *
 * A basebackup_policy row is a monitor-side object, not tied to any one
 * node's local pgdata (unlike `pg_autoctl create archiver`'s own --pgdata-
 * rooted config), so these commands connect straight to --monitor, the
 * same self-contained shape create_archiver_command already uses, rather
 * than resolving a monitor URL through an existing node's config file the
 * way the `get`/`set` property commands (cli_get_set_properties.c) do.
 *
 * --config <path> is a JSON document read from disk and passed straight
 * through, as text, to the monitor's own create_basebackup_policy()/set_
 * basebackup_policy() (pgautofailover.sql) -- their own jsonb cast and
 * per-field coalesce-to-default/coalesce-to-current-value logic is the one
 * and only place this document actually gets validated and applied, so
 * there is nothing to duplicate client-side. The document is the flat
 * policy body itself (source/replaymode/cache/frequency/maxcount/maxage/
 * onpromotion/concurrency, whichever subset is being set) -- not wrapped
 * in the design doc's own illustrative "pgaf-archiver"/"basebackup-policy"
 * namespace, since the monitor-side functions this calls don't unwrap one.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <getopt.h>
#include <inttypes.h>

#include "postgres_fe.h"

#include "parson.h"

#include "cli_basebackup_policy.h"
#include "cli_common.h"
#include "commandline.h"
#include "defaults.h"
#include "file_utils.h"
#include "log.h"
#include "monitor.h"
#include "string_utils.h"

typedef struct BasebackupPolicyCLIOptions
{
	char monitorPguri[MAXCONNINFO];
	char policyName[NAMEDATALEN];
	char configFilePath[MAXPGPATH];
} BasebackupPolicyCLIOptions;

static BasebackupPolicyCLIOptions basebackupPolicyOptions = { 0 };

static int cli_basebackup_policy_getopts(int argc, char **argv,
										 bool requireConfig);
static int cli_create_basebackup_policy_getopts(int argc, char **argv);
static int cli_show_basebackup_policy_getopts(int argc, char **argv);
static int cli_set_basebackup_policy_getopts(int argc, char **argv);

static void cli_create_basebackup_policy(int argc, char **argv);
static void cli_show_basebackup_policy(int argc, char **argv);
static void cli_set_basebackup_policy(int argc, char **argv);

static bool read_json_config_file(const char *path, char *jsonOut,
								  size_t jsonOutSize);
static void print_basebackup_policy(BasebackupPolicy *policy);


/*
 * cli_basebackup_policy_getopts parses the option set shared by all three
 * commands (--monitor --name --json, plus --config for create/set). Kept
 * as one function with a requireConfig switch rather than three near-
 * duplicates, matching cli_create_archiver_getopts's own minimal, hand-
 * rolled style for this milestone's own archiver-adjacent commands
 * (rather than the ordinary-node cli_create_node_getopts, which assumes a
 * real PostgresSetup none of these commands have any use for).
 */
static int
cli_basebackup_policy_getopts(int argc, char **argv, bool requireConfig)
{
	int c, option_index = 0, errors = 0;

	static struct option long_options[] = {
		{ "monitor", required_argument, NULL, 'm' },
		{ "name", required_argument, NULL, 'a' },
		{ "config", required_argument, NULL, 'c' },
		{ "json", no_argument, NULL, 'J' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	while ((c = getopt_long(argc, argv, "m:a:c:JVvqh",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'm':
			{
				if (!validate_connection_string(optarg))
				{
					log_fatal("Failed to parse --monitor connection string, "
							  "see above for details.");
					exit(EXIT_CODE_BAD_ARGS);
				}
				strlcpy(basebackupPolicyOptions.monitorPguri, optarg,
						MAXCONNINFO);
				log_trace("--monitor %s", basebackupPolicyOptions.monitorPguri);
				break;
			}

			case 'a':
			{
				strlcpy(basebackupPolicyOptions.policyName, optarg,
						NAMEDATALEN);
				log_trace("--name %s", basebackupPolicyOptions.policyName);
				break;
			}

			case 'c':
			{
				strlcpy(basebackupPolicyOptions.configFilePath, optarg,
						MAXPGPATH);
				log_trace("--config %s", basebackupPolicyOptions.configFilePath);
				break;
			}

			case 'J':
			{
				outputJSON = true;
				break;
			}

			case 'V':
			{
				keeper_cli_print_version(argc, argv);
				exit(EXIT_CODE_QUIT);
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

	if (IS_EMPTY_STRING_BUFFER(basebackupPolicyOptions.monitorPguri))
	{
		log_fatal("Failed to get value for --monitor");
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (IS_EMPTY_STRING_BUFFER(basebackupPolicyOptions.policyName))
	{
		log_fatal("Failed to get value for --name");
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (requireConfig && IS_EMPTY_STRING_BUFFER(basebackupPolicyOptions.configFilePath))
	{
		log_fatal("Failed to get value for --config");
		exit(EXIT_CODE_BAD_ARGS);
	}

	return optind;
}


static int
cli_create_basebackup_policy_getopts(int argc, char **argv)
{
	return cli_basebackup_policy_getopts(argc, argv, true);
}


static int
cli_set_basebackup_policy_getopts(int argc, char **argv)
{
	return cli_basebackup_policy_getopts(argc, argv, true);
}


static int
cli_show_basebackup_policy_getopts(int argc, char **argv)
{
	return cli_basebackup_policy_getopts(argc, argv, false);
}


/*
 * read_json_config_file reads path's whole contents into jsonOut, for
 * pass-through to the monitor's own ::jsonb cast -- no client-side JSON
 * parsing/validation, see this file's own header comment on why.
 */
static bool
read_json_config_file(const char *path, char *jsonOut, size_t jsonOutSize)
{
	char *contents = NULL;
	long fileSize = 0;

	if (!read_file(path, &contents, &fileSize))
	{
		log_error("Failed to read base-backup policy config file \"%s\"",
				  path);
		return false;
	}

	strlcpy(jsonOut, contents, jsonOutSize);
	free(contents);

	return true;
}


/*
 * print_basebackup_policy prints a resolved policy either as plain text
 * (one "field: value" line each) or, with --json, the same fields as a
 * JSON object -- matching cli_get_set_properties.c's own established
 * plain/--json duality for monitor-resolved properties.
 */
static void
print_basebackup_policy(BasebackupPolicy *policy)
{
	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();
		JSON_Object *jsObj = json_value_get_object(js);

		json_object_set_string(jsObj, "name", policy->policyName);
		json_object_set_string(jsObj, "source", policy->source);
		json_object_set_string(jsObj, "replaymode", policy->replayMode);
		json_object_set_string(jsObj, "cache", policy->cache);
		json_object_set_number(jsObj, "frequency-seconds",
							   (double) policy->frequencySeconds);
		json_object_set_number(jsObj, "maxcount", (double) policy->maxCount);
		json_object_set_number(jsObj, "maxage-seconds",
							   (double) policy->maxAgeSeconds);
		json_object_set_boolean(jsObj, "onpromotion", policy->onPromotion);
		json_object_set_number(jsObj, "concurrency",
							   (double) policy->concurrency);

		(void) cli_pprint_json(js);

		return;
	}

	fformat(stdout, "%12s: %s\n", "name", policy->policyName);
	fformat(stdout, "%12s: %s\n", "source", policy->source);
	fformat(stdout, "%12s: %s\n", "replaymode",
			IS_EMPTY_STRING_BUFFER(policy->replayMode) ? "-" : policy->replayMode);
	fformat(stdout, "%12s: %s\n", "cache", policy->cache);
	fformat(stdout, "%12s: %d\n", "frequency", policy->frequencySeconds);
	fformat(stdout, "%12s: %d\n", "maxcount", policy->maxCount);
	fformat(stdout, "%12s: %d\n", "maxage", policy->maxAgeSeconds);
	fformat(stdout, "%12s: %s\n", "onpromotion",
			policy->onPromotion ? "true" : "false");
	fformat(stdout, "%12s: %d\n", "concurrency", policy->concurrency);
}


/*
 * cli_create_basebackup_policy implements `pg_autoctl create basebackup-
 * policy`.
 */
static void
cli_create_basebackup_policy(int argc, char **argv)
{
	char jsonSpec[BUFSIZE] = { 0 };

	if (!read_json_config_file(basebackupPolicyOptions.configFilePath,
							   jsonSpec, sizeof(jsonSpec)))
	{
		/* errors already logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	Monitor monitor = { 0 };

	if (!monitor_init(&monitor, basebackupPolicyOptions.monitorPguri))
	{
		/* errors already logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	int64_t basebackupPolicyId = 0;

	if (!monitor_create_basebackup_policy(&monitor,
										  basebackupPolicyOptions.policyName,
										  jsonSpec, &basebackupPolicyId))
	{
		log_fatal("Failed to create base-backup policy \"%s\", see above "
				  "for details", basebackupPolicyOptions.policyName);
		exit(EXIT_CODE_MONITOR);
	}

	log_info("Created base-backup policy \"%s\" (id %" PRId64 ")",
			 basebackupPolicyOptions.policyName, basebackupPolicyId);
}


/*
 * cli_show_basebackup_policy implements `pg_autoctl show basebackup-
 * policy`.
 */
static void
cli_show_basebackup_policy(int argc, char **argv)
{
	Monitor monitor = { 0 };

	if (!monitor_init(&monitor, basebackupPolicyOptions.monitorPguri))
	{
		/* errors already logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	BasebackupPolicy policy = { 0 };
	bool found = false;

	if (!monitor_get_basebackup_policy(&monitor,
									   basebackupPolicyOptions.policyName,
									   &policy, &found))
	{
		log_fatal("Failed to get base-backup policy \"%s\", see above for "
				  "details", basebackupPolicyOptions.policyName);
		exit(EXIT_CODE_MONITOR);
	}

	if (!found)
	{
		log_fatal("Base-backup policy \"%s\" does not exist",
				  basebackupPolicyOptions.policyName);
		exit(EXIT_CODE_BAD_ARGS);
	}

	print_basebackup_policy(&policy);
}


/*
 * cli_set_basebackup_policy implements `pg_autoctl set basebackup-
 * policy`.
 */
static void
cli_set_basebackup_policy(int argc, char **argv)
{
	char jsonSpec[BUFSIZE] = { 0 };

	if (!read_json_config_file(basebackupPolicyOptions.configFilePath,
							   jsonSpec, sizeof(jsonSpec)))
	{
		/* errors already logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	Monitor monitor = { 0 };

	if (!monitor_init(&monitor, basebackupPolicyOptions.monitorPguri))
	{
		/* errors already logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!monitor_set_basebackup_policy(&monitor,
									   basebackupPolicyOptions.policyName,
									   jsonSpec))
	{
		log_fatal("Failed to set base-backup policy \"%s\", see above for "
				  "details", basebackupPolicyOptions.policyName);
		exit(EXIT_CODE_MONITOR);
	}

	log_info("Updated base-backup policy \"%s\"",
			 basebackupPolicyOptions.policyName);
}


CommandLine create_basebackup_policy_command =
	make_command(
		"basebackup-policy",
		"Create a named base-backup production/retention policy",
		" --monitor --name --config ",
		"  --monitor   pg_auto_failover Monitor Postgres URL\n"
		"  --name      policy name\n"
		"  --config    path to a JSON document with the policy body\n",
		cli_create_basebackup_policy_getopts,
		cli_create_basebackup_policy);

CommandLine show_basebackup_policy_command =
	make_command(
		"basebackup-policy",
		"Show a named base-backup production/retention policy",
		" --monitor --name [ --json ] ",
		"  --monitor   pg_auto_failover Monitor Postgres URL\n"
		"  --name      policy name\n"
		"  --json      output data in the JSON format\n",
		cli_show_basebackup_policy_getopts,
		cli_show_basebackup_policy);

CommandLine set_basebackup_policy_command =
	make_command(
		"basebackup-policy",
		"Update a named base-backup production/retention policy",
		" --monitor --name --config ",
		"  --monitor   pg_auto_failover Monitor Postgres URL\n"
		"  --name      policy name\n"
		"  --config    path to a JSON document with the fields to change\n",
		cli_set_basebackup_policy_getopts,
		cli_set_basebackup_policy);
