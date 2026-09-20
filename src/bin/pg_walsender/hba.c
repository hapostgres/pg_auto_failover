/*
 * src/bin/pg_walsender/hba.c
 *   pg_walsender's host-based authentication file, see hba.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "postgres_fe.h"

#include "hba.h"

#include "defaults.h"
#include "file_utils.h"
#include "log.h"
#include "string_utils.h"

#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))

#define HBA_MAX_FIELDS 5


static const char *defaultHbaContents =
	"# pg_walsender host-based authentication, read on every connection.\n"
	"# The first matching line wins; no match (or an unreadable file) rejects.\n"
	"#\n"
	"# TYPE  ROUTE  USER  ADDRESS  METHOD\n"
	"#\n"
	"# ROUTE    all, or <formation>/<group>\n"
	"# USER     all, or a role name\n"
	"# ADDRESS  all, monitor, an IP address, IP/prefix, or a hostname;\n"
	"#          \"monitor\" is every node the monitor lists for the route\n"
	"# METHOD   trust, scram-sha-256 (see archiver-passwd), or reject\n"
	"#\n"
	"# Nodes registered with the monitor (standbys and their pg_basebackup,\n"
	"# streaming and restore_command connections):\n"
	"host  all  " PG_AUTOCTL_REPLICA_USERNAME "  monitor  trust\n"
	"#\n"
	"# A host the monitor does not know about, such as a PITR restore target,\n"
	"# needs a line of its own, for instance:\n"
	"# host  default/0  " PG_AUTOCTL_REPLICA_USERNAME "  192.0.2.0/24  scram-sha-256\n";


bool
hba_write_default_if_missing(const char *hbaPath)
{
	if (file_exists(hbaPath))
	{
		return true;
	}

	log_info("Creating the default pg_walsender HBA file \"%s\"", hbaPath);

	return write_file_atomic((char *) defaultHbaContents,
							 (long) strlen(defaultHbaContents), hbaPath);
}


/*
 * hba_host_matches_peer: literal numeric address match first, then resolve
 * the name and compare each resulting numeric address.
 */
bool
hba_host_matches_peer(const char *hostOrIp, const char *peerIP)
{
	if (streq(hostOrIp, peerIP))
	{
		return true;
	}

	struct addrinfo hints;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;

	struct addrinfo *res = NULL;

	if (getaddrinfo(hostOrIp, NULL, &hints, &res) != 0)
	{
		return false;
	}

	bool found = false;

	for (struct addrinfo *rp = res; rp != NULL && !found; rp = rp->ai_next)
	{
		char resolved[NI_MAXHOST];

		found = getnameinfo(rp->ai_addr, rp->ai_addrlen, resolved,
							sizeof(resolved), NULL, 0, NI_NUMERICHOST) == 0 &&
				streq(resolved, peerIP);
	}

	freeaddrinfo(res);

	return found;
}


/*
 * cidr_matches: does peerIP fall in "addr/prefix"? IPv4 and IPv6, and an
 * IPv4-mapped IPv6 peer is compared as the IPv4 address it carries.
 */
static bool
cidr_matches(const char *cidr, const char *peerIP)
{
	char network[INET6_ADDRSTRLEN + 8];

	strlcpy(network, cidr, sizeof(network));

	char *slash = strchr(network, '/');

	if (slash == NULL)
	{
		return false;
	}

	*slash = '\0';

	int prefix = 0;

	if (!stringToInt(slash + 1, &prefix) || prefix < 0)
	{
		return false;
	}

	unsigned char net[16], peer[16];
	int family = strchr(network, ':') != NULL ? AF_INET6 : AF_INET;
	int addrLen = family == AF_INET6 ? 16 : 4;

	if (prefix > addrLen * 8 || inet_pton(family, network, net) != 1)
	{
		return false;
	}

	if (inet_pton(family, peerIP, peer) != 1)
	{
		/* an IPv4-mapped IPv6 peer against an IPv4 network */
		const char *mapped = strncmp(peerIP, "::ffff:", 7) == 0 ? peerIP + 7 : NULL;

		if (family != AF_INET || mapped == NULL ||
			inet_pton(AF_INET, mapped, peer) != 1)
		{
			return false;
		}
	}

	int fullBytes = prefix / 8;
	int restBits = prefix % 8;

	if (memcmp(net, peer, fullBytes) != 0)
	{
		return false;
	}

	if (restBits == 0)
	{
		return true;
	}

	unsigned char mask = (unsigned char) (0xFF << (8 - restBits));

	return (net[fullBytes] & mask) == (peer[fullBytes] & mask);
}


/*
 * is peerIP one of the hosts in the route's own nodes file (one hostname
 * per line, maintained by the membership's capture service)?
 */
static bool
nodes_file_contains(const char *routePath, const char *peerIP)
{
	char nodesPath[MAXPGPATH];

	sformat(nodesPath, sizeof(nodesPath), "%s/" PG_AUTOCTL_ARCHIVER_NODES_FILE,
			routePath);

	char *contents = NULL;
	long size = 0;

	if (!read_file_if_exists(nodesPath, &contents, &size) || contents == NULL)
	{
		return false;
	}

	bool found = false;
	char *lineSave = NULL;

	for (char *host = strtok_r(contents, "\n", &lineSave);
		 host != NULL && !found;
		 host = strtok_r(NULL, "\n", &lineSave))
	{
		found = hba_host_matches_peer(host, peerIP);
	}

	free(contents);

	return found;
}


static bool
rule_address_matches(const char *address, const char *routePath,
					 const char *peerIP)
{
	if (streq(address, "all"))
	{
		return true;
	}

	if (streq(address, "monitor"))
	{
		return nodes_file_contains(routePath, peerIP);
	}

	if (strchr(address, '/') != NULL)
	{
		return cidr_matches(address, peerIP);
	}

	return hba_host_matches_peer(address, peerIP);
}


static bool
parse_method(const char *token, WsAuthMethod *method)
{
	if (streq(token, "trust"))
	{
		*method = WS_AUTH_TRUST;
	}
	else if (streq(token, "scram-sha-256"))
	{
		*method = WS_AUTH_SCRAM;
	}
	else if (streq(token, "reject"))
	{
		*method = WS_AUTH_REJECT;
	}
	else
	{
		return false;
	}

	return true;
}


bool
hba_lookup(const char *hbaPath, const char *routePath, const char *routeKey,
		   const char *user, const char *peerIP, WsAuthMethod *method)
{
	char *contents = NULL;
	long size = 0;

	*method = WS_AUTH_REJECT;

	if (!read_file(hbaPath, &contents, &size) || contents == NULL)
	{
		log_error("Failed to read the HBA file \"%s\": rejecting", hbaPath);
		return false;
	}

	char *lineSave = NULL;
	int lineNumber = 0;

	for (char *line = strtok_r(contents, "\n", &lineSave);
		 line != NULL;
		 line = strtok_r(NULL, "\n", &lineSave))
	{
		lineNumber++;

		char *hash = strchr(line, '#');

		if (hash != NULL)
		{
			*hash = '\0';
		}

		char *fields[HBA_MAX_FIELDS] = { 0 };
		int count = 0;
		char *fieldSave = NULL;

		for (char *tok = strtok_r(line, " \t\r", &fieldSave);
			 tok != NULL && count < HBA_MAX_FIELDS;
			 tok = strtok_r(NULL, " \t\r", &fieldSave))
		{
			fields[count++] = tok;
		}

		if (count == 0)
		{
			continue;
		}

		WsAuthMethod ruleMethod = WS_AUTH_REJECT;

		if (count != HBA_MAX_FIELDS || !streq(fields[0], "host") ||
			!parse_method(fields[4], &ruleMethod))
		{
			log_warn("Ignoring malformed HBA line %d in \"%s\"",
					 lineNumber, hbaPath);
			continue;
		}

		if ((streq(fields[1], "all") || streq(fields[1], routeKey)) &&
			(streq(fields[2], "all") || streq(fields[2], user)) &&
			rule_address_matches(fields[3], routePath, peerIP))
		{
			*method = ruleMethod;
			free(contents);
			return true;
		}
	}

	free(contents);

	return true;
}
