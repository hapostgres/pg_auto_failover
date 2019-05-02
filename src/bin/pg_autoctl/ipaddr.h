/*
 * src/bin/pg_autoctl/ipaddr.h
 *   Find local ip used as source ip in ip packets, using getsockname and a udp
 *   connection.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef __IPADDRH__
#define __IPADDRH__

#include <stdbool.h>

bool fetchLocalIPAddress(char *localIpAddress, int size);
bool fetchLocalCIDR(const char *localIpAddress, char *localCIDR, int size);

#endif /* __IPADDRH__ */
