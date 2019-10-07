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

#include "httpd.h"
#include "parson.h"

#include "cli_root.h"
#include "defaults.h"
#include "fsm.h"
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
	char method[MAX_URL_SCRIPT_SIZE];
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
	{ "GET",  "/",                       http_home },
	{ "GET",  "/versions",               http_version },
	{ "GET",  "/api/version",            http_api_version },
	{ "GET",  "/api/1.0/state",          http_state },
	{ "GET",  "/api/1.0/fsm/state",      http_fsm_state },
	{ "GET",  "/api/1.0/fsm/assign/*",   http_fsm_state },
	{ "POST", "/api/1.0/fsm/assign/*",   http_fsm_assign },
	{ "GET",  "/api/1.0/config/get/*",   http_config_get },
	{ "", "", NULL }
};


typedef struct server_state
{
    bool quit;
	char pgdata[MAXPGPATH];
} HttpServerState
;

static int httpd_dispatch(struct wby_con *connection, void *userdata);
static bool httpd_route_match_query(struct wby_con *connection,
									HttpRoutingTable routingTableEntry);
static void httpd_log(const char* text);

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
		if (httpd_route_match_query(connection, routingTableEntry))
		{
			log_info("%s \"%s\"",
					 routingTableEntry.method,
					 routingTableEntry.script);

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
httpd_route_match_query(struct wby_con *connection,
						HttpRoutingTable routingTableEntry)
{
	const char *uri = connection->request.uri;

	/* first, HTTP method must match */
	if (strcmp(connection->request.method, routingTableEntry.method) != 0)
	{
		return false;
	}

	/* then, match connection script to our routing pattern */
	return fnmatch(routingTableEntry.script, uri, FNM_PATHNAME) == 0;
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

    JSON_Value *js = json_value_init_object();
    JSON_Object *root = json_value_get_object(js);
    char *serialized_string = NULL;

    json_object_dotset_string(
		root, "version.pg_auto_failover", PG_AUTOCTL_VERSION);

    json_object_dotset_string(
		root, "version.pgautofailover", PG_AUTOCTL_EXTENSION_VERSION);

    json_object_dotset_string(
		root, "version.api", HTTPD_CURRENT_API_VERSION);

    serialized_string = json_serialize_to_string_pretty(js);
	len = strlen(serialized_string);

	wby_response_begin(connection, 200, len, NULL, 0);
	wby_write(connection, serialized_string, len);
	wby_response_end(connection);

    json_free_serialized_string(serialized_string);
    json_value_free(js);

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
