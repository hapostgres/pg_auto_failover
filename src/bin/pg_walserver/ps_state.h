/*
 * src/bin/pg_walserver/ps_state.h
 *   A tiny, plain-text cross-process status snapshot: "pg_walserver serve"
 *   writes "<pgdata>/pg_walserver_ps.status" every time its own supervised
 *   child set changes (an embedded pull capturer starting/restarting, an
 *   automatic bootstrap backup job starting) and at least once a second
 *   from its own main accept-loop tick -- see accept_loop.c's own
 *   refresh_ps_state(). "pg_walserver ps"/"pg_walserver status" (cli_ps.c/
 *   cli_status.c), run as a brand-new, separate process, read this file
 *   back rather than trying to inspect "serve"'s own in-process C structs,
 *   which obviously do not exist in a different process's address space.
 *
 *   Why a state file rather than /proc scraping: "serve"'s own capturer
 *   children (capture.c) are exec()'d as "pg_walserver internal service
 *   pg-receivewal --route <key> ..." -- their route key IS visible in
 *   /proc/<pid>/cmdline, so a determined "ps" could, in principle, walk
 *   /proc, find every pid whose cmdline matches, and reconstruct the
 *   route/pid mapping without any cooperation from "serve" at all. Two
 *   reasons this file is preferred over that: (1) the one-shot bootstrap-
 *   backup child (backup_bootstrap.c) is a *plain* fork(), not an exec()
 *   into a distinguishable command line -- ws_bootstrap_missing_backups()'s
 *   in-process bookkeeping (accept_loop.c's own bootstrapChildren[]) is the
 *   only place its route association exists at all, so a state file this
 *   process itself writes is the only way to expose that mapping to
 *   another process without adding a second, redundant execv()-into-a-
 *   distinguishable-title indirection for a one-shot job that does not
 *   otherwise need one; (2) start time and restart counters live in
 *   ProcessRestartCounters (process_supervisor.h), an in-process ring
 *   buffer with no on-disk representation at all -- /proc gives a process's
 *   own start time (stat's starttime field) but not this project's own
 *   restart-count bookkeeping. A small state file "serve" already knows
 *   how to write, kept current on every state change, is simpler and more
 *   accurate than reconstructing partial answers from two different
 *   information sources (a fork()-only job and a fork()+exec() one) that
 *   don't expose the same things.
 *
 *   Deliberately plain text, not JSON: this project has no JSON writer
 *   linked into pg_walserver at all (a real dependency this small file
 *   does not need to introduce), and the same "key = value" line shape
 *   this project's own wal_dir_scan.c "archiver-position" cache file
 *   already uses is enough for a handful of small, fixed fields per line.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_PS_STATE_H
#define WS_PS_STATE_H

#include <stdbool.h>
#include <sys/types.h>
#include <time.h>

#include "postgres_fe.h"

#define WS_PS_STATE_FILENAME "pg_walserver_ps.status"
#define WS_PS_MAX_ENTRIES 64

typedef struct WsPsCapturerEntry
{
	char routeKey[NAMEDATALEN + 16];
	char path[MAXPGPATH];
	pid_t pid;              /* <= 0: not currently running (gave up, or
	                         * never started) */
	time_t startedAt;       /* this incarnation's own start time */
	int restarts;           /* how many times it has been restarted */
} WsPsCapturerEntry;

typedef struct WsPsBootstrapEntry
{
	char routeKey[NAMEDATALEN + 16];
	pid_t pid;
	time_t startedAt;
} WsPsBootstrapEntry;

typedef struct WsPsState
{
	pid_t servePid;
	time_t serveStartedAt;

	WsPsCapturerEntry capturers[WS_PS_MAX_ENTRIES];
	int capturerCount;

	WsPsBootstrapEntry bootstraps[WS_PS_MAX_ENTRIES];
	int bootstrapCount;
} WsPsState;

/*
 * ws_ps_state_path fills dest with "<pgdata>/" WS_PS_STATE_FILENAME.
 */
void ws_ps_state_path(const char *pgdata, char *dest, size_t destSize);

/*
 * ws_ps_state_write overwrites the state file under pgdata with state's
 * current contents (write_file_atomic(), so a concurrent reader never sees
 * a half-written file). A no-op (returns true) when pgdata is NULL/empty --
 * "serve --insecure" with no --pgdata has nowhere to write one, exactly as
 * it has no pidfile either.
 */
bool ws_ps_state_write(const char *pgdata, const WsPsState *state);

/*
 * ws_ps_state_read reads the state file back. Returns false (state
 * untouched) when pgdata is NULL/empty, or the file does not exist (no
 * "serve" has ever run for this --pgdata) -- callers must treat that as
 * "not running", never as an error.
 */
bool ws_ps_state_read(const char *pgdata, WsPsState *state);

#endif /* WS_PS_STATE_H */
