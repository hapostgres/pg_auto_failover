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

volatile sig_atomic_t window_size_changed = 0;      /* SIGWINCH */

static bool print_nodes_array(WatchContext *context, int r, int c);


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
watch_main_loop(Monitor *monitor, char *formation, int groupId)
{
	WatchContext context = { 0 };

	context.monitor = monitor;
	strlcpy(context.formation, formation, sizeof(context.formation));
	context.groupId = groupId;

	(void) watch_init_window(&context);

	/* the main loop */
	for (;;)
	{
		/* we quit when watch_update returns false */
		if (!watch_update(&context))
		{
			break;
		}

		/* now display the context we have */
		(void) watch_render(&context);

		/* and then sleep for 250 ms */
		pg_usleep(250 * 1000);
	}

	(void) watch_end_window(&context);
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
	CurrentNodeStateArray *nodesArray = &(context->nodesArray);

	if (!monitor_get_current_state(context->monitor,
								   context->formation,
								   context->groupId,
								   nodesArray))
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
	mvprintw(0, 0, "Press F1 to exit [%dx%d]", context->rows, context->cols);

	(void) print_nodes_array(context, 2, 0);

	refresh();

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
	PgInstanceKind firstNodeKind = NODE_KIND_UNKNOWN;

	if (nodesArray->count > 0)
	{
		firstNodeKind = nodesArray->nodes[0].pgKind;
	}

	(void) nodestatePrepareHeaders(nodesArray, firstNodeKind);

	/* display the headers */
	attron(A_STANDOUT | A_UNDERLINE);

	mvprintw(currentRow++, c,
			 "%*s",
			 nodesArray->headers.maxNameSize,
			 "Name");

	attroff(A_STANDOUT | A_UNDERLINE);

	for (int index = 0; index < nodesArray->count; index++)
	{
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

		int currentCol = c;

		mvprintw(currentRow, currentCol, "%*s",
				 nodesArray->headers.maxNameSize,
				 nodeState->node.name);

		currentCol += nodesArray->headers.maxNameSize + 1;

		if ((currentCol + nodesArray->headers.maxHealthSize) < context->cols)
		{
			mvprintw(currentRow, currentCol, "%*s",
					 nodesArray->headers.maxHealthSize,
					 connection);
		}

		++currentRow;
	}

	return true;
}
