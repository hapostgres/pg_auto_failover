/*
 * src/bin/pg_walserver/hba.c
 *   pg_walserver's host-based authentication file, see hba.h.
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
#include "string_utils.h"
#include "ws_util.h"

#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))

#define HBA_MAX_FIELDS WS_HBA_MAX_FIELDS


static const char *hbaHeader =
	"# pg_walserver host-based authentication, read on every connection.\n"
	"# The first matching line wins; no match, an unreadable file or any\n"
	"# malformed line rejects every connection.\n"
	"#\n"
	"# TYPE  ROUTE  USER  ADDRESS  METHOD\n"
	"#\n"
	"# TYPE     host (TLS or not), hostssl (TLS only), hostnossl (no TLS)\n"
	"# ROUTE    all, or a route key exactly as it appears in pg_walserver.ini\n"
	"#          (an opaque string, never a path; pg_auto_failover's own\n"
	"#          convention is \"<formation>/<group>\", e.g. \"default/0\")\n"
	"# USER     all, or a role name\n"
	"# ADDRESS  all, samehost, samenet, an IP address, IP/prefix, a hostname,\n"
	"#          or a .domain.suffix (matched through every reverse DNS name\n"
	"#          of the client, each confirmed by a forward lookup)\n"
	"# METHOD   scram-sha-256 (checked against archiver-passwd), trust, reject\n"
	"#\n";


/*
 * There is no automatic node admission in this PR (no monitor integration
 * yet, see hba.h's own header comment): the default file only documents how
 * to add a rule, it never admits anything by itself, so every connection is
 * rejected until an operator adds a line.
 */
bool
hba_write_default_if_missing(const char *hbaPath, bool tlsAvailable)
{
	if (file_exists(hbaPath))
	{
		return true;
	}

	log_info("Creating the default pg_walserver HBA file \"%s\"", hbaPath);

	PQExpBuffer buffer = createPQExpBuffer();

	appendPQExpBufferStr(buffer, hbaHeader);
	appendPQExpBufferStr(
		buffer,
		"# No rule matches anything yet: every connection is rejected until\n"
		"# a line is added below, one per host allowed to connect, with the\n"
		"# replication password given to pg_autoctl create archiver\n"
		"# --replication-password, for instance:\n");

	if (tlsAvailable)
	{
		appendPQExpBuffer(buffer,
						  "# hostssl  all  " PG_AUTOCTL_REPLICA_USERNAME
						  "  10.1.0.0/16  scram-sha-256\n");
	}
	else
	{
		appendPQExpBuffer(buffer,
						  "# no server.crt/server.key in this directory: TLS is off\n"
						  "# host     all  " PG_AUTOCTL_REPLICA_USERNAME
						  "  10.1.0.0/16  scram-sha-256\n");
	}

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


/*
 * rule_address_matches implements one HBA rule's own ADDRESS field against
 * peerIP, trying each supported form in turn: "all" (always matches), an
 * IP/prefix (CIDR match), "samehost"/"samenet" (as in PostgreSQL, matching
 * the server's own address or subnet), ".domain.suffix" (forward-confirmed
 * reverse DNS, checked against every PTR answer, not just the first), and,
 * falling through, a bare hostname resolved forward. See README.md's "HBA"
 * section for why each form behaves the way it does here.
 */
static bool
rule_address_matches(const char *address, const char *peerIP)
{
	if (streq(address, "all"))
	{
		return true;
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


/*
 * parse_method maps an HBA METHOD field ("trust", "scram-sha-256",
 * "reject") to its WsAuthMethod value. Returns false, *method untouched, on
 * anything else -- this project's own HBA format has no other method.
 */
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
 * HbaRule is declared in hba.h now (WsHbaRuleSet, the cached, in-memory
 * ruleset installed at startup and swapped on a successful SIGHUP reload,
 * needs it visible outside this file). Each field is its own strdup'd,
 * dequoted copy (see next_hba_token), independent of the file's own buffer,
 * which hba_parse_file() frees right after hba_parse() returns.
 */


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
 * @-file-inclusion, regular expressions). This is a deliberate
 * simplification, not an oversight: PostgreSQL needs comma-separated lists
 * and @file inclusion because its own pg_hba.conf's DATABASE/USER fields
 * can each name several databases/roles, or include a whole external list
 * file. This project's own HBA dialect has no such thing -- ROUTE and USER
 * (see hba.h's own grammar) are each always a single value ("all" or one
 * exact string), never a list -- so there is nothing for a comma or an
 * @file reference to ever separate or expand here:
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
hba_parse_file(const char *hbaPath, WsHbaRuleSet *ruleSet)
{
	char *contents = NULL;
	size_t size = 0;

	ruleSet->rules = NULL;
	ruleSet->count = 0;

	if (!ws_read_file_capped(hbaPath, WS_MAX_CONFIG_FILE_SIZE, false,
							 &contents, &size, NULL))
	{
		log_error("Failed to read the HBA file \"%s\": rejecting", hbaPath);
		return false;
	}

	bool parsed = hba_parse(hbaPath, contents, &ruleSet->rules, &ruleSet->count);

	free(contents);

	return parsed;
}


void
hba_ruleset_free(WsHbaRuleSet *ruleSet)
{
	if (ruleSet == NULL || ruleSet->rules == NULL)
	{
		return;
	}

	for (int i = 0; i < ruleSet->count; i++)
	{
		hba_rule_free_fields(&ruleSet->rules[i]);
	}

	free(ruleSet->rules);
	ruleSet->rules = NULL;
	ruleSet->count = 0;
}


void
hba_match(const WsHbaRuleSet *ruleSet, const char *routeKey, const char *user,
		  const char *peerIP, bool isTLS, WsAuthMethod *method)
{
	*method = WS_AUTH_REJECT;

	for (int i = 0; i < ruleSet->count; i++)
	{
		char **fields = ruleSet->rules[i].fields;

		bool typeMatches = streq(fields[0], "host") ||
						   (streq(fields[0], "hostssl") && isTLS) ||
						   (streq(fields[0], "hostnossl") && !isTLS);

		if (typeMatches &&
			(streq(fields[1], "all") || streq(fields[1], routeKey)) &&
			(streq(fields[2], "all") || streq(fields[2], user)) &&
			rule_address_matches(fields[3], peerIP))
		{
			*method = ruleSet->rules[i].method;
			return;
		}
	}
}
