/*
 * src/bin/pg_walsender/hba.c
 *   pg_walsender's host-based authentication file, see hba.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <netdb.h>
#include <resolv.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/socket.h>

#include "postgres_fe.h"

#include "pqexpbuffer.h"

#include "hba.h"

#include "defaults.h"
#include "file_utils.h"
#include "log.h"
#include "monitor_hosts.h"
#include "string_utils.h"

#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))

#define HBA_MAX_FIELDS 5


static const char *hbaHeader =
	"# pg_walsender host-based authentication, read on every connection.\n"
	"# The first matching line wins; no match (or an unreadable file) rejects.\n"
	"#\n"
	"# TYPE  ROUTE  USER  ADDRESS  METHOD\n"
	"#\n"
	"# TYPE     host (TLS or not), hostssl (TLS only), hostnossl (no TLS)\n"
	"# ROUTE    all, or <formation>/<group>\n"
	"# USER     all, or a role name\n"
	"# ADDRESS  all, monitor, an IP address, IP/prefix, a hostname, or a\n"
	"#          .domain.suffix (matched through every reverse DNS name of\n"
	"#          the client, each confirmed by a forward lookup);\n"
	"#          \"monitor\" is every node the monitor lists for the route\n"
	"# METHOD   scram-sha-256 (checked against archiver-passwd), trust, reject\n"
	"#\n";


/*
 * The default admits the nodes the monitor lists for a route, with a
 * password (SCRAM-SHA-256) and over TLS. Without a server certificate TLS
 * is not available, and the rule is a plain "host" one.
 */
bool
hba_write_default_if_missing(const char *hbaPath, bool tlsAvailable)
{
	if (file_exists(hbaPath))
	{
		return true;
	}

	log_info("Creating the default pg_walsender HBA file \"%s\"", hbaPath);

	PQExpBuffer buffer = createPQExpBuffer();

	appendPQExpBufferStr(buffer, hbaHeader);
	appendPQExpBufferStr(
		buffer,
		"# Nodes registered with the monitor (standbys and their pg_basebackup,\n"
		"# streaming and restore_command connections), with the replication\n"
		"# password given to pg_autoctl create archiver --replication-password:\n");

	if (tlsAvailable)
	{
		appendPQExpBuffer(buffer,
						  "hostssl  all  " PG_AUTOCTL_REPLICA_USERNAME
						  "  monitor  scram-sha-256\n");
	}
	else
	{
		appendPQExpBuffer(buffer,
						  "# no server.crt/server.key in this directory: TLS is off\n"
						  "host     all  " PG_AUTOCTL_REPLICA_USERNAME
						  "  monitor  scram-sha-256\n");
	}

	appendPQExpBufferStr(
		buffer,
		"#\n"
		"# A host the monitor does not know about, such as a PITR restore target,\n"
		"# needs a line of its own, for instance:\n"
		"# hostssl  default/0  pitr_restore  192.0.2.0/24  scram-sha-256\n");

	bool ok = !PQExpBufferBroken(buffer) &&
			  write_file_atomic(buffer->data, buffer->len, (char *) hbaPath);

	destroyPQExpBuffer(buffer);

	return ok;
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
 * reverse_names collects every PTR name of peerIP (not only the first one
 * getnameinfo() returns: a Docker network, or a host with several names,
 * answers with a list, and any of them may be the one an HBA rule names).
 * Returns the number of names stored.
 */
#define HBA_MAX_PTR_NAMES 16

static int
reverse_names(const char *peerIP, char names[][NS_MAXDNAME])
{
	unsigned char addr[16];
	char query[NS_MAXDNAME];
	int count = 0;

	if (inet_pton(AF_INET, peerIP, addr) == 1)
	{
		sformat(query, sizeof(query), "%u.%u.%u.%u.in-addr.arpa",
				addr[3], addr[2], addr[1], addr[0]);
	}
	else if (inet_pton(AF_INET6, peerIP, addr) == 1)
	{
		size_t len = 0;

		query[0] = '\0';

		for (int i = 15; i >= 0; i--)
		{
			len += (size_t) sformat(query + len, sizeof(query) - len, "%x.%x.",
									addr[i] & 0x0F, addr[i] >> 4);
		}

		strlcpy(query + len, "ip6.arpa", sizeof(query) - len);
	}
	else
	{
		return 0;
	}

	unsigned char answer[4096];
	int answerLen = res_query(query, ns_c_in, ns_t_ptr, answer, sizeof(answer));

	if (answerLen <= 0)
	{
		return 0;
	}

	ns_msg msg;

	if (ns_initparse(answer, answerLen, &msg) != 0)
	{
		return 0;
	}

	for (int i = 0; i < ns_msg_count(msg, ns_s_an) && count < HBA_MAX_PTR_NAMES; i++)
	{
		ns_rr rr;

		if (ns_parserr(&msg, ns_s_an, i, &rr) != 0 || ns_rr_type(rr) != ns_t_ptr)
		{
			continue;
		}

		if (ns_name_uncompress(ns_msg_base(msg), ns_msg_end(msg),
							   ns_rr_rdata(rr), names[count],
							   NS_MAXDNAME) >= 0)
		{
			count++;
		}
	}

	return count;
}


/*
 * suffix_matches implements ".example.com": true when any reverse name of
 * the peer ends with the suffix AND that name resolves (forward) back to
 * the peer -- PostgreSQL's forward-confirmed reverse DNS, but over every
 * PTR answer rather than the first one only.
 */
static bool
suffix_matches(const char *suffix, const char *peerIP)
{
	char names[HBA_MAX_PTR_NAMES][NS_MAXDNAME];
	int count = reverse_names(peerIP, names);
	size_t suffixLen = strlen(suffix);

	log_debug("HBA suffix %s peer %s: %d reverse names, first \"%s\"", suffix, peerIP,
			  count, count > 0 ? names[0] : "");

	for (int i = 0; i < count; i++)
	{
		size_t len = strlen(names[i]);

		/* PTR names may carry a trailing dot in some resolvers */
		if (len > 0 && names[i][len - 1] == '.')
		{
			names[i][--len] = '\0';
		}

		if (len > suffixLen &&
			strcasecmp(names[i] + len - suffixLen, suffix) == 0 &&
			hba_host_matches_peer(names[i], peerIP))
		{
			return true;
		}
	}

	return false;
}


static bool
rule_address_matches(const char *address, const char *routeKey,
					 const char *routePath, const char *monitorUriPath,
					 const char *peerIP)
{
	if (streq(address, "all"))
	{
		return true;
	}

	if (streq(address, "monitor"))
	{
		return monitor_hosts_contain(routeKey, routePath, monitorUriPath, peerIP);
	}

	if (strchr(address, '/') != NULL)
	{
		return cidr_matches(address, peerIP);
	}

	if (address[0] == '.')
	{
		return suffix_matches(address, peerIP);
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
hba_lookup(const char *hbaPath, const char *routePath,
		   const char *monitorUriPath, const char *routeKey, const char *user,
		   const char *peerIP, bool isTLS, WsAuthMethod *method)
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

		bool typeOk = count > 0 && (streq(fields[0], "host") ||
									streq(fields[0], "hostssl") ||
									streq(fields[0], "hostnossl"));

		if (count != HBA_MAX_FIELDS || !typeOk ||
			!parse_method(fields[4], &ruleMethod))
		{
			log_warn("Ignoring malformed HBA line %d in \"%s\"",
					 lineNumber, hbaPath);
			continue;
		}

		bool typeMatches = streq(fields[0], "host") ||
						   (streq(fields[0], "hostssl") && isTLS) ||
						   (streq(fields[0], "hostnossl") && !isTLS);

		if (typeMatches &&
			(streq(fields[1], "all") || streq(fields[1], routeKey)) &&
			(streq(fields[2], "all") || streq(fields[2], user)) &&
			rule_address_matches(fields[3], routeKey, routePath,
								 monitorUriPath, peerIP))
		{
			*method = ruleMethod;
			free(contents);
			return true;
		}
	}

	free(contents);

	return true;
}
