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
#include "postgres_fe.h"    /* SIGNAL_ARGS, pqsigfunc */

/* This flag controls termination of the main loop. */
extern volatile sig_atomic_t asked_to_stop;      /* SIGTERM */
extern volatile sig_atomic_t asked_to_stop_fast; /* SIGINT */
extern volatile sig_atomic_t asked_to_reload;    /* SIGHUP */
extern volatile sig_atomic_t asked_to_quit;      /* SIGQUIT */

#define CHECK_FOR_FAST_SHUTDOWN { if (asked_to_stop_fast) { break; } \
}

void set_signal_handlers(bool exitOnQuit);
bool block_signals(sigset_t *mask, sigset_t *orig_mask);
void unblock_signals(sigset_t *orig_mask);
void catch_reload(SIGNAL_ARGS);
void catch_int(SIGNAL_ARGS);
void catch_term(SIGNAL_ARGS);
void catch_quit(SIGNAL_ARGS);
void catch_quit_and_exit(SIGNAL_ARGS);

int get_current_signal(int defaultSignal);
int pick_stronger_signal(int sig1, int sig2);
char * signal_to_string(int signal);

#endif /* SIGNALS_H */
