/*
 * src/bin/pg_autoctl/watch.c
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

#include <curses.h>
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
#include "watch.h"
#include "watch_colspecs.h"

volatile sig_atomic_t window_size_changed = 0;      /* SIGWINCH */

static bool print_nodes_array(WatchContext *context, int r, int c);

static ColPolicy * pick_column_policy(WatchContext *context);
static bool compute_column_spec_lens(WatchContext *context);
static int compute_column_size(ColumnType type, NodeAddressHeaders *headers);

static bool print_watch_header(WatchContext *context, int r);
static bool print_watch_footer(WatchContext *context);

static void print_column_headers(WatchContext *context,
								 ColPolicy *policy,
								 int r,
								 int c);

static void print_node_state(WatchContext *context,
							 ColPolicy *policy,
							 int index,
							 int r,
							 int c);

static void clear_line_at(int row);

/*
 * catch_sigwinch is registered as the SIGWINCH signal handler.
 */
static void
catch_sigwinch(int sig)
{
	window_size_changed = 1;
	pqsignal(sig, catch_sigwinch);
}


/*
 * watch_display_window takes over the terminal window and displays the state
 * and events in there, refreshing the output often, as when using the watch(1)
 * command, or similar to what top(1) would be doing.
 */
void
watch_main_loop(WatchContext *context)
{
	(void) watch_init_window(context);

	/* the main loop */
	for (;;)
	{
		/* we quit when watch_update returns false */
		if (!watch_update(context))
		{
			break;
		}

		/* now display the context we have */
		(void) watch_render(context);

		/* and then sleep for 250 ms */
		pg_usleep(250 * 1000);
	}

	(void) watch_end_window(context);
}


/*
 * watch_init_window takes care of displaying information on the current
 * interactive terminal window, handled with the ncurses API.
 */
void
watch_init_window(WatchContext *context)
{
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
	curs_set(0);                /* Do not display the cursor */

	pqsignal(SIGWINCH, catch_sigwinch);

	refresh();

	/* get the current size of the window */
	getmaxyx(stdscr, context->rows, context->cols);
}


/*
 * watch_end_window finishes our ncurses session and gives control back.
 */
void
watch_end_window(WatchContext *context)
{
	refresh();
	endwin();
}


/*
 * watch_update updates the context to be displayed on the terminal window.
 */
bool
watch_update(WatchContext *context)
{
	Monitor *monitor = &(context->monitor);
	CurrentNodeStateArray *nodesArray = &(context->nodesArray);

	if (!monitor_get_current_state(monitor,
								   context->formation,
								   context->groupId,
								   nodesArray))
	{
		/* errors have already been logged */
		return false;
	}

	if (!monitor_get_formation_number_sync_standbys(
			monitor,
			context->formation,
			&(context->number_sync_standbys)))
	{
		/* errors have already been logged */
		return false;
	}

	/* we have setup ncurses in non-blocking behaviour */
	int ch = getch();

	if (ch == KEY_F(1) || ch == 'q')
	{
		return false;
	}

	if (ch == KEY_RESIZE || window_size_changed == 1)
	{
		struct winsize size = { 0 };

		window_size_changed = 0;

		/* get current terminal rows and columns and resize our display */
		int ioctl_result = ioctl(STDOUT_FILENO, TIOCGWINSZ, (char *) &size);

		if (ioctl_result >= 0)
		{
			context->rows = size.ws_row;
			context->cols = size.ws_col;

			resizeterm(context->rows, context->cols);
		}
	}

	return true;
}


/*
 * watch_render displays the context on the terminal window.
 */
bool
watch_render(WatchContext *context)
{
	(void) print_watch_header(context, 0);
	(void) print_nodes_array(context, 2, 0);
	(void) print_watch_footer(context);

	refresh();

	return true;
}


/*
 * print_watch_header prints the first line of the screen, with the current
 * formation that's being displayed, the number_sync_standbys, and the current
 * time.
 */
static bool
print_watch_header(WatchContext *context, int r)
{
	uint64_t now = time(NULL);
	char timestring[MAXCTIMESIZE] = { 0 };

	/* format the current time to be user-friendly */
	epoch_to_string(now, timestring);

	/* "Wed Jun 30 21:49:08 1993" -> "21:49:08" */
	timestring[11 + 8] = '\0';

	clear_line_at(r);

	mvprintw(r, context->cols - 9, "%s", timestring + 11);

	mvprintw(r, 0, "Formation: %s - Sync Standbys: %d",
			 context->formation,
			 context->number_sync_standbys);

	return true;
}


/*
 * print_watch_footer prints the last line of the screen, an help message.
 */
static bool
print_watch_footer(WatchContext *context)
{
	int r = context->rows - 1;
	char *help = "Press F1 to exit";

	clear_line_at(r);
	mvprintw(r, context->cols - strlen(help) - 1, help);

	return true;
}


/*
 * print_nodes_array prints a nodes array at the given position (r, c) in a
 * window of size (context->rows, context->cols).
 */
static bool
print_nodes_array(WatchContext *context, int r, int c)
{
	CurrentNodeStateArray *nodesArray = &(context->nodesArray);

	int currentRow = r;

	(void) compute_column_spec_lens(context);

	ColPolicy *columnPolicy = pick_column_policy(context);

	if (columnPolicy == NULL)
	{
		clear();
		mvprintw(0, 0, "Window too small: %dx%d", context->rows, context->cols);
		refresh();

		return false;
	}

	/* display the headers */
	clear_line_at(currentRow);
	(void) print_column_headers(context, columnPolicy, currentRow++, c);

	/* display the data */
	for (int index = 0; index < nodesArray->count; index++)
	{
		clear_line_at(currentRow);
		(void) print_node_state(context, columnPolicy, index, currentRow++, c);
	}

	return true;
}


/*
 * pick_column_spec chooses which column spec should be used depending on the
 * current size (rows, cols) of the display, and given update column specs with
 * the actual lenghts of the data to be displayed.
 */
static ColPolicy *
pick_column_policy(WatchContext *context)
{
	ColPolicy *bestPolicy = NULL;

	for (int i = 0; i < ColumnPoliciesCount; i++)
	{
		/* minimal, terse, verbose, full */
		ColPolicy *policy = &(ColumnPolicies[i]);

		if (policy->totalSize <= context->cols && bestPolicy == NULL)
		{
			bestPolicy = policy;
		}
		else if (policy->totalSize <= context->cols &&
				 policy->totalSize >= bestPolicy->totalSize)
		{
			bestPolicy = policy;
		}
	}

	return bestPolicy;
}


/*
 * compute_column_spec_lens computes the len of each known column
 * specification, given the actual data to print.
 */
static bool
compute_column_spec_lens(WatchContext *context)
{
	CurrentNodeStateArray *nodesArray = &(context->nodesArray);

	PgInstanceKind firstNodeKind = NODE_KIND_UNKNOWN;

	if (nodesArray->count > 0)
	{
		firstNodeKind = nodesArray->nodes[0].pgKind;
	}

	(void) nodestatePrepareHeaders(nodesArray, firstNodeKind);

	for (int i = 0; i < ColumnPoliciesCount; i++)
	{
		/* minimal, terse, verbose, full */
		ColPolicy *policy = &(ColumnPolicies[i]);

		/* reset last computed size */
		policy->totalSize = 0;

		for (int col = 0; policy->specs[col].type != COLUMN_TYPE_LAST; col++)
		{
			ColumnType cType = policy->specs[col].type;

			int headerLen = strlen(policy->specs[col].name);
			int dataLen = compute_column_size(cType, &(nodesArray->headers));

			/* the column header name might be larger than the data */
			int len = headerLen > dataLen ? headerLen : dataLen;

			policy->specs[col].len = len;
			policy->totalSize += len + 1; /* add one space between columns */
		}

		/* remove extra space after last column */
		policy->totalSize -= 1;
	}

	return true;
}


/*
 * compute_column_size returns the size needed to display a given column type
 * given the pre-computed size of the nodes array header, where the alignment
 * with the rest of the array is taken in consideration.
 */
static int
compute_column_size(ColumnType type, NodeAddressHeaders *headers)
{
	switch (type)
	{
		case COLUMN_TYPE_NAME:
		{
			return headers->maxNameSize;
		}

		case COLUMN_TYPE_ID:
		{
			return headers->maxNodeSize;
		}

		case COLUMN_TYPE_REPLICATION_QUORUM:
		{
			/* that one is going to be "yes" or "no" */
			return 3;
		}

		case COLUMN_TYPE_CANDIDATE_PRIORITY:
		{
			/* that's an integer in the range 0..100 */
			return 3;
		}

		case COLUMN_TYPE_HOST_PORT:
		{
			return headers->maxHostSize;
		}

		case COLUMN_TYPE_TLI_LSN:
		{
			return headers->maxLSNSize;
		}

		case COLUMN_TYPE_CONN_HEALTH:
		{
			return headers->maxHealthSize;
		}

		case COLUMN_TYPE_REPORTED_STATE:
		{
			return headers->maxStateSize;
		}

		case COLUMN_TYPE_ASSIGNED_STATE:
		{
			return headers->maxStateSize;
		}

		default:
		{
			log_fatal("BUG: compute_column_size(%d)", type);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}

	/* keep compiler happy */
	return 0;
}


/*
 * print_column_headers prints the headers of the selection column policy.
 */
static void
print_column_headers(WatchContext *context, ColPolicy *policy, int r, int c)
{
	int cc = c;

	attron(A_STANDOUT | A_UNDERLINE);

	for (int col = 0; col < COLUMN_TYPE_LAST; col++)
	{
		int len = policy->specs[col].len;
		char *name = policy->specs[col].name;

		mvprintw(r, cc, "%*s", len, name);

		cc += len + 1;
	}

	attroff(A_STANDOUT | A_UNDERLINE);
}


/*
 * print_node_state prints the given nodestate with the selected column policy.
 */
static void
print_node_state(WatchContext *context, ColPolicy *policy,
				 int index, int r, int c)
{
	CurrentNodeStateArray *nodesArray = &(context->nodesArray);
	CurrentNodeState *nodeState = &(nodesArray->nodes[index]);

	char hostport[BUFSIZE] = { 0 };
	char composedId[BUFSIZE] = { 0 };
	char tliLSN[BUFSIZE] = { 0 };
	char connection[BUFSIZE] = { 0 };
	char healthChar = nodestateHealthToChar(nodeState->health);

	(void) nodestatePrepareNode(&(nodesArray->headers),
								&(nodeState->node),
								nodeState->groupId,
								hostport,
								composedId,
								tliLSN);

	if (healthChar == ' ')
	{
		sformat(connection, BUFSIZE, "%s", nodestateConnectionType(nodeState));
	}
	else
	{
		sformat(connection, BUFSIZE, "%s %c",
				nodestateConnectionType(nodeState), healthChar);
	}

	int cc = c;

	for (int col = 0; policy->specs[col].type != COLUMN_TYPE_LAST; col++)
	{
		ColumnType cType = policy->specs[col].type;
		int len = policy->specs[col].len;

		switch (cType)
		{
			case COLUMN_TYPE_NAME:
			{
				mvprintw(r, cc, "%*s", len, nodeState->node.name);
				break;
			}

			case COLUMN_TYPE_ID:
			{
				mvprintw(r, cc, "%*s", len, composedId);
				break;
			}

			case COLUMN_TYPE_REPLICATION_QUORUM:
			{
				mvprintw(r, cc, "%*s", len,
						 nodeState->replicationQuorum ? "yes" : "no");
				break;
			}

			case COLUMN_TYPE_CANDIDATE_PRIORITY:
			{
				mvprintw(r, cc, "%*d", len, nodeState->candidatePriority);
				break;
			}

			case COLUMN_TYPE_HOST_PORT:
			{
				mvprintw(r, cc, "%*s", len, hostport);
				break;
			}

			case COLUMN_TYPE_TLI_LSN:
			{
				mvprintw(r, cc, "%*s", len, tliLSN);
				break;
			}

			case COLUMN_TYPE_CONN_HEALTH:
			{
				mvprintw(r, cc, "%*s", len, connection);
				break;
			}

			case COLUMN_TYPE_REPORTED_STATE:
			{
				mvprintw(r, cc, "%*s", len,
						 NodeStateToString(nodeState->reportedState));
				break;
			}

			case COLUMN_TYPE_ASSIGNED_STATE:
			{
				mvprintw(r, cc, "%*s", len,
						 NodeStateToString(nodeState->goalState));
				break;
			}

			default:
			{
				log_fatal("BUG: print_node_state(%d)", cType);
				exit(EXIT_CODE_INTERNAL_ERROR);
			}
		}

		cc += len + 1;
	}
}


/*
 * clear_line_at clears the line at given row number by displaying space
 * characters on the whole line.
 */
static void
clear_line_at(int row)
{
	move(row, 0);
	clrtoeol();
}
