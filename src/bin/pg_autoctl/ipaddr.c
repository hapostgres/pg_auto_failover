/*
 * src/bin/pg_autoctl/ipaddr.c
 *   Find local ip used as source ip in ip packets, using getsockname and a udp
 *   connection.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "ipaddr.h"
#include "log.h"

static unsigned int countSetBits(unsigned int n);
static unsigned int countSetBitsv6(unsigned char *addr);
static bool ipv4eq(struct sockaddr_in *a, struct sockaddr_in *b);
static bool ipv6eq(struct sockaddr_in6 *a, struct sockaddr_in6 *b);
static bool fetchIPAddressFromInterfaceList(char *localIpAddress, int size);
static bool isTestEnv(void);

/*
 * Connect in UDP to a known DNS server on the external network, and grab our
 * local IP address from the established socket.
 */
bool
fetchLocalIPAddress(char *localIpAddress, int size,
					const char *serviceName, int servicePort)
{
    char buffer[INET_ADDRSTRLEN];
	const char *ipAddr;
    struct sockaddr_in name;
    socklen_t namelen = sizeof(name);
    int err = -1;

    struct sockaddr_in serv;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    //Socket could not be created
    if (sock < 0)
    {
        log_warn("Failed to create a socket: %s", strerror(errno));
        return false;
    }

    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr(serviceName);
    serv.sin_port = htons(servicePort);

    err = connect(sock, (const struct sockaddr*) &serv, sizeof(serv));
    if (err < 0)
    {
		if (isTestEnv())
		{
			/*
			 * In test environment, in case of no internet access, just use the
			 * address of the non-loopback network interface.
			 */
			return fetchIPAddressFromInterfaceList(localIpAddress, size);
		}
		else
		{
			log_warn("Failed to connect: %s", strerror(errno));
			return false;
		}
    }

    err = getsockname(sock, (struct sockaddr*) &name, &namelen);
    if (err < 0)
    {
        log_warn("Failed to get IP address from socket: %s", strerror(errno));
        return false;
    }

    ipAddr = inet_ntop(AF_INET, &name.sin_addr, buffer, INET_ADDRSTRLEN);

    if (ipAddr != NULL)
    {
        snprintf(localIpAddress, size, "%s", buffer);
    }
    else
    {
		log_warn("Failed to determine local ip address: %s", strerror(errno));
    }
    close(sock);

    return ipAddr != NULL;
}


/*
 * fetchLocalCIDR loops over the local interfaces on the host and finds the one
 * for which the IP address is the same as the given localIpAddress parameter.
 * Then using the netmask information from the network interface,
 * fetchLocalCIDR computes the local CIDR to use in HBA in order to allow
 * authentication of all servers in the local network.
 */
bool
fetchLocalCIDR(const char *localIpAddress, char *localCIDR, int size)
{
	char network[INET6_ADDRSTRLEN];
	struct ifaddrs *ifaddr, *ifa;
	int prefix = 0;
	bool found = false;

	if (getifaddrs(&ifaddr) == -1)
	{
		log_warn("Failed to get the list of local network inferfaces: %s",
				  strerror(errno));
		return false;
	}

    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next)
    {
		char netmask[INET6_ADDRSTRLEN] = { 0 };
		char address[INET6_ADDRSTRLEN] = { 0 };

		/*
		 * Some interfaces might have an empty ifa_addr, such as when using the
		 * PPTP protocol. With a NULL ifa_addr we can't inquire about the IP
		 * address and its netmask to compute any CIDR notation, so we skip the
		 * entry.
		 */
		if (ifa->ifa_addr == NULL)
		{
			log_debug("Skipping interface \"%s\" with NULL ifa_addr",
					  ifa->ifa_name);
			continue;
		}

		switch (ifa->ifa_addr->sa_family)
		{
			case AF_INET:
			{
				struct sockaddr_in *netmask4
					= (struct sockaddr_in *) ifa->ifa_netmask;
				struct sockaddr_in *address4
					= (struct sockaddr_in*) ifa->ifa_addr;

				struct in_addr s_network;

				if (inet_ntop(AF_INET, (void*) &netmask4->sin_addr,
							  netmask, INET_ADDRSTRLEN) == NULL)
				{
					/* just skip that entry then */
					log_trace("Failed to determine local network CIDR: %s",
							  strerror(errno));
					continue;
				}

				if (inet_ntop(AF_INET, (void*) &address4->sin_addr,
							  address, INET_ADDRSTRLEN) == NULL)
				{
					/* just skip that entry then */
					log_trace("Failed to determine local network CIDR: %s",
							  strerror(errno));
					continue;
				}


				s_network.s_addr =
					address4->sin_addr.s_addr & netmask4->sin_addr.s_addr;

				prefix = countSetBits(netmask4->sin_addr.s_addr);

				if (inet_ntop(AF_INET, (void*) &s_network,
							  network, INET_ADDRSTRLEN) == NULL)
				{
					/* just skip that entry then */
					log_trace("Failed to determine local network CIDR: %s",
							  strerror(errno));
					continue;
				}

				break;
			}

			case AF_INET6:
			{
				int i = 0;
				struct sockaddr_in6 *netmask6
					= (struct sockaddr_in6 *) ifa->ifa_netmask;
				struct sockaddr_in6 *address6
					= (struct sockaddr_in6 *) ifa->ifa_addr;

				struct in6_addr s_network;

				if (inet_ntop(AF_INET6, (void*) &netmask6->sin6_addr,
							  netmask, INET6_ADDRSTRLEN) == NULL)
				{
					/* just skip that entry then */
					log_trace("Failed to determine local network CIDR: %s",
							  strerror(errno));
					continue;
				}

				if (inet_ntop(AF_INET6, (void*) &address6->sin6_addr,
							  address, INET6_ADDRSTRLEN) == NULL)
				{
					/* just skip that entry then */
					log_trace("Failed to determine local network CIDR: %s",
							  strerror(errno));
					continue;
				}

				for (i = 0; i < sizeof(struct in6_addr); i++)
				{
					s_network.s6_addr[i] =
						address6->sin6_addr.s6_addr[i]
						& netmask6->sin6_addr.s6_addr[i];
				}

				prefix = countSetBitsv6(netmask6->sin6_addr.s6_addr);

				if (inet_ntop(AF_INET6, &s_network,
							  network, INET6_ADDRSTRLEN) == NULL)
				{
					/* just skip that entry then */
					log_trace("Failed to determine local network CIDR: %s",
							  strerror(errno));
					continue;
				}

				break;
			}

			default:
				continue;
		}

		if (strcmp(address, localIpAddress) == 0)
		{
			found = true;
			break;
		}
	}
	freeifaddrs(ifaddr);

	if (!found)
	{
		return false;
	}

	snprintf(localCIDR, size, "%s/%d", network, prefix);

	return true;
}


/*
 * countSetBits return how many bits are set (to 1) in an integer. When given a
 * netmask, that's the CIDR prefix.
 */
static unsigned int
countSetBits(unsigned int n)
{
	unsigned int count = 0;

	while (n)
	{
		count += n & 1;
		n >>= 1;
	}

	return count;
}


/*
 * countSetBitsv6 returns how many bits are set (to 1) in an IPv6 address, an
 * array of 16 unsigned char values. When given a netmask, that's the
 * prefixlen.
 */
static unsigned int
countSetBitsv6(unsigned char *addr)
{
	int i = 0;
	unsigned int count = 0;

	for(i=0; i<16; i++)
	{
		unsigned char n = addr[i];

		while (n)
		{
			count += n & 1;
			n >>= 1;
		}
	}

	return count;
}


/*
 * Fetches the IP address of the first non-loopback interface with an ip4
 * address.
 */
static bool
fetchIPAddressFromInterfaceList(char *localIpAddress, int size)
{
	bool found = false;
	struct ifaddrs *ifaddrList = NULL, *ifaddr = NULL;

	if (getifaddrs(&ifaddr) == -1)
	{
		log_error("Failed to get the list of local network inferfaces: %s",
				  strerror(errno));
		return false;
	}

	for (ifaddr = ifaddrList; ifaddr != NULL; ifaddr = ifaddr->ifa_next)
 	{
		if (ifaddr->ifa_flags & IFF_LOOPBACK)
		{
			log_trace("Skipping loopback interface \"%s\"", ifaddr->ifa_name);
			continue;
		}

		/*
		 * Some interfaces might have an empty ifa_addr, such as when using the
		 * PPTP protocol. With a NULL ifa_addr we can't inquire about the IP
		 * address and its netmask to compute any CIDR notation, so we skip the
		 * entry.
		 */
		if (ifaddr->ifa_addr == NULL)
		{
			log_debug("Skipping interface \"%s\" with NULL ifa_addr",
					  ifaddr->ifa_name);
			continue;
		}

		/*
		 * We only support IPv4 here, also this function is only called in test
		 * environment where we run in a docker container with a network
		 * namespace in which we use only IPv4, so that's ok.
		 */
		if (ifaddr->ifa_addr->sa_family == AF_INET)
		{
			struct sockaddr_in *ip =
				(struct sockaddr_in*) ifaddr->ifa_addr;

			if (inet_ntop(AF_INET, (void*) &(ip->sin_addr),
						  localIpAddress, size) == NULL)
			{
				/* skip that address, silently */
				log_trace("Failed to determine local network CIDR: %s",
						  strerror(errno));
				continue;
			}

			found = true;
			break;
		}
	}

	freeifaddrs(ifaddrList);

	return found;
}


/*
 * Returns whether we are running inside the test environment.
 */
static bool
isTestEnv(void)
{
	const char *socketDir = getenv("PG_REGRESS_SOCK_DIR");
	return socketDir != NULL && socketDir[0] == '\0';
}


/*
 * From /Users/dim/dev/PostgreSQL/postgresql/src/backend/libpq/hba.c
 */
static bool
ipv4eq(struct sockaddr_in *a, struct sockaddr_in *b)
{
	return (a->sin_addr.s_addr == b->sin_addr.s_addr);
}


/*
 * From /Users/dim/dev/PostgreSQL/postgresql/src/backend/libpq/hba.c
 */
static bool
ipv6eq(struct sockaddr_in6 *a, struct sockaddr_in6 *b)
{
	int			i;

	for (i = 0; i < 16; i++)
	{
		if (a->sin6_addr.s6_addr[i] != b->sin6_addr.s6_addr[i])
		{
			return false;
		}
	}

	return true;
}


/*
 * findHostnameLocalAddress does a reverse DNS lookup given a hostname
 * (--nodename), and if the DNS lookup fails or doesn't return any local IP
 * address, then returns false.
 */
bool
findHostnameLocalAddress(const char *hostname, char *localIpAddress, int size)
{
	int error;
	struct addrinfo *dns_lookup_addr;
	struct addrinfo *dns_addr;
	struct ifaddrs *ifaddrList, *ifaddr;

	error = getaddrinfo(hostname, NULL, 0, &dns_lookup_addr);
	if (error != 0)
	{
		log_warn("Failed to resolve DNS name \"%s\": %s",
				 hostname, gai_strerror(error));
		return false;
	}

	/*
	 * Loop over DNS results for the given hostname. Filter out loopback
	 * devices, and for each IP address given by the look-up, check if we
	 * have a corresponding local interface bound to the IP address.
	 */
	if (getifaddrs(&ifaddrList) == -1)
	{
		log_warn("Failed to get the list of local network inferfaces: %s",
				 strerror(errno));
		return false;
	}

	/*
	 * Compare both addresses list (dns lookup and list of interface
	 * addresses) in a nested loop fashion: lists are not sorted, and we
	 * expect something like a dozen entry per list anyway.
	 */
	for (dns_addr = dns_lookup_addr;
		 dns_addr != NULL;
		 dns_addr = dns_addr->ai_next)
	{
		for (ifaddr = ifaddrList; ifaddr != NULL; ifaddr = ifaddr->ifa_next)
		{
			/*
			 * Some interfaces might have an empty ifa_addr, such as when using
			 * the PPTP protocol. With a NULL ifa_addr we can't inquire about
			 * the IP address and its netmask to compute any CIDR notation, so
			 * we skip the entry.
			 */
			if (ifaddr->ifa_addr == NULL)
			{
				log_debug("Skipping interface \"%s\" with NULL ifa_addr",
						  ifaddr->ifa_name);
				continue;
			}

			if (ifaddr->ifa_addr->sa_family == AF_INET
				&& dns_addr->ai_family == AF_INET)
			{
				struct sockaddr_in *ip =
					(struct sockaddr_in *) ifaddr->ifa_addr;

				if (ipv4eq(ip, (struct sockaddr_in *) dns_addr->ai_addr))
				{
					/*
					 * Found an IP address in the DNS answer that
					 * matches one of the interfaces IP addresses on
					 * the machine.
					 */
					freeaddrinfo(dns_lookup_addr);

					if (inet_ntop(AF_INET,
								  (void *) &(ip->sin_addr),
								  localIpAddress,
								  size) == NULL)
					{
						log_warn("Failed to determine local ip address: %s",
								 strerror(errno));
						return false;
					}

					return true;
				}
			}
			else if (ifaddr->ifa_addr->sa_family == AF_INET6
					 && dns_addr->ai_family == AF_INET6)
			{
				struct sockaddr_in6 *ip =
					(struct sockaddr_in6 *) ifaddr->ifa_addr;

				if (ipv6eq(ip, (struct sockaddr_in6 *) dns_addr->ai_addr))
				{
					/*
					 * Found an IP address in the DNS answer that
					 * matches one of the interfaces IP addresses on
					 * the machine.
					 */
					freeaddrinfo(dns_lookup_addr);

					if (inet_ntop(AF_INET6,
								  (void *) &(ip->sin6_addr),
								  localIpAddress,
								  size) == NULL)
					{
						/* check size >= INET6_ADDRSTRLEN */
						log_warn("Failed to determine local ip address: %s",
								 strerror(errno));
						return false;
					}

					return true;
				}
			}
		}
	}

	freeaddrinfo(dns_lookup_addr);
	return false;
}


/*
 * ip_address_type parses the hostname and determines whether it is an IPv4
 * address, IPv6 address, or DNS name.
 *
 * To edit pg HBA file, when given an IP address (rather than a hostname), we
 * need to compute the CIDR mask. In the case of ipv4, that's /32, in the case
 * of ipv6, that's /128. The `ip_address_type' function discovers which type of
 * IP address we are dealing with.
 */
IPType
ip_address_type(const char *hostname)
{
	struct in_addr ipv4;
	struct in6_addr ipv6;

	if (hostname == NULL)
	{
		return IPTYPE_NONE;
	}
	else if (inet_pton(AF_INET, hostname, &ipv4) == 1)
	{
		log_trace("hostname \"%s\" is ipv4", hostname);
		return IPTYPE_V4;
	}
	else if (inet_pton(AF_INET6, hostname, &ipv6) == 1)
	{
		log_trace("hostname \"%s\" is ipv6", hostname);
		return IPTYPE_V6;
	}
	return IPTYPE_NONE;
}


/*
 * findHostnameFromLocalIpAddress does a reverse DNS lookup from a given IP
 * address, and returns the first hostname of the DNS response.
 */
bool
findHostnameFromLocalIpAddress(char *localIpAddress, char *hostname, int size)
{
	int ret = 0;
	char hbuf[NI_MAXHOST];
	struct addrinfo *lookup, *ai;

	/* parse ipv4 or ipv6 address using getaddrinfo() */
	ret = getaddrinfo(localIpAddress, NULL, 0, &lookup);
	if (ret != 0)
	{
		log_warn("Failed to resolve DNS name \"%s\": %s",
				 localIpAddress, gai_strerror(ret));
		return false;
	}

	/* now reverse lookup (NI_NAMEREQD) the address with getnameinfo() */
	for (ai = lookup; ai; ai = ai->ai_next)
	{
		ret = getnameinfo(ai->ai_addr, ai->ai_addrlen,
						  hbuf, sizeof(hbuf), NULL, 0, NI_NAMEREQD);

		if (ret != 0)
		{
			log_warn("Failed to resolve hostname from address \"%s\": %s",
					  localIpAddress, gai_strerror(ret));
			return false;
		}

		snprintf(hostname, size, "%s", hbuf);

		/* stop at the first hostname found */
		break;
	}
	freeaddrinfo(lookup);

	return true;
}
