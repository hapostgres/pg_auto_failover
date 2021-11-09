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
#include <math.h>
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

static bool cli_watch_update_from_monitor(WatchContext *context);
static bool cli_watch_process_keys(WatchContext *context);

static int print_watch_header(WatchContext *context, int r);
static int print_watch_footer(WatchContext *context);
static int print_nodes_array(WatchContext *context, int r, int c);
static int print_events_array(WatchContext *context, int r, int c);

static void print_current_time(WatchContext *context, int r);

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

static EventColPolicy * pick_event_column_policy(WatchContext *context);
static bool compute_event_column_spec_lens(WatchContext *context);
static bool compute_events_sizes(WatchContext *context);
static int compute_event_column_size(EventColumnType type,
									 MonitorEventsHeaders *headers);

static void print_events_headers(WatchContext *context,
								 EventColPolicy *policy,
								 int r,
								 int c);

static int print_event(WatchContext *context,
					   EventColPolicy *policy,
					   int index,
					   int r,
					   int c);

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
 * watch_main_loop takes over the terminal window and displays the state and
 * events in there, refreshing the output often, as when using the watch(1)
 * command, or similar to what top(1) would be doing.
 */
void
cli_watch_main_loop(WatchContext *context)
{
	WatchContext previous = { 0 };

	int step = -1;

	/* the main loop */
	for (;;)
	{
		instr_time start;
		instr_time duration;

		INSTR_TIME_SET_CURRENT(start);

		/*
		 * First, update the data that we want to display, and process key
		 * strokes. We are going to update our data set twice per second, and
		 * we want to react to key strokes and other events much faster than
		 * this, every 50ms.
		 */
		step = (step + 1) % 10;

		(void) cli_watch_update(context, step);

		if (context->shouldExit)
		{
			break;
		}

		/* now display the context we have */
		if (context->couldContactMonitor)
		{
			(void) cli_watch_render(context, &previous);
		}
		else if (!context->cookedMode)
		{
			/* get back to "cooked" terminal mode, showing stderr logs */
			context->cookedMode = true;
			def_prog_mode();
			endwin();
		}

		/* and then sleep for the rest of the 50 ms */
		INSTR_TIME_SET_CURRENT(duration);
		INSTR_TIME_SUBTRACT(duration, start);

		int sleepMs = 50 - INSTR_TIME_GET_MILLISEC(duration);

		if (sleepMs > 0)
		{
			pg_usleep(sleepMs * 1000);
		}

		/* update the previous context */
		previous = *context;
	}

	(void) cli_watch_end_window(context);
}


/*
 * watch_init_window takes care of displaying information on the current
 * interactive terminal window, handled with the ncurses API.
 */
void
cli_watch_init_window(WatchContext *context)
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
	nonl();                     /* Do not translate RETURN key */
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
cli_watch_end_window(WatchContext *context)
{
	refresh();
	endwin();
}


/*
 * watch_update updates the context to be displayed on the terminal window.
 */
bool
cli_watch_update(WatchContext *context, int step)
{
	/* only update data from the monitor at step 0 */
	if (step == 0)
	{
		context->couldContactMonitor = cli_watch_update_from_monitor(context);
	}

	/* now process any key pressed by the user */
	bool processKeys = cli_watch_process_keys(context);

	/* failure to process keys signals we should exit now */
	context->shouldExit = (processKeys == false);

	return true;
}


/*
 * cli_watch_update_from_monitor fetches the data to display from the
 * pg_auto_failover monitor database.
 */
static bool
cli_watch_update_from_monitor(WatchContext *context)
{
	Monitor *monitor = &(context->monitor);
	CurrentNodeStateArray *nodesArray = &(context->nodesArray);
	MonitorEventsArray *eventsArray = &(context->eventsArray);

	/*
	 * We use a transaction despite being read-only, because we want to re-use
	 * a single connection to the monitor.
	 */
	PGSQL *pgsql = &(monitor->pgsql);

	pgsql->connectionStatementType = PGSQL_CONNECTION_MULTI_STATEMENT;

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

	if (!monitor_get_last_events(monitor,
								 context->formation,
								 context->groupId,
								 EVENTS_BUFFER_COUNT,
								 eventsArray))
	{
		/* errors have already been logged */
		return false;
	}

	/* time to finish our connection */
	pgsql_finish(pgsql);

	return true;
}


/* Capture CTRL + a key */
#define ctrl(x) ((x) & 0x1f)

/*
 * cli_watch_process_keys processes the user input.
 */
static bool
cli_watch_process_keys(WatchContext *context)
{
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
				if (context->rows != size.ws_row || context->cols != size.ws_col)
				{
					context->rows = size.ws_row;
					context->cols = size.ws_col;

					resizeterm(context->rows, context->cols);
				}
			}
		}
		/* left and right moves are conditionnal / relative */
		else if (ch == KEY_LEFT || ch == ctrl('b') || ch == 'h')
		{
			if (context->move == WATCH_MOVE_FOCUS_NONE)
			{
				context->move = WATCH_MOVE_FOCUS_LEFT;

				if (context->startCol > 0)
				{
					/* move by half the description column */
					context->startCol -= (context->cols - 21) / 2;

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
		else if (ch == KEY_RIGHT || ch == ctrl('f') || ch == 'l')
		{
			if (context->move == WATCH_MOVE_FOCUS_NONE)
			{
				context->move = WATCH_MOVE_FOCUS_RIGHT;

				/* move by half the description column */
				context->startCol += (context->cols - 21) / 2;
			}
			else if (context->move == WATCH_MOVE_FOCUS_LEFT)
			{
				context->move = WATCH_MOVE_FOCUS_NONE;
			}
		}
		/* home and end moves are unconditionnal / absolute */
		else if (ch == KEY_HOME || ch == ctrl('a') || ch == '0')
		{
			context->move = WATCH_MOVE_FOCUS_HOME;

			context->startCol = 0;
		}
		else if (ch == KEY_END || ch == ctrl('e') || ch == '$')
		{
			context->move = WATCH_MOVE_FOCUS_END;
		}
		/* up is C-p in Emacs, k in vi(m) */
		else if (ch == KEY_UP || ch == ctrl('p') || ch == 'k')
		{
			context->move = WATCH_MOVE_FOCUS_UP;

			if (context->selectedRow > 0)
			{
				--context->selectedRow;
			}
		}
		/* page up, which is also C-u in the terminal with less/more etc */
		else if (ch == KEY_PPAGE || ch == ctrl('u'))
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
		/* down is C-n in Emacs, j in vi(m) */
		else if (ch == KEY_DOWN || ch == ctrl('n') || ch == 'j')
		{
			context->move = WATCH_MOVE_FOCUS_DOWN;

			if (context->selectedRow < context->rows)
			{
				++context->selectedRow;
			}
		}
		/* page down, which is also C-d in the terminal with less/more etc */
		else if (ch == KEY_NPAGE || ch == ctrl('d'))
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
			context->selectedArea = 0;
		}
	} while (ch != ERR);

	return true;
}


/*
 * watch_render displays the context on the terminal window.
 */
bool
cli_watch_render(WatchContext *context, WatchContext *previous)
{
	int printedRows = 0;

	/* on the first call to render, initialize the ncurses terminal control */
	if (!context->initialized)
	{
		(void) cli_watch_init_window(context);
		context->initialized = true;
	}

	/*
	 * When we fail to contact the monitor, we switch the terminal back to
	 * cookedMode so that the usual stderr logs are visible. In that case the
	 * render function is not called. When cli_watch_render() is called again,
	 * it means we could contact the monitor and get an update, and we need to
	 * take control of the terminal again.
	 */
	if (context->cookedMode)
	{
		reset_prog_mode();
		refresh();

		context->cookedMode = false;
	}

	/* adjust selected row to fit the selected area */
	int nodeHeaderRow = 2;
	int firstNodeRow = nodeHeaderRow + 1;
	int lastNodeRow = firstNodeRow + context->nodesArray.count - 1;

	int eventHeaderRow = lastNodeRow + 2; /* blank line, evenzt headers */
	int firstEventRow = eventHeaderRow + 1;
	int lastEventRow = firstEventRow + context->eventsArray.count - 1;

	if (lastEventRow > context->rows)
	{
		lastEventRow = context->rows;
	}

	/* first usage of the arrow keys select an area */
	if (context->selectedArea == 0 && context->selectedRow > 0)
	{
		context->selectedArea = 1;
	}

	/*
	 * Adjust the selectedRow position to make sure we always select a row
	 * that's part of the data: avoid empty separation lines, avoid header
	 * lines.
	 *
	 * We conceptually divide the screen in two areas: first, the nodes array
	 * area, and then the events area. When scrolling away from an area we may
	 * jump to the other area directly.
	 */
	if (context->selectedArea == 1)
	{
		if (context->selectedRow < firstNodeRow)
		{
			context->selectedRow = firstNodeRow;
		}
		else if (context->selectedRow > lastNodeRow)
		{
			context->selectedArea = 2;
			context->selectedRow = firstEventRow;
		}
	}
	else if (context->selectedArea == 2)
	{
		if (context->selectedRow < firstEventRow)
		{
			context->selectedArea = 1;
			context->selectedRow = lastNodeRow;
		}
		else if (context->selectedRow > lastEventRow)
		{
			context->selectedRow = lastEventRow;
		}
	}

	/*
	 * Print the main header and then the nodes array.
	 */
	printedRows += print_watch_header(context, 0);

	/* skip empty lines and headers */
	(void) clear_line_at(1);
	++printedRows;

	int nodeRows = print_nodes_array(context, nodeHeaderRow, 0);
	printedRows += nodeRows;

	(void) clear_line_at(printedRows);

	/*
	 * Now print the events array. Because that operation is more expensive,
	 * and because most of the times there is no event happening, we compare
	 * the current context with the previous one and avoid this part of the
	 * code entirely when we figure out that we would only redisplay what's
	 * already visible on the terminal.
	 */

	if (context->rows != previous->rows ||
		context->cols != previous->cols ||
		context->selectedRow != previous->selectedRow ||
		context->selectedArea != previous->selectedArea ||
		context->startCol != previous->startCol ||
		context->cookedMode != previous->cookedMode ||
		context->eventsArray.count != previous->eventsArray.count ||
		(context->eventsArray.events[0].eventId !=
		 previous->eventsArray.events[0].eventId))
	{
		(void) clear_line_at(++printedRows);

		printedRows += print_events_array(context, eventHeaderRow, 0);

		/* clean the remaining rows that we didn't use for displaying events */
		if (printedRows < context->rows)
		{
			for (int r = printedRows; r < context->rows; r++)
			{
				(void) clear_line_at(r);
			}
		}
	}

	/* now display the footer */
	(void) print_watch_footer(context);

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
	int c = 0;

	(void) print_current_time(context, r);

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
 * print_current_time prints the current time on the far right of the first
 * line of the screen.
 */
static void
print_current_time(WatchContext *context, int r)
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
}


/*
 * print_watch_footer prints the last line of the screen, an help message.
 */
static int
print_watch_footer(WatchContext *context)
{
	int r = context->rows - 1;
	char *help = "Press F1 to exit";

	attron(A_STANDOUT);

	mvprintw(r, context->cols - strlen(help), "%s", help);

	attroff(A_STANDOUT);

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
 * pick_event_column_policy chooses which column spec should be used depending
 * on the current size (rows, cols) of the display, and given update column
 * specs with the actual lenghts of the data to be displayed.
 */
static EventColPolicy *
pick_event_column_policy(WatchContext *context)
{
	EventColPolicy *bestPolicy = NULL;

	for (int i = 0; i < EventColumnPoliciesCount; i++)
	{
		/* minimal, terse, verbose, full */
		EventColPolicy *policy = &(EventColumnPolicies[i]);

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
compute_event_column_spec_lens(WatchContext *context)
{
	MonitorEventsHeaders *headers = &(context->eventsHeaders);

	(void) compute_events_sizes(context);

	for (int i = 0; i < EventColumnPoliciesCount; i++)
	{
		/* minimal, terse, verbose, full */
		EventColPolicy *policy = &(EventColumnPolicies[i]);

		/* reset last computed size */
		policy->totalSize = 0;

		for (int col = 0; policy->specs[col].type != EVENT_COLUMN_TYPE_LAST; col++)
		{
			EventColumnType cType = policy->specs[col].type;

			int headerLen = strlen(policy->specs[col].name);
			int dataLen = compute_event_column_size(cType, headers);

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
 * compute events len properties (maximum length for the columns we have)
 */
static bool
compute_events_sizes(WatchContext *context)
{
	MonitorEventsArray *eventsArray = &(context->eventsArray);
	MonitorEventsHeaders *headers = &(context->eventsHeaders);

	for (int index = 0; index < eventsArray->count; index++)
	{
		MonitorEvent *event = &(eventsArray->events[index]);

		int idSize = log10(event->eventId) + 1;
		int nameSize = strlen(event->nodeName);
		int timeSize = 19;      /* "YYYY-MM-DD HH:MI:SS" is 19 chars long */
		int descSize = 60;      /* desc. has horizontal scrolling */

		if (headers->maxEventIdSize < idSize)
		{
			headers->maxEventIdSize = idSize;
		}

		if (headers->maxEventTimeSize < timeSize)
		{
			headers->maxEventTimeSize = timeSize;
		}

		if (headers->maxEventNodeNameSize < nameSize)
		{
			headers->maxEventNodeNameSize = nameSize;
		}

		if (headers->maxEventDescSize < descSize)
		{
			headers->maxEventDescSize = descSize;
		}
	}

	return true;
}


/*
 * compute_event_column_size returns the size needed to display a given column
 * type given the pre-computed size of the events array header, where the
 * alignment with the rest of the array is taken in consideration.
 */
static int
compute_event_column_size(EventColumnType type, MonitorEventsHeaders *headers)
{
	switch (type)
	{
		case EVENT_COLUMN_TYPE_ID:
		{
			return headers->maxEventIdSize;
		}

		case EVENT_COLUMN_TYPE_TIME:
		{
			return headers->maxEventTimeSize;
		}

		case EVENT_COLUMN_TYPE_NODE_NAME:
		{
			return headers->maxEventNodeNameSize;
		}

		case EVENT_COLUMN_TYPE_DESCRIPTION:
		{
			return headers->maxEventDescSize;
		}

		default:
		{
			log_fatal("BUG: compute_event_column_size(%d)", type);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}

	/* keep compiler happy */
	return 0;
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
	int maxStartCol = 0;

	/* compute column sizes */
	(void) compute_event_column_spec_lens(context);

	/* pick a display policy for the events table */
	EventColPolicy *eventColumnPolicy = pick_event_column_policy(context);

	if (eventColumnPolicy == NULL)
	{
		clear();
		mvprintw(0, 0, "Window too small: %dx%d", context->rows, context->cols);
		refresh();

		return false;
	}

	/* display the events headers */
	(void) print_events_headers(context, eventColumnPolicy, currentRow++, c);
	++lines;

	int capacity = context->rows - currentRow;

	int start =
		eventsArray->count <= capacity ? 0 : eventsArray->count - capacity;

	/* display most recent events first */
	for (int index = eventsArray->count - 1; index >= start; index--)
	{
		bool selected = currentRow == context->selectedRow;

		clear_line_at(currentRow);

		if (selected)
		{
			attron(A_REVERSE);
		}

		int sc = print_event(context, eventColumnPolicy, index, currentRow, c);

		if (sc > maxStartCol)
		{
			maxStartCol = sc;
		}

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

	/* reset context->startCol to something sensible when it needs to be */
	if (maxStartCol > 0 && maxStartCol < context->startCol)
	{
		context->startCol = maxStartCol;
	}

	return lines;
}


/*
 * print_node_state prints the given nodestate with the selected column policy.
 */
static int
print_event(WatchContext *context, EventColPolicy *policy, int index, int r, int c)
{
	MonitorEventsArray *eventsArray = &(context->eventsArray);
	MonitorEvent *event = &(eventsArray->events[index]);

	int cc = c;
	int startCol = context->startCol;

	for (int col = 0; policy->specs[col].type < EVENT_COLUMN_TYPE_LAST; col++)
	{
		EventColumnType cType = policy->specs[col].type;
		int len = policy->specs[col].len;

		switch (cType)
		{
			case EVENT_COLUMN_TYPE_ID:
			{
				mvprintw(r, cc, "%*lld", len, (long long) event->eventId);
				break;
			}

			case EVENT_COLUMN_TYPE_TIME:
			{
				mvprintw(r, cc, "%*s", len, event->eventTime);
				break;
			}

			case EVENT_COLUMN_TYPE_NODE_NAME:
			{
				mvprintw(r, cc, "%*s", len, event->nodeName);
				break;
			}

			case EVENT_COLUMN_TYPE_DESCRIPTION:
			{
				char *text = event->description;
				int len = strlen(text);

				/* when KEY_END is used, ensure we see the end of text */
				if (context->move == WATCH_MOVE_FOCUS_END)
				{
					/*
					 * The eventTime format plus spacing takes up 21 chars
					 * on-screen
					 */
					if (strlen(text) > (context->cols - cc))
					{
						text = text + len - cc;
					}
				}
				else if (context->startCol > 0 && len > (context->cols - cc))
				{
					/*
					 * Shift our text following the current startCol, or if we
					 * don't have that many chars in the text, then shift from
					 * as much as we can in steps of 10 increments.
					 */
					int step = (context->cols - cc) / 2;

					for (; startCol > 0; startCol -= step)
					{
						if (len >= startCol)
						{
							text = text + startCol;

							break;
						}
					}
				}

				mvprintw(r, cc, "%s%s",
						 text == event->description ? " " : " -- ",
						 text);

				break;
			}

			default:
			{
				log_fatal("BUG: print_event(%d)", cType);
				exit(EXIT_CODE_INTERNAL_ERROR);
			}
		}

		/* We know DESCRIPTION is the last column, and we skip computing its
		 * actual size... so the len of this field is a static value (60).
		 * Avoid printing the column separator in the middle of the actual
		 * description text.
		 */
		if (cType != EVENT_COLUMN_TYPE_DESCRIPTION)
		{
			cc += len;
			mvprintw(r, cc, "  ");
			cc += 2;
		}
	}

	return startCol;
}


/*
 * print_column_headers prints the headers of the selection column policy.
 */
static void
print_events_headers(WatchContext *context, EventColPolicy *policy, int r, int c)
{
	int cc = c;

	clear_line_at(r);

	attron(A_STANDOUT);

	for (int col = 0; col < EVENT_COLUMN_TYPE_LAST; col++)
	{
		int len = policy->specs[col].len;
		char *name = policy->specs[col].name;
		EventColumnType cType = policy->specs[col].type;

		/* the description field takes all that's left on the display */
		if (cType == EVENT_COLUMN_TYPE_DESCRIPTION)
		{
			mvprintw(r, cc, " %-*s", context->cols - cc - 1, name);
		}
		else
		{
			mvprintw(r, cc, "%*s", len, name);
		}

		cc += len;
		mvprintw(r, cc, "  ");
		cc += 2;
	}

	attroff(A_STANDOUT);
}
