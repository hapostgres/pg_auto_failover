/*
 * cli_pgbouncer.c
 *     Implementation of a CLI to manage a pgbouncer instance.
 *
 * TODO: Add Copyright note
 *
 */
#include <getopt.h>
#include <sys/types.h>
#include <unistd.h>
#include <wait.h>

#include "postgres_fe.h"

#include "commandline.h"
#include "cli_root.h"
#include "defaults.h"
#include "ini_file.h"
#include "log.h"
#include "monitor.h"
#include "pgsql.h"
#include "string_utils.h"

struct PgbouncerConfig {
	int		listenPort;

	char	adminUsers[NAMEDATALEN];
	char	monitorPgUri[MAXCONNINFO];
	char	configFile[MAXPGPATH];
	char	pidFile[MAXPGPATH];
} pgbouncerConfig;

/* Forward declaration of commands */
static int pgbouncer_create_getopts(int argc, char **argv);
static void pgbouncer_create(int argc, char **argv);

CommandLine create_pgbouncer_command =
	make_command("pgbouncer",
				 "Create a new pgbouncer instance to connect to primary",
				 "[ --config --monitor --help ] ",
				 "  --config     pgbouncer config file (required)\n"
				 "  --monitor    pg_auto_failover Monitor Postgres URL (required)\n"
				 "  --help       show this message \n",
				 pgbouncer_create_getopts,
				 pgbouncer_create);

/*--------------------
 * Get opts section
 */

static int
pgbouncer_create_getopts(int argc, char **argv)
{
	int	c;
	int	errors = 0;
	int	option_index = 0;

	static struct option long_options[] = {
		{ "config",	required_argument, NULL, 'c' },
		{ "help", no_argument, NULL, 'h' },
		{ "monitor", required_argument, NULL, 'm' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	while ((c = getopt_long(argc, argv, "c:hm:",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'c':
				if (strlcpy(pgbouncerConfig.configFile,
							optarg,
							sizeof(pgbouncerConfig.configFile)) >=
					sizeof(pgbouncerConfig.configFile))
				{
					log_error("config file too long, greater than  %ld",
								sizeof(pgbouncerConfig.configFile) -1);
					exit(EXIT_CODE_BAD_ARGS);
				}

				log_trace("--config %s", pgbouncerConfig.configFile);
				break;

			case 'm':
				if (!validate_connection_string(optarg))
				{
					log_fatal("Failed to parse --monitor connection string, "
							  "see above for details.");
					exit(EXIT_CODE_BAD_ARGS);
				}
				/* validate_connection_string has already checked size */
				strlcpy(pgbouncerConfig.monitorPgUri, optarg, MAXCONNINFO);
				log_trace("--monitor %s", pgbouncerConfig.monitorPgUri);
				break;

			case 'h':
				commandline_help(stderr);
				exit(EXIT_CODE_QUIT);
				break;

			default:
				/* getopt_long already wrote an error message */
				errors++;
		}
	}

	if (errors > 0 ||
		IS_EMPTY_STRING_BUFFER(pgbouncerConfig.configFile) ||
		IS_EMPTY_STRING_BUFFER(pgbouncerConfig.monitorPgUri)
		)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	return optind;
}


/*--------------------
 * The actual commands section
 * 		and their helpers
 */

/*
 * Write a databases.ini file for pgbouncer to point to primary
 *
 * 		Which databases to follow?
 *		Which users to set up?
 */
static bool
write_pgbouncer_databases_ini_section(NodeAddress primary)
{
	FILE	   *fileStream = NULL;
	const char *filePath = "/tmp/databases.ini"; /* XXX: which path to use */
	IniOption  *databasesOption;
	char		buf[BUFSIZE];
	bool		success = false;

	if (snprintf(buf, sizeof(buf),
					"host=%s "
					"port=%d "
					"dbname=postgres" /* XXX: This needs expansion */,
					primary.host, primary.port) >= sizeof(buf))
	{
		log_error("Failed to write database section %m");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	databasesOption = (IniOption[]){
		make_strbuf_option("databases", "primary", "primary",
							true, BUFSIZE, buf),
		INI_OPTION_LAST
	};

	log_trace("databases.ini \"%s\"", filePath);

	fileStream = fopen_with_umask(filePath, "w", FOPEN_FLAGS_W, 0644);
	if (fileStream == NULL)
	{
		/* errors have already been logged */
		return success;
	}

	success = write_ini_to_stream(fileStream, databasesOption);
	if (fclose(fileStream) == EOF)
	{
		log_error("Failed to write file \"%s\"", filePath);
		return success;
	}

	return success;
}

/*
 * Write our private configuration
 * 		XXX: This for the moment is a poor man's implementation as we only read
 * 		the whole user supplied ini file and simply add the databases section on
 * 		top.
 *
 *		To be done properly, we have to use the INI infrastructure to ALSO
 *		validate the contents. Then merge our database section and write it to
 *		where it should be.
 */
static bool
write_pgbouncer_private_config(void)
{
	FILE   *fp = NULL;
	char   *buf;
	long	s;

	fp = fopen(pgbouncerConfig.configFile, "rb");
	if (!fp)
	{
		log_error("Failed to open config file \"%s\" %m",
					pgbouncerConfig.configFile);
		return false;
	}

	fseek(fp, 0L, SEEK_END);
	s = ftell(fp);
	rewind(fp);

	buf = malloc(s);
	if (fread(buf, 1, s, fp) != s)
	{
		log_error("Failed to read config file \"%s\" %m",
					pgbouncerConfig.configFile);
		free(buf);
		fclose(fp);

		return false;
	}
	fclose(fp);

	/* XXX: Where to keep it ? */
	fp = fopen("/tmp/ourPgbouncerConfig.ini", "w");
	if (!fp)
	{
		log_error("Failed to open config file \"%s\" %m",
					pgbouncerConfig.configFile);
		free(buf);
		return false;
	}

	(void) fwrite("\%include /tmp/databases.ini\n",
					strlen("\%include /tmp/databases.ini\n"),
					1, fp);
	(void) fwrite(buf, s, 1, fp);
	fclose(fp);

	free(buf);
	log_info("Wrote /tmp/ourPgbouncerConfig.ini");

	return true;
};


static void
dance(const char *pgbouncerProgram, Monitor monitor, NodeAddress primary)
{
	pid_t	pid;

	IntString semIdString = intToString(log_semaphore.semId);
	setenv(PG_AUTOCTL_DEBUG, "1", 1);
	setenv(PG_AUTOCTL_LOG_SEMAPHORE, semIdString.strValue, 1);

	pid = fork();

	switch (pid)
	{
		case -1:
			return;
		case 0:
		{
			char * const args[] = {
				(char *)pgbouncerProgram,
				"/tmp/ourPgbouncerConfig.ini",
				NULL
			};

			if (execv(pgbouncerProgram, args) == -1)
			{
				fprintf(stdout, "%s\n", strerror(errno));
				fprintf(stderr, "%s\n", strerror(errno));
				exit(EXIT_CODE_INTERNAL_ERROR);
			}

			return;
		}
		default:
		{
			int wstatus;

			(void) monitor_setup_notifications(&monitor, 0 /* groupId */, primary.nodeId);

			do
			{
				int timeoutMs = PG_AUTOCTL_KEEPER_SLEEP_TIME * 1000;
				bool groupStateHasChanged = false;

				(void) monitor_wait_for_state_change(&monitor,
													"default" /* formation */,
													0 /* group */,
													primary.nodeId,
													timeoutMs,
													&groupStateHasChanged);

				/*
				 * If the group state has changed, then we have to make certain
				 * that pgbouncer is pointing to the correct primary.
				 * All current connections to the primary should get paused and
				 * the configuration has to get re-writen.
				 */
				if (groupStateHasChanged)
				{
					log_info("State has changed, rewriting configuration");

					if (waitpid(pid, &wstatus, WNOHANG) < 0)
					{
						log_fatal("Child not running, exiting");
						exit(EXIT_CODE_BAD_STATE);
					}

					/*
					 * Signaling SIGUSR1 to pgbouncer is equivalent to issuing
					 * PAUSE in the console
					 */
					if (kill(pid, SIGUSR1))
					{
						log_fatal("Failed to pause pgbouncer");
						exit(EXIT_CODE_INTERNAL_ERROR);
					}

					if (!monitor_wait_until_some_node_reported_state(&monitor,
																	 "default" /* formation */,
																	 0 /* groupId */,
																	 NODE_KIND_UNKNOWN,
																	 PRIMARY_STATE))
					{
						log_error("Failed to wait until a new primary has been notified");
						exit(EXIT_CODE_INTERNAL_ERROR);
					}

					if (!monitor_get_primary(&monitor,
										  "default" /* formation */,
										   0 /* groupId */,
										  &primary))
					{
						log_fatal("Failed to get primary node info from monitor");
						pgsql_finish(&monitor.pgsql);
						exit(EXIT_CODE_BAD_STATE);
					}

					/* Set up pgbouncer config*/
					if (!write_pgbouncer_databases_ini_section(primary) ||
						!write_pgbouncer_private_config())
					{
						/* It has already logged why */
						pgsql_finish(&monitor.pgsql);
						exit(EXIT_CODE_INTERNAL_ERROR);
					}

					/* Signal pgbouncer to reload config */
					if (kill(pid, SIGHUP))
					{
						log_fatal("Failed to reload configuration");
						pgsql_finish(&monitor.pgsql);
						exit(EXIT_CODE_INTERNAL_ERROR);
					}

					/*
					 * Signaling SIGUSR2 to pgbouncer is equivalent to issuing
					 * RESUME in the console
					 */
					if (kill(pid, SIGUSR2))
					{
						log_fatal("Failed to reload configuration");
						pgsql_finish(&monitor.pgsql);
						exit(EXIT_CODE_INTERNAL_ERROR);
					}
				}

				if (waitpid(pid, &wstatus, WNOHANG) < 0)
				{
					if (WIFEXITED(wstatus))
					{
						/*
						 * Child terminated normally, it shouldn't have really
						 * but nothing we should do. Exit happy.
						 */
						log_info("Child existed with %d", WEXITSTATUS(wstatus));
						break;
					}
					else if (WIFSIGNALED(wstatus))
					{
						/*
						 * Child terminated by a signal. Exit??
						 */
						log_info("Child got signaled with %d", WTERMSIG(wstatus));
						break;
					}
#ifdef WCOREDUMP
					else if (WCOREDUMP(wstatus))
					{
						/*
						 * Child coredumped, restart it
						 */
						break;
					}
#endif
					else if (WIFCONTINUED(wstatus))
					{
						/*
						 * Child continued, nothing to do
						 */
					}
				}
			} while (true);
			break;
		}
	}

	pgsql_finish(&monitor.pgsql);
}

static void
pgbouncer_create(int argc, char **argv)
{
	Monitor		monitor;
	NodeAddress primary;
	char		pgbouncerProgram[MAXPGPATH];

	if (!search_path_first("pgbouncer", pgbouncerProgram))
	{
		log_error("Failed to find pgbouncer binary in env");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/*
	 * * First try to get the active primary, if none is set, exit
	 * * Register to get notified on the right channel
	 * * Set up pgbouncer and launch in the background
	 * * React when notification is received
	 */

	if (!monitor_init(&monitor, pgbouncerConfig.monitorPgUri))
	{
		log_fatal("Failed to contact the monitor because its URL is invalid, "
				  "see above for details");
		exit(EXIT_CODE_BAD_ARGS);
	}

	/*
	 * XXX:
	 * find where you can get the formation from and the groupId I suppose
	 * * from pgdata? Monitor? configuration?
	 * * User supplied it from the command line options
	 */
	if (!monitor_get_primary(&monitor,
							 "default" /* formation */,
							 0 /* groupId */,
							 &primary))
	{
		log_fatal("Failed to get primary node info from monitor. "
				  "see above for details");
		pgsql_finish(&monitor.pgsql);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!primary.isPrimary)
	{
		log_fatal("Failed to get primary node info from monitor");
		pgsql_finish(&monitor.pgsql);
		exit(EXIT_CODE_BAD_STATE);
	}

	log_debug("Primary: %s "
			  "nodeId: %d, name %s, host %s, port %d",
					primary.isPrimary ? "True": "False",
					primary.nodeId, primary.name,
					primary.host, primary.port);

	/* Set up pgbouncer config*/
	if (!write_pgbouncer_databases_ini_section(primary) ||
		!write_pgbouncer_private_config())
	{
		/* It has already logged why */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	dance(pgbouncerProgram, monitor, primary);
}
