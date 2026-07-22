/*
 * src/bin/pgaftest/cli_indent.c
 *   pgaftest indent — parse a .pgaf file and rewrite it with canonical
 *   indentation.
 *
 * The output format follows these conventions:
 *   - 4-space indentation for all block contents
 *   - multi-line node specs in cluster{}: each property on its own continuation
 *     line, indented to align under the node name
 *   - multi-condition wait until: each "and <node> state is <s>" on its own
 *     line, indented under the first condition
 *   - sequence block: one step name per line, indented
 *   - leading file comment block is preserved verbatim
 *   - comment blocks immediately before setup/teardown/step are preserved
 *   - inline comments inside step bodies are dropped (AST does not store them)
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "cli_indent.h"
#include "test_spec.h"
#include "test_spec_parse.h"
#include "defaults.h"
#include "file_utils.h"
#include "log.h"
#include "string_utils.h"

/*
 * StepComment — comment block that precedes a named block in the raw file.
 * name is "setup", "teardown", or the step name.
 * text is malloc'd; NULL if no comment precedes that block.
 */
#define MAX_STEP_COMMENTS 256

typedef struct
{
	char name[128];
	char *text;
} StepComment;

typedef struct
{
	StepComment entries[MAX_STEP_COMMENTS];
	int count;
} StepCommentMap;

/* forward declarations */
static void print_header(FILE *out, const char *path);
static StepCommentMap * collect_comments(const char *path);
static const char * lookup_comment(const StepCommentMap *m, const char *name);
static void free_comments(StepCommentMap *m);
static void print_cluster(FILE *out, const TestSpec *spec);
static void print_step(FILE *out, const TestStep *step, bool named);
static void print_cmd(FILE *out, const TestCmd *cmd, int indent);
static void print_cmd_wait(FILE *out, const TestCmd *cmd, int indent);
static void normalize_sql(const char *in, char *out, int outlen);
static int match_clause_keyword(const char *p);
static void emit_segment(FILE *out, const char *seg, int bodyIndent);
static void print_sql_body(FILE *out, const char *raw_sql, int bodyIndent);

/* -----------------------------------------------------------------------
 * Entry point called from cli_root.c
 * ----------------------------------------------------------------------- */
void
cli_indent(int argc, char **argv)
{
	if (argc < 1 || argv[0] == NULL)
	{
		fformat(stderr, "Usage: pgaftest indent <spec.pgaf>\n");
		exit(EXIT_CODE_BAD_ARGS);
	}

	const char *path = argv[0];

	TestSpec *spec = parse_test_spec(path);
	if (!spec)
	{
		log_error("Failed to parse \"%s\"", path);
		exit(EXIT_CODE_BAD_ARGS);
	}

	StepCommentMap *cmap = collect_comments(path);

	FILE *out = stdout;

	print_header(out, path);

	print_cluster(out, spec);

	if (spec->setup)
	{
		const char *cmt = lookup_comment(cmap, "setup");
		if (cmt)
		{
			fformat(out, "\n%s\nsetup {\n", cmt);
		}
		else
		{
			fformat(out, "\nsetup {\n");
		}
		print_step(out, spec->setup, false);
		fformat(out, "}\n");
	}

	if (spec->teardown)
	{
		const char *cmt = lookup_comment(cmap, "teardown");
		if (cmt)
		{
			fformat(out, "\n%s\nteardown {\n", cmt);
		}
		else
		{
			fformat(out, "\nteardown {\n");
		}
		print_step(out, spec->teardown, false);
		fformat(out, "}\n");
	}

	for (TestStep *s = spec->steps; s; s = s->next)
	{
		const char *cmt = lookup_comment(cmap, s->name);
		if (cmt)
		{
			fformat(out, "\n%s\nstep %s {\n", cmt, s->name);
		}
		else
		{
			fformat(out, "\nstep %s {\n", s->name);
		}
		print_step(out, s, true);
		fformat(out, "}\n");
	}

	/*
	 * Emit the sequence block only when it differs from definition order.
	 * When sequence == steps-in-order the block is redundant noise; drop it.
	 */
	if (spec->sequenceLength > 0)
	{
		/* Check whether sequence matches step definition order exactly */
		bool redundant = true;
		int si = 0;
		for (TestStep *s = spec->steps; s && si < spec->sequenceLength; s = s->next, si++)
		{
			if (strcmp(spec->sequence[si], s->name) != 0)
			{
				redundant = false;
				break;
			}
		}

		/* Also redundant only if counts match */
		if (si != spec->sequenceLength || spec->sequenceLength != spec->stepCount)
		{
			redundant = false;
		}

		if (!redundant)
		{
			fformat(out, "\nsequence\n");
			for (int i = 0; i < spec->sequenceLength; i++)
			{
				fformat(out, "    %s\n", spec->sequence[i]);
			}
		}
	}

	free_comments(cmap);
	exit(0);
}


/* -----------------------------------------------------------------------
 * Header: emit the leading # comment block from the original file.
 * The parser drops comments, so we re-read the raw file here.
 * Falls back to "# <path>" when the file has no leading comment.
 * ----------------------------------------------------------------------- */
static void
print_header(FILE *out, const char *path)
{
	FILE *f = fopen(path, "r"); /* IGNORE-BANNED */
	if (!f)
	{
		fformat(out, "# %s\n\n", path);
		return;
	}

	char line[1024];
	bool any = false;

	char pending_blank[1024] = "";   /* blank lines between comment blocks */

	while (fgets(line, sizeof(line), f))
	{
		/* stop at first non-comment, non-blank line */
		if (line[0] != '#' && line[0] != '\n' && line[0] != '\r')
		{
			break;
		}

		if (line[0] == '\n' || line[0] == '\r')
		{
			if (any)
			{
				/* accumulate blank lines; flush only if more comments follow */
				strlcat(pending_blank, line, sizeof(pending_blank));
			}
			continue;
		}

		/* comment line: flush any accumulated blanks first */
		if (pending_blank[0])
		{
			fputs(pending_blank, out);
			pending_blank[0] = '\0';
		}
		fputs(line, out);
		any = true;
	}

	/* discard trailing blank lines — add a single blank after */
	fclose(f);

	if (!any)
	{
		fformat(out, "# %s\n", path);
	}

	fformat(out, "\n");
}


/* -----------------------------------------------------------------------
 * Step comment collection — scan the raw file for # blocks that
 * immediately precede "step <name> {", "setup {", or "teardown {".
 * ----------------------------------------------------------------------- */
static StepCommentMap *
collect_comments(const char *path)
{
	StepCommentMap *m = calloc(1, sizeof(StepCommentMap));
	if (!m)
	{
		return m;
	}

	FILE *f = fopen(path, "r"); /* IGNORE-BANNED */
	if (!f)
	{
		return m;
	}

	char line[1024];
	char pending[8192] = "";   /* accumulated # lines not yet assigned */
	bool skip_file_header = true;

	while (fgets(line, sizeof(line), f))
	{
		/* Skip the leading file-header comment block (consumed by print_header) */
		if (skip_file_header)
		{
			if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
			{
				continue;
			}
			skip_file_header = false;
		}

		if (line[0] == '#')
		{
			/* Accumulate comment line */
			strlcat(pending, line, sizeof(pending));
			continue;
		}

		if (line[0] == '\n' || line[0] == '\r')
		{
			/* Blank line: keep pending — comment may continue after blank */
			if (pending[0])
			{
				strlcat(pending, line, sizeof(pending));
			}
			continue;
		}

		/* Check for a block keyword */
		char kw[128] = "";
		if (sscanf(line, "step %127s", kw) == 1) /* IGNORE-BANNED */
		{
			/* strip trailing " {" from name */
			char *brace = strchr(kw, '{');
			if (brace)
			{
				*brace = '\0';
			}
		}
		else if (strncmp(line, "setup", 5) == 0)
		{
			strlcpy(kw, "setup", sizeof(kw));
		}
		else if (strncmp(line, "teardown", 8) == 0)
		{
			strlcpy(kw, "teardown", sizeof(kw));
		}

		if (kw[0] && pending[0] && m->count < MAX_STEP_COMMENTS)
		{
			/* Strip leading blank lines */
			char *start = pending;
			while (*start == '\n' || *start == '\r')
			{
				start++;
			}

			/* Strip trailing blank lines; add exactly one trailing \n */
			int len = (int) strlen(start);
			while (len > 0 && (start[len - 1] == '\n' || start[len - 1] == '\r' ||
							   start[len - 1] == ' '))
			{
				len--;
			}

			if (len > 0)
			{
				char clean[8192];
				sformat(clean, sizeof(clean), "%.*s\n", len, start);
				strlcpy(m->entries[m->count].name, kw,
						sizeof(m->entries[0].name));
				m->entries[m->count].text = strdup(clean);
				m->count++;
			}
		}

		/* Reset pending regardless */
		pending[0] = '\0';
	}

	fclose(f);
	return m;
}


static const char *
lookup_comment(const StepCommentMap *m, const char *name)
{
	if (!m)
	{
		return NULL;
	}
	for (int i = 0; i < m->count; i++)
	{
		if (strcmp(m->entries[i].name, name) == 0)
		{
			return m->entries[i].text;
		}
	}
	return NULL;
}


static void
free_comments(StepCommentMap *m)
{
	if (!m)
	{
		return;
	}
	for (int i = 0; i < m->count; i++)
	{
		free(m->entries[i].text);
	}
	free(m);
}


/* -----------------------------------------------------------------------
 * Cluster block
 * ----------------------------------------------------------------------- */

/*
 * Node property token: a keyword and an optional value string (NULL = flag only).
 */
typedef struct
{
	const char *kw;
	char val[128];
} NodeProp;

static void
print_node(FILE *out, const TestNode *n, int baseIndent)
{
	NodeProp props[32];
	int pc = 0;
	char numbuf[32];

	/* kind prefix — emitted inline after the name; for workers group is also inline */
	char kindbuf[64] = "";
	if (n->kind == NODE_KIND_CITUS_COORDINATOR)
	{
		strlcpy(kindbuf, "coordinator", sizeof(kindbuf));
	}
	else if (n->kind == NODE_KIND_CITUS_WORKER)
	{
		if (n->group != 0)
		{
			sformat(kindbuf, sizeof(kindbuf), "worker group %d", n->group);
		}
		else
		{
			strlcpy(kindbuf, "worker", sizeof(kindbuf));
		}
	}
	const char *kind = kindbuf;

#define ADD(k, v) do { props[pc].kw = (k); strlcpy(props[pc].val, (v), \
												   sizeof(props[0].val)); pc++; \
} while (0)
#define ADDF(k) do { props[pc].kw = (k); props[pc].val[0] = '\0'; pc++; } while (0)
	if (n->createDeferred && n->launchDeferred)
	{
		ADDF("create and launch deferred");
	}
	else if (n->createDeferred)
	{
		ADDF("create deferred");
	}
	else if (n->launchDeferred)
	{
		ADDF("launch deferred");
	}
	if (n->async)
	{
		ADDF("async");
	}
	if (n->noMonitor)
	{
		ADDF("no-monitor");
	}
	if (n->listen)
	{
		ADDF("listen");
	}
	if (n->candidatePriority != 50)
	{
		sformat(numbuf, sizeof(numbuf), "%d", n->candidatePriority);
		ADD("candidate-priority", numbuf);
	}
	if (n->region[0])
	{
		ADD("region", n->region);
	}
	if (!n->replicationQuorum)
	{
		ADD("replication-quorum", "false");
	}
	if (n->citusSecondary)
	{
		ADDF("citus-secondary");
	}
	if (n->citusClusterName[0])
	{
		ADD("citus-cluster-name", n->citusClusterName);
	}
	if (n->pgPort != 0)
	{
		sformat(numbuf, sizeof(numbuf), "%d", n->pgPort);
		ADD("port", numbuf);
	}
	if (n->ssl[0])
	{
		ADD("ssl", n->ssl);
	}
	if (n->auth[0])
	{
		ADD("auth", n->auth);
	}
	if (n->debianCluster[0])
	{
		ADD("debian-cluster", n->debianCluster);
	}

	/* volumes are always multi-line; handled separately below */

#undef ADD

	/* compute inline representation */
	char inline_props[512] = "";
	for (int i = 0; i < pc; i++)
	{
		strlcat(inline_props, " ", sizeof(inline_props));
		strlcat(inline_props, props[i].kw, sizeof(inline_props));
		if (props[i].val[0])
		{
			strlcat(inline_props, " ", sizeof(inline_props));
			strlcat(inline_props, props[i].val, sizeof(inline_props));
		}
	}

	int nameLen = strlen(n->name);
	int kindLen = kind[0] ? (int) strlen(kind) + 1 : 0;
	int lineLen = baseIndent + nameLen + kindLen + strlen(inline_props);

	if ((lineLen <= 72 || pc == 0) && n->volumeCount == 0)
	{
		/* fits on one line — flat syntax, no "node" keyword needed */
		fformat(out, "%*s%s", baseIndent, "", n->name);
		if (kind[0])
		{
			fformat(out, " %s", kind);
		}
		fformat(out, "%s\n", inline_props);
		return;
	}

	/* multi-line — use block syntax: node <name> { \n  kind\n  opt\n  opt\n } */
	int innerIndent = baseIndent + 4;
	fformat(out, "%*snode %s {\n", baseIndent, "", n->name);
	if (kind[0])
	{
		fformat(out, "%*s%s\n", innerIndent, "", kind);
	}

	for (int i = 0; i < pc; i++)
	{
		fformat(out, "%*s%s", innerIndent, "", props[i].kw);
		if (props[i].val[0])
		{
			fformat(out, " %s", props[i].val);
		}
		fformat(out, "\n");
	}
	for (int vi = 0; vi < n->volumeCount; vi++)
	{
		fformat(out, "%*svolume %s \"%s\"\n",
				innerIndent, "",
				n->volumes[vi].name, n->volumes[vi].path);
	}
	fformat(out, "%*s}\n", baseIndent, "");
}


static void
print_cluster(FILE *out, const TestSpec *spec)
{
	const TestCluster *cl = &spec->cluster;

	fformat(out, "cluster {\n");

	if (cl->withMonitor)
	{
		fformat(out, "    monitor\n");
	}

	if (cl->secondMonitorName[0])
	{
		fformat(out, "    monitor %s initially stopped\n", cl->secondMonitorName);
	}

	if (cl->image[0])
	{
		fformat(out, "    image \"%s\"\n", cl->image);
	}
	if (cl->ssl[0] && strcmp(cl->ssl, "self-signed") != 0)
	{
		fformat(out, "    ssl %s\n", cl->ssl);
	}
	if (cl->auth[0] && strcmp(cl->auth, "trust") != 0)
	{
		fformat(out, "    auth %s\n", cl->auth);
	}
	if (cl->extensionVersion[0])
	{
		fformat(out, "    extension-version %s\n", cl->extensionVersion);
	}

	for (int fi = 0; fi < cl->formationCount; fi++)
	{
		const TestFormation *f = &cl->formations[fi];

		if (strcmp(f->name, "default") == 0)
		{
			fformat(out, "    formation {\n");
		}
		else
		{
			fformat(out, "    formation %s {\n", f->name);
		}

		for (int ni = 0; ni < f->nodeCount; ni++)
		{
			print_node(out, &f->nodes[ni], 8);
		}

		fformat(out, "    }\n");
	}

	fformat(out, "}\n");
}


/* -----------------------------------------------------------------------
 * Step / command printing
 * ----------------------------------------------------------------------- */

/*
 * normalize_sql — collapse all whitespace runs (including embedded newlines
 * from a multi-line block in the source file) to single spaces and trim
 * leading/trailing whitespace.  This makes `pgaftest indent` idempotent: a
 * file that has already been indented produces the same output when indented
 * again.
 */
static void
normalize_sql(const char *in, char *out, int outlen)
{
	const char *p = in;
	char *q = out;
	char *lim = out + outlen - 1;
	bool ws = true;           /* suppress leading whitespace */

	while (*p && q < lim)
	{
		bool isws = (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r');
		if (isws)
		{
			if (!ws)
			{
				*q++ = ' ';
			}
			ws = true;
		}
		else
		{
			*q++ = *p;
			ws = false;
		}
		p++;
	}

	/* trim trailing space */
	if (q > out && q[-1] == ' ')
	{
		q--;
	}
	*q = '\0';
}


/*
 * SQL_CLAUSE_KEYWORDS — clause-level keywords that force a new output line.
 * Two-word keywords (ORDER BY, GROUP BY) must appear before any single-word
 * prefix (BY, ORDER, GROUP) to prevent partial matches.
 */
static const char *const SQL_CLAUSE_KEYWORDS[] = {
	"ORDER BY",
	"GROUP BY",
	"FROM",
	"WHERE",
	"HAVING",
	"LIMIT",
	"OFFSET",
	NULL
};

/*
 * match_clause_keyword — if a SQL clause keyword starts at p (case-
 * insensitive, followed by space, semicolon, or end-of-string), return its
 * length; otherwise return 0.
 */
static int
match_clause_keyword(const char *p)
{
	for (int i = 0; SQL_CLAUSE_KEYWORDS[i]; i++)
	{
		const char *kw = SQL_CLAUSE_KEYWORDS[i];
		int len = (int) strlen(kw);
		if (strncasecmp(p, kw, len) == 0)
		{
			char next = p[len];
			if (next == ' ' || next == '\0' || next == ';')
			{
				return len;
			}
		}
	}
	return 0;
}


/*
 * emit_segment — word-wrap a single normalized segment (no embedded newlines,
 * no leading/trailing spaces) onto one or more lines, each prefixed with
 * bodyIndent spaces and at most 79 characters wide total.
 */
static void
emit_segment(FILE *out, const char *seg, int bodyIndent)
{
	int avail = 79 - bodyIndent;
	if (avail < 20)
	{
		avail = 20;
	}

	const char *p = seg;

	while (*p)
	{
		if ((int) strlen(p) <= avail)
		{
			fformat(out, "%*s%s\n", bodyIndent, "", p);
			break;
		}

		/* Find the last space within avail columns. */
		const char *last_spc = NULL;
		for (int i = 0; i < avail && p[i]; i++)
		{
			if (p[i] == ' ')
			{
				last_spc = p + i;
			}
		}

		if (last_spc)
		{
			fformat(out, "%*s%.*s\n",
					bodyIndent, "", (int) (last_spc - p), p);
			p = last_spc + 1;   /* skip the space */
		}
		else
		{
			/* No space — hard break (avoids infinite loop on long tokens). */
			fformat(out, "%*s%.*s\n", bodyIndent, "", avail, p);
			p += avail;
		}
	}
}


/*
 * print_sql_body — emit normalized SQL with clause-keyword-aware line
 * splitting inside a  sql <svc> { … }  block.
 *
 * Algorithm:
 *   1. Normalize whitespace (fixes idempotency when the input was already
 *      a multi-line block from a previous indent pass).
 *   2. If the whole thing fits in avail columns, emit it on one line.
 *   3. Otherwise, scan for clause keywords (FROM, WHERE, ORDER BY, …).
 *      Each keyword starts a new output line; within each segment, apply
 *      word-wrap at 79 columns.
 */
static void
print_sql_body(FILE *out, const char *raw_sql, int bodyIndent)
{
	char norm[8192];
	normalize_sql(raw_sql, norm, sizeof(norm));

	int avail = 79 - bodyIndent;
	if (avail < 20)
	{
		avail = 20;
	}

	/* Short enough to fit on one line? */
	if ((int) strlen(norm) <= avail)
	{
		fformat(out, "%*s%s\n", bodyIndent, "", norm);
		return;
	}

	/*
	 * Scan for clause-keyword split points.  A keyword is a split point when
	 * it is preceded by a space (word boundary in the normalized string).
	 * We accumulate characters into a segment and flush when we hit a keyword.
	 */
	const char *p = norm;
	const char *seg_start = norm;

	while (*p)
	{
		if (p > norm && p[-1] == ' ')
		{
			int kwlen = match_clause_keyword(p);
			if (kwlen)
			{
				/* Flush the segment up to (but not including) the space
				 * that preceded this keyword. */
				int seglen = (int) ((p - 1) - seg_start);
				if (seglen > 0)
				{
					char seg[8192];
					memcpy(seg, seg_start, (size_t) seglen); /* IGNORE-BANNED */
					seg[seglen] = '\0';
					emit_segment(out, seg, bodyIndent);
				}
				seg_start = p;   /* keyword begins the next segment */
			}
		}
		p++;
	}

	/* Flush the final segment. */
	if (*seg_start)
	{
		emit_segment(out, seg_start, bodyIndent);
	}
}


static void
print_step(FILE *out, const TestStep *step, bool named)
{
	(void) named;
	for (const TestCmd *cmd = step->commands; cmd; cmd = cmd->next)
	{
		print_cmd(out, cmd, 4);
	}
}


static void
print_cmd(FILE *out, const TestCmd *cmd, int indent)
{
	switch (cmd->kind)
	{
		case CMD_EXEC:
		{
			fformat(out, "%*sexec %s  %s\n", indent, "", cmd->service, cmd->args);
			break;
		}

		case CMD_EXEC_FAILS:
		{
			fformat(out, "%*sexec-fails %s  %s\n", indent, "",
					cmd->service, cmd->args);
			break;
		}

		case CMD_RUN:
		{
			fformat(out, "%*srun %s  %s\n", indent, "", cmd->service, cmd->args);
			break;
		}

		case CMD_WAIT_STATE:
		case CMD_ASSERT_STATE:
		case CMD_ASSERT_ASSIGNED:
		case CMD_WAIT_MULTI:
		{
			print_cmd_wait(out, cmd, indent);
			break;
		}

		case CMD_WAIT_STATES:
		{
			/* wait until s1, s2 [in group N] timeout Ns */
			fformat(out, "%*swait until", indent, "");
			for (int i = 0; i < cmd->waitStateCount; i++)
			{
				if (i > 0)
				{
					fformat(out, ",");
				}
				fformat(out, " %s", cmd->waitStates[i]);
			}
			if (cmd->waitGroupCount > 0)
			{
				for (int g = 0; g < cmd->waitGroupCount; g++)
				{
					fformat(out, " in group %d", cmd->waitGroups[g]);
				}
			}
			fformat(out, "  timeout %ds\n", cmd->timeoutSeconds);
			break;
		}

		case CMD_WAIT_STOPPED:
		{
			fformat(out, "%*swait until %s stopped  timeout %ds\n",
					indent, "", cmd->service, cmd->timeoutSeconds);
			break;
		}

		case CMD_PROMOTE:
		{
			fformat(out, "%*spromote", indent, "");
			for (int i = 0; i < cmd->promoteCount; i++)
			{
				if (i > 0)
				{
					fformat(out, ",");
				}
				fformat(out, " %s", cmd->promoteNodes[i]);
			}
			fformat(out, "\n");
			break;
		}

		case CMD_SQL:
		{
			/*
			 * Normalise the SQL first (collapses embedded newlines from a
			 * previous multi-line block), then check if the whole thing fits
			 * on one line: "    sql <svc> { <sql> }".
			 * If not, open a block and emit clause-keyword-split SQL.
			 */
			char norm[8192];
			normalize_sql(cmd->args, norm, sizeof(norm));

			int oneliner_len = indent +
							   4 + /* "sql " */
							   (int) strlen(cmd->service) +
							   3 + /* " { " */
							   (int) strlen(norm) +
							   2;  /* " }" */

			if (oneliner_len <= 79)
			{
				fformat(out, "%*ssql %s { %s }\n",
						indent, "", cmd->service, norm);
			}
			else
			{
				fformat(out, "%*ssql %s {\n", indent, "", cmd->service);
				print_sql_body(out, norm, indent + 4);
				fformat(out, "%*s}\n", indent, "");
			}
			break;
		}

		case CMD_EXPECT:
		{
			/* If the stored value has newlines it was converted from
			 * { r1 } { r2 } tuple syntax by expand_tuple_expect — reverse it. */
			if (strchr(cmd->expected, '\n'))
			{
				fformat(out, "%*sexpect {", indent, "");
				const char *p = cmd->expected;
				while (*p)
				{
					const char *nl = strchr(p, '\n');
					int len = nl ? (int) (nl - p) : (int) strlen(p);
					fformat(out, " { %.*s }", len, p);
					p += len;
					if (*p == '\n')
					{
						p++;
					}
				}
				fformat(out, " }\n");
			}
			else
			{
				fformat(out, "%*sexpect { %s }\n", indent, "", cmd->expected);
			}
			break;
		}

		case CMD_EXPECT_ERROR:
		{
			fformat(out, "%*sexpect error %s\n", indent, "", cmd->state);
			break;
		}

		case CMD_NETWORK_OFF:
		{
			fformat(out, "%*snetwork disconnect %s\n", indent, "", cmd->service);
			break;
		}

		case CMD_NETWORK_ON:
		{
			fformat(out, "%*snetwork connect %s\n", indent, "", cmd->service);
			break;
		}

		case CMD_SLEEP:
		{
			fformat(out, "%*ssleep %ds\n", indent, "", cmd->timeoutSeconds);
			break;
		}

		case CMD_COMPOSE_DOWN:
		{
			fformat(out, "%*scompose down\n", indent, "");
			break;
		}

		case CMD_COMPOSE_START:
		{
			fformat(out, "%*scompose start %s\n", indent, "", cmd->service);
			break;
		}

		case CMD_COMPOSE_STOP:
		{
			fformat(out, "%*scompose stop %s\n", indent, "", cmd->service);
			break;
		}

		case CMD_COMPOSE_KILL:
		{
			fformat(out, "%*scompose kill %s\n", indent, "", cmd->service);
			break;
		}

		case CMD_PG_AUTOCTL:
		{
			fformat(out, "%*spg_autoctl %s\n", indent, "", cmd->args);
			break;
		}

		case CMD_STOP_POSTGRES:
		{
			fformat(out, "%*sstop postgres %s\n", indent, "", cmd->service);
			break;
		}

		case CMD_START_POSTGRES:
		{
			fformat(out, "%*sstart postgres %s\n", indent, "", cmd->service);
			break;
		}

		case CMD_STAYS_WHILE:
		{
			fformat(out, "%*sassert %s stays %s while {\n",
					indent, "", cmd->service, cmd->state);
			for (const TestCmd *bc = cmd->body; bc; bc = bc->next)
			{
				print_cmd(out, bc, indent + 4);
			}
			fformat(out, "%*s}\n", indent, "");
			break;
		}

		case CMD_COMPOSE_INJECT:
		{
			fformat(out, "%*scompose inject %s %s %s:%s\n",
					indent, "",
					cmd->expected,  /* image */
					cmd->args,      /* src path */
					cmd->service,   /* dst service */
					cmd->state);    /* dst path */
			break;
		}

		case CMD_SET_MONITOR:
		{
			fformat(out, "%*sset monitor %s\n", indent, "", cmd->service);
			break;
		}

		case CMD_FAILOVER:
		{
			/*
			 * service holds the formation name ("default" when omitted in
			 * the source); waitGroups[0] holds the group (0 when omitted).
			 * Reconstruct whichever of the four "perform failover" forms
			 * matches (see the perform_cmd grammar rule).
			 */
			bool hasFormation = strcmp(cmd->service, "default") != 0;
			bool hasGroup = cmd->waitGroups[0] != 0;

			fformat(out, "%*sperform failover", indent, "");
			if (hasFormation)
			{
				fformat(out, " in formation %s", cmd->service);
			}
			if (hasGroup)
			{
				fformat(out, " group %d", cmd->waitGroups[0]);
			}
			fformat(out, "\n");
			break;
		}

		case CMD_LOGS_CHECK:
		{
			/*
			 * logs <svc> [not] contains|matches "<pattern>"
			 * The logsNegate flag adds "not " and allowError picks the verb.
			 */
			const char *verb = cmd->allowError ? "matches" : "contains";
			fformat(out, "%*slogs %s %s%s \"%s\"\n",
					indent, "",
					cmd->service,
					cmd->logsNegate ? "not " : "",
					verb,
					cmd->args);
			break;
		}

		default:
		{
			fformat(out, "%*s(unknown cmd %d)\n", indent, "", cmd->kind);
			break;
		}
	}
}


/*
 * print_cmd_wait handles the various wait/assert forms, emitting multi-line
 * output for CMD_WAIT_MULTI and for single-node waits with pass-through.
 */
static void
print_cmd_wait(FILE *out, const TestCmd *cmd, int indent)
{
	if (cmd->kind == CMD_WAIT_MULTI)
	{
		/* first condition on same line, subsequent "and" lines indented */
		fformat(out, "%*swait until %s state is %s\n",
				indent, "", cmd->waitNodes[0], cmd->waitStates[0]);
		for (int i = 1; i < cmd->waitStateCount; i++)
		{
			fformat(out, "%*sand %s state is %s\n",
					indent + 4, "",
					cmd->waitNodes[i], cmd->waitStates[i]);
		}
		fformat(out, "%*s    timeout %ds\n", indent, "", cmd->timeoutSeconds);
		return;
	}

	/*
	 * CMD_ASSERT_ASSIGNED: grammar uses `=` not `is`.
	 * Distinguish wait-form (has timeout) from assert-form (no timeout).
	 */
	if (cmd->kind == CMD_ASSERT_ASSIGNED)
	{
		if (cmd->timeoutSeconds > 0)
		{
			fformat(out, "%*swait until %s assigned-state = %s  timeout %ds\n",
					indent, "", cmd->service, cmd->state, cmd->timeoutSeconds);
		}
		else
		{
			fformat(out, "%*sassert %s assigned-state = %s\n",
					indent, "", cmd->service, cmd->state);
		}
		return;
	}

	/* CMD_ASSERT_STATE with no timeout: instant check → canonical assert form */
	if (cmd->kind == CMD_ASSERT_STATE && cmd->timeoutSeconds == 0)
	{
		fformat(out, "%*sassert %s state = %s\n",
				indent, "", cmd->service, cmd->state);
		return;
	}

	/* CMD_ASSERT_STATE with timeout, and CMD_WAIT_STATE: polling wait */
	const char *op = "wait until";

	if (cmd->passThroughCount > 0)
	{
		/* multi-line: target state, then "passing through" line */
		fformat(out, "%*s%s %s state is %s\n",
				indent, "", op, cmd->service, cmd->state);
		fformat(out, "%*s    passing through", indent, "");
		for (int i = 0; i < cmd->passThroughCount; i++)
		{
			if (i > 0)
			{
				fformat(out, ",");
			}
			fformat(out, " %s", cmd->passThroughStates[i]);
		}
		fformat(out, "\n");
		fformat(out, "%*s    timeout %ds\n", indent, "", cmd->timeoutSeconds);
		return;
	}

	/* simple single-line */
	if (cmd->timeoutSeconds > 0)
	{
		fformat(out, "%*s%s %s state is %s  timeout %ds\n",
				indent, "", op, cmd->service, cmd->state, cmd->timeoutSeconds);
	}
	else
	{
		fformat(out, "%*s%s %s state is %s\n",
				indent, "", op, cmd->service, cmd->state);
	}
}
