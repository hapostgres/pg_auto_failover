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

static int print_watch_header(WatchContext *context, int r);
static int print_watch_footer(WatchContext *context);
static int print_nodes_array(WatchContext *context, int r, int c);
static int print_events_array(WatchContext *context, int r, int c);

static ColPolicy * pick_column_policy(WatchContext *context);
static bool compute_column_spec_lens(WatchContext *context);
static int compute_column_size(ColumnType type, NodeAddressHeaders *headers);


static void print_column_headers(WatchContext *context,
								 ColPolicy *policy,
								 int r,
								 int c);

static void print_node_state(WatchContext *context,
							 ColPolicy *policy,
							 int index,
							 int r,
							 int c);

static void print_events_headers(WatchContext *context, int r, int c);

static void watch_set_state_attributes(NodeState state, bool toggle);

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
	/* the main loop */
	for (bool first = true;; first = false)
	{
		instr_time start;
		instr_time duration;

		INSTR_TIME_SET_CURRENT(start);

		/* we quit when watch_update returns false */
		if (!watch_update(context))
		{
			break;
		}

		/* now display the context we have */
		if (first)
		{
			(void) watch_init_window(context);
		}

		(void) watch_render(context);

		/* and then sleep for the rest of the 500 ms */
		INSTR_TIME_SET_CURRENT(duration);
		INSTR_TIME_SUBTRACT(duration, start);

		int sleepMs = 500 - INSTR_TIME_GET_MILLISEC(duration);

		pg_usleep(sleepMs * 1000);
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
	MonitorEventsArray *eventsArray = &(context->eventsArray);

	/*
	 * We use a transaction despite being read-only, because we want to re-use
	 * a single connection to the monitor.
	 */
	monitor->pgsql.connectionStatementType = PGSQL_CONNECTION_MULTI_STATEMENT;

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

	if (!monitor_get_last_events(monitor, context->formation, context->groupId,
								 EVENTS_BUFFER_COUNT,
								 eventsArray))
	{
		/* errors have already been logged */
		return false;
	}

	/* time to finish our connection */
	pgsql_finish(&(monitor->pgsql));

	int ch;

	/*
	 * Reset our move from the last update session. We need to keep the END
	 * movement set in between update calls, though, because this one is
	 * handled on a line-by-line basis, and is not reflected on the value of
	 * context->startCol.
	 */
	if (context->move != WATCH_MOVE_FOCUS_END)
	{
		context->move = WATCH_MOVE_FOCUS_NONE;
	}

	do {
		/* we have setup ncurses in non-blocking behaviour */
		ch = getch();

		if (ch == KEY_F(1) || ch == 'q')
		{
			return false;
		}
		else if (ch == KEY_RESIZE || window_size_changed == 1)
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

		/* left and right moves are conditionnal / relative */
		else if (ch == KEY_LEFT || ch == 'b' || ch == 'h')
		{
			if (context->move == WATCH_MOVE_FOCUS_NONE)
			{
				context->move = WATCH_MOVE_FOCUS_LEFT;

				if (context->startCol > 0)
				{
					context->startCol -= context->cols - 10;

					if (context->startCol < 0)
					{
						context->startCol = 0;
					}
				}
			}
			else if (context->move == WATCH_MOVE_FOCUS_RIGHT)
			{
				context->move = WATCH_MOVE_FOCUS_NONE;
			}
		}

		/* left and right moves are conditionnal / relative */
		else if (ch == KEY_RIGHT || ch == 'f' || ch == 'l')
		{
			if (context->move == WATCH_MOVE_FOCUS_NONE)
			{
				context->move = WATCH_MOVE_FOCUS_RIGHT;

				context->startCol += context->cols - 10;
			}
			else if (context->move == WATCH_MOVE_FOCUS_LEFT)
			{
				context->move = WATCH_MOVE_FOCUS_NONE;
			}
		}

		/* home and end moves are unconditionnal / absolute */
		else if (ch == KEY_HOME || ch == 'a' || ch == '0')
		{
			context->move = WATCH_MOVE_FOCUS_HOME;

			context->startCol = 0;
		}
		else if (ch == KEY_END || ch == 'e' || ch == '$')
		{
			context->move = WATCH_MOVE_FOCUS_END;
		}

		/* up is C-p in Emacs, C-u in less/more, k in vi(m) */
		else if (ch == KEY_UP || ch == 'p' || ch == 'u' || ch == 'k')
		{
			if (context->selectedRow > 0)
			{
				--context->selectedRow;
			}
		}

		/* page up */
		else if (ch == KEY_PPAGE)
		{
			if (context->selectedRow > 0 && context->selectedRow <= 6)
			{
				context->selectedRow = 1;
			}
			else if (context->selectedRow > 6)
			{
				context->selectedRow -= 5;
			}
		}

		/* down is C-n in Emacs, C-d in less/more, j in vi(m) */
		else if (ch == KEY_DOWN || ch == 'n' || ch == 'd' || ch == 'j')
		{
			if (context->selectedRow < context->rows)
			{
				++context->selectedRow;
			}
		}

		/* page down */
		else if (ch == KEY_NPAGE)
		{
			if (context->selectedRow < context->rows &&
				context->selectedRow >= (context->rows - 6))
			{
				context->selectedRow = context->rows - 1;
			}
			else if (context->selectedRow < (context->rows - 6))
			{
				context->selectedRow += 5;
			}
		}

		/* cancel current selected row */
		else if (ch == KEY_DL || ch == KEY_DC)
		{
			context->selectedRow = 0;
		}
	} while (ch != ERR);

	return true;
}


/*
 * watch_render displays the context on the terminal window.
 */
bool
watch_render(WatchContext *context)
{
	int printedRows = 0;

	printedRows += print_watch_header(context, 0);

	(void) clear_line_at(1);
	++printedRows;

	/* skip empty lines and headers */
	if (context->selectedRow > 0 && context->selectedRow < 3)
	{
		context->selectedRow = 3;
	}

	int nodeRows = print_nodes_array(context, 2, 0);

	printedRows += nodeRows;

	(void) clear_line_at(2 + nodeRows);
	++printedRows;

	/* skip empty lines and headers */
	if (context->selectedRow >= (2 + nodeRows) &&
		context->selectedRow <= 2 + nodeRows + 1)
	{
		context->selectedRow = 2 + nodeRows + 2;
	}

	printedRows += print_events_array(context, 2 + nodeRows + 1, 0);

	if (printedRows < context->rows)
	{
		for (int r = printedRows; r < (context->rows - 1); r++)
		{
			(void) clear_line_at(r);
		}
		(void) print_watch_footer(context);
	}

	refresh();

	return true;
}


/*
 * print_watch_header prints the first line of the screen, with the current
 * formation that's being displayed, the number_sync_standbys, and the current
 * time.
 */
static int
print_watch_header(WatchContext *context, int r)
{
	uint64_t now = time(NULL);
	char timestring[MAXCTIMESIZE] = { 0 };

	/* make sure we start with an empty line */
	(void) clear_line_at(0);

	/* format the current time to be user-friendly */
	epoch_to_string(now, timestring);

	/* "Wed Jun 30 21:49:08 1993" -> "21:49:08" */
	timestring[11 + 8] = '\0';

	clear_line_at(r);

	mvprintw(r, context->cols - 9, "%s", timestring + 11);

	int c = 0;

	mvprintw(r, c, "Formation: "); /* that's 11 chars */
	c += 11;

	attron(A_BOLD);
	mvprintw(r, c, "%s", context->formation);
	attroff(A_BOLD);

	c += strlen(context->formation);

	/*
	 * Check if we have enough room for a full label here:
	 *  - add  9 cols for the date at the end of the line
	 *  - add 18 cols for the label " - Sync Standbys: "
	 *  - add  3 cols for the number itself (e.g. "1")
	 */
	if (context->cols > (c + 9 + 18 + 3))
	{
		mvprintw(r, c, " - Sync Standbys: "); /* that's 18 chars */
		c += 18;
	}
	else
	{
		mvprintw(r, c, " - nss: "); /* that's 8 chars */
		c += 8;
	}

	attron(A_BOLD);
	mvprintw(r, c, "%d", context->number_sync_standbys);
	attroff(A_BOLD);

	/* we only use one row */
	return 1;
}


/*
 * print_watch_footer prints the last line of the screen, an help message.
 */
static int
print_watch_footer(WatchContext *context)
{
	int r = context->rows - 1;
	char *help = "Press F1 to exit";

	clear_line_at(r);
	mvprintw(r, context->cols - strlen(help) - 1, help);

	/* we only use one row */
	return 1;
}


/*
 * print_nodes_array prints a nodes array at the given position (r, c) in a
 * window of size (context->rows, context->cols).
 */
static int
print_nodes_array(WatchContext *context, int r, int c)
{
	CurrentNodeStateArray *nodesArray = &(context->nodesArray);

	int lines = 0;
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

	++lines;

	/* display the data */
	for (int index = 0; index < nodesArray->count; index++)
	{
		bool selected = currentRow == context->selectedRow;

		clear_line_at(currentRow);

		if (selected)
		{
			attron(A_REVERSE);
		}

		(void) print_node_state(context, columnPolicy, index, currentRow++, c);

		if (selected)
		{
			attroff(A_REVERSE);
		}

		++lines;

		if (context->rows <= currentRow)
		{
			break;
		}
	}

	return lines;
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

		case COLUMN_TYPE_CONN_HEALTH_LAG:
		{
			/* that's an interval in seconds/mins/hours/days: XXuYYu */
			return 7;
		}

		case COLUMN_TYPE_CONN_REPORT_LAG:
		{
			/* that's an interval in seconds/mins/hours/days: XXuYYu */
			return 7;
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

	clear_line_at(r);

	attron(A_STANDOUT);

	for (int col = 0; col < COLUMN_TYPE_LAST; col++)
	{
		int len = policy->specs[col].len;
		char *name = policy->specs[col].name;

		mvprintw(r, cc, "%*s ", len, name);

		cc += len + 1;
	}

	attroff(A_STANDOUT);
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
				if (nodeState->health != 1)
				{
					attron(A_REVERSE | A_BOLD);
				}

				mvprintw(r, cc, "%*s", len, connection);

				if (nodeState->health != 1)
				{
					attroff(A_REVERSE | A_BOLD);
				}

				break;
			}

			case COLUMN_TYPE_CONN_HEALTH_LAG:
			{
				char str[9] = { 0 };

				(void) IntervalToString(nodeState->healthLag, str, sizeof(str));

				mvprintw(r, cc, "%*s", len, str);
				break;
			}

			case COLUMN_TYPE_CONN_REPORT_LAG:
			{
				char str[9] = { 0 };

				if (nodeState->reportLag > 10.0)
				{
					attron(A_REVERSE);
				}

				(void) IntervalToString(nodeState->reportLag, str, sizeof(str));

				mvprintw(r, cc, "%*s", len, str);

				if (nodeState->reportLag > 10.0)
				{
					attroff(A_REVERSE);
				}

				break;
			}

			case COLUMN_TYPE_REPORTED_STATE:
			{
				watch_set_state_attributes(nodeState->reportedState, true);

				mvprintw(r, cc, "%*s", len,
						 NodeStateToString(nodeState->reportedState));

				watch_set_state_attributes(nodeState->reportedState, false);
				break;
			}

			case COLUMN_TYPE_ASSIGNED_STATE:
			{
				watch_set_state_attributes(nodeState->goalState, true);

				mvprintw(r, cc, "%*s", len,
						 NodeStateToString(nodeState->goalState));

				watch_set_state_attributes(nodeState->goalState, false);
				break;
			}

			default:
			{
				log_fatal("BUG: print_node_state(%d)", cType);
				exit(EXIT_CODE_INTERNAL_ERROR);
			}
		}

		cc += len;
		mvprintw(r, cc++, " ");
	}
}


/*
 * Routine used to set attributes to display node states.
 */
static void
watch_set_state_attributes(NodeState state, bool toggle)
{
	switch (state)
	{
		/* states where Postgres is not running */
		case DEMOTED_STATE:
		case DEMOTE_TIMEOUT_STATE:
		case DRAINING_STATE:
		case REPORT_LSN_STATE:
		{
			if (toggle)
			{
				attron(A_BOLD);
			}
			else
			{
				attroff(A_BOLD);
			}

			break;
		}

		/* states where the node is not participating in the failover */
		case MAINTENANCE_STATE:
		case WAIT_MAINTENANCE_STATE:
		case PREPARE_MAINTENANCE_STATE:
		case WAIT_STANDBY_STATE:
		case DROPPED_STATE:
		{
			if (toggle)
			{
				attron(A_DIM | A_UNDERLINE);
			}
			else
			{
				attroff(A_DIM | A_UNDERLINE);
			}

			break;
		}

		default:
		{
			/* do not change attributes for most cases */
			break;
		}
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


/*
 * print_events_array prints an events array at the given position (r, c) in a
 * window of size (context-rows, context->cols).
 */
static int
print_events_array(WatchContext *context, int r, int c)
{
	MonitorEventsArray *eventsArray = &(context->eventsArray);

	int lines = 0;
	int currentRow = r;

	(void) print_events_headers(context, currentRow++, c);
	++lines;

	int capacity = context->rows - currentRow;

	int start =
		eventsArray->count <= capacity ? 0 : eventsArray->count - capacity;

	for (int index = start; index < eventsArray->count; index++)
	{
		MonitorEvent *event = &(eventsArray->events[index]);
		bool selected = currentRow == context->selectedRow;

		char *text = event->description;
		int len = strlen(text);

		/* when KEY_END is used, ensure we see the end of text */
		if (context->move == WATCH_MOVE_FOCUS_END)
		{
			/* the eventTime format plus spacing takes up 21 chars on-screen */
			if (strlen(text) > (context->cols - 21))
			{
				text = text + len - 21;
			}
		}
		else if (context->startCol > 0 && len > (context->cols - 21))
		{
			/*
			 * Shift our text following the current startCol, or if we don't
			 * have that many chars in the text, then shift from as much as we
			 * can in steps of 10 increments.
			 */
			for (int sc = context->startCol; sc > 0; sc -= 10)
			{
				if (len >= sc)
				{
					text = text + sc;
					break;
				}
			}
		}

		clear_line_at(currentRow);

		if (selected)
		{
			attron(A_REVERSE);
		}

		mvprintw(currentRow, 0, "%19s %s%s",
				 event->eventTime,
				 text == event->description ? " " : " -- ",
				 text);

		if (selected)
		{
			attroff(A_REVERSE);
		}

		if (context->rows < currentRow)
		{
			break;
		}

		++currentRow;
		++lines;
	}

	return lines;
}


/*
 * print_event_headers prints the headers of the event list
 */
static void
print_events_headers(WatchContext *context, int r, int c)
{
	clear_line_at(r);

	attron(A_STANDOUT);

	mvprintw(r, c, "%19s  %-*s", "Event Time", context->cols - 20, "Description");

	attroff(A_STANDOUT);
}
