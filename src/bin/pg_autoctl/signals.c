/*
 * src/bin/pg_autoctl/signals.c
 *   Signal handlers for pg_autoctl, used in loop.c and pgsetup.c
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "defaults.h"
#include "log.h"
#include "signals.h"

/* This flag controls termination of the main loop. */
volatile sig_atomic_t asked_to_stop = 0;      /* SIGTERM */
volatile sig_atomic_t asked_to_stop_fast = 0; /* SIGINT */
volatile sig_atomic_t asked_to_reload = 0;    /* SIGHUP */


/*
 * set_signal_handlers sets our signal handlers for the 4 signals that we
 * specifically handle in pg_autoctl.
 */
void
set_signal_handlers()
{
	/* Establish a handler for signals. */
	signal(SIGHUP, catch_reload);
	signal(SIGINT, catch_int);
	signal(SIGTERM, catch_term);
	signal(SIGQUIT, catch_quit);
}


/*
 * catch_reload receives the SIGHUP signal.
 */
void
catch_reload(int sig)
{
	asked_to_reload = 1;
	log_warn("Received signal %s", strsignal(sig));
	signal(sig, catch_reload);
}


/*
 * catch_int receives the SIGINT signal.
 */
void
catch_int(int sig)
{
	asked_to_stop_fast = 1;
	log_warn("Fast shutdown: received signal %s", strsignal(sig));
	signal(sig, catch_int);
}


/*
 * catch_stop receives SIGTERM signal.
 */
void
catch_term(int sig)
{
	asked_to_stop = 1;
	log_warn("Smart shutdown: received signal %s", strsignal(sig));
	signal(sig, catch_term);
}


/*
 * catch_quit receives the SIGQUIT signal.
 */
void
catch_quit(int sig)
{
	/* default signal handler disposition is to core dump, we don't */
	log_warn("Immediate shutdown: received signal %s", strsignal(sig));
	exit(EXIT_CODE_QUIT);
}
