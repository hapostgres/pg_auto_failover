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
 * A parsed rule: each field is its own strdup'd, dequoted copy (see
 * next_hba_token), independent of the file's own buffer, which hba_lookup
 * frees right after hba_parse returns.
 */
typedef struct HbaRule
{
	char *fields[HBA_MAX_FIELDS];
	WsAuthMethod method;
	int lineNumber;
} HbaRule;


/*
 * hba_rule_free_fields releases the strdup'd fields of one rule (but not the
 * HbaRule itself, which lives in the caller's array).
 */
static void
hba_rule_free_fields(HbaRule *rule)
{
	for (int i = 0; i < HBA_MAX_FIELDS; i++)
	{
		free(rule->fields[i]);
		rule->fields[i] = NULL;
	}
}


/*
 * next_hba_token extracts the next whitespace-delimited field from *lineptr
 * into buf (truncating to bufSize, like PostgreSQL's own tokens this should
 * never matter in practice) and advances *lineptr past it. Mirrors
 * PostgreSQL's own next_token() in src/backend/libpq/hba.c, minus the parts
 * this project's own HBA format does not use (comma-separated lists,
 * @-file-inclusion, regular expressions):
 *
 *   - a field may be wrapped in double quotes, so it can contain spaces or a
 *     literal '#'; a doubled "" inside a quoted field is a literal '"'
 *     (exactly the SQL-style escaping PostgreSQL uses here);
 *   - an unquoted '#' begins a comment that runs to the end of the line
 *     (already continuation-joined by hba_read_logical_line, so a comment
 *     started before a trailing backslash also swallows the continued text,
 *     the same as PostgreSQL's own behavior).
 *
 * Returns false when there is no more token on the line (buf is then empty).
 */
static bool
next_hba_token(char **lineptr, char *buf, size_t bufSize)
{
	char *p = *lineptr;
	char *out = buf;
	char *end = buf + bufSize - 1;
	bool inQuote = false;
	bool sawQuote = false;

	while (*p == ' ' || *p == '\t')
	{
		p++;
	}

	while (*p != '\0' && (inQuote || (*p != ' ' && *p != '\t')))
	{
		char c = *p;

		if (c == '#' && !inQuote)
		{
			while (*p != '\0')
			{
				p++;
			}
			break;
		}

		if (c == '"')
		{
			if (inQuote && *(p + 1) == '"')
			{
				/* doubled quote inside a quoted field: literal '"' */
				if (out < end)
				{
					*out++ = '"';
				}
				p += 2;
				continue;
			}

			inQuote = !inQuote;
			sawQuote = true;
			p++;
			continue;
		}

		if (out < end)
		{
			*out++ = c;
		}

		p++;
	}

	*out = '\0';
	*lineptr = p;

	return sawQuote || out > buf;
}


/*
 * A cursor over the file's raw contents, tracking how many physical lines
 * have already been consumed (for error messages).
 */
typedef struct HbaLineReader
{
	char *cursor;
	int lineNumber;         /* physical lines already consumed */
} HbaLineReader;


/*
 * hba_read_logical_line reads the next logical line from *reader into
 * buffer, joining physical lines that end with a trailing backslash --
 * PostgreSQL's own line-continuation rule in tokenize_auth_file(): the
 * backslash and the newline it precedes are both dropped, and the next
 * physical line is appended in their place, however many times that
 * repeats. A trailing '\r' (CRLF file) is stripped from each physical line
 * first. *firstLineNumber is set to the 1-based line number of the logical
 * line's first physical line, which is what a malformed-line error reports.
 *
 * Returns false once the whole file has been consumed.
 */
static bool
hba_read_logical_line(HbaLineReader *reader, PQExpBuffer buffer,
					  int *firstLineNumber)
{
	if (*reader->cursor == '\0')
	{
		return false;
	}

	resetPQExpBuffer(buffer);
	*firstLineNumber = reader->lineNumber + 1;

	for (;;)
	{
		char *nl = strchr(reader->cursor, '\n');
		char *lineEnd = nl != NULL ? nl : reader->cursor + strlen(reader->cursor);
		size_t len = (size_t) (lineEnd - reader->cursor);

		if (len > 0 && reader->cursor[len - 1] == '\r')
		{
			len--;
		}

		bool continues = len > 0 && reader->cursor[len - 1] == '\\';

		appendBinaryPQExpBuffer(buffer, reader->cursor,
								(int) (continues ? len - 1 : len));

		reader->cursor = nl != NULL ? nl + 1 : lineEnd;
		reader->lineNumber++;

		if (!continues || nl == NULL)
		{
			break;
		}
	}

	return true;
}


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
	HbaLineReader reader = { contents, 0 };
	PQExpBuffer lineBuffer = createPQExpBuffer();
	int lineNumber = 0;

	while (lineBuffer != NULL && !PQExpBufferBroken(lineBuffer) &&
		   hba_read_logical_line(&reader, lineBuffer, &lineNumber))
	{
		char *lineptr = lineBuffer->data;
		char *fields[HBA_MAX_FIELDS + 1] = { 0 };
		int nfields = 0;
		char token[1024];

		while (nfields <= HBA_MAX_FIELDS && next_hba_token(&lineptr, token,
														   sizeof(token)))
		{
			fields[nfields] = strdup(token);

			if (fields[nfields] == NULL)
			{
				for (int i = 0; i < nfields; i++)
				{
					free(fields[i]);
				}
				destroyPQExpBuffer(lineBuffer);
				free(rules);
				return false;
			}

			nfields++;
		}

		if (nfields == 0)
		{
			/* blank line or comment-only line: nothing to record */
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

			for (int i = 0; i < nfields; i++)
			{
				free(fields[i]);
			}
			destroyPQExpBuffer(lineBuffer);
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

	bool bufferBroken = lineBuffer == NULL || PQExpBufferBroken(lineBuffer);

	destroyPQExpBuffer(lineBuffer);

	if (bufferBroken)
	{
		for (int i = 0; i < count; i++)
		{
			hba_rule_free_fields(&rules[i]);
		}
		free(rules);
		return false;
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

	for (int i = 0; i < count; i++)
	{
		hba_rule_free_fields(&rules[i]);
	}
	free(rules);
	free(contents);

	return true;
}
