/*
 * src/bin/pg_autoctl/cli_watch.c
 *     Implementation of a CLI to show events, states, and URI from the
 *     pg_auto_failover monitor.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <inttypes.h>
#include <getopt.h>
#include <unistd.h>

#include <ncurses.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "postgres_fe.h"

#include "cli_common.h"
#include "commandline.h"
#include "defaults.h"
#include "env_utils.h"
#include "ipaddr.h"
#include "keeper_config.h"
#include "keeper.h"
#include "monitor_config.h"
#include "monitor_pg_init.h"
#include "monitor.h"
#include "nodestate_utils.h"
#include "parsing.h"
#include "pgctl.h"
#include "pghba.h"
#include "pgsetup.h"
#include "pgsql.h"
#include "pidfile.h"
#include "state.h"
#include "string_utils.h"

static int cli_watch_getopts(int argc, char **argv);
static void cli_watch(int argc, char **argv);

static WINDOW * create_newwin(int height, int width, int starty, int startx);
static void destroy_win(WINDOW *local_win);

volatile sig_atomic_t window_size_changed = 0;      /* SIGWINCH */


static void
catch_sigwinch(int sig)
{
	window_size_changed = 1;
	pqsignal(sig, catch_sigwinch);
}


CommandLine watch_command =
	make_command("watch",
				 "Display a dashboard to watch monitor's events and state",
				 " [ --pgdata --formation --group ] ",
				 "  --pgdata      path to data directory	 \n"
				 "  --monitor     show the monitor uri\n"
				 "  --formation   formation to query, defaults to 'default' \n"
				 "  --group       group to query formation, defaults to all \n"
				 "  --json        output data in the JSON format\n",
				 cli_watch_getopts,
				 cli_watch);

static int
cli_watch_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "monitor", required_argument, NULL, 'm' },
		{ "formation", required_argument, NULL, 'f' },
		{ "group", required_argument, NULL, 'g' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	/* set default values for our options, when we have some */
	options.groupId = -1;
	options.network_partition_timeout = -1;
	options.prepare_promotion_catchup = -1;
	options.prepare_promotion_walreceiver = -1;
	options.postgresql_restart_failure_timeout = -1;
	options.postgresql_restart_failure_max_retries = -1;

	optind = 0;

	while ((c = getopt_long(argc, argv, "D:f:g:n:Vvqh",
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

			case 'm':
			{
				if (!validate_connection_string(optarg))
				{
					log_fatal("Failed to parse --monitor connection string, "
							  "see above for details.");
					exit(EXIT_CODE_BAD_ARGS);
				}
				strlcpy(options.monitor_pguri, optarg, MAXCONNINFO);
				log_trace("--monitor %s", options.monitor_pguri);
				break;
			}

			case 'f':
			{
				strlcpy(options.formation, optarg, NAMEDATALEN);
				log_trace("--formation %s", options.formation);
				break;
			}

			case 'g':
			{
				if (!stringToInt(optarg, &options.groupId))
				{
					log_fatal("--group argument is not a valid group ID: \"%s\"",
							  optarg);
					exit(EXIT_CODE_BAD_ARGS);
				}
				log_trace("--group %d", options.groupId);
				break;
			}

			case 'V':
			{
				/* keeper_cli_print_version prints version and exits. */
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
				/* getopt_long already wrote an error message */
				errors++;
			}
		}
	}

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* when we have a monitor URI we don't need PGDATA */
	if (cli_use_monitor_option(&options))
	{
		if (!IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata))
		{
			log_warn("Given --monitor URI, the --pgdata option is ignored");
			log_info("Connecting to monitor at \"%s\"", options.monitor_pguri);
		}
	}
	else
	{
		cli_common_get_set_pgdata_or_exit(&(options.pgSetup));
	}

	/* when --pgdata is given, still initialise our pathnames */
	if (!IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata))
	{
		if (!keeper_config_set_pathnames_from_pgdata(&(options.pathnames),
													 options.pgSetup.pgdata))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_CONFIG);
		}
	}

	/* ensure --formation, or get it from the configuration file */
	if (!cli_common_ensure_formation(&options))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	keeperOptions = options;

	return optind;
}


/*
 * cli_watch starts a ncurses dashboard that displays relevant information
 * about a running formation at a given monitor.
 */
static void
cli_watch(int argc, char **argv)
{
	int row, col;                /* to store the number of rows and */
	int ch;

	int startx, starty, width, height;

	struct winsize size = { 0 };
	int ioctl_result = 0;

	if ((ioctl_result = ioctl(STDOUT_FILENO, TIOCGWINSZ, (char *) &size)) >= 0)
	{
		resize_term(size.ws_row, size.ws_col);
	}

	initscr();                  /* Start curses mode          */

	cbreak();                   /* Line buffering disabled	*/
	intrflush(stdscr, FALSE);   /* No flushing on interrupts */
	keypad(stdscr, TRUE);       /* We get F1, F2 etc..		*/
	noecho();                   /* Don't echo() while we do getch */
	nodelay(stdscr, TRUE);      /* Non blocking getch() variants */

	pqsignal(SIGWINCH, catch_sigwinch);

	height = 3;
	width = 10;
	starty = (LINES - height) / 2;  /* Calculating for a center placement */
	startx = (COLS - width) / 2;    /* of the window		*/

	printw("Press F1 to exit");
	refresh();

	WINDOW *my_win = create_newwin(height, width, starty, startx);

	while ((ch = getch()) != KEY_F(1))
	{
		if (ch == KEY_RESIZE || window_size_changed == 1)
		{
			window_size_changed = 0;

			/* get current terminal rows and columns and resize our display */
			ioctl_result = ioctl(STDOUT_FILENO, TIOCGWINSZ, (char *) &size);

			if (ioctl_result >= 0)
			{
				row = size.ws_row;
				col = size.ws_col;

				resizeterm(row, col);
			}

			mvprintw(0, 0, "Press F1 to exit [%dx%d]", row, col);

			mvwprintw(my_win, 1, 1, "%d x %d", row, col);
			wrefresh(my_win);
		}

		switch (ch)
		{
			case KEY_LEFT:
			{
				destroy_win(my_win);
				my_win = create_newwin(height, width, starty, --startx);
				break;
			}

			case KEY_RIGHT:
			{
				destroy_win(my_win);
				my_win = create_newwin(height, width, starty, ++startx);
				break;
			}

			case KEY_UP:
			{
				destroy_win(my_win);
				my_win = create_newwin(height, width, --starty, startx);
				break;
			}

			case KEY_DOWN:
			{
				destroy_win(my_win);
				my_win = create_newwin(height, width, ++starty, startx);
				break;
			}
		}
	}

	getmaxyx(stdscr, row, col);       /* get the number of rows and columns */
	mvprintw(row - 2, 0, "This screen has %d rows and %d columns\n", row, col);

	refresh();                  /* Print it on to the real screen */
	getch();                    /* Wait for user input */
	endwin();                   /* End curses mode		  */
}


static WINDOW *
create_newwin(int height, int width, int starty, int startx)
{
	WINDOW *local_win;

	local_win = newwin(height, width, starty, startx);
	box(local_win, 0, 0);       /* 0, 0 gives default characters
	                             * for the vertical and horizontal
	                             * lines			*/

	mvwprintw(local_win, 1, 1, "%d x %d", LINES, COLS);

	wrefresh(local_win);        /* Show that box        */

	return local_win;
}


static void
destroy_win(WINDOW *local_win)
{
	/* box(local_win, ' ', ' '); : This won't produce the desired
	 * result of erasing the window. It will leave it's four corners
	 * and so an ugly remnant of window.
	 */
	wborder(local_win, ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ');

	/* The parameters taken are
	 * 1. win: the window on which to operate
	 * 2. ls: character to be used for the left side of the window
	 * 3. rs: character to be used for the right side of the window
	 * 4. ts: character to be used for the top side of the window
	 * 5. bs: character to be used for the bottom side of the window
	 * 6. tl: character to be used for the top left corner of the window
	 * 7. tr: character to be used for the top right corner of the window
	 * 8. bl: character to be used for the bottom left corner of the window
	 * 9. br: character to be used for the bottom right corner of the window
	 */
	wrefresh(local_win);
	delwin(local_win);
}
