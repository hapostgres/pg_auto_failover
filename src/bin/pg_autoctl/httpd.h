/*
 * src/bin/pg_autoctl/httpd.h
 *	 HTTP server that published status and an API to use pg_auto_failover
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef KEEPER_HTTPD_H
#define KEEPER_HTTPD_H

#include <stdbool.h>

#if defined(WIN32) && !defined(__CYGWIN__)
#define DEV_NULL "NUL"
#else
#define DEV_NULL "/dev/null"
#endif

typedef enum
{
	HTTP_API_MONTOR = 0,
	HTTP_API_KEEPER_NO_MONITOR,
	HTTP_API_KEEPER_WITH_MONITOR,
} HTTPApi;

bool httpd_start_process(const char *pgdata,
						 const char *listen_address, int port,
						 HTTPApi routingApi, pid_t *httpdPid);
bool httpd_start(const char *pgdata, const char *listen_address, int port,
				 HTTPApi routingApi);

#endif	/* KEEPER_HTTPD_H */
