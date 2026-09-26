/*
 * src/bin/common/ifaddr.h
 *   IP netmask calculations and network interface enumeration, vendored
 *   from PostgreSQL's src/include/libpq/ifaddr.h (see ifaddr.c).
 *
 * Copyright (c) 2003-2026, PostgreSQL Global Development Group
 */

#ifndef PGAF_IFADDR_H
#define PGAF_IFADDR_H

#include <sys/socket.h>

typedef void (*PgIfAddrCallback) (struct sockaddr *addr,
								  struct sockaddr *netmask,
								  void *cb_data);

extern int pg_range_sockaddr(const struct sockaddr_storage *addr,
							 const struct sockaddr_storage *netaddr,
							 const struct sockaddr_storage *netmask);

extern int pg_sockaddr_cidr_mask(struct sockaddr_storage *mask,
								 char *numbits, int family);

extern int pg_foreach_ifaddr(PgIfAddrCallback callback, void *cb_data);

#endif /* PGAF_IFADDR_H */
