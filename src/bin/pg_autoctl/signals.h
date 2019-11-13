/*
 * src/bin/pg_autoctl/signals.h
 *   Signal handlers for pg_autoctl, used in loop.c and pgsetup.c
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef SIGNALS_H
#define SIGNALS_H

#include <inttypes.h>
#include <signal.h>

/* This flag controls termination of the main loop. */
extern volatile sig_atomic_t asked_to_stop;		 /* SIGTERM */
extern volatile sig_atomic_t asked_to_stop_fast; /* SIGINT */
extern volatile sig_atomic_t asked_to_reload;	 /* SIGHUP */

#define CHECK_FOR_FAST_SHUTDOWN {if (asked_to_stop_fast) {break;}}

void set_signal_handlers(void);
void catch_reload(int sig);
void catch_int(int sig);
void catch_term(int sig);
void catch_quit(int sig);

#endif /* SIGNALS_H */
