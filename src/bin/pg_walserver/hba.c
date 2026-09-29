/*
 * src/bin/pg_walserver/hba.c
 *   pg_walserver's host-based authentication file, see hba.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
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
	"# METHOD   scram-sha-256 (checked against pg_walserver_passwd), trust, reject\n"
	"#\n"
	"# An optional sixth field, clientcert=verify-full, may follow METHOD:\n"
	"# the TLS peer certificate's CN must equal USER exactly. Requires\n"
	"# --ssl-ca-file to be configured. With METHOD trust the certificate\n"
	"# check is the whole authentication; with scram-sha-256 both the\n"
	"# certificate and the password are required (two-factor).\n"
	"#\n";


/*
 * There is no automatic node admission in this PR (no monitor integration
 * yet, see hba.h's own header comment): the default file only documents how
 * to add a rule, it never admits anything by itself, so every connection is
 * rejected until an operator adds a line -- unless localCIDR is given (see
 * hba_write_setup_default(), which is the one caller that passes it), in
 * which case a real, active rule for that CIDR is written instead of a
 * commented-out example.
 */
bool
hba_write_default_if_missing(const char *hbaPath, bool tlsAvailable)
{
	return hba_write_setup_default(hbaPath, tlsAvailable, NULL);
}


/*
 * hba_write_setup_default is hba_write_default_if_missing()'s own
 * implementation, plus an optional localCIDR: when given (non-empty), the
 * written file's one example rule is an active one (no leading "#"), open
 * to that CIDR, rather than a commented-out placeholder -- "pg_walserver
 * setup"'s own use, once it has auto-discovered its local network's CIDR
 * (see cli_setup.c). hba_write_default_if_missing() itself always passes
 * NULL: "serve"'s own bootstrap path never auto-admits a CIDR it hasn't
 * been asked to.
 */
bool
hba_write_setup_default(const char *hbaPath, bool tlsAvailable,
						const char *localCIDR)
{
	if (file_exists(hbaPath))
	{
		return true;
	}

	log_info("Creating the default pg_walserver HBA file \"%s\"", hbaPath);

	PQExpBuffer buffer = createPQExpBuffer();

	appendPQExpBufferStr(buffer, hbaHeader);

	bool haveCIDR = localCIDR != NULL && localCIDR[0] != '\0';

	if (haveCIDR)
	{
		/*
		 * USER is "all" here, deliberately never PG_AUTOCTL_REPLICA_USERNAME:
		 * that constant names pg_auto_failover's own conventional role, but
		 * an operator is free to register a cluster under any role name at
		 * all ("cluster register --pguri ..."), which "setup" has no way
		 * to know yet -- pinning this active rule to one specific,
		 * possibly-wrong role name would silently reject every other one.
		 * "all" still requires a valid scram-sha-256 password for whichever
		 * role actually connects, so this is not an open door, only not
		 * restricted to a role name setup cannot possibly know in advance.
		 */
		appendPQExpBufferStr(
			buffer,
			"# One rule below, open to this machine's own local network\n"
			"# (auto-discovered by \"pg_walserver setup\"), for any role\n"
			"# with the right scram-sha-256 password -- narrow ROUTE/USER\n"
			"# to a specific cluster/role once you know them, or add more\n"
			"# lines below; the first matching line always wins:\n");

		if (tlsAvailable)
		{
			appendPQExpBuffer(buffer,
							  "hostssl  all  all  %s  scram-sha-256\n",
							  localCIDR);
		}
		else
		{
			appendPQExpBuffer(buffer,
							  "# no server.crt/server.key in this directory: "
							  "TLS is off\n"
							  "host     all  all  %s  scram-sha-256\n",
							  localCIDR);
		}

		bool ok = !PQExpBufferBroken(buffer) &&
				  write_file_atomic(buffer->data, buffer->len, (char *) hbaPath);

		destroyPQExpBuffer(buffer);

		return ok;
	}

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
 * ws_setup_autodetect_cidr is a best-effort, non-fatal discovery of this
 * machine's own local-network CIDR, for "pg_walserver setup" to seed its
 * HBA file with a real, working rule instead of a commented-out example.
 *
 * pg_autoctl's own discovery (fetchLocalIPAddress() then fetchLocalCIDR(),
 * src/bin/common/ipaddr.c) needs a real remote target to connect() toward
 * first (--monitor's own address, always already known by the time it
 * runs it) -- "setup" has no such target at all, by design: it runs before
 * any cluster/upstream is known (see cli_setup.h). So this walks
 * getifaddrs() directly instead, picking the first UP, non-loopback IPv4
 * interface as "this machine's own address", then hands that straight to
 * the same fetchLocalCIDR() pg_autoctl itself uses to turn an address into
 * its interface's own CIDR (netmask-derived) -- no network reachability of
 * any kind required, only that the host has at least one configured
 * interface, which every real deployment does.
 *
 * Returns false, cidrOut untouched, when no such interface exists (e.g. an
 * otherwise-unconfigured container with only loopback) -- callers treat
 * that as "skip it", never as a hard error: setup's other work still
 * completes.
 */
bool
ws_setup_autodetect_cidr(char *cidrOut, size_t cidrOutSize)
{
	struct ifaddrs *ifaddrList = NULL;
	char localIP[INET6_ADDRSTRLEN] = { 0 };
	bool found = false;

	if (getifaddrs(&ifaddrList) == -1)
	{
		log_debug("Failed to get the list of local network interfaces: %m");
		return false;
	}

	for (struct ifaddrs *ifa = ifaddrList; ifa != NULL; ifa = ifa->ifa_next)
	{
		if (ifa->ifa_addr == NULL ||
			ifa->ifa_addr->sa_family != AF_INET ||
			(ifa->ifa_flags & IFF_LOOPBACK) != 0 ||
			(ifa->ifa_flags & IFF_UP) == 0)
		{
			continue;
		}

		struct sockaddr_in *addr = (struct sockaddr_in *) ifa->ifa_addr;

		if (inet_ntop(AF_INET, &(addr->sin_addr),
					  localIP, sizeof(localIP)) != NULL)
		{
			log_debug("Using local interface \"%s\" (%s) to discover this "
					  "machine's own CIDR", ifa->ifa_name, localIP);
			found = true;
			break;
		}
	}

	freeifaddrs(ifaddrList);

	if (!found)
	{
		return false;
	}

	char localCIDR[INET6_ADDRSTRLEN + 8] = { 0 };

	if (!fetchLocalCIDR(localIP, localCIDR, sizeof(localCIDR)))
	{
		/* errors have already been logged */
		return false;
	}

	strlcpy(cidrOut, localCIDR, cidrOutSize);

	return true;
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
 * parse_clientcert_option recognizes exactly one optional 6th HBA field,
 * "clientcert=verify-full", mirroring real PostgreSQL's own pg_hba.conf
 * "clientcert" option (src/backend/libpq/hba.c upstream). This project does
 * not implement "clientcert=verify-ca": real PostgreSQL's TLS layer already
 * validates any client certificate it is handed against ssl_ca_file the
 * moment one is presented (SSL_VERIFY_PEER at the SSL_CTX level, set
 * whenever --ssl-ca-file is configured, see tls.c), independent of any HBA
 * line at all -- "verify-ca" on a line adds nothing beyond that already-
 * enforced TLS-level check, so there is nothing distinct for it to mean
 * here (see hba.h's own header comment). Anything else after METHOD is a
 * malformed line, exactly like an unrecognized METHOD itself.
 */
static bool
parse_clientcert_option(const char *token, bool *requireClientCert)
{
	if (streq(token, "clientcert=verify-full"))
	{
		*requireClientCert = true;
		return true;
	}

	return false;
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
		char *fields[HBA_MAX_FIELDS + 2] = { 0 };
		int nfields = 0;
		char token[1024];

		while (nfields <= HBA_MAX_FIELDS + 1 && next_hba_token(&lineptr, token,
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
		bool requireClientCert = false;

		bool typeOk = streq(fields[0], "host") ||
					  streq(fields[0], "hostssl") ||
					  streq(fields[0], "hostnossl");

		bool hasClientCertField = nfields == HBA_MAX_FIELDS + 1;

		if ((nfields != HBA_MAX_FIELDS && !hasClientCertField) || !typeOk ||
			!parse_method(fields[4], &ruleMethod) ||
			(hasClientCertField &&
			 !parse_clientcert_option(fields[HBA_MAX_FIELDS], &requireClientCert)))
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

		if (hasClientCertField)
		{
			free(fields[HBA_MAX_FIELDS]);
		}

		rule->method = ruleMethod;
		rule->requireClientCert = requireClientCert;
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
		  const char *peerIP, bool isTLS, WsAuthMethod *method,
		  bool *requireClientCert)
{
	*method = WS_AUTH_REJECT;
	*requireClientCert = false;

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
			*requireClientCert = ruleSet->rules[i].requireClientCert;
			return;
		}
	}
}


/*
 * hba_ruleset_requires_client_cert reports whether any rule in ruleSet has
 * "clientcert=verify-full" -- see hba.h's own comment.
 */
bool
hba_ruleset_requires_client_cert(const WsHbaRuleSet *ruleSet)
{
	for (int i = 0; i < ruleSet->count; i++)
	{
		if (ruleSet->rules[i].requireClientCert)
		{
			return true;
		}
	}

	return false;
}
