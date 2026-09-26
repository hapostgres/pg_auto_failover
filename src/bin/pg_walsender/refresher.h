/*
 * src/bin/pg_walsender/refresher.h
 *   The node list REFRESHER: the single process that talks to the monitor
 *   and the single writer of "<route>/archiver-nodes.list", see
 *   monitor_hosts.h for the whole design and its PostgreSQL precedent.
 *   Forked (and supervised: restarted when it dies, terminated at
 *   shutdown) by the accept loop parent, which reaps it in its main loop
 *   like the postmaster does with its auxiliary processes.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_REFRESHER_H
#define WS_REFRESHER_H

#include "postgres_fe.h"

#define WS_REFRESH_SOCKET_FILE "archiver-refresh.sock"

/*
 * ws_refresh_socket_create creates and binds the AF_UNIX datagram socket
 * at path (mode 0600), returning its descriptor or -1. Called by the
 * parent before anything is forked.
 */
int ws_refresh_socket_create(const char *path);

/*
 * ws_refresher_main is the refresher process' body: serve datagrams from
 * sockFd (one route key each) until asked to stop or orphaned. Never
 * returns: _exit()s.
 */
void ws_refresher_main(int sockFd, const char *routesPath,
					   const char *monitorUriPath) __attribute__((noreturn));

#endif /* WS_REFRESHER_H */
