/*
 * src/bin/pg_walsender/hba.c
 *   pg_walsender's host-based authentication file, see hba.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <stdlib.h>
#include <strings.h>
#include <string.h>

#include "postgres_fe.h"

#include "pqexpbuffer.h"

#include "hba.h"

#include "defaults.h"
#include "file_utils.h"
#include "ipaddr.h"
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
 * suffix_matches implements ".example.com": true when any reverse name of
 * the peer ends with the suffix AND that name resolves (forward) back to
 * the peer -- PostgreSQL's forward-confirmed reverse DNS, but over every
 * PTR answer rather than the first one only.
 */
static bool
suffix_matches(const char *suffix, const char *peerIP)
{
	char names[IPADDR_MAX_HOSTNAMES][IPADDR_MAX_HOSTNAME_SIZE];
	int count = ipaddrFindHostnamesFromAddress(peerIP, names,
											   IPADDR_MAX_HOSTNAMES);
	size_t suffixLen = strlen(suffix);

	log_debug("HBA suffix %s peer %s: %d reverse names", suffix, peerIP, count);

	for (int i = 0; i < count; i++)
	{
		size_t len = strlen(names[i]);

		if (len > suffixLen &&
			strcasecmp(names[i] + len - suffixLen, suffix) == 0 &&
			ipaddrHostMatchesAddress(names[i], peerIP))
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
		return ipaddrInCIDR(address, peerIP);
	}

	if (address[0] == '.')
	{
		return suffix_matches(address, peerIP);
	}

	return ipaddrHostMatchesAddress(address, peerIP);
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
