/*
 * src/bin/pg_walsender/hba.c
 *   pg_walsender's host-based authentication file, see hba.h.
 *
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
#include "ws_util.h"

#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))

#define HBA_MAX_FIELDS 5


static const char *hbaHeader =
	"# pg_walsender host-based authentication, read on every connection.\n"
	"# The first matching line wins; no match, an unreadable file or any\n"
	"# malformed line rejects every connection.\n"
	"#\n"
	"# TYPE  ROUTE  USER  ADDRESS  METHOD\n"
	"#\n"
	"# TYPE     host (TLS or not), hostssl (TLS only), hostnossl (no TLS)\n"
	"# ROUTE    all, or <formation>/<group>\n"
	"# USER     all, or a role name\n"
	"# ADDRESS  all, samehost, samenet, monitor, an IP address, IP/prefix, a\n"
	"#          hostname, or a\n"
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
	/*
	 * Like PostgreSQL's check_hostname(), which keeps the client's reverse
	 * name in the Port, resolve it once per connection (this process serves
	 * exactly one) however many suffix rules are tried.
	 */
	static char names[IPADDR_MAX_HOSTNAMES][IPADDR_MAX_HOSTNAME_SIZE];
	static char resolvedFor[64] = "";
	static int count = 0;

	if (strcmp(resolvedFor, peerIP) != 0)
	{
		count = ipaddrFindHostnamesFromAddress(peerIP, names,
											   IPADDR_MAX_HOSTNAMES);
		strlcpy(resolvedFor, peerIP, sizeof(resolvedFor));
	}

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
					 const char *refreshSockPath, const char *peerIP)
{
	if (streq(address, "all"))
	{
		return true;
	}

	if (streq(address, "monitor"))
	{
		/* no known route (routePath NULL): "monitor" matches nothing */
		return routePath != NULL &&
			   monitor_hosts_contain(routeKey, routePath, monitorUriPath,
									 refreshSockPath, peerIP);
	}

	if (strchr(address, '/') != NULL)
	{
		return ipaddrInCIDR(address, peerIP);
	}

	if (streq(address, "samehost") || streq(address, "samenet"))
	{
		return ipaddrIsSameHostOrNet(peerIP, streq(address, "samenet"));
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


/*
 * A parsed rule: the fields point into the file's own buffer, which
 * hba_lookup keeps alive while the rules are used.
 */
typedef struct HbaRule
{
	char *fields[HBA_MAX_FIELDS];
	WsAuthMethod method;
	int lineNumber;
} HbaRule;


/*
 * hba_parse parses the whole file before anything is matched, the way
 * PostgreSQL refuses to load a pg_hba.conf with a bad line: one malformed
 * line makes the whole lookup fail (and so every connection be rejected),
 * it is never skipped over -- skipping a line that was meant to be a
 * "reject" rule would silently open the door.
 */
static bool
hba_parse(const char *hbaPath, char *contents, HbaRule **rulesOut,
		  int *countOut)
{
	int maxRules = 1;

	for (const char *c = contents; *c != '\0'; c++)
	{
		if (*c == '\n')
		{
			maxRules++;
		}
	}

	HbaRule *rules = (HbaRule *) calloc(maxRules, sizeof(HbaRule));

	if (rules == NULL)
	{
		return false;
	}

	int count = 0;
	int lineNumber = 0;
	char *line = contents;

	while (line != NULL && *line != '\0')
	{
		char *nl = strchr(line, '\n');
		char *next = NULL;

		if (nl != NULL)
		{
			*nl = '\0';
			next = nl + 1;
		}

		lineNumber++;

		char *hash = strchr(line, '#');

		if (hash != NULL)
		{
			*hash = '\0';
		}

		char *fields[HBA_MAX_FIELDS + 1] = { 0 };
		int nfields = 0;
		char *fieldSave = NULL;

		for (char *tok = strtok_r(line, " \t\r", &fieldSave);
			 tok != NULL && nfields <= HBA_MAX_FIELDS;
			 tok = strtok_r(NULL, " \t\r", &fieldSave))
		{
			fields[nfields++] = tok;
		}

		line = next;

		if (nfields == 0)
		{
			continue;
		}

		WsAuthMethod ruleMethod = WS_AUTH_REJECT;

		bool typeOk = streq(fields[0], "host") ||
					  streq(fields[0], "hostssl") ||
					  streq(fields[0], "hostnossl");

		if (nfields != HBA_MAX_FIELDS || !typeOk ||
			!parse_method(fields[4], &ruleMethod))
		{
			log_error("Malformed HBA line %d in \"%s\": rejecting every "
					  "connection until the file is fixed",
					  lineNumber, hbaPath);
			free(rules);
			return false;
		}

		HbaRule *rule = &rules[count++];

		for (int i = 0; i < HBA_MAX_FIELDS; i++)
		{
			rule->fields[i] = fields[i];
		}

		rule->method = ruleMethod;
		rule->lineNumber = lineNumber;
	}

	*rulesOut = rules;
	*countOut = count;

	return true;
}


bool
hba_lookup(const char *hbaPath, const char *routePath,
		   const char *monitorUriPath, const char *refreshSockPath,
		   const char *routeKey, const char *user,
		   const char *peerIP, bool isTLS, WsAuthMethod *method)
{
	char *contents = NULL;
	size_t size = 0;

	*method = WS_AUTH_REJECT;

	if (!ws_read_file_capped(hbaPath, WS_MAX_CONFIG_FILE_SIZE, false,
							 &contents, &size, NULL))
	{
		log_error("Failed to read the HBA file \"%s\": rejecting", hbaPath);
		return false;
	}

	HbaRule *rules = NULL;
	int count = 0;

	if (!hba_parse(hbaPath, contents, &rules, &count))
	{
		free(contents);
		return false;
	}

	for (int i = 0; i < count; i++)
	{
		char **fields = rules[i].fields;

		bool typeMatches = streq(fields[0], "host") ||
						   (streq(fields[0], "hostssl") && isTLS) ||
						   (streq(fields[0], "hostnossl") && !isTLS);

		if (typeMatches &&
			(streq(fields[1], "all") || streq(fields[1], routeKey)) &&
			(streq(fields[2], "all") || streq(fields[2], user)) &&
			rule_address_matches(fields[3], routeKey, routePath,
								 monitorUriPath, refreshSockPath, peerIP))
		{
			*method = rules[i].method;
			break;
		}
	}

	free(rules);
	free(contents);

	return true;
}
