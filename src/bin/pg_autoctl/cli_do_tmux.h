/*
 * src/bin/pg_autoctl/cli_do_tmux.h
 *     Implementation of a CLI which lets you run operations on the local
 *     postgres server directly.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef CLI_DO_TMUX_H
#define CLI_DO_TMUX_H

#include "postgres_fe.h"
#include "pqexpbuffer.h"
#include "snprintf.h"

#include "cli_common.h"
#include "cli_do_root.h"
#include "cli_root.h"
#include "commandline.h"
#include "config.h"
#include "env_utils.h"
#include "log.h"
#include "pidfile.h"
#include "signals.h"
#include "string_utils.h"

#define MAX_NODES 12

typedef struct TmuxOptions
{
	char root[MAXPGPATH];
	int firstPort;
	int nodes;                  /* number of nodes per groups, total */
	int asyncNodes;             /* number of async nodes, within the total */
	int priorities[MAX_NODES];  /* node priorities */
	int numSync;                /* number-sync-standbys */
	bool skipHBA;               /* do we want to use --skip-pg-hba? */
	char layout[BUFSIZE];
	char binpath[MAXPGPATH];
} TmuxOptions;

typedef struct TmuxNode
{
	char name[NAMEDATALEN];
	int pgport;
	bool replicationQuorum;
	int candidatePriority;
} TmuxNode;

typedef struct TmuxNodeArray
{
	int count;                  /* array actual size */
	int numSync;                /* number-sync-standbys */
	TmuxNode nodes[MAX_NODES];
} TmuxNodeArray;

extern char *tmux_banner[];
extern TmuxOptions tmuxOptions;
extern TmuxNodeArray tmuxNodeArray;

bool parseCandidatePriorities(char *prioritiesString, int *priorities);

void tmux_add_command(PQExpBuffer script, const char *fmt, ...)
__attribute__((format(printf, 2, 3)));

void tmux_add_send_keys_command(PQExpBuffer script, const char *fmt, ...)
__attribute__((format(printf, 2, 3)));

bool tmux_has_session(const char *tmux_path, const char *sessionName);
void tmux_add_new_session(PQExpBuffer script,
						  const char *root, int pgport);

void tmux_add_xdg_environment(PQExpBuffer script);
void tmux_setenv(PQExpBuffer script,
				 const char *sessionName, const char *root, int firstPort);
bool tmux_prepare_XDG_environment(const char *root,
								  bool createDirectories);

void tmux_pg_autoctl_create_monitor(PQExpBuffer script,
									const char *root,
									const char *binpath,
									int pgport,
									bool skipHBA);

void tmux_pg_autoctl_create_postgres(PQExpBuffer script,
									 const char *root,
									 const char *binpath,
									 int pgport,
									 const char *name,
									 bool replicationQuorum,
									 int candidatePriority,
									 bool skipHBA);

bool tmux_start_server(const char *scriptName, const char *binpath);
bool pg_autoctl_getpid(const char *pgdata, pid_t *pid);

bool tmux_has_session(const char *tmux_path, const char *sessionName);
bool tmux_attach_session(const char *tmux_path, const char *sessionName);
bool tmux_kill_session(TmuxOptions *options);
bool tmux_kill_session_by_name(const char *sessionName);

void tmux_process_options(TmuxOptions *options);
void tmux_cleanup_stale_directory(TmuxOptions *options);


#endif  /* CLI_DO_TMUX_H */
