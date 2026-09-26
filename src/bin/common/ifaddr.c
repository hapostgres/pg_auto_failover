/*-------------------------------------------------------------------------
 *
 * ifaddr.c
 *	  IP netmask calculations, and enumerating network interfaces.
 *
 * Vendored from PostgreSQL's src/backend/libpq/ifaddr.c (master, identical
 * in the REL_16 .. REL_19 branches): logic unchanged, only the includes
 * adapted to a frontend build and the Windows and non-getifaddrs branches of
 * pg_foreach_ifaddr() dropped, as this tree is built on systems with
 * getifaddrs(3) only. The functions are pure C over sockaddr structures
 * (nothing backend-specific), which is why they can be lifted as they are.
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/libpq/ifaddr.c
 *
 * This file and the IPV6 implementation were initially provided by
 * Nigel Kukard <nkukard@lbsd.net>, Linux Based Systems Design
 * http://www.lbsd.net.
 *
 *-------------------------------------------------------------------------
 */

#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

#include "postgres_fe.h"

#include "ifaddr.h"
#include "port/pg_bswap.h"

static int range_sockaddr_AF_INET(const struct sockaddr_in *addr,
								  const struct sockaddr_in *netaddr,
								  const struct sockaddr_in *netmask);

static int range_sockaddr_AF_INET6(const struct sockaddr_in6 *addr,
								   const struct sockaddr_in6 *netaddr,
								   const struct sockaddr_in6 *netmask);


/*
 * pg_range_sockaddr - is addr within the subnet specified by netaddr/netmask ?
 *
 * Note: caller must already have verified that all three addresses are
 * in the same address family; and AF_UNIX addresses are not supported.
 */
int
pg_range_sockaddr(const struct sockaddr_storage *addr,
				  const struct sockaddr_storage *netaddr,
				  const struct sockaddr_storage *netmask)
{
	if (addr->ss_family == AF_INET)
	{
		return range_sockaddr_AF_INET((const struct sockaddr_in *) addr,
									  (const struct sockaddr_in *) netaddr,
									  (const struct sockaddr_in *) netmask);
	}
	else if (addr->ss_family == AF_INET6)
	{
		return range_sockaddr_AF_INET6((const struct sockaddr_in6 *) addr,
									   (const struct sockaddr_in6 *) netaddr,
									   (const struct sockaddr_in6 *) netmask);
	}
	else
	{
		return 0;
	}
}


static int
range_sockaddr_AF_INET(const struct sockaddr_in *addr,
					   const struct sockaddr_in *netaddr,
					   const struct sockaddr_in *netmask)
{
	if (((addr->sin_addr.s_addr ^ netaddr->sin_addr.s_addr) &
		 netmask->sin_addr.s_addr) == 0)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}


static int
range_sockaddr_AF_INET6(const struct sockaddr_in6 *addr,
						const struct sockaddr_in6 *netaddr,
						const struct sockaddr_in6 *netmask)
{
	int i;

	for (i = 0; i < 16; i++)
	{
		if (((addr->sin6_addr.s6_addr[i] ^ netaddr->sin6_addr.s6_addr[i]) &
			 netmask->sin6_addr.s6_addr[i]) != 0)
		{
			return 0;
		}
	}

	return 1;
}


/*
 *	pg_sockaddr_cidr_mask - make a network mask of the appropriate family
 *	  and required number of significant bits
 *
 * numbits can be null, in which case the mask is fully set.
 *
 * The resulting mask is placed in *mask, which had better be big enough.
 *
 * Return value is 0 if okay, -1 if not.
 */
int
pg_sockaddr_cidr_mask(struct sockaddr_storage *mask, char *numbits, int family)
{
	long bits;
	char *endptr;

	if (numbits == NULL)
	{
		bits = (family == AF_INET) ? 32 : 128;
	}
	else
	{
		bits = strtol(numbits, &endptr, 10);
		if (*numbits == '\0' || *endptr != '\0')
		{
			return -1;
		}
	}

	switch (family)
	{
		case AF_INET:
		{
			struct sockaddr_in mask4;
			long maskl;

			if (bits < 0 || bits > 32)
			{
				return -1;
			}
			memset(&mask4, 0, sizeof(mask4));

			/* avoid "x << 32", which is not portable */
			if (bits > 0)
			{
				maskl = (0xffffffffUL << (32 - (int) bits)) &
						0xffffffffUL;
			}
			else
			{
				maskl = 0;
			}
			mask4.sin_addr.s_addr = pg_hton32(maskl);
			memcpy(mask, &mask4, sizeof(mask4));     /* IGNORE-BANNED */
			break;
		}

		case AF_INET6:
		{
			struct sockaddr_in6 mask6;
			int i;

			if (bits < 0 || bits > 128)
			{
				return -1;
			}
			memset(&mask6, 0, sizeof(mask6));
			for (i = 0; i < 16; i++)
			{
				if (bits <= 0)
				{
					mask6.sin6_addr.s6_addr[i] = 0;
				}
				else if (bits >= 8)
				{
					mask6.sin6_addr.s6_addr[i] = 0xff;
				}
				else
				{
					mask6.sin6_addr.s6_addr[i] =
						(0xff << (8 - (int) bits)) & 0xff;
				}
				bits -= 8;
			}
			memcpy(mask, &mask6, sizeof(mask6));     /* IGNORE-BANNED */
			break;
		}

		default:
			return -1;
	}

	mask->ss_family = family;
	return 0;
}


/*
 * Run the callback function for the addr/mask, after making sure the
 * mask is sane for the addr.
 */
static void
run_ifaddr_callback(PgIfAddrCallback callback, void *cb_data,
					struct sockaddr *addr, struct sockaddr *mask)
{
	struct sockaddr_storage fullmask;

	if (!addr)
	{
		return;
	}

	/* Check that the mask is valid */
	if (mask)
	{
		if (mask->sa_family != addr->sa_family)
		{
			mask = NULL;
		}
		else if (mask->sa_family == AF_INET)
		{
			if (((struct sockaddr_in *) mask)->sin_addr.s_addr == INADDR_ANY)
			{
				mask = NULL;
			}
		}
		else if (mask->sa_family == AF_INET6)
		{
			if (IN6_IS_ADDR_UNSPECIFIED(&((struct sockaddr_in6 *) mask)->sin6_addr))
			{
				mask = NULL;
			}
		}
	}

	/* If mask is invalid, generate our own fully-set mask */
	if (!mask)
	{
		pg_sockaddr_cidr_mask(&fullmask, NULL, addr->sa_family);
		mask = (struct sockaddr *) &fullmask;
	}

	(*callback)(addr, mask, cb_data);
}


#include <ifaddrs.h>

/*
 * Enumerate the system's network interface addresses and call the callback
 * for each one.  Returns 0 if successful, -1 if trouble.
 *
 * This version uses the getifaddrs() interface, which is available on
 * BSDs, macOS, Solaris, illumos and Linux.
 */
int
pg_foreach_ifaddr(PgIfAddrCallback callback, void *cb_data)
{
	struct ifaddrs *ifa,
				   *l;

	if (getifaddrs(&ifa) < 0)
	{
		return -1;
	}

	for (l = ifa; l; l = l->ifa_next)
	{
		run_ifaddr_callback(callback, cb_data,
							l->ifa_addr, l->ifa_netmask);
	}

	freeifaddrs(ifa);
	return 0;
}
