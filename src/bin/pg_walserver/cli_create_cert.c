/*
 * src/bin/pg_walserver/cli_create_cert.c
 *   See cli_create_cert.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <getopt.h>
#include <string.h>

#include "postgres_fe.h"

#include "commandline.h"

#include "cli_common.h"
#include "cli_create_cert.h"
#include "cli_root.h"
#include "file_utils.h"
#include "log.h"
#include "pgctl.h"
#include "pgsetup.h"


/* -----------------------------------------------------------------------
 * pg_walserver create-cert --pgdata <path> --hostname <name> [--force]
 * ----------------------------------------------------------------------- */

static char createCertPgdata[MAXPGPATH] = { 0 };
static char createCertHostname[_POSIX_HOST_NAME_MAX] = { 0 };
static bool createCertForce = false;

static struct option createCertLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "hostname", required_argument, NULL, 'n' },
	{ "force", no_argument, NULL, 'f' },
	{ NULL, 0, NULL, 0 }
};

static int cli_create_cert_getopt(int argc, char **argv);
static void cli_create_cert_command_run(int argc, char **argv);

CommandLine create_cert_command =
	make_command("create-cert",
				 "Create a self-signed TLS certificate for --pgdata",
				 "--pgdata <path> --hostname <name> [--force]",
				 "  --pgdata    this instance's own top-level storage root "
				 "(defaults to\n"
				 "              PGDATA); the certificate is written as "
				 "\"<pgdata>/server.crt\"\n"
				 "              and \"<pgdata>/server.key\"\n"
				 "  --hostname  the certificate's own CN/subject (the name "
				 "a client's\n"
				 "              TLS SNI, or a human, is expected to use to "
				 "reach this\n"
				 "              server)\n"
				 "  --force     overwrite an already-existing server.crt/"
				 "server.key\n",
				 cli_create_cert_getopt, cli_create_cert_command_run);


/*
 * ws_create_cert_run writes <pgdata>/server.crt and server.key via
 * pg_create_self_signed_cert() (src/bin/common/pgctl.c), refusing to
 * overwrite already-existing files unless force is set. The one shared
 * helper both `pg_walserver create-cert` (by hand) and cli_setup.c's own
 * ensure_tls_for_multiple_routes() (automatically, the moment a second
 * named route needs a certificate) call, so the create-and-log sequence
 * isn't duplicated between the two call sites. Returns true on success,
 * false with an error already logged otherwise.
 */
bool
ws_create_cert_run(const char *pgdata, const char *hostname, bool force)
{
	if (pgdata == NULL || pgdata[0] == '\0')
	{
		log_error("create-cert requires --pgdata");
		return false;
	}

	if (hostname == NULL || hostname[0] == '\0')
	{
		log_error("create-cert requires --hostname");
		return false;
	}

	char certPath[MAXPGPATH] = { 0 };
	char keyPath[MAXPGPATH] = { 0 };

	sformat(certPath, sizeof(certPath), "%s/server.crt", pgdata);
	sformat(keyPath, sizeof(keyPath), "%s/server.key", pgdata);

	if (!force && (file_exists(certPath) || file_exists(keyPath)))
	{
		log_error("\"%s\" and/or \"%s\" already exist -- pass --force to "
				  "overwrite them", certPath, keyPath);
		return false;
	}

	PostgresSetup pgSetup = { 0 };

	strlcpy(pgSetup.pgdata, pgdata, sizeof(pgSetup.pgdata));

	if (!pg_create_self_signed_cert(&pgSetup, hostname))
	{
		/* errors have already been logged */
		return false;
	}

	log_info("Created a self-signed certificate for \"%s\" (\"%s\"/\"%s\", "
			 "CN=%s) -- replace it with a real one before running on a "
			 "reachable network", pgdata, certPath, keyPath, hostname);

	return true;
}


/*
 * cli_create_cert_getopt parses "pg_walserver create-cert"'s own flags into the
 * file-scope statics above.
 */
static int
cli_create_cert_getopt(int argc, char **argv)
{
	optind = 0;
	createCertForce = false;
	ws_prefill_pgdata_from_env(createCertPgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:n:f",
							createCertLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(createCertPgdata, optarg, sizeof(createCertPgdata));
				break;
			}

			case 'n':
			{
				strlcpy(createCertHostname, optarg, sizeof(createCertHostname));
				break;
			}

			case 'f':
			{
				createCertForce = true;
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
 * cli_create_cert_command_run runs "pg_walserver create-cert" against the
 * options cli_create_cert_getopt parsed above, then exit()s with its own
 * result.
 */
static void
cli_create_cert_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	exit(ws_create_cert_run(createCertPgdata, createCertHostname,
							createCertForce) ? 0 : 1);
}
