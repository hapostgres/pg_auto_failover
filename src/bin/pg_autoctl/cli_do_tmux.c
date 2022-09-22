/*
 * src/bin/pg_autoctl/cli_do_tmux.c
 *     Implementation of a CLI which lets you run operations on the local
 *     postgres server directly.
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

#if defined(__linux__)
#include <linux/limits.h>
#endif

#include "postgres_fe.h"
#include "pqexpbuffer.h"
#include "snprintf.h"

#include "cli_common.h"
#include "cli_do_root.h"
#include "cli_do_tmux.h"
#include "cli_root.h"
#include "commandline.h"
#include "config.h"
#include "env_utils.h"
#include "log.h"
#include "pidfile.h"
#include "signals.h"
#include "string_utils.h"

#include "runprogram.h"

char *tmux_banner[] = {
	"# to quit tmux: type either `Ctrl+b d` or `tmux detach`",
	"# to test failover: pg_autoctl perform failover",
	NULL
};

TmuxOptions tmuxOptions = { 0 };
TmuxNodeArray tmuxNodeArray = { 0 };

char *xdg[][3] = {
	{ "XDG_DATA_HOME", "share" },
	{ "XDG_CONFIG_HOME", "config" },
	{ "XDG_RUNTIME_DIR", "run" },
	{ NULL, NULL }
};

static void prepare_node_name(TmuxOptions *options, TmuxNode *node, int index);

static int compute_citus_group(int nodes, int index);
static int compute_citus_node_seq_in_group(int nodes, int index);
static bool compute_citus_node_is_secondary(int nodes, int secondaries, int index);

static int compute_node_count(TmuxOptions *options);

static void prepare_tmux_script(TmuxOptions *options, PQExpBuffer script);
static bool tmux_stop_pg_autoctl(TmuxOptions *options);
static bool parseCandidatePriority(char *priorityString,
								   int pIndex, int *priorities);
static bool prepareTmuxNodeArray(TmuxOptions *options, TmuxNodeArray *nodeArray);


/*
 * parseCandidatePriority parses a single candidate priority item into given
 * index in the priorities integer array of MAX_NODES capacity
 */
static bool
parseCandidatePriority(char *priorityString, int pIndex, int *priorities)
{
	if (MAX_NODES <= pIndex)
	{
		log_error("Failed to parse --node-priorities: "
				  "pg_autoctl do tmux session supports up to %d nodes",
				  MAX_NODES);
		return false;
	}

	if (!stringToInt(priorityString, &(priorities[pIndex])))
	{
		log_error("Failed to parse --node-priorities \"%s\"", priorityString);
		return false;
	}

	log_trace("parseCandidatePriorities[%d] = %d", pIndex, priorities[pIndex]);

	return true;
}


/*
 * parseCandidatePriorities parses the --node-priorities options on the command
 * line and fills-in an array of nodes.
 *
 *   --node-priorities 50:       all node have 50
 *   --node-priorities 50,50,0:  3+ nodes, first two have 50, then 0
 */
bool
parseCandidatePriorities(char *prioritiesString, int *priorities)
{
	char sep = ',';
	char *ptr = prioritiesString;
	char *previous = prioritiesString;

	int pIndex = 0;

	if (strcmp(prioritiesString, "") == 0)
	{
		/* fill-in the priorities array with default values (50) */
		for (int i = 0; i < MAX_NODES; i++)
		{
			priorities[i] = FAILOVER_NODE_CANDIDATE_PRIORITY;
		}
		return true;
	}

	while ((ptr = strchr(ptr, sep)) != NULL)
	{
		*ptr = '\0';

		if (!parseCandidatePriority(previous, pIndex++, priorities))
		{
			/* errors have already been logged */
			return false;
		}

		previous = ++ptr;
	}

	/* there is no separator left, parse the end of the option string */
	if (!parseCandidatePriority(previous, pIndex++, priorities))
	{
		/* errors have already been logged */
		return false;
	}

	/* mark final entry in the array; remember that pIndex > 0 here */
	for (int i = pIndex; i < MAX_NODES; i++)
	{
		priorities[i] = priorities[i - 1];
	}

	return true;
}


/*
 * prepare_node_name prepares a node name that is suitable for a
 * pg_auto_failover node in a formation, depending on the given index and how
 * many nodes per group are asked for, and whether this is a Citus formation or
 * a Postgres standalone formation.
 *
 * Whatever the number of nodes asked for, we always create a monitor, one
 * coordinator and W workers. We actually build N coordinators and N nodes per
 * workers (N*W). With WORKERS=3 and NODES=2 we have
 *
 *  index  name          index-1    (index-1)/2   (index-1)%2  group
 *      0  monitor       -1                                    -1
 *      1  coord0a        0         0             0             0
 *      2  coord0b        1         0             1             0
 *      3  worker1a       2         1             0             1
 *      4  worker1b       3         1             1             1
 *      5  worker2a       4         2             0             2
 *      6  worker2b       5         2             1             2
 *      7  worker3a       6         3             0             3
 *      8  worker3b       7         3             1             3
 *
 * Citus secondary nodes are using a specific naming convention: coord0S,
 * worker1S, and worker2S. That helps make them easy to spot.
 */
static void
prepare_node_name(TmuxOptions *options, TmuxNode *node, int index)
{
	/* index zero is always the monitor */
	char *alphabet = "abcdefghijklmnopqrstuvwxyz";

	/* for standalone Postgres formations, that's easy: node%d */
	if (options->withCitus == false)
	{
		sformat(node->name, sizeof(node->name), "node%d", index);
		return;
	}

	/* now, Citus formations are more complex */
	if (options->nodes > strlen(alphabet))
	{
		log_fatal("Node count %d is not supported, must be less than %lu",
				  options->nodes, strlen(alphabet));
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* we have N coordinators (N = nodes), and N nodes per workers */
	int totalNodesCount = compute_node_count(options);

	if (index == 0)
	{
		sformat(node->name, sizeof(node->name), "monitor");
	}
	else if (1 <= index && index <= options->nodes)
	{
		/* first N nodes are for the coordinator nodes */
		char suffix = alphabet[index - 1];
		bool secondary =
			compute_citus_node_is_secondary(
				options->nodes,
				options->citusSecondaries,
				index);

		sformat(node->name, sizeof(node->name), "coord0%c", suffix);

		if (secondary)
		{
			sformat(node->clusterName, sizeof(node->clusterName),
					"cluster_%c", suffix);
		}
	}
	else if (options->nodes < index && index <= totalNodesCount)
	{
		/* then nodes are worker nodes */
		int group = compute_citus_group(options->nodes, index);
		int seq = compute_citus_node_seq_in_group(options->nodes, index);
		bool secondary =
			compute_citus_node_is_secondary(
				options->nodes,
				options->citusSecondaries,
				index);
		char suffix = alphabet[seq];

		char *prefix = secondary ? "reader" : "worker";

		sformat(node->name, sizeof(node->name), "%s%d%c", prefix, group, suffix);

		if (secondary)
		{
			sformat(node->clusterName, sizeof(node->clusterName),
					"cluster_%c", suffix);
		}
	}
	else
	{
		log_fatal("Node index %d is not supported, must be in 0..%d ",
				  index, totalNodesCount);
		exit(EXIT_CODE_BAD_ARGS);
	}
}


/*
 * compute_node_count computes how many nodes we're going to create, depending
 * on whether this is a Citus formation or a Postgres standalone formation.
 */
static int
compute_node_count(TmuxOptions *options)
{
	if (options->withCitus)
	{
		/* we have NODES nodes per WORKERS (including the coordinator) */
		return options->nodes + (options->nodes * options->citusWorkers);
	}

	return options->nodes;
}


/*
 * compute_citus_group returns a node group for a Citus cluster node, given a
 * index and how many nodes per group are asked for. See prepare_node_name for
 * more information.
 */
static int
compute_citus_group(int nodes, int index)
{
	if (index == 0)
	{
		return -1;
	}
	else
	{
		return (index - 1) / nodes;
	}
}


/*
 * compute_citus_node_seq_in_group returns a node sequence number in its own
 * group for a Citus cluster node, given a index and how many nodes per group
 * are asked for. See prepare_node_name for more information.
 */
static int
compute_citus_node_seq_in_group(int nodes, int index)
{
	if (index == 0)
	{
		return -1;
	}
	else
	{
		return (index - 1) % nodes;
	}
}


/*
 * compute_citus_node_is_secondary returns true if the given node index is
 * going to be initialized as a citus secondary node, per the given --nodes and
 * --citus-secondaries options.
 */
static bool
compute_citus_node_is_secondary(int nodes, int secondaries, int index)
{
	int seq = compute_citus_node_seq_in_group(nodes, index);

	/* first nodes are HA nodes, then Citus Secondary nodes */
	return (nodes - secondaries) <= seq;
}


/*
 * prepareTmuxNodeArray expands the command line options into an array of
 * nodes, where each node name and properties have been computed.
 */
bool
prepareTmuxNodeArray(TmuxOptions *options, TmuxNodeArray *nodeArray)
{
	/* first pgport is for the monitor */
	int pgport = options->firstPort + 1;

	int totalNodesCount = compute_node_count(options);

	if (NODE_ARRAY_MAX_COUNT < totalNodesCount)
	{
		log_fatal("This setup requires %d nodes and pg_autoctl is limited "
				  "to %d nodes",
				  totalNodesCount,
				  NODE_ARRAY_MAX_COUNT);
		return false;
	}

	/* make sure we initialize our nodes array */
	nodeArray->count = 0;
	nodeArray->numSync = options->numSync;

	for (int i = 0; i < totalNodesCount; i++)
	{
		TmuxNode *node = &(nodeArray->nodes[i]);

		int nodeIndex = i + 1;

		node->pgport = pgport++;

		if (options->withCitus)
		{
			node->group =
				compute_citus_group(options->nodes, nodeIndex);

			node->seq =
				compute_citus_node_seq_in_group(options->nodes, nodeIndex);

			node->secondary =
				compute_citus_node_is_secondary(options->nodes,
												options->citusSecondaries,
												nodeIndex);
		}
		else
		{
			node->group = 0;
			node->seq = nodeIndex - 1;
			node->secondary = false;
		}

		/* compute node name (node1, coord0a, worker2b, etc) */
		(void) prepare_node_name(options, node, nodeIndex);

		/* install default values for now */
		node->replicationQuorum = true;
		node->candidatePriority = 50;

		/* the first nodes are sync, then async, threshold is asyncNodes */
		if ((options->nodes - options->asyncNodes) <= node->seq)
		{
			node->replicationQuorum = false;
		}

		/*
		 * Node priorities have been expanded correctly in the options, still
		 * we force priority for secondary nodes
		 */
		node->candidatePriority =
			node->secondary ? 0 : options->priorities[node->seq];

		++(nodeArray->count);
	}

	/* some useful debug information */
	int padding = totalNodesCount < 10 ? 1 : totalNodesCount < 100 ? 2 : 3;

	for (int i = 0; i < totalNodesCount; i++)
	{
		TmuxNode *node = &(nodeArray->nodes[i]);

		if (options->withCitus)
		{
			log_debug("prepareTmuxNodeArray[%*d]: %d/%d %10s %d %5s %d",
					  padding,
					  i,
					  node->seq,
					  node->group,
					  node->name,
					  node->pgport,
					  node->replicationQuorum ? "true" : "false",
					  node->candidatePriority);
		}
		else
		{
			log_debug("prepareTmuxNodeArray[%*d]: %s %d %5s %d",
					  padding,
					  i,
					  node->name,
					  node->pgport,
					  node->replicationQuorum ? "true" : "false",
					  node->candidatePriority);
		}
	}

	return true;
}


/*
 * cli_print_version_getopts parses the CLI options for the pg_autoctl version
 * command, which are the usual suspects.
 */
int
cli_do_tmux_script_getopts(int argc, char **argv)
{
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;
	bool printVersion = false;

	TmuxOptions options = { 0 };

	static struct option long_options[] = {
		{ "root", required_argument, NULL, 'D' },
		{ "first-pgport", required_argument, NULL, 'p' },
		{ "nodes", required_argument, NULL, 'n' },
		{ "async-nodes", required_argument, NULL, 'a' },
		{ "node-priorities", required_argument, NULL, 'P' },
		{ "sync-standbys", required_argument, NULL, 's' },
		{ "skip-pg-hba", required_argument, NULL, 'S' },
		{ "citus", no_argument, NULL, 'c' },
		{ "citus-workers", required_argument, NULL, 'w' },
		{ "citus-secondaries", required_argument, NULL, 'z' },
		{ "layout", required_argument, NULL, 'l' },
		{ "binpath", required_argument, NULL, 'b' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	/* set our defaults */
	options.firstPort = 5500;
	options.nodes = 2;
	options.asyncNodes = 0;
	options.numSync = -1;       /* use pg_autoctl defaults */
	options.skipHBA = false;
	options.withCitus = false;
	options.citusWorkers = 2;
	options.citusSecondaries = 0;
	strlcpy(options.root, "/tmp/pgaf/tmux", sizeof(options.root));
	strlcpy(options.layout, "even-vertical", sizeof(options.layout));
	strlcpy(options.binpath, pg_autoctl_argv0, sizeof(options.binpath));

	if (!parseCandidatePriorities("", options.priorities))
	{
		log_error("BUG: failed to initialize candidate priorities");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/*
	 * The only command lines that are using keeper_cli_getopt_pgdata are
	 * terminal ones: they don't accept subcommands. In that case our option
	 * parsing can happen in any order and we don't need getopt_long to behave
	 * in a POSIXLY_CORRECT way.
	 *
	 * The unsetenv() call allows getopt_long() to reorder arguments for us.
	 */
	unsetenv("POSIXLY_CORRECT");

	while ((c = getopt_long(argc, argv, "D:p:Vvqh",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(options.root, optarg, MAXPGPATH);
				log_trace("--root %s", options.root);
				break;
			}

			case 'p':
			{
				if (!stringToInt(optarg, &options.firstPort))
				{
					log_error("Failed to parse --first-port number \"%s\"",
							  optarg);
					errors++;
				}
				log_trace("--first-port %d", options.firstPort);
				break;
			}

			case 'n':
			{
				if (!stringToInt(optarg, &options.nodes))
				{
					log_error("Failed to parse --nodes number \"%s\"",
							  optarg);
					errors++;
				}

				if (MAX_NODES < options.nodes)
				{
					log_error("pg_autoctl do tmux session supports up to %d "
							  "nodes, and --nodes %d has been asked for",
							  MAX_NODES, options.nodes);
					errors++;
				}

				log_trace("--nodes %d", options.nodes);
				break;
			}

			case 'a':
			{
				if (!stringToInt(optarg, &options.asyncNodes))
				{
					log_error("Failed to parse --async-nodes number \"%s\"",
							  optarg);
					errors++;
				}
				log_trace("--async-nodes %d", options.asyncNodes);
				break;
			}

			case 'P':
			{
				char priorities[BUFSIZE] = { 0 };

				/* parsing mangles the string, keep a copy */
				strlcpy(priorities, optarg, sizeof(priorities));

				if (!parseCandidatePriorities(priorities, options.priorities))
				{
					log_error("Failed to parse --node-priorities \"%s\"",
							  optarg);
					errors++;
				}
				break;
			}

			case 's':
			{
				if (!stringToInt(optarg, &options.numSync))
				{
					log_error("Failed to parse --sync-standbys number \"%s\"",
							  optarg);
					errors++;
				}
				log_trace("--sync-standbys %d", options.numSync);
				break;
			}

			case 'S':
			{
				options.skipHBA = true;
				log_trace("--skip-pg-hba");
				break;
			}

			case 'c':
			{
				options.withCitus = true;
				log_trace("--citus");
				break;
			}

			case 'w':
			{
				if (!stringToInt(optarg, &options.citusWorkers))
				{
					log_error("Failed to parse --citus-workers number \"%s\"",
							  optarg);
					errors++;
				}
				log_trace("--citus-workers %d", options.firstPort);
				break;
			}

			case 'z':
			{
				/* { "citus-secondaries", no_argument, NULL, 'z' }, */
				if (!stringToInt(optarg, &options.citusSecondaries))
				{
					log_error("Failed to parse --citus-secondaries number \"%s\"",
							  optarg);
					errors++;
				}

				log_trace("--citus-secondaries %d", options.citusSecondaries);
				break;
			}

			case 'l':
			{
				strlcpy(options.layout, optarg, MAXPGPATH);
				log_trace("--layout %s", options.layout);
				break;
			}

			case 'b':
			{
				strlcpy(options.binpath, optarg, MAXPGPATH);
				log_trace("--binpath %s", options.binpath);
				break;
			}

			case 'h':
			{
				commandline_help(stderr);
				exit(EXIT_CODE_QUIT);
				break;
			}

			case 'V':
			{
				/* keeper_cli_print_version prints version and exits. */
				printVersion = true;
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

			default:
			{
				/* getopt_long already wrote an error message */
				errors++;
				break;
			}
		}
	}

	if (!prepareTmuxNodeArray(&options, &tmuxNodeArray))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}


	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (printVersion)
	{
		keeper_cli_print_version(argc, argv);
	}

	/* publish parsed options */
	tmuxOptions = options;

	return optind;
}


/*
 * tmux_add_command appends a tmux command to the given script buffer.
 */
void
tmux_add_command(PQExpBuffer script, const char *fmt, ...)
{
	char buffer[ARG_MAX] = { 0 };
	va_list args;

	va_start(args, fmt);
	pg_vsprintf(buffer, fmt, args);
	va_end(args);

	appendPQExpBuffer(script, "%s\n", buffer);
}


/*
 * tmux_add_send_keys_command appends a tmux send-keys command to the given
 * script buffer, with an additional Enter command.
 */
void
tmux_add_send_keys_command(PQExpBuffer script, const char *fmt, ...)
{
	char buffer[BUFSIZE] = { 0 };
	va_list args;

	va_start(args, fmt);
	pg_vsprintf(buffer, fmt, args);
	va_end(args);

	appendPQExpBuffer(script, "send-keys '%s' Enter\n", buffer);
}


/*
 * tmux_add_xdg_environment sets the environment variables that we need for the
 * whole session to be self-contained in the given root directory. The
 * implementation of this function relies on the fact that the tmux script has
 * been prepared with tmux set-environment commands, per tmux_setenv.
 */
void
tmux_add_xdg_environment(PQExpBuffer script)
{
	tmux_add_send_keys_command(script, "eval $(tmux show-environment -s)");
}


/*
 * tmux_setenv adds setenv commands to the tmux script.
 */
void
tmux_setenv(PQExpBuffer script,
			const char *sessionName, const char *root, int firstPort)
{
	char PATH[ARG_MAX] = { 0 };
	char PG_CONFIG[MAXPGPATH] = { 0 };
	char monitor_pguri[MAXCONNINFO] = { 0 };

	if (env_exists("PG_CONFIG"))
	{
		if (!get_env_copy("PG_CONFIG", PG_CONFIG, sizeof(PG_CONFIG)))
		{
			log_fatal("Failed to get PG_CONFIG from the environment");
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		tmux_add_command(script,
						 "set-environment -t %s PG_CONFIG \"%s\"",
						 sessionName,
						 PG_CONFIG);
	}

	if (!get_env_copy("PATH", PATH, sizeof(PATH)))
	{
		log_fatal("Failed to get PATH from the environment");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	tmux_add_command(script,
					 "set-environment -t %s PATH \"%s\"", sessionName, PATH);

	sformat(monitor_pguri, sizeof(monitor_pguri),
			"postgres://autoctl_node@localhost:%d/pg_auto_failover?sslmode=prefer",
			firstPort);

	tmux_add_command(script,
					 "set-environment -t %s PG_AUTOCTL_MONITOR \"%s\"",
					 sessionName,
					 monitor_pguri);

	for (int i = 0; xdg[i][0] != NULL; i++)
	{
		char *var = xdg[i][0];
		char *dir = xdg[i][1];

		tmux_add_command(script, "set-environment -t %s %s \"%s/%s\"",
						 sessionName,
						 var,
						 root,
						 dir);
	}
}


/*
 * tmux_prepare_XDG_environment set XDG environment variables in the current
 * process tree.
 */
bool
tmux_prepare_XDG_environment(const char *root, bool createDirectories)
{
	log_info("Preparing XDG setting for self-contained session in \"%s\"",
			 root);

	for (int i = 0; xdg[i][0] != NULL; i++)
	{
		char *var = xdg[i][0];
		char *dir = xdg[i][1];
		char *env = (char *) malloc(MAXPGPATH * sizeof(char));

		if (env == NULL)
		{
			log_fatal(ALLOCATION_FAILED_ERROR);
			return false;
		}

		sformat(env, MAXPGPATH, "%s/%s", root, dir);

		if (createDirectories)
		{
			log_debug("mkdir -p \"%s\"", env);
			if (pg_mkdir_p(env, 0700) == -1)
			{
				log_error("mkdir -p \"%s\": %m", env);
				free(env);
				return false;
			}
		}

		if (!normalize_filename(env, env, MAXPGPATH))
		{
			/* errors have already been logged */
			free(env);
			return false;
		}

		log_info("export %s=\"%s\"", var, env);

		if (setenv(var, env, 1) != 0)
		{
			log_error("Failed to set environment variable %s to \"%s\": %m",
					  var, env);
		}

		/* also create our actual target directory for our files */
		if (createDirectories)
		{
			char targetPath[MAXPGPATH] = { 0 };

			sformat(targetPath, sizeof(targetPath),
					"%s/pg_config/%s",
					env,

			        /* skip first / in the root directory */
					root[0] == '/' ? root + 1 : root);

			log_debug("mkdir -p \"%s\"", targetPath);
			if (pg_mkdir_p(targetPath, 0700) == -1)
			{
				log_error("mkdir -p \"%s\": %m", targetPath);

				free(env);
				return false;
			}
		}

		free(env);
	}

	return true;
}


/*
 * tmux_add_new_session appends a new-session command with the
 * update-environment options for our XDG settings, as a series of tmux
 * send-keys commands, to the given script buffer.
 */
void
tmux_add_new_session(PQExpBuffer script, const char *root, int pgport)
{
	char sessionName[BUFSIZE] = { 0 };

	sformat(sessionName, BUFSIZE, "pgautofailover-%d", pgport);

	/*
	 * For demo/tests purposes, arrange a self-contained setup where everything
	 * is to be found in the given options.root directory.
	 */

	/* for (int i = 0; xdg[i][0] != NULL; i++) */
	/* { */
	/*  char *var = xdg[i][0]; */

	/*  tmux_add_command(script, "set-option update-environment %s", var); */
	/* } */

	tmux_add_command(script, "new-session -s %s", sessionName);
}


/*
 * tmux_pg_autoctl_create_monitor appends a pg_autoctl create monitor command
 * to the given script buffer, and also the commands to set PGDATA and PGPORT.
 */
void
tmux_pg_autoctl_create_monitor(PQExpBuffer script,
							   const char *root,
							   const char *binpath,
							   int pgport,
							   bool skipHBA)
{
	char *pg_ctl_opts =
		skipHBA
		? "--hostname localhost --ssl-self-signed --skip-pg-hba"
		: "--hostname localhost --ssl-self-signed --auth trust";

	tmux_add_send_keys_command(script, "export PGPORT=%d", pgport);

	/* the monitor is always named monitor, and does not need --monitor */
	tmux_add_send_keys_command(script, "export PGDATA=\"%s/monitor\"", root);

	tmux_add_send_keys_command(script,
							   "%s create monitor %s --run",
							   binpath,
							   pg_ctl_opts);
}


/*
 * tmux_pg_autoctl_create_postgres appends a pg_autoctl create postgres command
 * to the given script buffer, and also the commands to set PGDATA and PGPORT.
 */
void
tmux_pg_autoctl_create_postgres(PQExpBuffer script,
								const char *root,
								const char *binpath,
								TmuxNode *node,
								bool skipHBA)
{
	char monitor[BUFSIZE] = { 0 };
	char *pg_ctl_opts =
		skipHBA
		? "--hostname localhost --ssl-self-signed --skip-pg-hba"
		: "--hostname localhost --ssl-self-signed --auth trust --pg-hba-lan";

	tmux_add_send_keys_command(script, "export PGPORT=%d", node->pgport);

	sformat(monitor, sizeof(monitor),
			"$(%s show uri --pgdata %s/monitor --formation monitor)",
			binpath,
			root);

	tmux_add_send_keys_command(script,
							   "export PGDATA=\"%s/%s\"",
							   root,
							   node->name);

	tmux_add_send_keys_command(script,
							   "%s create postgres %s "
							   "--monitor %s "
							   "--name %s "
							   "--dbname demo "
							   "--replication-quorum %s "
							   "--candidate-priority %d "
							   "--run",
							   binpath,
							   pg_ctl_opts,
							   monitor,
							   node->name,
							   node->replicationQuorum ? "true" : "false",
							   node->candidatePriority);
}


/*
 * tmux_pg_autoctl_activate appens a pg_autoctl activate command to the given
 * script buffer.
 */
void
tmux_pg_autoctl_activate(PQExpBuffer script,
						 const char *root,
						 const char *binpath,
						 const char *name)
{
	tmux_add_send_keys_command(script,
							   "%s activate --pgdata \"%s/%s\"",
							   binpath,
							   root,
							   name);
}


/*
 * tmux_pg_autoctl_create_coordinator appends a pg_autoctl create coordinator
 * command to the given script buffer, and also the commands to set PGDATA and
 * PGPORT.
 */
void
tmux_pg_autoctl_create_coordinator(PQExpBuffer script,
								   const char *root,
								   const char *binpath,
								   TmuxNode *node,
								   bool skipHBA)
{
	char monitor[BUFSIZE] = { 0 };
	char *pg_ctl_opts =
		skipHBA
		? "--hostname localhost --ssl-self-signed --skip-pg-hba"
		: "--hostname localhost --ssl-self-signed --auth trust";

	tmux_add_send_keys_command(script, "export PGPORT=%d", node->pgport);

	sformat(monitor, sizeof(monitor),
			"$(%s show uri --pgdata %s/monitor --formation monitor)",
			binpath,
			root);

	tmux_add_send_keys_command(script,
							   "export PGDATA=\"%s/%s\"",
							   root,
							   node->name);

	tmux_add_send_keys_command(script,
							   "%s create coordinator %s "
							   "--monitor %s --name %s "
							   "%s"
							   "%s %s "
							   "--replication-quorum %s "
							   "--candidate-priority %d "
							   "--run",
							   binpath,
							   pg_ctl_opts,
							   monitor,
							   node->name,
							   node->secondary ? "--citus-secondary " : "",
							   node->secondary ? "--citus-cluster" : "",
							   node->secondary ? node->clusterName : "",
							   node->replicationQuorum ? "true" : "false",
							   node->candidatePriority);
}


/*
 * tmux_pg_autoctl_create_coordinator appends a pg_autoctl create coordinator
 * command to the given script buffer, and also the commands to set PGDATA and
 * PGPORT.
 */
void
tmux_pg_autoctl_create_worker(PQExpBuffer script,
							  const char *root,
							  const char *binpath,
							  TmuxNode *node,
							  bool skipHBA)
{
	char monitor[BUFSIZE] = { 0 };
	char *pg_ctl_opts =
		skipHBA
		? "--hostname localhost --ssl-self-signed --skip-pg-hba"
		: "--hostname localhost --ssl-self-signed --auth trust";

	tmux_add_send_keys_command(script, "export PGPORT=%d", node->pgport);

	sformat(monitor, sizeof(monitor),
			"$(%s show uri --pgdata %s/monitor --formation monitor)",
			binpath,
			root);

	tmux_add_send_keys_command(script,
							   "export PGDATA=\"%s/%s\"",
							   root,
							   node->name);

	tmux_add_send_keys_command(script,
							   "%s create worker %s "
							   "--monitor %s --name %s --group %d "
							   "%s"
							   "%s %s "
							   "--replication-quorum %s "
							   "--candidate-priority %d "
							   "--run",
							   binpath,
							   pg_ctl_opts,
							   monitor,
							   node->name,
							   node->group,
							   node->secondary ? "--citus-secondary " : "",
							   node->secondary ? "--citus-cluster" : "",
							   node->secondary ? node->clusterName : "",
							   node->replicationQuorum ? "true" : "false",
							   node->candidatePriority);
}


/*
 * prepare_tmux_script prepares a script for a tmux session with the given
 * nodes, root directory, first pgPort, and layout.
 */
static void
prepare_tmux_script(TmuxOptions *options, PQExpBuffer script)
{
	char *root = options->root;
	int pgport = options->firstPort;
	char sessionName[BUFSIZE] = { 0 };

	char previousName[NAMEDATALEN] = { 0 };

	sformat(sessionName, BUFSIZE, "pgautofailover-%d", options->firstPort);

	tmux_add_command(script, "set-option -g default-shell /bin/bash");

	(void) tmux_add_new_session(script, root, pgport);
	(void) tmux_setenv(script, sessionName, root, options->firstPort);

	/* start a monitor */
	(void) tmux_add_xdg_environment(script);
	tmux_pg_autoctl_create_monitor(script, root, options->binpath, pgport++,
								   options->skipHBA);

	/* start the Postgres nodes, using the monitor URI */
	sformat(previousName, sizeof(previousName), "monitor");

	for (int i = 0; i < tmuxNodeArray.count; i++)
	{
		TmuxNode *node = &(tmuxNodeArray.nodes[i]);

		tmux_add_command(script, "split-window -v");
		tmux_add_command(script, "select-layout even-vertical");

		(void) tmux_add_xdg_environment(script);

		/*
		 * Force node ordering to easy debugging of interactive sessions: each
		 * node waits until the previous one has been started or registered.
		 *
		 * First worker in a group is named worker1a or worker2a and can be
		 * started as soon as the monitor is ready, other workers in the same
		 * group have to wait until the first worker is ready to ensure their
		 * naming worker1b or worker2b.
		 */
		if (node->group > 0 && node->seq == 0)
		{
			sformat(previousName, sizeof(previousName), "monitor");
		}

		tmux_add_send_keys_command(script,
								   "PG_AUTOCTL_DEBUG=1 "
								   "%s do tmux wait --root %s %s",
								   options->binpath,
								   options->root,
								   previousName);

		if (options->withCitus)
		{
			if (node->group == 0)
			{
				tmux_pg_autoctl_create_coordinator(script,
												   root,
												   options->binpath,
												   node,
												   options->skipHBA);
			}
			else
			{
				tmux_pg_autoctl_create_worker(script,
											  root,
											  options->binpath,
											  node,
											  options->skipHBA);
			}
		}
		else
		{
			tmux_pg_autoctl_create_postgres(script,
											root,
											options->binpath,
											node,
											options->skipHBA);
		}

		strlcpy(previousName, node->name, sizeof(previousName));
	}

	/* add a window for pg_autoctl show state */
	tmux_add_command(script, "split-window -v");
	tmux_add_command(script, "select-layout even-vertical");

	(void) tmux_add_xdg_environment(script);
	tmux_add_send_keys_command(script, "export PGDATA=\"%s/monitor\"", root);
	tmux_add_send_keys_command(script,
							   "PG_AUTOCTL_DEBUG=1 "
							   "%s do tmux wait --root %s %s",
							   options->binpath,
							   options->root,
							   "monitor");
	tmux_add_send_keys_command(script,
							   "%s watch",
							   options->binpath);

	/* add a window for interactive pg_autoctl commands */
	tmux_add_command(script, "split-window -v");
	tmux_add_command(script, "select-layout even-vertical");
	(void) tmux_add_xdg_environment(script);

	/* activate the first (primary) coordinator when asked to */
	if (env_exists("PG_AUTOCTL_ACTIVATE_COORDINATOR"))
	{
		TmuxNode coord0a = { 0 };

		/* wait until the coordinator is ready */
		(void) prepare_node_name(options, &coord0a, 1);

		tmux_add_send_keys_command(script,
								   "PG_AUTOCTL_DEBUG=1 "
								   "%s do tmux wait --root %s %s %s",
								   pg_autoctl_argv0,
								   options->root,
								   coord0a.name,
								   NodeStateToString(WAIT_PRIMARY_STATE));

		tmux_pg_autoctl_activate(script,
								 options->root,
								 options->binpath,
								 coord0a.name);
	}

	if (options->numSync != -1)
	{
		/*
		 * We need to wait until the first node is either WAIT_PRIMARY or
		 * PRIMARY before we can go on and change formation settings with
		 *   pg_autoctl set formation ...
		 */
		TmuxNode firstNode = { 0 };
		NodeState targetPrimaryState =
			options->numSync == 0 ? WAIT_PRIMARY_STATE : PRIMARY_STATE;

		(void) prepare_node_name(options, &firstNode, 1);

		tmux_add_send_keys_command(script,
								   "PG_AUTOCTL_DEBUG=1 "
								   "%s do tmux wait --root %s %s %s",
								   options->binpath,
								   options->root,
								   firstNode.name,
								   NodeStateToString(targetPrimaryState));

		/* PGDATA has just been exported, rely on it */
		tmux_add_send_keys_command(script,
								   "%s set formation number-sync-standbys %d",
								   options->binpath,
								   options->numSync);
	}

	/* now change to the user given options->root directory */
	tmux_add_send_keys_command(script, "cd \"%s\"", options->root);

	/* now select our target layout */
	tmux_add_command(script, "select-layout %s", options->layout);

	if (env_exists("TMUX_EXTRA_COMMANDS"))
	{
		char extra_commands[BUFSIZE] = { 0 };

		char *extraLines[BUFSIZE];
		int lineNumber = 0;

		if (!get_env_copy("TMUX_EXTRA_COMMANDS", extra_commands, BUFSIZE))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		int lineCount = splitLines(extra_commands, extraLines, BUFSIZE);

		for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
		{
			appendPQExpBuffer(script, "%s\n", extraLines[lineNumber]);
		}
	}

	for (int i = 0; tmux_banner[i] != NULL; i++)
	{
		tmux_add_send_keys_command(script, "%s", tmux_banner[i]);
	}
}


/*
 * tmux_start_server starts a tmux session with the given script.
 */
bool
tmux_start_server(const char *scriptName, const char *binpath)
{
	char *args[8];
	int argsIndex = 0;

	char tmux[MAXPGPATH] = { 0 };
	char command[BUFSIZE] = { 0 };

	/*
	 * Here we are going to remain the parent process of multiple pg_autoctl
	 * top-level processes, one per node (and the monitor). We don't want all
	 * those processes to use the same semaphore for logging, so make sure we
	 * remove ourselves from the environment before we start all the
	 * sub-processes.
	 */
	unsetenv(PG_AUTOCTL_LOG_SEMAPHORE);

	if (setenv("PG_AUTOCTL_DEBUG", "1", 1) != 0)
	{
		log_error("Failed to set environment PG_AUTOCTL_DEBUG: %m");
		return false;
	}

	if (binpath && setenv("PG_AUTOCTL_DEBUG_BIN_PATH", binpath, 1) != 0)
	{
		log_error("Failed to set environment PG_AUTOCTL_DEBUG_BIN_PATH: %m");
		return false;
	}

	if (!search_path_first("tmux", tmux, LOG_ERROR))
	{
		log_fatal("Failed to find program tmux in PATH");
		return false;
	}

	/*
	 * Run the tmux command with our script:
	 *   tmux start-server \; source-file ${scriptName}
	 */
	args[argsIndex++] = (char *) tmux;
	args[argsIndex++] = "-u";
	args[argsIndex++] = "start-server";
	args[argsIndex++] = ";";
	args[argsIndex++] = "source-file";
	args[argsIndex++] = (char *) scriptName;
	args[argsIndex] = NULL;

	/* we do not want to call setsid() when running this program. */
	Program program = { 0 };

	(void) initialize_program(&program, args, false);

	program.capture = false;    /* don't capture output */
	program.tty = true;         /* allow sharing the parent's tty */

	/* log the exact command line we're using */
	(void) snprintf_program_command_line(&program, command, BUFSIZE);
	log_info("%s", command);

	(void) execute_subprogram(&program);

	/* we only get there when the tmux session is done */
	free_program(&program);
	return true;
}


/*
 * tmux_attach_session_by_name runs the command:
 *   tmux attach-session -t sessionName
 */
bool
tmux_attach_session(const char *tmux_path, const char *sessionName)
{
	Program program = { 0 };

	char *args[8];
	int argsIndex = 0;

	char command[BUFSIZE] = { 0 };

	/*
	 * Run the tmux command with our script:
	 *   tmux start-server \; source-file ${scriptName}
	 */
	args[argsIndex++] = (char *) tmux_path;
	args[argsIndex++] = "attach-session";
	args[argsIndex++] = "-t";
	args[argsIndex++] = (char *) sessionName;
	args[argsIndex] = NULL;

	/* we do not want to call setsid() when running this program. */
	(void) initialize_program(&program, args, false);

	program.capture = false;    /* don't capture output */
	program.tty = true;         /* allow sharing the parent's tty */

	/* log the exact command line we're using */
	(void) snprintf_program_command_line(&program, command, BUFSIZE);
	log_info("%s", command);

	(void) execute_subprogram(&program);

	/* we only get there when the tmux session is done */
	free_program(&program);

	return true;
}


/*
 * pg_autoctl_getpid gets the pid of the pg_autoctl process that is running for
 * the given PGDATA location.
 */
bool
pg_autoctl_getpid(const char *pgdata, pid_t *pid)
{
	ConfigFilePaths pathnames = { 0 };

	if (!keeper_config_set_pathnames_from_pgdata(&pathnames, pgdata))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	return read_pidfile(pathnames.pid, pid);
}


/*
 * tmux_stop_pg_autoctl stops all started pg_autoctl programs in a tmux
 * sessions.
 */
static bool
tmux_stop_pg_autoctl(TmuxOptions *options)
{
	bool success = true;

	int signals[] = { SIGTERM, SIGINT, SIGQUIT };
	int signalsCount = sizeof(signals) / sizeof(signals[0]);

	/* signal processes using increasing levels of urge to quit now */
	for (int s = 0; s < signalsCount; s++)
	{
		int totalNodesCount = compute_node_count(options);
		int countRunning = options->nodes + 1;

		for (int i = 0; i <= totalNodesCount; i++)
		{
			pid_t pid = 0;
			TmuxNode node = { 0 };
			char pgdata[MAXPGPATH] = { 0 };

			(void) prepare_node_name(options, &node, i);

			sformat(pgdata, sizeof(pgdata), "%s/%s", options->root, node.name);

			if (!pg_autoctl_getpid(pgdata, &pid))
			{
				/* we don't have a pid */
				log_info("No pidfile for pg_autoctl for node \"%s\"", node.name);
				--countRunning;
				continue;
			}

			if (kill(pid, 0) == -1 && errno == ESRCH)
			{
				log_info("Pid %d for node \"%s\" is not running anymore",
						 pid, node.name);
				--countRunning;
			}
			else
			{
				log_info("Sending signal %s to pid %d for node \"%s\"",
						 signal_to_string(signals[s]), pid, node.name);

				if (kill(pid, signals[s]) != 0)
				{
					log_info("Failed to send %s to pid %d",
							 signal_to_string(signals[s]), pid);
				}
			}
		}

		if (countRunning == 0)
		{
			break;
		}

		/* sleep enough time that the processes might already be dead */
		sleep(1);
	}

	return success;
}


/*
 * tmux_has_session runs the command `tmux has-session -f sessionName`.
 */
bool
tmux_has_session(const char *tmux_path, const char *sessionName)
{
	int returnCode;
	char command[BUFSIZE] = { 0 };

	Program program = run_program(tmux_path, "has-session", "-t", sessionName, NULL);
	returnCode = program.returnCode;

	(void) snprintf_program_command_line(&program, command, BUFSIZE);
	log_debug("%s", command);

	if (program.stdOut)
	{
		char *outLines[BUFSIZE] = { 0 };
		int lineCount = splitLines(program.stdOut, outLines, BUFSIZE);
		int lineNumber = 0;

		for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
		{
			log_info("tmux has-session: %s", outLines[lineNumber]);
		}
	}

	if (program.stdErr)
	{
		char *errLines[BUFSIZE] = { 0 };
		int lineCount = splitLines(program.stdOut, errLines, BUFSIZE);
		int lineNumber = 0;

		for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
		{
			log_error("tmux has-session: %s", errLines[lineNumber]);
		}
	}

	free_program(&program);

	/*
	 * From tmux has-session manual page:
	 *
	 * Report an error and exit with 1 if the specified session does not exist.
	 * If it does exist, exit with 0.
	 */
	return returnCode == 0;
}


/*
 * tmux_kill_session runs the command:
 *   tmux kill-session -t pgautofailover-${first-pgport}
 */
bool
tmux_kill_session(TmuxOptions *options)
{
	char sessionName[BUFSIZE] = { 0 };

	sformat(sessionName, BUFSIZE, "pgautofailover-%d", options->firstPort);

	return tmux_kill_session_by_name(sessionName);
}


/*
 * tmux_kill_session_by_name kills a tmux session of the given name.
 */
bool
tmux_kill_session_by_name(const char *sessionName)
{
	char tmux[MAXPGPATH] = { 0 };
	char command[BUFSIZE] = { 0 };

	bool success = true;

	if (!search_path_first("tmux", tmux, LOG_ERROR))
	{
		log_fatal("Failed to find program tmux in PATH");
		return false;
	}

	if (!tmux_has_session(tmux, sessionName))
	{
		log_info("Tmux session \"%s\" does not exist", sessionName);
		return true;
	}

	Program program = run_program(tmux, "kill-session", "-t", sessionName, NULL);

	(void) snprintf_program_command_line(&program, command, BUFSIZE);
	log_info("%s", command);

	if (program.stdOut)
	{
		char *outLines[BUFSIZE] = { 0 };
		int lineCount = splitLines(program.stdOut, outLines, BUFSIZE);
		int lineNumber = 0;

		for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
		{
			log_info("tmux kill-session: %s", outLines[lineNumber]);
		}
	}

	if (program.stdErr)
	{
		char *errLines[BUFSIZE] = { 0 };
		int lineCount = splitLines(program.stdErr, errLines, BUFSIZE);
		int lineNumber = 0;

		for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
		{
			log_error("tmux kill-session: %s", errLines[lineNumber]);
		}
	}

	if (program.returnCode != 0)
	{
		success = false;
		log_warn("Failed to kill tmux sessions \"%s\"", sessionName);
	}

	free_program(&program);

	return success;
}


/*
 * tmux_process_options processes the tmux commands options. The main activity
 * here is to ensure that the "root" directory exists and normalize its
 * internal pathname in the options structure.
 */
void
tmux_process_options(TmuxOptions *options)
{
	log_debug("tmux_process_options");
	log_debug("mkdir -p \"%s\"", options->root);

	if (pg_mkdir_p(options->root, 0700) == -1)
	{
		log_fatal("mkdir -p \"%s\": %m", options->root);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	log_debug("normalize_filename \"%s\"", options->root);

	if (!normalize_filename(options->root, options->root, MAXPGPATH))
	{
		/* errors have already been logged. */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	log_debug("Real path for root directory: \"%s\"", options->root);
}


/*
 * tmux_cleanup_stale_directory cleans-up the pg_autoctl processes and then the
 * root directory of a tmux session, and then kills the tmux session.
 */
void
tmux_cleanup_stale_directory(TmuxOptions *options)
{
	if (!directory_exists(options->root))
	{
		log_info("Directory \"%s\" does not exist, nothing to clean-up",
				 options->root);
		return;
	}

	if (!normalize_filename(options->root, options->root, MAXPGPATH))
	{
		/* errors have already been logged. */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/* prepare the XDG environment */
	if (!tmux_prepare_XDG_environment(options->root, false))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	log_info("Checking for stale pg_autoctl process in \"%s\"", options->root);
	(void) tmux_stop_pg_autoctl(options);

	log_info("Removing stale directory: rm -rf \"%s\"", options->root);
	if (!rmtree(options->root, true))
	{
		log_error("Failed to remove directory \"%s\": %m", options->root);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	log_info("Checking for stale tmux session \"pgautofailover-%d\"",
			 options->firstPort);

	if (!tmux_kill_session(options))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * keeper_cli_tmux_script generates a tmux script to run a test case or a demo
 * for pg_auto_failover easily.
 */
void
cli_do_tmux_script(int argc, char **argv)
{
	TmuxOptions options = tmuxOptions;
	PQExpBuffer script = createPQExpBuffer();

	(void) tmux_process_options(&options);

	/* prepare the XDG environment */
	if (!tmux_prepare_XDG_environment(options.root, true))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	if (script == NULL)
	{
		log_error("Failed to allocate memory");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/* prepare the tmux script */
	(void) prepare_tmux_script(&options, script);

	/* memory allocation could have failed while building string */
	if (PQExpBufferBroken(script))
	{
		log_error("Failed to allocate memory");
		destroyPQExpBuffer(script);

		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	fformat(stdout, "%s", script->data);

	destroyPQExpBuffer(script);
}


/*
 * cli_do_tmux_session starts an interactive tmux session with the given
 * specifications for a cluster. When the session is detached, the pg_autoctl
 * processes are stopped.
 */
void
cli_do_tmux_session(int argc, char **argv)
{
	TmuxOptions options = tmuxOptions;
	PQExpBuffer script = createPQExpBuffer();

	char scriptName[MAXPGPATH] = { 0 };

	bool success = true;

	/*
	 * We need to make sure we start from a clean slate.
	 */
	(void) tmux_cleanup_stale_directory(&options);

	/*
	 * Write the script to "script-${first-pgport}.tmux" file in the root
	 * directory.
	 */
	(void) tmux_process_options(&options);

	/* prepare the XDG environment */
	if (!tmux_prepare_XDG_environment(options.root, true))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/*
	 * Prepare the tmux script.
	 */
	if (script == NULL)
	{
		log_error("Failed to allocate memory");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	(void) prepare_tmux_script(&options, script);

	/* memory allocation could have failed while building string */
	if (PQExpBufferBroken(script))
	{
		log_error("Failed to allocate memory");
		destroyPQExpBuffer(script);

		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/*
	 * Write the script to file.
	 */
	sformat(scriptName, sizeof(scriptName),
			"%s/script-%d.tmux", options.root, options.firstPort);

	log_info("Writing tmux session script \"%s\"", scriptName);

	if (!write_file(script->data, script->len, scriptName))
	{
		log_fatal("Failed to write tmux script at \"%s\"", scriptName);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	destroyPQExpBuffer(script);

	/*
	 * Start a tmux session from the script.
	 */
	if (!tmux_start_server(scriptName, options.binpath))
	{
		success = false;
		log_fatal("Failed to start the tmux session, see above for details");
	}

	/*
	 * Stop our pg_autoctl processes and kill the tmux session.
	 */
	log_info("tmux session ended: kill pg_autoct processes");
	success = success && tmux_stop_pg_autoctl(&options);
	success = success && tmux_kill_session(&options);

	if (!success)
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * cli_do_tmux_stop send termination signals on all the pg_autoctl process that
 * might be running in a tmux session.
 */
void
cli_do_tmux_stop(int argc, char **argv)
{
	TmuxOptions options = tmuxOptions;

	(void) tmux_process_options(&options);

	/* prepare the XDG environment */
	if (!tmux_prepare_XDG_environment(options.root, false))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	if (!tmux_stop_pg_autoctl(&options))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * cli_do_tmux_clean cleans-up the pg_autoctl processes and then the root
 * directory of a tmux session, and then kills the tmux session.
 */
void
cli_do_tmux_clean(int argc, char **argv)
{
	TmuxOptions options = tmuxOptions;

	(void) tmux_cleanup_stale_directory(&options);
}


/*
 * cli_do_tmux_wait waits until given node name has been registered. When the
 * target node name is the "monitor" just wait until Postgres is running.
 */
void
cli_do_tmux_wait(int argc, char **argv)
{
	TmuxOptions options = tmuxOptions;
	char nodeName[NAMEDATALEN] = { 0 };
	NodeState targetState = INIT_STATE;

	(void) tmux_process_options(&options);

	/* prepare the XDG environment */
	if (!tmux_prepare_XDG_environment(options.root, false))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	switch (argc)
	{
		case 1:
		{
			strlcpy(nodeName, argv[0], sizeof(nodeName));

			break;
		}

		case 2:
		{
			strlcpy(nodeName, argv[0], sizeof(nodeName));
			targetState = NodeStateFromString(argv[1]);

			/* when we fail to parse the target state we wait 30s and exit */
			if (targetState == NO_STATE)
			{
				sleep(30);
				exit(EXIT_CODE_INTERNAL_ERROR);
			}

			break;
		}

		default:
		{
			commandline_help(stderr);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}

	if (strcmp(nodeName, "monitor") == 0)
	{
		int timeout = 30;
		bool ready = false;
		char pgdata[MAXPGPATH] = { 0 };

		sformat(pgdata, sizeof(pgdata), "%s/%s", options.root, nodeName);

		/* leave some time for initdb and stuff */
		sleep(2);

		Program program = run_program(pg_autoctl_program, "do", "pgsetup", "wait",
									  "--pgdata", pgdata, NULL);

		if (program.returnCode != 0)
		{
			char command[BUFSIZE];

			(void) snprintf_program_command_line(&program, command, BUFSIZE);

			log_error("%s [%d]", command, program.returnCode);
			free_program(&program);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		log_info("Postgres is running at \"%s\"", pgdata);

		/* Postgres is running on the monitor, is it ready though? */
		while (!ready && timeout > 0)
		{
			char command[BUFSIZE];

			Program showUri = run_program(pg_autoctl_program,
										  "show", "uri", "--monitor",
										  "--pgdata", pgdata, NULL);

			(void) snprintf_program_command_line(&showUri, command, BUFSIZE);
			log_info("%s [%d]", command, showUri.returnCode);

			ready = showUri.returnCode == 0;
			--timeout;

			if (ready)
			{
				log_info("The monitor is ready at: %s", showUri.stdOut);
			}

			free_program(&showUri);
		}

		free_program(&program);

		if (!ready)
		{
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
	else
	{
		/*
		 * Not a monitor node: only wait until the node has been registered to
		 * the monitor. We know that the node has been registered when a state
		 * file exists.
		 */
		int timeout = 60;
		char pgdata[MAXPGPATH] = { 0 };
		ConfigFilePaths pathnames = { 0 };

		sformat(pgdata, sizeof(pgdata), "%s/%s", options.root, nodeName);

		log_info("Waiting for a node state file for PGDATA \"%s\"", pgdata);

		/* when waiting for PRIMARY or some other state, raise the timeout */
		if (targetState == INIT_STATE)
		{
			timeout = 60;
		}
		else
		{
			timeout = 120;
		}

		while (timeout > 0)
		{
			if (IS_EMPTY_STRING_BUFFER(pathnames.state))
			{
				if (keeper_config_set_pathnames_from_pgdata(&pathnames,
															pgdata))
				{
					log_info("Waiting for creation of a state file at \"%s\"",
							 pathnames.state);
				}
			}

			if (file_exists(pathnames.state))
			{
				KeeperStateData keeperState = { 0 };

				if (keeper_state_read(&keeperState, pathnames.state))
				{
					if (targetState == INIT_STATE &&
						keeperState.assigned_role > targetState)
					{
						log_info("Node \"%s\" is now assigned %s, done waiting",
								 nodeName,
								 NodeStateToString(keeperState.assigned_role));
						break;
					}

					if (keeperState.assigned_role == targetState &&
						keeperState.current_role == targetState)
					{
						log_info("Node \"%s\" is currently %s/%s, done waiting",
								 nodeName,
								 NodeStateToString(keeperState.current_role),
								 NodeStateToString(keeperState.assigned_role));
						break;
					}

					log_info("Node \"%s\" is currently %s/%s, waiting for %s",
							 nodeName,
							 NodeStateToString(keeperState.current_role),
							 NodeStateToString(keeperState.assigned_role),
							 NodeStateToString(targetState));
				}
				else
				{
					log_info("Waiting for node \"%s\" to be registered",
							 nodeName);
				}
			}

			sleep(1);
			--timeout;
		}

		/* we might have reached the timeout */
		if (!file_exists(pathnames.state))
		{
			log_fatal("Reached timeout while waiting for state file \"%s\"",
					  pathnames.state);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
}
