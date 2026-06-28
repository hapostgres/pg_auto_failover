/*
 * src/bin/pg_autoctl/nodespec.h
 *   Declarative node description file (.ini) — read, write, and converge.
 *
 * A NodeSpec is the in-memory representation of a pg_autoctl_node.ini file.
 * It covers all parameters needed to create and run one pg_auto_failover node.
 *
 * Lifecycle:
 *   1. nodespec_read()     — parse the file into a NodeSpec
 *   2. nodespec_create()   — run `pg_autoctl create <kind>` if PGDATA absent
 *   3. nodespec_run()      — hand off to the normal run path
 *   4. nodespec_apply()    — converge mutable fields on an already-running node
 *
 * The supervisor calls nodespec_check_and_apply() on a timer so that editing
 * the file and saving it is sufficient to reconfigure a running node.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#ifndef NODESPEC_H
#define NODESPEC_H

#include <stdbool.h>
#include <sys/stat.h>
#include <time.h>

#include "postgres_fe.h"  /* MAXPGPATH, NAMEDATALEN, etc. */
#include "pgsql.h"        /* MAXCONNINFO */
#include "pgsetup.h"      /* PgInstanceKind, NODE_KIND_* */

/*
 * Fixed location inside every container.  The compose generator writes one
 * ini file per node and bind-mounts it here.  Having a fixed path means the
 * Docker image needs only a single CMD:
 *
 *   CMD ["pg_autoctl", "node", "run", PG_AUTOCTL_NODESPEC_PATH]
 */
#define PG_AUTOCTL_NODESPEC_PATH "/etc/pgaf/node.ini"

/*
 * How often the supervisor polls the spec file for changes (seconds).
 * inotify/kqueue are used when available; this is the fallback interval.
 */
#define NODESPEC_WATCH_INTERVAL_SECS 10

/* -----------------------------------------------------------------------
 * NodeSpec — one-to-one with the [sections] of pg_autoctl_node.ini
 * ----------------------------------------------------------------------- */

typedef struct NodeSpec
{
	/* [node] */
	PgInstanceKind kind;         /* postgres | coordinator | worker | monitor */
	char hostname[_POSIX_HOST_NAME_MAX];
	int  port;                   /* Postgres port, default 5432 */

	/* [postgresql] */
	char pgdata[MAXPGPATH];

	/* [monitor]   — empty for kind == monitor */
	char monitor_pguri[MAXCONNINFO];

	/* [formation] */
	char formation[NAMEDATALEN]; /* default "default" */
	int  group;                  /* Citus group; 0 = coordinator */

	/* [settings]  — mutable; applied on SIGHUP / file change */
	int  candidate_priority;     /* 0-100, default 50 */
	bool replication_quorum;     /* sync quorum participant, default true */

	/* [options]    — immutable; used only at pg_autoctl create time */
	char ssl[32];                /* self-signed | cert | off */
	char auth[32];               /* trust | md5 | scram */
	bool pg_hba_lan;             /* add --pg-hba-lan flag */
} NodeSpec;

/* -----------------------------------------------------------------------
 * File watcher state — embedded in Supervisor
 * ----------------------------------------------------------------------- */

typedef struct NodeSpecWatcher
{
	bool     active;             /* true once nodespec_watcher_init() succeeds */
	char     path[MAXPGPATH];   /* path of the watched file                   */
	time_t   last_mtime;        /* mtime at last check                        */
	time_t   last_checked;      /* wall-clock time of last poll               */

#ifdef __linux__
	int      inotify_fd;        /* inotify instance fd, -1 if unavailable     */
	int      watch_fd;          /* inotify watch descriptor                   */
#endif
} NodeSpecWatcher;

/* -----------------------------------------------------------------------
 * API
 * ----------------------------------------------------------------------- */

/* Parse a pg_autoctl_node.ini file into *spec.  Returns false on error. */
bool nodespec_read(const char *path, NodeSpec *spec);

/* Write *spec as a pg_autoctl_node.ini file (for pg_autoctl node show). */
bool nodespec_write(const NodeSpec *spec, FILE *out);

/* Build the argv[] for `pg_autoctl create <kind> [flags]`.
 * Caller provides args[] with room for at least 32 char* entries.
 * Returns the number of entries filled (not counting the trailing NULL). */
int  nodespec_create_argv(const NodeSpec *spec,
                          const char *pg_autoctl_path,
                          char **args, int args_size);

/* Apply mutable fields (candidate_priority, replication_quorum) to a
 * running node by calling into the keeper/monitor APIs directly.
 * Logs what changed.  Returns false only on hard error. */
bool nodespec_apply(const NodeSpec *new_spec, const NodeSpec *old_spec);

/* -----------------------------------------------------------------------
 * Watcher — called from supervisor loop
 * ----------------------------------------------------------------------- */

/* Initialise watcher state.  Sets up inotify on Linux if available. */
bool nodespec_watcher_init(NodeSpecWatcher *w, const char *path);

/* Called from the supervisor's 100 ms tick.
 * Returns true if the file changed and nodespec_apply() was called. */
bool nodespec_watcher_check(NodeSpecWatcher *w, const NodeSpec *current);

/* Release inotify fds (called at supervisor shutdown). */
void nodespec_watcher_close(NodeSpecWatcher *w);

#endif /* NODESPEC_H */
