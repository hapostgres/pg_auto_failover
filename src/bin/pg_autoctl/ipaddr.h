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


typedef enum
{
	IPTYPE_V4, IPTYPE_V6, IPTYPE_NONE
} IPType;


IPType ip_address_type(const char *hostname);
bool fetchLocalIPAddress(char *localIpAddress, int size,
						 const char *serviceName, int servicePort,
						 int logLevel, bool *mayRetry);
bool fetchLocalCIDR(const char *localIpAddress, char *localCIDR, int size);
bool findHostnameLocalAddress(const char *hostname,
							  char *localIpAddress, int size);
bool findHostnameFromLocalIpAddress(char *localIpAddress,
									char *hostname, int size);

bool resolveHostnameForwardAndReverse(const char *hostname,
									  char *ipaddr, int size,
									  bool *foundHostnameFromAddress);

bool ipaddrGetLocalHostname(char *hostname, size_t size);


#endif /* __IPADDRH__ */
