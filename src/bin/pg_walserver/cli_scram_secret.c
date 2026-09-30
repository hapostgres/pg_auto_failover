/*
 * src/bin/pg_walserver/cli_scram_secret.c
 *   See cli_scram_secret.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <getopt.h>
#include <stdlib.h>
#include <string.h>

#include "postgres_fe.h"

#include "commandline.h"

#include "cli_root.h"
#include "cli_scram_secret.h"
#include "defaults.h"
#include "env_utils.h"
#include "file_utils.h"
#include "log.h"
#include "scram.h"
#include "string_utils.h"

/* -----------------------------------------------------------------------
 * pg_walserver scram-secret [--user <name>]   (password in PGPASSWORD)
 * ----------------------------------------------------------------------- */

static char scramUser[NAMEDATALEN] = PG_AUTOCTL_REPLICA_USERNAME;

static struct option scramSecretLongOptions[] = {
	{ "user", required_argument, NULL, 'U' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_scram_secret_getopt parses scram-secret's only flag, --user.
 */
static int
cli_scram_secret_getopt(int argc, char **argv)
{
	optind = 0;
	strlcpy(scramUser, PG_AUTOCTL_REPLICA_USERNAME, sizeof(scramUser));

	int c;

	while ((c = getopt_long(argc, argv, "U:", scramSecretLongOptions,
							NULL)) != -1)
	{
		switch (c)
		{
			case 'U':
			{
				strlcpy(scramUser, optarg, sizeof(scramUser));
				break;
			}

			default:
			{
				commandline_print_usage(&ws_root, stderr);
				exit(1);
			}
		}
	}

	return optind;
}


/*
 * cli_scram_secret_run reads the password from the PGPASSWORD environment
 * variable (never from the command line, where it would show up in the
 * process list), builds its SCRAM-SHA-256 verifier, and prints one
 * "user:secret" pg_walserver_passwd line to stdout.
 */
static void
cli_scram_secret_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	char password[512] = { 0 };

	if (!get_env_copy("PGPASSWORD", password, sizeof(password)) ||
		password[0] == '\0')
	{
		fformat(stderr,
				"scram-secret: set the password in PGPASSWORD\n");
		exit(1);
	}

	char secret[512];

	if (!scram_build_verifier(password, WS_SCRAM_ITERATIONS,
							  secret, sizeof(secret)))
	{
		fformat(stderr, "scram-secret: failed to build the secret\n");
		exit(1);
	}

	fformat(stdout, "%s:%s\n", scramUser, secret);

	exit(0);
}


CommandLine scram_secret_command =
	make_command("scram-secret",
				 "Print one pg_walserver_passwd line for a user",
				 "[--user <name>]  (password read from PGPASSWORD)",
				 "  --user      role name (default: " PG_AUTOCTL_REPLICA_USERNAME ")\n"
																				  "\n"
																				  "  The password is read from the PGPASSWORD environment "
																				  "variable, never\n"
																				  "  from the command line.\n",
				 cli_scram_secret_getopt, cli_scram_secret_run);
