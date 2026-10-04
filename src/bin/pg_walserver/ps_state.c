/*
 * src/bin/pg_walserver/ps_state.c
 *   See ps_state.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <string.h>

#include "postgres_fe.h"

#include "pqexpbuffer.h"

#include "ps_state.h"
#include "file_utils.h"
#include "log.h"
#include "string_utils.h"

/*
 * ws_ps_state_path fills dest with "<pgdata>/" WS_PS_STATE_FILENAME.
 */
void
ws_ps_state_path(const char *pgdata, char *dest, size_t destSize)
{
	sformat(dest, destSize, "%s/" WS_PS_STATE_FILENAME, pgdata);
}


/*
 * ws_ps_state_write overwrites the state file under pgdata with state's
 * current contents (write_file_atomic(), so a concurrent reader never sees
 * a half-written file). A no-op (returns true) when pgdata is NULL/empty --
 * "serve --insecure" with no --pgdata has nowhere to write one, exactly as
 * it has no pidfile either.
 */
bool
ws_ps_state_write(const char *pgdata, const WsPsState *state)
{
	if (pgdata == NULL || pgdata[0] == '\0')
	{
		return true;
	}

	PQExpBuffer content = createPQExpBuffer();

	if (content == NULL)
	{
		log_error("Failed to allocate memory to write the pg_walserver "
				  "ps state file");
		return false;
	}

	appendPQExpBuffer(content, "serve pid=%d started=%lld\n",
					  (int) state->servePid, (long long) state->serveStartedAt);

	for (int i = 0; i < state->receivewalWorkerCount; i++)
	{
		const WsPsReceivewalEntry *c = &state->receivewalWorkers[i];

		appendPQExpBuffer(content,
						  "receivewal route=%s path=%s pid=%d started=%lld restarts=%d "
						  "lsn=%s timeline=%u observed=%lld\n",
						  c->routeKey, c->path, (int) c->pid,
						  (long long) c->startedAt, c->restarts,
						  c->lsn, c->lsnTimeline, (long long) c->lsnObservedAt);
	}

	for (int i = 0; i < state->bootstrapCount; i++)
	{
		const WsPsBootstrapEntry *b = &state->bootstraps[i];

		appendPQExpBuffer(content, "bootstrap route=%s pid=%d started=%lld\n",
						  b->routeKey, (int) b->pid, (long long) b->startedAt);
	}

	if (PQExpBufferBroken(content))
	{
		log_error("Failed to build the pg_walserver ps state file: out of memory");
		destroyPQExpBuffer(content);
		return false;
	}

	char path[MAXPGPATH];

	ws_ps_state_path(pgdata, path, sizeof(path));

	bool ok = write_file_atomic(content->data, content->len, path);

	destroyPQExpBuffer(content);

	return ok;
}


/*
 * parse_one_line parses one "key=value key=value ..." line (the first
 * whitespace-separated token, "serve"/"receivewal"/"bootstrap", has already
 * been consumed by the caller) into whatever the caller's own out-params
 * point at, recognizing only the field names it is given.
 */
static void
parse_kv(char *line, char *routeKeyOut, char *pathOut, pid_t *pidOut,
		 time_t *startedAtOut, int *restartsOut, char *lsnOut,
		 uint32_t *lsnTimelineOut, time_t *lsnObservedAtOut)
{
	char *savePtr = NULL;
	char *tok = strtok_r(line, " ", &savePtr);

	while (tok != NULL)
	{
		char *eq = strchr(tok, '=');

		if (eq != NULL)
		{
			*eq = '\0';

			const char *key = tok;
			const char *value = eq + 1;

			if (strcmp(key, "route") == 0 && routeKeyOut != NULL)
			{
				strlcpy(routeKeyOut, value, NAMEDATALEN + 16);
			}
			else if (strcmp(key, "path") == 0 && pathOut != NULL)
			{
				strlcpy(pathOut, value, MAXPGPATH);
			}
			else if (strcmp(key, "pid") == 0 && pidOut != NULL)
			{
				*pidOut = (pid_t) atoll(value); /* IGNORE-BANNED */
			}
			else if (strcmp(key, "started") == 0 && startedAtOut != NULL)
			{
				*startedAtOut = (time_t) atoll(value); /* IGNORE-BANNED */
			}
			else if (strcmp(key, "restarts") == 0 && restartsOut != NULL)
			{
				*restartsOut = (int) atoll(value); /* IGNORE-BANNED */
			}
			else if (strcmp(key, "lsn") == 0 && lsnOut != NULL)
			{
				strlcpy(lsnOut, value, 32);
			}
			else if (strcmp(key, "timeline") == 0 && lsnTimelineOut != NULL)
			{
				*lsnTimelineOut = (uint32_t) atoll(value); /* IGNORE-BANNED */
			}
			else if (strcmp(key, "observed") == 0 && lsnObservedAtOut != NULL)
			{
				*lsnObservedAtOut = (time_t) atoll(value); /* IGNORE-BANNED */
			}
		}

		tok = strtok_r(NULL, " ", &savePtr);
	}
}


/*
 * ws_ps_state_read reads the state file back. Returns false (state
 * untouched) when pgdata is NULL/empty, or the file does not exist (no
 * "serve" has ever run for this --pgdata) -- callers must treat that as
 * "not running", never as an error.
 */
bool
ws_ps_state_read(const char *pgdata, WsPsState *state)
{
	if (pgdata == NULL || pgdata[0] == '\0')
	{
		return false;
	}

	memset(state, 0, sizeof(WsPsState));

	char path[MAXPGPATH];

	ws_ps_state_path(pgdata, path, sizeof(path));

	char *contents = NULL;
	long fileSize = 0;

	if (!read_file_if_exists(path, &contents, &fileSize) || contents == NULL)
	{
		return false;
	}

	char *saveLine = NULL;
	char *line = strtok_r(contents, "\n", &saveLine);

	while (line != NULL)
	{
		char *saveTok = NULL;
		char *kind = strtok_r(line, " ", &saveTok);
		char *rest = saveTok; /* everything after the first token */

		if (kind == NULL)
		{
			line = strtok_r(NULL, "\n", &saveLine);
			continue;
		}

		if (strcmp(kind, "serve") == 0)
		{
			parse_kv(rest, NULL, NULL, &state->servePid, &state->serveStartedAt,
					 NULL, NULL, NULL, NULL);
		}
		else if (strcmp(kind, "receivewal") == 0 &&
				 state->receivewalWorkerCount < WS_PS_MAX_ENTRIES)
		{
			WsPsReceivewalEntry *c =
				&state->receivewalWorkers[state->receivewalWorkerCount];

			memset(c, 0, sizeof(WsPsReceivewalEntry));
			parse_kv(rest, c->routeKey, c->path, &c->pid, &c->startedAt,
					 &c->restarts, c->lsn, &c->lsnTimeline, &c->lsnObservedAt);
			state->receivewalWorkerCount++;
		}
		else if (strcmp(kind, "bootstrap") == 0 &&
				 state->bootstrapCount < WS_PS_MAX_ENTRIES)
		{
			WsPsBootstrapEntry *b = &state->bootstraps[state->bootstrapCount];

			memset(b, 0, sizeof(WsPsBootstrapEntry));
			parse_kv(rest, b->routeKey, NULL, &b->pid, &b->startedAt, NULL,
					 NULL, NULL, NULL);
			state->bootstrapCount++;
		}

		line = strtok_r(NULL, "\n", &saveLine);
	}

	free(contents);

	return true;
}
