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
static bool fetchIPAddressFromInterfaceList(char *localIpAddress, int size);
static bool isTestEnv(void);

/*
 * Connect in UDP to a known DNS server on the external network, and grab our
 * local IP address from the established socket.
 */
bool
fetchLocalIPAddress(char *localIpAddress, int size)
{
    char buffer[INET_ADDRSTRLEN];
	const char *ipAddr;
    struct sockaddr_in name;
    socklen_t namelen = sizeof(name);
    const char* google_dns_server = "8.8.8.8";
    int dns_port = 53, err = -1;

    struct sockaddr_in serv;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    //Socket could not be created
    if (sock < 0)
    {
        log_error("Failed to create a socket: %s", strerror(errno));
        return false;
    }

    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr(google_dns_server);
    serv.sin_port = htons(dns_port);

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
			log_error("Failed to connect: %s", strerror(errno));
			return false;
		}
    }

    err = getsockname(sock, (struct sockaddr*) &name, &namelen);
    if (err < 0)
    {
        log_error("Failed to get IP address from socket: %s", strerror(errno));
        return false;
    }

    ipAddr = inet_ntop(AF_INET, &name.sin_addr, buffer, INET_ADDRSTRLEN);

    if (ipAddr != NULL)
    {
        snprintf(localIpAddress, size, "%s", buffer);
    }
    else
    {
		log_error("Failed to determine local ip address: %s", strerror(errno));
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
	char network[INET_ADDRSTRLEN];
	struct ifaddrs *ifaddr, *ifa;
	int prefix = 0;

	if (getifaddrs(&ifaddr) == -1)
	{
		log_error("Failed to get the list of local network inferfaces: %s",
				  strerror(errno));
		return false;
	}

    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next)
    {
		char netmask[INET_ADDRSTRLEN];
		char address[INET_ADDRSTRLEN];

		switch (ifa->ifa_addr->sa_family)
		{
			/*
			 * TODO: implement ipv6 support (case AF_INET6).
			 */
			case AF_INET:
			{
				struct sockaddr_in *netmask4
					= (struct sockaddr_in *) ifa->ifa_netmask;
				struct sockaddr_in *address4
					= (struct sockaddr_in*) ifa->ifa_addr;

				struct in_addr s_network;

				inet_ntop(AF_INET,
						  (void*) &netmask4->sin_addr, netmask, INET_ADDRSTRLEN);

				inet_ntop(AF_INET,
						  (void*) &address4->sin_addr, address, INET_ADDRSTRLEN);

				s_network.s_addr =
					address4->sin_addr.s_addr & netmask4->sin_addr.s_addr;

				prefix = countSetBits(netmask4->sin_addr.s_addr);

				inet_ntop(AF_INET, (void*) &s_network, network, INET_ADDRSTRLEN);

				break;
			}

			default:
				continue;
		}

		if (strcmp(address, localIpAddress) != 0)
		{
			continue;
		}
		break;
    }
	freeifaddrs(ifaddr);

	if (prefix == 0)
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
 * Fetches the IP address of the first non-loopback interface with an ip4 address.
 */
static bool
fetchIPAddressFromInterfaceList(char *localIpAddress, int size)
{
	bool found = false;
	struct ifaddrs *ifaddr = NULL, *ifa = NULL;

	if (getifaddrs(&ifaddr) == -1)
	{
		log_error("Failed to get the list of local network inferfaces: %s",
				  strerror(errno));
		return false;
	}

	for (ifa = ifaddr; ifa; ifa = ifa->ifa_next)
 	{
		if (ifa->ifa_addr->sa_family == AF_INET && !(ifa->ifa_flags & IFF_LOOPBACK))
		{
			char buffer[INET_ADDRSTRLEN];
			struct sockaddr_in *address4 = (struct sockaddr_in*) ifa->ifa_addr;
			inet_ntop(AF_INET, (void*) &address4->sin_addr, buffer, INET_ADDRSTRLEN);
			strlcpy(localIpAddress, buffer, size);
			found = true;
			break;
		}
	}

	freeifaddrs(ifaddr);

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
