/*
 * src/bin/pg_autoctl/httpd.c
 *	 HTTP server that published status and an API to use pg_auto_failover
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <fnmatch.h>
#include <inttypes.h>
#include <libgen.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "postgres_fe.h"
#include "pqexpbuffer.h"

#include "cli_root.h"
#include "defaults.h"
#include "fsm.h"
#include "httpd.h"
#include "keeper.h"
#include "keeper_listener.h"
#include "log.h"
#include "signals.h"
#include "state.h"

#define WBY_STATIC
#define WBY_IMPLEMENTATION
#define WBY_USE_FIXED_TYPES
#define WBY_USE_ASSERT
#define WBY_UINT_PTR size_t
#include "web.h"

#define MAX_WSCONN 8
#define MAX_URL_SCRIPT_SIZE 512
#define RE_MATCH_COUNT 10

/*
 * The HTTP server routing table associate an URL script (/api/1.0/status) to a
 * function that implements reading the input and writing the output.
 */
typedef bool (*HttpDispatchFunction)(struct wby_con *connection, void *userdata);

static bool http_home(struct wby_con *connection, void *userdata);
static bool http_version(struct wby_con *connection, void *userdata);
static bool http_api_version(struct wby_con *connection, void *userdata);
static bool http_state(struct wby_con *connection, void *userdata);
static bool http_fsm_state(struct wby_con *connection, void *userdata);
static bool http_fsm_assign(struct wby_con *connection, void *userdata);
static bool http_config_get(struct wby_con *connection, void *userdata);


typedef struct routing_table
{
	char script[MAX_URL_SCRIPT_SIZE];
	HttpDispatchFunction dispatchFunction;
} HttpRoutingTable;

/*
 * TODO: implement a different routing table depending on whether the monitor
 * is enabled (read-only + operations) or disabled (full control API).
 *
 * We can add the following operations to the API:
 *  /api/1.0/enable/maintenance
 *  /api/1.0/disable/maintenance
 *  /api/1.0/node/drop
 *  /api/1.0/config/get
 *  /api/1.0/config/set
 *  /api/1.0/config/reload
 *
 * We might also want to have a monitor specific API with
 *  /api/1.0/monitor/uri
 *  /api/1.0/monitor/events
 *  /api/1.0/monitor/state
 *  /api/1.0/formation/drop
 *  /api/1.0/formation/enable/secondary
 *  /api/1.0/formation/disable/secondary
 */
HttpRoutingTable KeeperRoutingTable[] = {
	{ "/",                       http_home },
	{ "/versions",               http_version },
	{ "/api/version",            http_api_version },
	{ "/api/1.0/state",          http_state },
	{ "/api/1.0/fsm/state",      http_fsm_state },
	{ "/api/1.0/fsm/assign/*",   http_fsm_assign },
	{ "/api/1.0/config/get/*",   http_config_get },
	{ "", NULL }
};


typedef struct server_state
{
    bool quit;
	char pgdata[MAXPGPATH];
} HttpServerState
;

static int httpd_dispatch(struct wby_con *connection, void *userdata);
static bool httpd_route_match_query(const char *uri,
									const char *script,
									char **matches,
									int matchesSize,
									int *matchesCount);

static void httpd_log(const char* text);

static bool keeper_fsm_as_json(KeeperConfig *config, char *buffer, int size);

/* we don't support websocket but still needs the API in place for web.h */
static int websocket_connect(struct wby_con *connection, void *userdata);
static void websocket_connected(struct wby_con *connection, void *userdata);
static int websocket_frame(struct wby_con *connection,
						   const struct wby_frame *frame, void *userdata);
static void websocket_closed(struct wby_con *connection, void *userdata);


/*
 * keeper_webservice_run forks and starts a web service in the child process,
 * to serve our HTTP based API to clients.
 */
bool
httpd_start_process(const char *pgdata,
					const char *listen_address, int port,
					pid_t *httpdPid)
{
	pid_t pid;
	int log_level = logLevel;

	/* Flush stdio channels just before fork, to avoid double-output problems */
	fflush(stdout);
	fflush(stderr);

	pid = fork();

	switch (pid)
	{
		case -1:
		{
			log_error("Failed to fork the HTTPd process");
			return false;
		}

		case 0:
		{
			/* fork succeeded, in child */

			/*
			 * We redirect /dev/null into stdin rather than closing stdin,
			 * because apparently closing it may cause undefined behavior if
			 * any read was to happen.
			 */
			int stdin = open(DEV_NULL, O_RDONLY);

			dup2(stdin, STDIN_FILENO);
			close(stdin);

			/* reset log level to same as the parent process */
			log_set_level(log_level);
			log_debug("set log level to %d/%d", log_level, logLevel);

			(void) httpd_start(pgdata, listen_address, port);

			/*
			 * When the "main" function for the child process is over, it's the
			 * end of our execution thread. Don't get back to the caller.
			 */
			if (asked_to_stop || asked_to_stop_fast)
			{
				exit(EXIT_CODE_QUIT);
			}
			else
			{
				log_error("BUG: keeper_listener_read_commands returned!");
				exit(EXIT_CODE_INTERNAL_ERROR);
			}
		}

		default:
		{
			/* fork succeeded, in parent */
			log_info("HTTP service started in subprocess %d", pid);
			*httpdPid = pid;
			return true;
		}
	}
}


/*
 * httpd_start starts our HTTP server.
 */
bool
httpd_start(const char *pgdata, const char *listen_address, int port)
{
	HttpServerState state = { 0 };
    void *memory = NULL;
    size_t needed_memory;
    struct wby_server server = { 0 };
    struct wby_config config = { 0 };

	state.quit = false;
	strlcpy(state.pgdata, pgdata, MAXPGPATH);

    config.userdata = &state;
    config.address = listen_address;
    config.port = port;
    config.connection_max = 4;
    config.request_buffer_size = 2048;
    config.io_buffer_size = 8192;
    config.log = httpd_log;
    config.dispatch = httpd_dispatch;

	/* we don't use them, but the lib seems to require those being set anyway */
    config.ws_connect = websocket_connect;
    config.ws_connected = websocket_connected;
    config.ws_frame = websocket_frame;
    config.ws_closed = websocket_closed;

    wby_init(&server, &config, &needed_memory);
    memory = calloc(needed_memory, 1);

    if( wby_start(&server, memory) == -1)
	{
		/*
		 * FIXME: edit ../lib/mmx/web.h to use log levels and proper error
		 * message when it should be user visible, e.g.:
		 *
		 *  bind() failed: Address already in use
		 */
		log_error("Failed to start HTTP API server");
		return false;
	}

	log_info(
		"HTTP server started at http://%s:%d/", config.address, config.port);

	while (!state.quit)
	{
        wby_update(&server);

		if (asked_to_stop || asked_to_stop_fast)
		{
			state.quit = true;
		}
	}

    wby_stop(&server);
    free(memory);

	return true;
}


/*
 * httpd_log logs output in DEBUG level
 */
static void
httpd_log(const char* text)
{
	log_trace("HTTP: %s", text);
}


/*
 * dispatch is called to set-up our HTTP server.
 */
static int
httpd_dispatch(struct wby_con *connection, void *userdata)
{
	int routingIndex = 0;
	HttpRoutingTable routingTableEntry = KeeperRoutingTable[0];

	while (routingTableEntry.dispatchFunction != NULL)
	{
		int matchesCount;
		char matches[RE_MATCH_COUNT][BUFSIZE] = { 0 };

		if (httpd_route_match_query(connection->request.uri,
									routingTableEntry.script,
									(char **)matches,
									RE_MATCH_COUNT,
									&matchesCount))
		{
			log_info("GET \"%s\"", routingTableEntry.script);
			return (*routingTableEntry.dispatchFunction)(connection, userdata);
		}

		routingTableEntry = KeeperRoutingTable[++routingIndex];
	}

	/* 404 */
	return 1;
}


/*
 * httpd_route_match_query matches a given URI from the request to one routing
 * table entry. When the query matches, we fill-in the given pre-allocated
 * matches array with the groups matched in the query.
 *
 * The first entry of the groups is going to be the whole URI itself.
 */
static bool
httpd_route_match_query(const char *uri,
						const char *pattern,
						char **matches,
						int matchesSize,
						int *matchesCount)
{
	return fnmatch(pattern, uri, FNM_PATHNAME) == 0;
}


/*
 * http_home is the dispatch function for /
 */
static bool
http_home(struct wby_con *connection, void *userdata)
{
	wby_response_begin(connection, 200, 14, NULL, 0);
	wby_write(connection, "Hello, world!\n", 14);
	wby_response_end(connection);

	return true;
}


/*
 * http_api_version is the dispatch function for /api/version
 */
static bool
http_api_version(struct wby_con *connection, void *userdata)
{
	char buffer[BUFSIZE];
	int len = snprintf(buffer, BUFSIZE, "%s", HTTPD_CURRENT_API_VERSION);

	wby_response_begin(connection, 200, len, NULL, 0);
	wby_write(connection, buffer, len);
	wby_response_end(connection);

	return true;
}


/*
 * http_home is the dispatch function for /state
 */
static bool
http_state(struct wby_con *connection, void *userdata)
{
	wby_response_begin(connection, 200, 3, NULL, 0);
	wby_write(connection, "Ok\n", 3);
	wby_response_end(connection);

	return true;
}

/*
 * http_version returns the current versions of pg_auto_failover CLI, API and
 * extension.
 */
static bool
http_version(struct wby_con *connection, void *userdata)
{
	char buffer[BUFSIZE];
	int len;

	wby_response_begin(connection, 200, -1, NULL, 0);

	len = snprintf(buffer, BUFSIZE, "pg_auto_failover %s\n",
				   PG_AUTOCTL_VERSION);
	wby_write(connection, buffer, len);

	len = snprintf(buffer, BUFSIZE, "pgautofailover extension %s\n",
				   PG_AUTOCTL_EXTENSION_VERSION);
	wby_write(connection, buffer, len);

	len = snprintf(buffer, BUFSIZE, "pg_auto_failover web API %s\n",
				   HTTPD_CURRENT_API_VERSION);
	wby_write(connection, buffer, len);

	wby_response_end(connection);

	return true;
}


/*
 * http_config_get is the dispatch function for /api/1.0/config/get
 */
static bool
http_config_get(struct wby_con *connection, void *userdata)
{
    HttpServerState *state = (HttpServerState *) userdata;

	char uri[BUFSIZE];
	char *paramName = NULL;
	char command[BUFSIZE];
	char output[BUFSIZE];

	/* use basename to retrieve the last part of the URI, on a copy of it */
	strlcpy(uri, connection->request.uri, BUFSIZE);
	paramName = basename(uri);

	snprintf(command, BUFSIZE, "config get %s", paramName);
	log_debug("http_config_get: %s", command);

	if (keeper_listener_send_command(command, output, BUFSIZE))
	{
		int size = strlen(output);

		wby_response_begin(connection, 200, size, NULL, 0);
		wby_write(connection, output, size);
		wby_response_end(connection);
	}
	else
	{
		wby_response_begin(connection, 404, 0, NULL, 0);
		wby_response_end(connection);
	}

	return true;
}


/*
 * http_keeper_state is the dispatch function for /1.0/fsm/state
 */
bool
http_fsm_state(struct wby_con *connection, void *userdata)
{
    HttpServerState *state = (HttpServerState *) userdata;

	char uri[BUFSIZE];
	char *paramName = NULL;
	char command[BUFSIZE];
	char output[BUFSIZE];

	/* use basename to retrieve the last part of the URI, on a copy of it */
	strlcpy(uri, connection->request.uri, BUFSIZE);
	paramName = basename(uri);

	snprintf(command, BUFSIZE, "do fsm state");
	log_debug("http_fsm_state: %s", command);

	if (keeper_listener_send_command(command, output, BUFSIZE))
	{
		int size = strlen(output);

		wby_response_begin(connection, 200, size, NULL, 0);
		wby_write(connection, output, size);
		wby_response_end(connection);
	}
	else
	{
		wby_response_begin(connection, 404, 0, NULL, 0);
		wby_response_end(connection);
	}

	return true;
}


/*
 * http_keeper_assign is the dispatch function for /1.0/fsm/assign
 */
static bool
http_fsm_assign(struct wby_con *connection, void *userdata)
{
    HttpServerState *state = (HttpServerState *) userdata;

	char uri[BUFSIZE];
	char *assignedStateString;
	NodeState goalState;

	Keeper keeper = { 0 };
	pgAutoCtlNodeRole nodeRole;
	KeeperConfig *config = &(keeper.config);
	KeeperStateData *keeperState = &(keeper.state);

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = true;

	char *goalStateString = NULL;
	char command[BUFSIZE];
	char output[BUFSIZE];

	/* use basename to retrieve the last part of the URI, on a copy of it */
	strlcpy(uri, connection->request.uri, BUFSIZE);
	goalStateString = basename(uri);

	snprintf(command, BUFSIZE, "do fsm assign %s", goalStateString);
	log_debug("http_fsm_assign: %s", command);

	if (keeper_listener_send_command(command, output, BUFSIZE))
	{
		int size = strlen(output);

		wby_response_begin(connection, 200, size, NULL, 0);
		wby_write(connection, output, size);
		wby_response_end(connection);
	}
	else
	{
		wby_response_begin(connection, 404, 0, NULL, 0);
		wby_response_end(connection);
	}

	return true;
}


/*
 * keeper_fsm_as_json reads the FSM state on-disk then returns a JSON formatted
 * version of it.
 *
 * The embedded webserver state keeps PGDATA only, so that we need to read the
 * config and the state from scratch at each call. We could implement this
 * another way but then would have to implement some kind of cache
 * invalidation.
 */
static bool
keeper_fsm_as_json(KeeperConfig *config, char *json, int size)
{
	PQExpBuffer buffer = NULL;

	Keeper keeper = { 0 };
	KeeperStateData *keeperState = &(keeper.state);

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = true;

	if (!keeper_config_read_file(config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged */
		return false;
	}

	if (!keeper_state_read(keeperState, config->pathnames.state))
	{
		snprintf(json, size, "Failed to read FSM state from \"%s\"",
				 config->pathnames.state);
		return false;
	}

	buffer = createPQExpBuffer();
	if (buffer == NULL)
	{
		snprintf(json, size, "Failed to allocate memory");
		return false;
	}

	appendPQExpBufferStr(buffer, "{\n");

	appendPQExpBufferStr(buffer, "\"postgres\": {");
	appendPQExpBuffer(buffer, "\"version\": %d,\n", keeperState->pg_version);
	appendPQExpBuffer(buffer, "\"pg_control_version\": %u,\n",
						 keeperState->pg_control_version);
	appendPQExpBuffer(buffer, "\"system_identifier\": %" PRIu64 "\n",
						 keeperState->system_identifier);
	appendPQExpBufferStr(buffer, "},\n");

	appendPQExpBufferStr(buffer, "\"fsm\": {\n");
	appendPQExpBuffer(buffer, "\"current_role\": \"%s\",\n",
					  NodeStateToString(keeperState->current_role));
	appendPQExpBuffer(buffer, "\"assigned_role\": \"%s\"\n",
					  NodeStateToString(keeperState->assigned_role));
	appendPQExpBufferStr(buffer, "},\n");

	appendPQExpBufferStr(buffer, "\"monitor\": {\n");
	appendPQExpBuffer(buffer, "\"current_node_id\": %d,\n",
					  keeperState->current_node_id);
	appendPQExpBuffer(buffer, "\"current_groupd\": %d\n",
					  keeperState->current_group);
	appendPQExpBufferStr(buffer, "}\n");

	appendPQExpBufferStr(buffer, "}\n");

	/* memory allocation could have failed while building string */
	if (PQExpBufferBroken(buffer))
	{
		snprintf(json, size,  "Failed to allocate memory");
		destroyPQExpBuffer(buffer);
		return false;
	}

	snprintf(json, size, "%s", buffer->data);
	destroyPQExpBuffer(buffer);

	return true;
}


/*
 * We don't support websockets at the moment.
 */
static int
websocket_connect(struct wby_con *connection, void *userdata)
{
	return 1;
}


static void
websocket_connected(struct wby_con *connection, void *userdata)
{
}


static int
websocket_frame(struct wby_con *connection,
				const struct wby_frame *frame, void *userdata)
{
	return 0;
}


static void
websocket_closed(struct wby_con *connection, void *userdata)
{
}
