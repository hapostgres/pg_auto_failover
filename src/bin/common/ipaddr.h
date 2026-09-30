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

/*
 * fetchLocalIPAddressForRouting is fetchLocalIPAddress()'s own UDP
 * counterpart -- see ipaddr.c's own comment. Prefer this one whenever
 * the caller only wants to know "what's my own local IP for reaching
 * serviceName", not whether serviceName is actually reachable right
 * now: it needs a route to exist, never a real connection.
 */
bool fetchLocalIPAddressForRouting(char *localIpAddress, int size,
								   const char *serviceName, int servicePort,
								   int logLevel);

bool fetchLocalCIDR(const char *localIpAddress, char *localCIDR, int size);
bool findHostnameLocalAddress(const char *hostname,
							  char *localIpAddress, int size);
bool findHostnameFromLocalIpAddress(char *localIpAddress,
									char *hostname, int size);

bool resolveHostnameForwardAndReverse(const char *hostname,
									  char *ipaddr, int size,
									  bool *foundHostnameFromAddress);

bool ipaddrGetLocalHostname(char *hostname, size_t size);

/*
 * Peer address matching, used to authenticate a client by its address (see
 * pg_walserver's HBA file): all of them take the client's numeric address.
 */
bool ipaddrHostMatchesAddress(const char *hostOrIp, const char *ipaddr);
bool ipaddrInCIDR(const char *cidr, const char *ipaddr);
bool ipaddrIsSameHostOrNet(const char *ipaddr, bool sameNet);

#define IPADDR_MAX_HOSTNAMES 16
#define IPADDR_MAX_HOSTNAME_SIZE 256

int ipaddrFindHostnamesFromAddress(const char *ipaddr,
								   char hostnames[][IPADDR_MAX_HOSTNAME_SIZE],
								   int maxCount);


#endif /* __IPADDRH__ */
