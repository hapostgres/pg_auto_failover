/*
 * src/bin/pg_walserver/ps_state.h
 *   A tiny, plain-text cross-process status snapshot: "pg_walserver serve"
 *   writes "<pgdata>/pg_walserver_ps.status" every time its own supervised
 *   child set changes (an embedded receivewal worker starting/restarting, an
 *   automatic bootstrap backup job starting) and at least once a second
 *   from its own main accept-loop tick -- see accept_loop.c's own
 *   refresh_ps_state(). "pg_walserver ps"/"pg_walserver status" (cli_ps.c/
 *   cli_status.c), run as a brand-new, separate process, read this file
 *   back rather than trying to inspect "serve"'s own in-process C structs,
 *   which obviously do not exist in a different process's address space.
 *
 *   Why a state file rather than /proc scraping: "serve"'s own receivewal worker
 *   children (receivewal.c) are exec()'d as "pg_walserver internal service
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
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#include "postgres_fe.h"

#define WS_PS_STATE_FILENAME "pg_walserver_ps.status"
#define WS_PS_MAX_ENTRIES 64

typedef struct WsPsReceivewalEntry
{
	char routeKey[NAMEDATALEN + 16];
	char path[MAXPGPATH];
	pid_t pid;              /* <= 0: not currently running (gave up, or
	                         * never started) */
	time_t startedAt;       /* this incarnation's own start time */
	int restarts;           /* how many times it has been restarted */

	/*
	 * The receivewal worker's own last-observed (lsn, timeline), relayed
	 * through wal_dir_scan.h's "<path>/receivewal-progress" file (written by
	 * the worker's own pgaf_wal_progress_hook/pgaf_wal_segment_closed_hook
	 * callbacks, cli_internal.c) and folded in here by accept_loop.c's own
	 * refresh_ps_state() tick. lsn[0] == '\0' means "no reading yet" (the
	 * worker has never ticked, or isn't running) -- display-only, NOT a
	 * safe resume/replay position, see wal_dir_scan.h's own comment.
	 */
	char lsn[32];
	uint32_t lsnTimeline;
	time_t lsnObservedAt;   /* wall-clock time of that reading; 0 = none */
} WsPsReceivewalEntry;

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

	WsPsReceivewalEntry receivewalWorkers[WS_PS_MAX_ENTRIES];
	int receivewalWorkerCount;

	WsPsBootstrapEntry bootstraps[WS_PS_MAX_ENTRIES];
	int bootstrapCount;
} WsPsState;

void ws_ps_state_path(const char *pgdata, char *dest, size_t destSize);

bool ws_ps_state_write(const char *pgdata, const WsPsState *state);

bool ws_ps_state_read(const char *pgdata, WsPsState *state);

#endif /* WS_PS_STATE_H */
