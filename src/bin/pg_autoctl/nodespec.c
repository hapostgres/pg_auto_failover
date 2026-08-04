/*
 * src/bin/pg_autoctl/nodespec.c
 *   Parse, write, and converge a pg_autoctl_node.ini file.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/inotify.h>
#endif

#include "nodespec.h"
#include "ini_file.h"
#include "ini.h"
#include "log.h"
#include "pgsetup.h"
#include "string_utils.h"
#include "file_utils.h"    /* sformat */
#include "env_utils.h"
#include "cli_root.h"      /* pg_autoctl_program */
#include "runprogram.h"    /* Program, run_program, free_program */


/*
 * nodespec_read parses a pg_autoctl_node.ini file.
 *
 * File format:
 *
 * [node]
 * kind     = postgres        # postgres | monitor | coordinator | worker
 * hostname = node1
 * port     = 5432
 *
 * [postgresql]
 * pgdata = /var/lib/postgres/pgaf
 *
 * [monitor]
 * pguri = postgresql://autoctl_node@monitor/pg_auto_failover
 *
 * [formation]
 * name  = default
 * group = 0
 *
 * [settings]
 * candidate_priority   = 50
 * replication_quorum   = true
 *
 * [options]
 * ssl      = self-signed
 * auth     = trust
 * pg_hba_lan = true
 */
bool
nodespec_read(const char *path, NodeSpec *spec)
{
	char kindStr[NAMEDATALEN] = { 0 };
	char replicationQuorumStr[8] = { 0 };
	char pgHbaLanStr[8] = { 0 };
	char launchModeStr[16] = { 0 };
	char createDeferredStr[16] = { 0 };
	char noMonitorStr[8] = { 0 };
	char citusRoleStr[NAMEDATALEN] = { 0 };
	int port = 5432;
	int group = 0;
	int candidatePriority = 50;

	/*
	 * Provide string buffers for booleans and the kind enum — we parse them
	 * after read_ini_file() returns so we can give decent error messages.
	 */
	IniOption opts[] = {
		/* [node] */
		make_strbuf_option_default("node", "kind", NULL, true,
								   sizeof(kindStr), kindStr,
								   "postgres"),
		make_strbuf_option_default("node", "name", NULL, false,
								   sizeof(spec->name), spec->name,
								   ""),
		make_strbuf_option_default("node", "hostname", NULL, false,
								   sizeof(spec->hostname), spec->hostname,
								   ""),
		make_int_option_default("node", "port", NULL, false,
								&port, 5432),

		/* [postgresql] */
		make_strbuf_option("postgresql", "pgdata", NULL, true,
						   sizeof(spec->pgdata), spec->pgdata),

		/* [monitor] */
		make_strbuf_option_default("monitor", "pguri", NULL, false,
								   sizeof(spec->monitor_pguri),
								   spec->monitor_pguri, ""),
		make_strbuf_option_default("monitor", "no_monitor", NULL, false,
								   sizeof(noMonitorStr), noMonitorStr, "false"),
		make_int_option_default("monitor", "node_id", NULL, false,
								&spec->nodeId, 0),

		/* [formation] */
		make_strbuf_option_default("formation", "name", NULL, false,
								   sizeof(spec->formation), spec->formation,
								   "default"),
		make_int_option_default("formation", "group", NULL, false,
								&group, 0),

		/* [settings] — mutable */
		make_int_option_default("settings", "candidate_priority", NULL, false,
								&candidatePriority, 50),
		make_strbuf_option_default("settings", "replication_quorum", NULL, false,
								   sizeof(replicationQuorumStr),
								   replicationQuorumStr, "true"),
		make_strbuf_option_default("settings", "region", NULL, false,
								   sizeof(spec->region), spec->region,
								   ""),

		/* [options] — immutable, used only at create time */
		make_strbuf_option_default("options", "ssl", NULL, false,
								   sizeof(spec->ssl), spec->ssl,
								   "self-signed"),
		make_strbuf_option_default("options", "auth", NULL, false,
								   sizeof(spec->auth), spec->auth,
								   "trust"),
		make_strbuf_option_default("options", "pg_hba_lan", NULL, false,
								   sizeof(pgHbaLanStr), pgHbaLanStr,
								   "true"),

		/* [options] — debian_cluster: run pg_createcluster before create */
		make_strbuf_option_default("options", "debian_cluster", NULL, false,
								   sizeof(spec->debianCluster),
								   spec->debianCluster, ""),

		/* [ssl] — certificate paths for verify-ca / verify-full mode */
		make_strbuf_option_default("ssl", "ca_file", NULL, false,
								   sizeof(spec->ssl_ca_file),
								   spec->ssl_ca_file, ""),
		make_strbuf_option_default("ssl", "cert_file", NULL, false,
								   sizeof(spec->ssl_cert_file),
								   spec->ssl_cert_file, ""),
		make_strbuf_option_default("ssl", "key_file", NULL, false,
								   sizeof(spec->ssl_key_file),
								   spec->ssl_key_file, ""),

		/* [launch] — optional section; run=deferred delays node run */
		make_strbuf_option_default("launch", "run", NULL, false,
								   sizeof(launchModeStr), launchModeStr,
								   "immediate"),
		make_strbuf_option_default("launch", "create", NULL, false,
								   sizeof(createDeferredStr),
								   createDeferredStr, "immediate"),

		/* [pg_auto_failover] — monitor: password for autoctl_node role */
		make_strbuf_option_default("pg_auto_failover", "autoctl_node_password",
								   NULL, false,
								   sizeof(spec->autoctl_node_password),
								   spec->autoctl_node_password, ""),

		/* [replication] — postgres: password for pgautofailover_replicator */
		make_strbuf_option_default("replication", "password", NULL, false,
								   sizeof(spec->replication_password),
								   spec->replication_password, ""),

		/* [pg_auto_failover] — postgres: password for pgautofailover_monitor */
		make_strbuf_option_default("pg_auto_failover", "monitor_password",
								   NULL, false,
								   sizeof(spec->monitor_password),
								   spec->monitor_password, ""),

		/* [citus] — optional; present only for Citus secondary/read-replica nodes */
		make_strbuf_option_default("citus", "role", NULL, false,
								   sizeof(citusRoleStr), citusRoleStr, ""),
		make_strbuf_option_default("citus", "cluster_name", NULL, false,
								   sizeof(spec->citusClusterName),
								   spec->citusClusterName, ""),

		INI_OPTION_LAST
	};

	if (!read_ini_file(path, opts))
	{
		log_error("Failed to parse node spec file \"%s\"", path);
		return false;
	}

	/* resolve kind string → enum */
	if (strcmp(kindStr, "monitor") == 0)
	{
		spec->kind = NODE_KIND_UNKNOWN;   /* handled specially: no formation */
	}
	else if (strcmp(kindStr, "postgres") == 0)
	{
		spec->kind = NODE_KIND_STANDALONE;
	}
	else if (strcmp(kindStr, "coordinator") == 0)
	{
		spec->kind = NODE_KIND_CITUS_COORDINATOR;
	}
	else if (strcmp(kindStr, "worker") == 0)
	{
		spec->kind = NODE_KIND_CITUS_WORKER;
	}
	else if (strcmp(kindStr, "archiver") == 0)
	{
		spec->kind = NODE_KIND_ARCHIVER;
	}
	else
	{
		log_error("Unknown node kind \"%s\" in \"%s\"; "
				  "expected: monitor, postgres, coordinator, worker, archiver",
				  kindStr, path);
		return false;
	}

	spec->port = port;
	spec->group = group;
	spec->candidate_priority = candidatePriority;

	/* parse boolean strings */
	spec->replication_quorum =
		(strcmp(replicationQuorumStr, "true") == 0 ||
		 strcmp(replicationQuorumStr, "yes") == 0 ||
		 strcmp(replicationQuorumStr, "1") == 0);

	spec->pg_hba_lan =
		(strcmp(pgHbaLanStr, "true") == 0 ||
		 strcmp(pgHbaLanStr, "yes") == 0 ||
		 strcmp(pgHbaLanStr, "1") == 0);

	spec->launchDeferred = (strcmp(launchModeStr, "deferred") == 0);
	spec->createDeferred = (strcmp(createDeferredStr, "deferred") == 0);
	spec->noMonitor =
		(strcmp(noMonitorStr, "true") == 0 ||
		 strcmp(noMonitorStr, "yes") == 0 ||
		 strcmp(noMonitorStr, "1") == 0);

	spec->citusSecondary = (strcmp(citusRoleStr, "secondary") == 0);

	/*
	 * Second pass: enumerate [formation <name>] sections.
	 * The standard IniOption machinery can't handle variable-count sections,
	 * so we open the file again with ini_load directly.
	 */
	{
		char *fileContents = NULL;
		long fileSize = 0L;

		if (read_file(path, &fileContents, &fileSize))
		{
			ini_t *raw = ini_load(fileContents, NULL);
			free(fileContents);

			if (raw)
			{
				int nsec = ini_section_count(raw);
				spec->formationCount = 0;

				for (int si = 0; si < nsec; si++)
				{
					const char *sname = ini_section_name(raw, si);
					if (!sname)
					{
						continue;
					}
					if (strncmp(sname, "formation ", 10) != 0)
					{
						continue;
					}

					const char *fname = sname + 10;  /* skip "formation " */
					if (fname[0] == '\0')
					{
						continue;
					}

					if (spec->formationCount >= NODESPEC_MAX_FORMATIONS)
					{
						log_warn("nodespec: too many [formation <name>] sections "
								 "in \"%s\"; max %d", path, NODESPEC_MAX_FORMATIONS);
						break;
					}

					int fi = spec->formationCount++;
					strlcpy(spec->formationNames[fi], fname,
							sizeof(spec->formationNames[fi]));
					spec->formationNumSync[fi] = -1; /* default: don't override */

					/* optional: kind = citus (default pgsql) */
					int ki = ini_find_property(raw, si, "kind", 0);
					if (ki != INI_NOT_FOUND)
					{
						const char *kv = ini_property_value(raw, si, ki);
						if (kv && kv[0])
						{
							strlcpy(spec->formationKinds[fi], kv,
									sizeof(spec->formationKinds[fi]));
						}
						else
						{
							strlcpy(spec->formationKinds[fi], "pgsql",
									sizeof(spec->formationKinds[fi]));
						}
					}
					else
					{
						strlcpy(spec->formationKinds[fi], "pgsql",
								sizeof(spec->formationKinds[fi]));
					}

					/* optional: secondary = false disables failover secondaries */
					int si2 = ini_find_property(raw, si, "secondary", 0);
					if (si2 != INI_NOT_FOUND)
					{
						const char *sv = ini_property_value(raw, si, si2);
						if (sv && (strcmp(sv, "false") == 0 ||
								   strcmp(sv, "no") == 0 ||
								   strcmp(sv, "0") == 0))
						{
							spec->formationDisableSecondary[fi] = true;
						}
					}

					/* optional: num_sync_standbys = N */
					int ni = ini_find_property(raw, si, "num_sync_standbys", 0);
					if (ni != INI_NOT_FOUND)
					{
						const char *nv = ini_property_value(raw, si, ni);
						if (nv && nv[0])
						{
							int n = 0;
							if (stringToInt(nv, &n))
							{
								spec->formationNumSync[fi] = n;
							}
						}
					}
				}
				ini_destroy(raw);
			}
		}
	}

	/* validate: non-monitor nodes need a monitor URI unless no_monitor=true */
	if (spec->kind != NODE_KIND_UNKNOWN &&
		IS_EMPTY_STRING_BUFFER(spec->monitor_pguri) &&
		!spec->noMonitor)
	{
		log_error("Node kind \"%s\" requires [monitor] pguri in \"%s\"",
				  kindStr, path);
		return false;
	}

	return true;
}


/*
 * nodespec_write serialises a NodeSpec as a pg_autoctl_node.ini file.
 */
bool
nodespec_write(const NodeSpec *spec, FILE *out)
{
	const char *kindStr;

	switch (spec->kind)
	{
		case NODE_KIND_UNKNOWN:
		{
			kindStr = "monitor";
			break;
		}

		case NODE_KIND_STANDALONE:
		{
			kindStr = "postgres";
			break;
		}

		case NODE_KIND_CITUS_COORDINATOR:
		{
			kindStr = "coordinator";
			break;
		}

		case NODE_KIND_CITUS_WORKER:
		{
			kindStr = "worker";
			break;
		}

		default:
		{
			kindStr = "postgres";
			break;
		}
	}

	fformat(out,
			"[node]\n"
			"kind     = %s\n",
			kindStr);

	if (!IS_EMPTY_STRING_BUFFER(spec->name))
	{
		fformat(out, "name     = %s\n", spec->name);
	}

	fformat(out,
			"hostname = %s\n"
			"port     = %d\n"
			"\n"
			"[postgresql]\n"
			"pgdata = %s\n"
			"\n",
			spec->hostname,
			spec->port,
			spec->pgdata);

	if (spec->kind != NODE_KIND_UNKNOWN)
	{
		if (spec->noMonitor)
		{
			fformat(out,
					"[monitor]\n"
					"no_monitor = true\n"
					"\n");
		}
		else
		{
			fformat(out,
					"[monitor]\n"
					"pguri = %s\n"
					"\n",
					spec->monitor_pguri);
		}

		fformat(out,
				"[formation]\n"
				"name  = %s\n"
				"group = %d\n"
				"\n",
				spec->formation,
				spec->group);
	}

	fformat(out,
			"[settings]\n"
			"candidate_priority = %d\n"
			"replication_quorum = %s\n"
			"\n"
			"[options]\n"
			"ssl        = %s\n"
			"auth       = %s\n"
			"pg_hba_lan = %s\n",
			spec->candidate_priority,
			spec->replication_quorum ? "true" : "false",
			spec->ssl,
			spec->auth,
			spec->pg_hba_lan ? "true" : "false");

	if (spec->debianCluster[0])
	{
		fformat(out, "debian_cluster = %s\n", spec->debianCluster);
	}

	/* only emit [launch] when deferred — omitting the section means immediate */
	if (spec->launchDeferred || spec->createDeferred)
	{
		fformat(out, "\n[launch]\n");
		if (spec->createDeferred)
		{
			fformat(out, "create = deferred\n");
		}
		if (spec->launchDeferred)
		{
			fformat(out, "run = deferred\n");
		}
	}

	/* [formation <name>] sections — monitor kind only */
	for (int fi = 0; fi < spec->formationCount; fi++)
	{
		fformat(out, "\n[formation %s]\n", spec->formationNames[fi]);
		if (spec->formationKinds[fi][0] &&
			strcmp(spec->formationKinds[fi], "pgsql") != 0)
		{
			fformat(out, "kind = %s\n", spec->formationKinds[fi]);
		}
		if (spec->formationDisableSecondary[fi])
		{
			fformat(out, "secondary = false\n");
		}
		if (spec->formationNumSync[fi] >= 0)
		{
			fformat(out, "num_sync_standbys = %d\n", spec->formationNumSync[fi]);
		}
	}

	return true;
}


/*
 * nodespec_create_argv builds an argv[] array for:
 *
 *   pg_autoctl create <kind> --pgdata <p> --hostname <h> --pgport <n>
 *                            --monitor <uri> [--formation <f>] [--group <g>]
 *                            [--ssl-self-signed | --no-ssl]
 *                            [--auth <m>] [--pg-hba-lan] --run
 *
 * Returns the number of entries written (terminating NULL not counted).
 * args[] must have room for at least 40 pointers.
 *
 * String values are pointers into *spec — the caller must keep spec alive.
 */
int
nodespec_create_argv(const NodeSpec *spec,
					 const char *pg_autoctl_path,
					 char **args, int args_size)
{
	int i = 0;

#define PUSH(v) do { \
		if (i >= args_size - 1) { \
			log_error("nodespec_create_argv: args[] overflow"); \
			return -1; \
		} \
		args[i++] = (char *) (v); \
} while (0)

	PUSH(pg_autoctl_path);
	PUSH("create");

	switch (spec->kind)
	{
		case NODE_KIND_UNKNOWN:
		{
			PUSH("monitor");
			break;
		}

		case NODE_KIND_STANDALONE:
		{
			PUSH("postgres");
			break;
		}

		case NODE_KIND_CITUS_COORDINATOR:
		{
			PUSH("coordinator");
			break;
		}

		case NODE_KIND_CITUS_WORKER:
		{
			PUSH("worker");
			break;
		}

		case NODE_KIND_ARCHIVER:
		{
			PUSH("archiver");
			break;
		}

		default:
		{
			PUSH("postgres");
			break;
		}
	}

	/*
	 * An archiver's own getopts (cli_create_archiver_getopts,
	 * cli_create_node.c) is deliberately minimal -- no --pgport, --ssl-*,
	 * --auth, --pg-hba-lan, --candidate-priority, ... -- none of which
	 * apply to a node with no real PostgresSetup (see haspgdata's own
	 * design comment, pgautofailover.sql). Building its own argv here
	 * rather than falling through into the rest of this function (which
	 * assumes every kind accepts the full postgres flag set) avoids
	 * "unrecognized option" failures on every one of those.
	 */
	if (spec->kind == NODE_KIND_ARCHIVER)
	{
		PUSH("--pgdata");
		PUSH(spec->pgdata);

		if (!IS_EMPTY_STRING_BUFFER(spec->name))
		{
			PUSH("--name");
			PUSH(spec->name);
		}

		if (!IS_EMPTY_STRING_BUFFER(spec->hostname))
		{
			PUSH("--hostname");
			PUSH(spec->hostname);
		}

		PUSH("--monitor");
		PUSH(spec->monitor_pguri);

		if (!IS_EMPTY_STRING_BUFFER(spec->formation) &&
			strcmp(spec->formation, "default") != 0)
		{
			PUSH("--formation");
			PUSH(spec->formation);
		}

		PUSH("--run");

		args[i] = NULL;

		return i;
	}

	PUSH("--pgdata");
	PUSH(spec->pgdata);

	if (!IS_EMPTY_STRING_BUFFER(spec->name))
	{
		PUSH("--name");
		PUSH(spec->name);
	}

	if (!IS_EMPTY_STRING_BUFFER(spec->hostname))
	{
		PUSH("--hostname");
		PUSH(spec->hostname);
	}

	if (spec->port != 5432)
	{
		/* static buffer — safe because spec is long-lived */
		static char portbuf[16];
		sformat(portbuf, sizeof(portbuf), "%d", spec->port);
		PUSH("--pgport");
		PUSH(portbuf);
	}

	if (spec->kind != NODE_KIND_UNKNOWN)
	{
		if (spec->noMonitor)
		{
			PUSH("--disable-monitor");
			if (spec->nodeId > 0)
			{
				static char nodeIdBuf[16];
				sformat(nodeIdBuf, sizeof(nodeIdBuf), "%d", spec->nodeId);
				PUSH("--node-id");
				PUSH(nodeIdBuf);
			}
		}
		else
		{
			PUSH("--monitor");
			PUSH(spec->monitor_pguri);
		}

		if (!IS_EMPTY_STRING_BUFFER(spec->formation) &&
			strcmp(spec->formation, "default") != 0)
		{
			PUSH("--formation");
			PUSH(spec->formation);
		}

		if (spec->kind == NODE_KIND_CITUS_WORKER && spec->group > 0)
		{
			static char groupbuf[16];
			sformat(groupbuf, sizeof(groupbuf), "%d", spec->group);
			PUSH("--group");
			PUSH(groupbuf);
		}
	}

	/* SSL */
	if (strcmp(spec->ssl, "self-signed") == 0)
	{
		PUSH("--ssl-self-signed");
	}
	else if (strcmp(spec->ssl, "off") == 0)
	{
		PUSH("--no-ssl");
	}
	else if (!IS_EMPTY_STRING_BUFFER(spec->ssl_ca_file))
	{
		/* verify-ca / verify-full: pass the cert paths explicitly */
		PUSH("--ssl-ca-file");
		PUSH(spec->ssl_ca_file);
		PUSH("--server-cert");
		PUSH(spec->ssl_cert_file);
		PUSH("--server-key");
		PUSH(spec->ssl_key_file);
		PUSH("--ssl-mode");
		PUSH(spec->ssl);
	}

	/* auth */
	if (!IS_EMPTY_STRING_BUFFER(spec->auth))
	{
		PUSH("--auth");
		PUSH(spec->auth);
	}

	if (spec->pg_hba_lan && spec->kind != NODE_KIND_UNKNOWN)
	{
		PUSH("--pg-hba-lan");
	}

	/* passwords */
	if (spec->kind == NODE_KIND_UNKNOWN &&
		!IS_EMPTY_STRING_BUFFER(spec->autoctl_node_password))
	{
		PUSH("--autoctl-node-password");
		PUSH(spec->autoctl_node_password);
	}

	/*
	 * Non-default formations are created by a post-init child in cli_node_run
	 * after pg_autoctl run starts postgres, so that each formation's kind and
	 * secondary flag can be applied correctly.  Nothing to add to the argv here.
	 */

	if (spec->kind != NODE_KIND_UNKNOWN)
	{
		if (!IS_EMPTY_STRING_BUFFER(spec->monitor_password))
		{
			PUSH("--monitor-password");
			PUSH(spec->monitor_password);
		}
		if (!IS_EMPTY_STRING_BUFFER(spec->replication_password))
		{
			PUSH("--replication-password");
			PUSH(spec->replication_password);
		}

		/* candidate priority and replication quorum (non-default values) */
		if (spec->candidate_priority != 50)
		{
			static char pribuf[16];
			sformat(pribuf, sizeof(pribuf), "%d", spec->candidate_priority);
			PUSH("--candidate-priority");
			PUSH(pribuf);
		}
		if (!spec->replication_quorum)
		{
			PUSH("--replication-quorum");
			PUSH("false");
		}

		/* region label (omit when empty — monitor defaults to "default") */
		if (!IS_EMPTY_STRING_BUFFER(spec->region))
		{
			PUSH("--region");
			PUSH(spec->region);
		}

		/* Citus secondary/read-replica cluster settings */
		if (spec->citusSecondary)
		{
			PUSH("--citus-secondary");
		}
		if (!IS_EMPTY_STRING_BUFFER(spec->citusClusterName))
		{
			PUSH("--citus-cluster");
			PUSH(spec->citusClusterName);
		}
	}

	PUSH("--run");

	args[i] = NULL;

#undef PUSH
	return i;
}


/*
 * nodespec_write_to_path writes the spec to the given filesystem path,
 * replacing the file in-place.  Used by pg_autoctl node start to clear the
 * [launch] deferred flag so the waiting pg_autoctl node run can proceed.
 */
bool
nodespec_write_to_path(const NodeSpec *spec, const char *path)
{
	FILE *f = fopen(path, "w"); /* IGNORE-BANNED */
	if (!f)
	{
		log_error("Cannot open \"%s\" for writing: %m", path);
		return false;
	}
	bool ok = nodespec_write(spec, f);
	fclose(f); /* IGNORE-BANNED */
	return ok;
}


/*
 * nodespec_apply compares new_spec against old_spec and applies any changes
 * to the mutable fields by calling into the keeper / monitor APIs.
 *
 * Currently mutable fields:
 *   - candidate_priority   → monitor_set_node_candidate_priority()
 *   - replication_quorum   → monitor_set_node_replication_quorum()
 *
 * The [launch] mode field is handled separately by pg_autoctl node start.
 * Applying a spec with run=deferred to an already-started node is a
 * non-fatal warning (ignored).
 *
 * Immutable fields (kind, pgdata, ssl, auth, pg_hba_lan) are not checked here.
 */
bool
nodespec_apply(const NodeSpec *new_spec, const NodeSpec *old_spec)
{
	bool changed = false;

	if (new_spec->candidate_priority != old_spec->candidate_priority)
	{
		char prio[16];
		sformat(prio, sizeof(prio), "%d", new_spec->candidate_priority);

		Program prog = run_program(pg_autoctl_program,
								   "set", "node", "candidate-priority",
								   "--pgdata", new_spec->pgdata,
								   prio, NULL);

		if (prog.returnCode != 0)
		{
			log_warn("nodespec_apply: set candidate-priority %s failed (rc=%d)",
					 prio, prog.returnCode);
			if (prog.stdOut)
			{
				log_warn("%s", prog.stdOut);
			}
		}
		else
		{
			log_info("nodespec: applied candidate_priority = %d",
					 new_spec->candidate_priority);
			changed = true;
		}
		free_program(&prog);
	}

	if (new_spec->replication_quorum != old_spec->replication_quorum)
	{
		const char *quorum = new_spec->replication_quorum ? "true" : "false";

		Program prog = run_program(pg_autoctl_program,
								   "set", "node", "replication-quorum",
								   "--pgdata", new_spec->pgdata,
								   quorum, NULL);

		if (prog.returnCode != 0)
		{
			log_warn("nodespec_apply: set replication-quorum %s failed (rc=%d)",
					 quorum, prog.returnCode);
			if (prog.stdOut)
			{
				log_warn("%s", prog.stdOut);
			}
		}
		else
		{
			log_info("nodespec: applied replication_quorum = %s", quorum);
			changed = true;
		}
		free_program(&prog);
	}

	if (strcmp(new_spec->region, old_spec->region) != 0)
	{
		Program prog = run_program(pg_autoctl_program,
								   "set", "node", "region",
								   "--pgdata", new_spec->pgdata,
								   new_spec->region, NULL);

		if (prog.returnCode != 0)
		{
			log_warn("nodespec_apply: set region %s failed (rc=%d)",
					 new_spec->region, prog.returnCode);
			if (prog.stdOut)
			{
				log_warn("%s", prog.stdOut);
			}
		}
		else
		{
			log_info("nodespec: applied region = %s", new_spec->region);
			changed = true;
		}
		free_program(&prog);
	}

	/* immediate → deferred on an already-started node: non-fatal, ignored */
	if (!old_spec->launchDeferred && new_spec->launchDeferred)
	{
		log_warn("nodespec_apply: ignoring attempt to set launch=deferred "
				 "on a node that is already running");
	}

	/*
	 * For monitor nodes, apply formation-level changes.
	 *
	 * New [formation <name>] sections → pg_autoctl create formation.
	 * Changed secondary setting     → pg_autoctl enable/disable secondary.
	 */
	if (new_spec->kind == NODE_KIND_UNKNOWN)
	{
		for (int fi = 0; fi < new_spec->formationCount; fi++)
		{
			const char *fname = new_spec->formationNames[fi];
			const char *fkind = new_spec->formationKinds[fi][0]
								? new_spec->formationKinds[fi] : "pgsql";
			bool newDisabled = new_spec->formationDisableSecondary[fi];

			/* look for this formation in the previous spec */
			int oi = -1;
			for (int k = 0; k < old_spec->formationCount; k++)
			{
				if (strcmp(old_spec->formationNames[k], fname) == 0)
				{
					oi = k;
					break;
				}
			}

			if (oi < 0)
			{
				/* formation is new: create it, passing --number-sync-standbys
				 * directly so there is no separate set call needed. */
				int newNs = new_spec->formationNumSync[fi];
				static char nsbuf[16];
				Program prog;

				if (newDisabled && newNs >= 0)
				{
					sformat(nsbuf, sizeof(nsbuf), "%d", newNs);
					prog = run_program(pg_autoctl_program,
									   "create", "formation",
									   "--pgdata", new_spec->pgdata,
									   "--formation", fname,
									   "--kind", fkind,
									   "--disable-secondary",
									   "--number-sync-standbys", nsbuf,
									   NULL);
				}
				else if (newDisabled)
				{
					prog = run_program(pg_autoctl_program,
									   "create", "formation",
									   "--pgdata", new_spec->pgdata,
									   "--formation", fname,
									   "--kind", fkind,
									   "--disable-secondary",
									   NULL);
				}
				else if (newNs >= 0)
				{
					sformat(nsbuf, sizeof(nsbuf), "%d", newNs);
					prog = run_program(pg_autoctl_program,
									   "create", "formation",
									   "--pgdata", new_spec->pgdata,
									   "--formation", fname,
									   "--kind", fkind,
									   "--number-sync-standbys", nsbuf,
									   NULL);
				}
				else
				{
					prog = run_program(pg_autoctl_program,
									   "create", "formation",
									   "--pgdata", new_spec->pgdata,
									   "--formation", fname,
									   "--kind", fkind,
									   NULL);
				}

				if (prog.returnCode != 0)
				{
					log_warn("nodespec_apply: create formation \"%s\" failed (rc=%d)",
							 fname, prog.returnCode);
					if (prog.stdOut)
					{
						log_warn("%s", prog.stdOut);
					}
				}
				else
				{
					log_info("nodespec: created formation \"%s\" (kind=%s%s)",
							 fname, fkind,
							 newNs >= 0 ? ", num_sync_standbys set" : "");
					changed = true;
				}
				free_program(&prog);
			}
			else if (old_spec->formationDisableSecondary[oi] != newDisabled)
			{
				/* secondary setting changed: enable or disable */
				const char *verb = newDisabled ? "disable" : "enable";

				Program prog = run_program(pg_autoctl_program,
										   verb, "secondary",
										   "--pgdata", new_spec->pgdata,
										   "--formation", fname,
										   NULL);

				if (prog.returnCode != 0)
				{
					log_warn("nodespec_apply: %s secondary for \"%s\" failed (rc=%d)",
							 verb, fname, prog.returnCode);
					if (prog.stdOut)
					{
						log_warn("%s", prog.stdOut);
					}
				}
				else
				{
					log_info("nodespec: %sd secondary for formation \"%s\"",
							 verb, fname);
					changed = true;
				}
				free_program(&prog);
			}

			/*
			 * num_sync_standbys for existing formations (including "default"):
			 * the formation already exists so use the set command.
			 * Skip when oi < 0 (new formation) — handled above via create.
			 */
			int newNs = new_spec->formationNumSync[fi];
			int oldNs = (oi >= 0) ? old_spec->formationNumSync[oi] : -1;
			if (oi >= 0 && newNs >= 0 && newNs != oldNs)
			{
				static char nsbuf[16];
				sformat(nsbuf, sizeof(nsbuf), "%d", newNs);
				Program nsp = run_program(pg_autoctl_program,
										  "set", "formation",
										  "number-sync-standbys",
										  nsbuf,
										  "--pgdata", new_spec->pgdata,
										  "--formation", fname,
										  NULL);
				if (nsp.returnCode != 0)
				{
					log_warn("nodespec_apply: set number-sync-standbys %d "
							 "for \"%s\" failed (rc=%d)",
							 newNs, fname, nsp.returnCode);
					if (nsp.stdOut)
					{
						log_warn("%s", nsp.stdOut);
					}
				}
				else
				{
					log_info("nodespec: set formation \"%s\" number-sync-standbys = %d",
							 fname, newNs);
					changed = true;
				}
				free_program(&nsp);
			}
		}
	}

	if (!changed)
	{
		log_debug("nodespec_apply: no mutable fields changed");
	}

	return true;
}


/* -----------------------------------------------------------------------
 * Watcher
 * ----------------------------------------------------------------------- */

/*
 * nodespec_file_crc computes the CRC32C of the current content of *path
 * into *crc.  Returns false (leaving *crc untouched) if the file can't be
 * read right now — a transient condition while it's being rewritten, not
 * treated as a change.
 */
static bool
nodespec_file_crc(const char *path, pg_crc32c *crc)
{
	char *contents = NULL;
	long fileSize = 0;

	if (!read_file(path, &contents, &fileSize))
	{
		return false;
	}

	pg_crc32c newCrc;
	INIT_CRC32C(newCrc);
	COMP_CRC32C(newCrc, contents, fileSize);
	FIN_CRC32C(newCrc);

	free(contents);

	*crc = newCrc;
	return true;
}


/*
 * nodespec_watcher_init sets up file watching for *path.
 * On Linux, inotify is also armed as a low-latency hint; see
 * nodespec_watcher_check for why it is never the sole source of truth.
 */
bool
nodespec_watcher_init(NodeSpecWatcher *w, const char *path)
{
	memset(w, 0, sizeof(*w));
	strlcpy(w->path, path, sizeof(w->path));

	w->last_checked = time(NULL);

#ifdef __linux__
	w->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);

	if (w->inotify_fd >= 0)
	{
		w->watch_fd = inotify_add_watch(w->inotify_fd, path,
										IN_CLOSE_WRITE | IN_MOVED_TO);
		if (w->watch_fd < 0)
		{
			log_debug("nodespec_watcher_init: inotify_add_watch(\"%s\"): %m; "
					  "relying on the CRC hash poll instead", path);
			close(w->inotify_fd);
			w->inotify_fd = -1;
		}
		else
		{
			log_info("nodespec: watching \"%s\" via inotify "
					 "(plus a %ds CRC hash poll as a reliability net)",
					 path, NODESPEC_HASH_POLL_INTERVAL_SECS);
		}
	}
	else
	{
		log_debug("nodespec_watcher_init: inotify_init1: %m; "
				  "relying on the CRC hash poll instead");
		w->inotify_fd = -1;
		w->watch_fd = -1;
	}
#endif

	/* record initial CRC so we don't fire on the very first check */
	if (!nodespec_file_crc(path, &w->last_crc))
	{
		INIT_CRC32C(w->last_crc);
		FIN_CRC32C(w->last_crc);
	}

	w->active = true;
	return true;
}


/*
 * nodespec_watcher_check is called from the supervisor's 100 ms tick.
 *
 * Change detection is always CRC32C-hash based: inotify (when available) is
 * only used to check sooner than the next scheduled hash poll, never as the
 * sole signal that something changed. inotify has been observed to silently
 * miss host-originated writes to a bind-mounted file under some
 * containerized/virtualized filesystems (e.g. Docker Desktop for macOS's
 * virtiofs: the write's content lands in the container right away, but no
 * IN_CLOSE_WRITE event is ever raised) — the hash poll guarantees a change
 * is still picked up within NODESPEC_HASH_POLL_INTERVAL_SECS regardless.
 *
 * Returns true if the file changed and nodespec_apply() was attempted.
 */
bool
nodespec_watcher_check(NodeSpecWatcher *w, const NodeSpec *current)
{
	bool inotify_hint = false;

	if (!w->active)
	{
		return false;
	}

#ifdef __linux__
	if (w->inotify_fd >= 0)
	{
		/*
		 * Drain all pending inotify events.  This only decides whether to
		 * hash-check *now* instead of waiting for the next poll interval —
		 * see the function comment for why it can't be trusted alone.
		 */
		char buf[sizeof(struct inotify_event) + NAME_MAX + 1];
		ssize_t n;

		while ((n = read(w->inotify_fd, buf, sizeof(buf))) > 0)
		{
			inotify_hint = true;
		}

		if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
		{
			log_warn("nodespec watcher: inotify read error: %m; "
					 "continuing with the CRC hash poll");
			close(w->inotify_fd);
			w->inotify_fd = -1;
			w->watch_fd = -1;
		}
	}
#endif

	time_t now = time(NULL);

	if (!inotify_hint &&
		now - w->last_checked < NODESPEC_HASH_POLL_INTERVAL_SECS)
	{
		return false;
	}

	w->last_checked = now;

	pg_crc32c newCrc;
	if (!nodespec_file_crc(w->path, &newCrc))
	{
		/* file transiently unreadable; try again on the next poll */
		return false;
	}

	if (EQ_CRC32C(newCrc, w->last_crc))
	{
		return false;
	}

	w->last_crc = newCrc;

	/* File changed — re-parse and apply */
	log_info("nodespec: \"%s\" changed, re-reading and converging", w->path);

	NodeSpec new_spec = { 0 };
	if (!nodespec_read(w->path, &new_spec))
	{
		log_warn("nodespec: failed to parse updated file \"%s\"; "
				 "keeping current configuration", w->path);
		return false;
	}

	/* Warn about immutable field changes rather than silently ignoring them */
	if (new_spec.kind != current->kind)
	{
		log_warn("nodespec: 'kind' changed in \"%s\" but cannot be applied "
				 "to a running node — restart required", w->path);
	}

	if (strcmp(new_spec.pgdata, current->pgdata) != 0)
	{
		log_warn("nodespec: 'pgdata' changed in \"%s\" but cannot be applied "
				 "to a running node — restart required", w->path);
	}

	(void) nodespec_apply(&new_spec, current);
	return true;
}


/*
 * nodespec_watcher_close releases inotify resources.
 */
void
nodespec_watcher_close(NodeSpecWatcher *w)
{
#ifdef __linux__
	if (w->inotify_fd >= 0)
	{
		close(w->inotify_fd);
		w->inotify_fd = -1;
		w->watch_fd = -1;
	}
#endif
	w->active = false;
}
