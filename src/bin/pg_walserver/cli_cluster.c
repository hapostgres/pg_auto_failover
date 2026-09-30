/*
 * src/bin/pg_walserver/cli_cluster.c
 *   See cli_cluster.h. "pg_walserver cluster register <name>", "pg_
 *   walserver cluster drop <name>", "pg_walserver cluster list", "pg_
 *   walserver cluster set-upstream <name>": registering, dropping,
 *   listing, and re-pointing the clusters (routes) one pg_walserver
 *   instance archives -- split out of what used to be "pg_walserver
 *   setup" (now cli_setup.c, narrowed to configuring pg_walserver itself:
 *   --pgdata's own port/TLS/auth-timeout defaults, nothing about any one
 *   cluster). "setup" configures the server; these configure what it
 *   serves.
 *
 *   "cluster register <name>", in order, stopping at the first failure:
 *
 *     1. resolve path/upstream (cli_upstream.c: --path/--pguri/--host/
 *        --port/--user, or <name> looked up in the config file when it
 *        already has a matching section);
 *     2. write (or validate) the config file's own section for <name>,
 *        refusing a route key that already exists with a *different*
 *        path/upstream unless --force -- the same overwrite-safety
 *        principle as cli_fetch_systemid.c's own systemid check, applied
 *        one layer up; the embedded receivewal worker is opted into by
 *        *default* now (an explicit "receivewal = pull" is written into the
 *        route's own section, routes.h, unless --no-receivewal / --receivewal
 *        none says otherwise), opting the route into it the next time
 *        "serve" starts -- "cluster register" itself never starts or
 *        touches that receivewal worker, it only records the intent.
 *        Writing the property explicitly (rather than changing what an
 *        *absent* "receivewal" property under a hand-edited config file
 *        means, which stays "off", unchanged in routes.c/routes.h) is a
 *        deliberate choice: anyone reading the config file by hand sees
 *        exactly what "cluster register" decided, with no implicit-
 *        default surprise to remember;
 *     3. fetch the system identifier (cli_fetch_systemid.c) -- this
 *        connection (pgctl_identify_system(), a real replication-mode
 *        IDENTIFY_SYSTEM) is also this step's own role-permission check:
 *        Postgres refuses a replication-mode connection for a role lacking
 *        REPLICATION at the *backend* level, independent of HBA, so a
 *        misconfigured role fails here with a clear message instead of a
 *        cryptic pg_basebackup/pg_receivewal error later;
 *     4. once every route in the config file is accounted for, create a
 *        self-signed certificate for <pgdata> if none exists yet
 *        (pg_create_self_signed_cert(), the exact function `pg_autoctl
 *        create ... --ssl-self-signed` already uses), in either of two
 *        cases: there is now more than one route (TLS becomes mandatory
 *        the moment dbname-based routing stops being reliable for a real
 *        physical standby -- see auth.c's own comment and README.md's
 *        "Routing beyond dbname: TLS SNI" section), or --ssl-self-signed
 *        was given explicitly, whether or not this is the only route.
 *        Warns (never refuses) if --hostname was never given for a route
 *        now sharing the file with others;
 *     5. reload an already-running "pg_walserver serve" for this same
 *        --pgdata, if one is running (cli_root.c's own cli_cluster_
 *        reload_running_server()) -- the same read_pidfile()/SIGHUP shape
 *        "pg_walserver reload" itself uses. "cluster register" never
 *        takes a base backup itself: a running server picks up the
 *        new/changed route the moment it is reloaded, and bootstraps a
 *        first base backup for it automatically if it doesn't have one
 *        yet.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "commandline.h"
#include "pqexpbuffer.h"

#include "cli_basebackup.h"
#include "cli_cluster.h"
#include "cli_common.h"
#include "cli_create_cert.h"
#include "cli_fetch_systemid.h"
#include "cli_root.h"
#include "cli_upstream.h"
#include "defaults.h"
#include "file_utils.h"
#include "log.h"
#include "pidfile.h"
#include "routes.h"
#include "string_utils.h"

/*
 * "pidfile.h" above may resolve to either src/bin/common/pidfile.h or, via
 * this project's own include-path fallback, pg_autoctl's own pidfile.h --
 * which pulls in keeper.h, and, with it, commandline.h's own "streq" macro.
 * Guard against a redefinition error either way, rather than relying on
 * which one the include path happens to pick (the same guard cli_list.c
 * already carries, for the same reason).
 */
#ifndef streq
#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))
#endif

static int cli_cluster_register_getopt(int argc, char **argv);
static void cli_cluster_register_command_run(int argc, char **argv);

static int cli_cluster_drop_getopt(int argc, char **argv);
static void cli_cluster_drop_command_run(int argc, char **argv);

static int cli_cluster_enable_getopt(int argc, char **argv);
static void cli_cluster_enable_command_run(int argc, char **argv);

static int cli_cluster_list_getopt(int argc, char **argv);
static void cli_cluster_list_command_run(int argc, char **argv);

static int cli_cluster_prune_getopt(int argc, char **argv);
static void cli_cluster_prune_command_run(int argc, char **argv);

static int cli_cluster_set_upstream_getopt(int argc, char **argv);
static void cli_cluster_set_upstream_command_run(int argc, char **argv);

CommandLine cluster_register_command =
	make_command(
		"register",
		"Register (or validate) one cluster this pg_walserver "
		"archives",
		"<name> --pgdata <path> [--config <path>] "
		"[--path <dir>] "
		"[--pguri <conninfo> | --host <host> [--port <port>] "
		"[--user <name>]] [--hostname <fqdn>] "
		"[--receivewal pull|none | --no-receivewal] "
		"[--ssl-self-signed] [--force]",
		"  <name>      the cluster's own name, given positionally "
		"(never a flag)\n"
		"  --pgdata    this instance's own data root (defaults to "
		"PGDATA)\n"
		"  --config  where the config file itself lives, "
		"independent of\n"
		"              --pgdata (defaults to "
		"<pgdata>/pg_walserver.ini, or\n"
		"              PG_WALSERVER_CONFIG_FILE)\n"
		"  --path      the route's own directory, created if "
		"missing; defaults\n"
		"              to <pgdata>/<name>\n"
		"  --pguri     a libpq connection string, written into the "
		"route's own\n"
		"              \"upstream\" property\n"
		"  --host / --port / --user  further override individual "
		"connection\n"
		"              parameters (default port: 5432, default "
		"user: " PG_AUTOCTL_REPLICA_USERNAME ")\n"
											 "  --hostname  the route's own TLS SNI hostname, written "
											 "into its\n"
											 "              \"hostname\" property -- the only way a "
											 "real physical\n"
											 "              standby can address this route by name "
											 "once more than\n"
											 "              one exists (dbname alone cannot, see "
											 "README.md's\n"
											 "              \"Routing beyond dbname: TLS SNI\" "
											 "section); creates a\n"
											 "              self-signed certificate for\n"
											 "              --pgdata automatically, the moment a "
											 "second route is\n"
											 "              added, if none exists yet (or right away "
											 "with\n"
											 "              --ssl-self-signed, below)\n"
											 "  --receivewal pull  write \"receivewal = pull\" into the "
											 "route's own section\n"
											 "              (the default now, even with no --receivewal "
											 "flag at all):\n"
											 "              the next \"pg_walserver serve\" forks a "
											 "supervised child\n"
											 "              running the embedded pg_receivewal "
											 "worker against\n"
											 "              this route's own \"upstream\" (receivewal.c)"
											 " -- see README.md's\n"
											 "              \"The embedded receivewal worker\" section\n"
											 "  --receivewal none / --no-receivewal  opt this route out of "
											 "the embedded\n"
											 "              receivewal worker (push-only, archive_command-only)"
											 "\n"
											 "  --ssl-self-signed  create a self-signed certificate "
											 "for --pgdata right\n"
											 "              away, whether or not this is the only "
											 "route -- skips a\n"
											 "              separate \"pg_walserver create-cert\" call "
											 "entirely; an\n"
											 "              already-existing certificate is left "
											 "untouched\n"
											 "  --force     change an already-existing route's path, "
											 "or overwrite an\n"
											 "              already-recorded, different system "
											 "identifier\n"
											 "\n"
											 "Reloads an already-running \"pg_walserver serve\" for this "
											 "--pgdata, if one is\n"
											 "running, so it picks up this route immediately; with none "
											 "running, the\n"
											 "config just written takes effect the next time \"serve\" "
											 "starts. Either\n"
											 "way, \"serve\" itself takes this route's first base backup "
											 "automatically\n"
											 "if it doesn't have one yet -- \"cluster register\" never "
											 "takes one itself.\n",
		cli_cluster_register_getopt, cli_cluster_register_command_run);

CommandLine cluster_drop_command =
	make_command("drop",
				 "Drop (disable) one cluster, or fully remove it with "
				 "--purge",
				 "<name> --pgdata <path> [--config <path>] [--purge]",
				 "  <name>      the cluster's own name, given positionally "
				 "(never a flag)\n"
				 "  --pgdata    this instance's own data root (defaults to "
				 "PGDATA)\n"
				 "  --config  where the config file itself lives "
				 "(defaults to\n"
				 "              <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n"
				 "  --purge     also remove the registration and the "
				 "route's own on-disk\n"
				 "              data (every base backup and WAL segment "
				 "it holds) -- without\n"
				 "              it, the route is only marked disabled: "
				 "its embedded\n"
				 "              receivewal worker is stopped, it refuses "
				 "every connection\n"
				 "              and command (basebackup, fetch-systemid, "
				 "set-upstream,\n"
				 "              CHECK_FILE/ARCHIVE_FILE/archive-wal/"
				 "restore-wal), and its\n"
				 "              own data is left in place -- see \"cluster "
				 "list --disabled\"\n"
				 "              to find it again, \"cluster enable\" to "
				 "bring it back, or\n"
				 "              \"cluster prune\" to remove every dropped "
				 "cluster at once\n",
				 cli_cluster_drop_getopt, cli_cluster_drop_command_run);

CommandLine cluster_enable_command =
	make_command("enable",
				 "Bring a dropped (disabled) cluster back",
				 "<name> --pgdata <path> [--config <path>]",
				 "  <name>      the cluster's own name, given positionally "
				 "(never a flag)\n"
				 "  --pgdata    this instance's own data root (defaults to "
				 "PGDATA)\n"
				 "  --config  where the config file itself lives "
				 "(defaults to\n"
				 "              <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n"
				 "\n"
				 "The symmetric counterpart to \"cluster drop\" (without "
				 "--purge): clears\n"
				 "the route's own \"disabled\" property, nothing else -- "
				 "its own \"path\"/\n"
				 "\"upstream\"/\"hostname\" are already on file, so, "
				 "unlike re-running\n"
				 "\"cluster register\" to the same end, no connection "
				 "URI needs to be\n"
				 "re-supplied. Reloads an already-running \"pg_walserver "
				 "serve\" for the\n"
				 "same --pgdata immediately afterward, so its embedded "
				 "receivewal worker\n"
				 "(if \"receivewal = pull\") starts again right away. A "
				 "cluster that was\n"
				 "already active is a safe no-op.\n",
				 cli_cluster_enable_getopt, cli_cluster_enable_command_run);

CommandLine cluster_list_command =
	make_command("list",
				 "List every cluster this pg_walserver has registered",
				 "[--pgdata <path> | --config <path>] [--upstream] "
				 "[--disabled]",
				 "  --pgdata    this instance's own data root (defaults to "
				 "PGDATA)\n"
				 "  --config  where the config file itself lives; either "
				 "this or\n"
				 "              --pgdata is enough (defaults to "
				 "<pgdata>/pg_walserver.ini,\n"
				 "              or PG_WALSERVER_CONFIG_FILE)\n"
				 "  --upstream  also print each cluster's own upstream "
				 "connection\n"
				 "              string, pivoted into one block per "
				 "cluster instead of\n"
				 "              a table column (skipped by default -- "
				 "these are often\n"
				 "              too wide for a readable row)\n"
				 "  --disabled  list dropped (disabled) clusters instead "
				 "of active ones\n"
				 "              -- \"cluster drop\" (without --purge) "
				 "marks a cluster\n"
				 "              this way rather than removing it; see "
				 "\"cluster prune\"\n"
				 "              to remove every one of them at once\n",
				 cli_cluster_list_getopt, cli_cluster_list_command_run);

CommandLine cluster_prune_command =
	make_command("prune",
				 "Remove every dropped (disabled) cluster's registration "
				 "and on-disk data",
				 "[--pgdata <path> | --config <path>]",
				 "  --pgdata    this instance's own data root. Either "
				 "this or --config\n"
				 "              is enough\n"
				 "  --config  where the config file itself lives "
				 "(defaults to\n"
				 "              <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n"
				 "\n"
				 "The bulk equivalent of \"cluster drop --purge <name>\" "
				 "run once per\n"
				 "cluster \"cluster list --disabled\" shows -- every "
				 "dropped cluster's own\n"
				 "registration and on-disk data (every base backup and "
				 "WAL segment it\n"
				 "holds) is removed. Never touches an active cluster.\n",
				 cli_cluster_prune_getopt, cli_cluster_prune_command_run);

CommandLine cluster_set_upstream_command =
	make_command("set-upstream",
				 "Point an already-registered cluster at a new upstream "
				 "(e.g. after a failover)",
				 "<name> --pgdata <path> [--config <path>] "
				 "--pguri <conninfo> [--force-basebackup]",
				 "  <name>      the cluster's own name, given positionally "
				 "(never a flag)\n"
				 "  --pgdata    this instance's own data root (defaults to "
				 "PGDATA)\n"
				 "  --config  where the config file itself lives "
				 "(defaults to\n"
				 "              <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n"
				 "  --pguri     the new libpq connection string, replacing "
				 "the route's\n"
				 "              own \"upstream\" property\n"
				 "  --force-basebackup  also take a fresh base backup "
				 "against the new\n"
				 "              upstream right away, rather than waiting "
				 "for the next\n"
				 "              scheduled \"pg_walserver basebackup\"\n"
				 "\n"
				 "Reloads an already-running \"pg_walserver serve\" for "
				 "this --pgdata, if\n"
				 "one is running: its own reconciliation already detects "
				 "the \"upstream\"\n"
				 "change and restarts this route's embedded receivewal "
				 "worker against\n"
				 "the new one -- no separate step needed to \"move\" it.\n",
				 cli_cluster_set_upstream_getopt,
				 cli_cluster_set_upstream_command_run);

static CommandLine *cluster_subcommands[] = {
	&cluster_register_command,
	&cluster_drop_command,
	&cluster_enable_command,
	&cluster_list_command,
	&cluster_set_upstream_command,
	&cluster_prune_command,
	NULL
};

CommandLine cluster_commands =
	make_command_set("cluster",
					 "Register, drop, enable, list, re-point, or prune "
					 "the clusters this pg_walserver archives",
					 NULL, NULL, NULL, cluster_subcommands);


/*
 * WS_RMTREE_RETRY_ATTEMPTS/WS_RMTREE_RETRY_USEC: SIGHUP-ing a running
 * "serve" only *asks* it to stop a route's own embedded receivewal
 * worker/bootstrap backup -- signal delivery and the child's own SIGINT
 * handling are asynchronous, so the child can still hold the route's
 * directory open (an in-progress ".partial" segment, in particular) for
 * a brief moment after ws_cluster_reload_running_server() above already
 * returned. Retrying rmtree() a handful of times, a short sleep apart,
 * covers that ordinary window without ws_cluster_drop_run() needing any
 * real cross-process synchronization with a "serve" it only ever talks
 * to via SIGHUP + the pidfile. rmtree() (PostgreSQL's own, src/common/
 * rmtree.c) logs its own warning on every failed attempt, not just the
 * last -- kept to a handful of attempts, not dozens, so a genuine
 * failure (not a transient race at all, e.g. a permissions problem)
 * doesn't spam the log before this function's own final warning above.
 */
#define WS_RMTREE_RETRY_ATTEMPTS 5
#define WS_RMTREE_RETRY_USEC (150 * 1000)

static bool
rmtree_retrying(const char *path)
{
	for (int attempt = 1; attempt <= WS_RMTREE_RETRY_ATTEMPTS; attempt++)
	{
		if (rmtree(path, true))
		{
			return true;
		}

		if (attempt < WS_RMTREE_RETRY_ATTEMPTS)
		{
			usleep(WS_RMTREE_RETRY_USEC);
		}
	}

	return false;
}


/*
 * ws_cluster_reload_running_server reloads an already-running
 * "pg_walserver serve" for the same --pgdata, if one is running, so it
 * immediately picks up whatever config-mutating command just wrote --
 * the same read_pidfile()/SIGHUP shape "pg_walserver reload" itself
 * uses, with one difference: no running server at all is not an error
 * here, only a normal, expected case -- logged, not fatal. Shared (not
 * cli_root.c-private) so every command that mutates the routes file can
 * call it at the point that actually matters for its own case -- in
 * particular, ws_cluster_drop_run()'s own --purge path below, which
 * must stop a still-running embedded receivewal worker/bootstrap backup
 * for the route *before* rmtree()ing its directory out from under it,
 * not after.
 */
void
ws_cluster_reload_running_server(const char *pgdata)
{
	if (pgdata == NULL || pgdata[0] == '\0')
	{
		return;
	}

	char pidfilePath[MAXPGPATH] = { 0 };

	sformat(pidfilePath, sizeof(pidfilePath), "%s/pg_walserver.pid", pgdata);

	/*
	 * Ignore SIGHUP in THIS one-shot process first, before ever sending it
	 * on -- the same guard cli_reload_run() (cli_root.c) applies to
	 * itself, for the same reason: this process is not "serve" and must
	 * never react to its own signal.
	 */
	signal(SIGHUP, SIG_IGN);

	pid_t pid = 0;

	if (!read_pidfile(pidfilePath, &pid))
	{
		log_info("No running \"pg_walserver serve\" found at \"%s\": the "
				 "route just written will take effect the next time "
				 "\"serve\" starts", pidfilePath);
		return;
	}

	if (kill(pid, SIGHUP) != 0)
	{
		if (errno == ESRCH)
		{
			log_info("Pidfile \"%s\" names pid %d, which is not running: "
					 "the route just written will take effect the next "
					 "time \"serve\" starts", pidfilePath, pid);
		}
		else
		{
			log_warn("Failed to send SIGHUP to pg_walserver pid %d: %m", pid);
		}
		return;
	}

	log_info("Reloaded the running pg_walserver (pid %d): it will pick up "
			 "this route immediately", pid);
}


/*
 * write_route_section creates or validates the [routeKey] section of the
 * config file: a brand new key is appended; an existing one must already
 * have the same path (an operator re-running "cluster register" must be
 * a safe no-op), or --force is required to change it -- the same "never
 * silently replace what's already there" principle as cli_fetch_
 * systemid.c's own systemid check.
 */
static bool
write_route_section(const char *configPath, const char *routeKey,
					const WsUpstreamTarget *target, const char *upstreamRaw,
					const char *hostname, bool receivewalPull, bool force)
{
	WsRoute *routes = NULL;
	int routeCount = 0;
	bool haveExisting = routes_load(configPath, &routes, &routeCount);
	const WsRoute *existing = haveExisting
							  ? routes_find(routes, routeCount, routeKey)
							  : NULL;

	if (existing != NULL && !streq(existing->key, routeKey))
	{
		/* the "*" wildcard can be found for a key that isn't literally
		* "*" -- never treat that as "the route already exists" here */
		existing = NULL;
	}

	if (existing != NULL && !streq(existing->path, target->path) && !force)
	{
		log_error("Route \"%s\" already exists in \"%s\" with path \"%s\", "
				  "not \"%s\" -- pass --force to change it",
				  routeKey, configPath, existing->path, target->path);
		routes_free(routes);
		return false;
	}

	/*
	 * Copy out of *existing everything still needed below before it's
	 * freed -- existing is a pointer into routes, which routes_free()
	 * invalidates.
	 */
	bool samePath = existing != NULL && streq(existing->path, target->path);
	bool existingHasReceivewalPull = existing != NULL && existing->receivewalPull;
	bool existingDisabled = existing != NULL && existing->disabled;

	routes_free(routes);

	if (samePath)
	{
		log_info("Route \"%s\" already configured in \"%s\"",
				 routeKey, configPath);

		if (receivewalPull && !existingHasReceivewalPull)
		{
			log_warn("Route \"%s\" already exists in \"%s\" without "
					 "\"receivewal = pull\" (the embedded receivewal worker is on "
					 "by default now, but was not the last time \"register "
					 "cluster\" wrote this route, or --no-receivewal/--receivewal "
					 "none was passed then) -- edit \"%s\" by hand to add it, "
					 "\"cluster register\" never changes an already-existing "
					 "route's properties beyond path", routeKey, configPath,
					 configPath);
		}

		if (existingDisabled)
		{
			if (!routes_set_property(configPath, routeKey, "disabled", "false"))
			{
				/* errors have already been logged */
				return false;
			}

			log_info("Route \"%s\" was dropped (disabled) -- registering it "
					 "again brings it back", routeKey);
		}

		return true;
	}

	PQExpBuffer section = createPQExpBuffer();

	appendPQExpBuffer(section, "\n[%s]\npath = %s\n", routeKey, target->path);

	if (upstreamRaw != NULL && upstreamRaw[0] != '\0')
	{
		appendPQExpBuffer(section, "upstream = %s\n", upstreamRaw);
	}

	if (hostname != NULL && hostname[0] != '\0')
	{
		appendPQExpBuffer(section, "hostname = %s\n", hostname);
	}

	if (receivewalPull)
	{
		appendPQExpBufferStr(section, "receivewal = pull\n");
	}

	if (PQExpBufferBroken(section))
	{
		destroyPQExpBuffer(section);
		log_error("Out of memory");
		return false;
	}

	char *existingContents = NULL;
	long existingSize = 0L;
	bool ok;

	if (read_file_if_exists(configPath, &existingContents, &existingSize) &&
		existingContents != NULL)
	{
		PQExpBuffer whole = createPQExpBuffer();

		appendPQExpBufferStr(whole, existingContents);
		appendPQExpBufferStr(whole, section->data);

		ok = !PQExpBufferBroken(whole) &&
			 write_file_atomic(whole->data, whole->len, configPath);

		destroyPQExpBuffer(whole);
		free(existingContents);
	}
	else
	{
		/* skip the leading blank line for a brand new file */
		ok = write_file_atomic(section->data + 1, section->len - 1, configPath);
	}

	destroyPQExpBuffer(section);

	if (!ok)
	{
		log_error("Failed to write \"%s\"", configPath);
		return false;
	}

	log_info("Added route \"%s\" (path \"%s\") to \"%s\"",
			 routeKey, target->path, configPath);

	return true;
}


/*
 * ensure_tls_certificate re-reads the config file after write_route_
 * section() and creates a self-signed certificate for <pgdata> (via ws_
 * create_cert_run() -- cli_create_cert.c, the same helper `pg_walserver
 * create-cert` itself calls, wrapping pg_create_self_signed_cert()) in
 * either of two cases: the file now holds more than one route, or
 * sslSelfSigned (--ssl-self-signed) was given explicitly, whether or not
 * this is the file's only route. Either way, an already-existing
 * certificate is left untouched; the certificate's own CN is routeKey's
 * own --hostname when one was given, else this machine's own hostname.
 * Also warns when routeKey itself has no "hostname" property in the
 * multi-route case. Never a hard failure in either case.
 */
static void
ensure_tls_certificate(const char *pgdata, const char *configPath,
					   const char *routeKey, const char *hostname,
					   bool sslSelfSigned)
{
	WsRoute *routes = NULL;
	int routeCount = 0;

	if (!routes_load(configPath, &routes, &routeCount))
	{
		routes_free(routes);
		return;
	}

	routes_free(routes);

	bool haveHostname = hostname != NULL && hostname[0] != '\0';
	bool multipleRoutes = routeCount > 1;

	if (multipleRoutes)
	{
		log_info("\"%s\" now has %d routes: TLS is required for more than "
				 "one route to be reachable by name (dbname-based routing "
				 "alone cannot tell a real physical standby's connection "
				 "apart from any other route once there is more than one, "
				 "see this project's own README.md)",
				 configPath, routeCount);
	}

	if (!multipleRoutes && !sslSelfSigned)
	{
		return;
	}

	char certPath[MAXPGPATH] = { 0 };
	char keyPath[MAXPGPATH] = { 0 };

	sformat(certPath, sizeof(certPath), "%s/server.crt", pgdata);
	sformat(keyPath, sizeof(keyPath), "%s/server.key", pgdata);

	if (!file_exists(certPath) || !file_exists(keyPath))
	{
		char localHostname[_POSIX_HOST_NAME_MAX] = "pg_walserver";

		if (!haveHostname)
		{
			(void) gethostname(localHostname, sizeof(localHostname));
		}

		if (!ws_create_cert_run(pgdata, haveHostname ? hostname : localHostname,
								false))
		{
			log_warn("Failed to create a self-signed certificate for "
					 "\"%s\" -- pass --ssl-cert-file/--ssl-key-file to "
					 "\"serve\", or create \"%s\"/\"%s\" yourself (\"pg_"
					 "walserver create-cert\"), before starting it",
					 pgdata, certPath, keyPath);
		}
	}

	if (multipleRoutes && !haveHostname)
	{
		log_warn("Route \"%s\" has no --hostname: it can only be reached "
				 "by dbname (pg_basebackup/pg_receivewal/archive_command) "
				 "or the \"*\" wildcard, never by name by a real physical "
				 "standby -- pass --hostname next time, or edit \"%s\" by "
				 "hand, to add one", routeKey, configPath);
	}
}


/*
 * ws_cluster_register_run -- see cli_cluster.h's own comment, and this
 * file's own header comment for the full sequence.
 */
bool
ws_cluster_register_run(const WsClusterRegisterOptions *options)
{
	if (options->cluster[0] == '\0')
	{
		log_error("cluster register requires a cluster name "
				  "(\"pg_walserver cluster register <name> ...\")");
		return false;
	}

	if (options->pgdata[0] == '\0')
	{
		log_error("cluster register requires --pgdata (this instance's "
				  "own data root)");
		return false;
	}

	char configPath[MAXPGPATH] = { 0 };

	config_file_path(options->pgdata, options->configFile,
					 configPath, sizeof(configPath));

	/*
	 * --path is only ever an override: a route's own directory defaults to
	 * "<pgdata>/<cluster>", the same top-level storage root every other
	 * pg_walserver file already lives under.
	 */
	char defaultPath[MAXPGPATH] = { 0 };
	const char *pathArg = options->path;

	if (pathArg[0] == '\0')
	{
		sformat(defaultPath, sizeof(defaultPath), "%s/%s",
				options->pgdata, options->cluster);
		pathArg = defaultPath;
	}

	WsUpstreamTarget target = { 0 };

	if (!cli_resolve_upstream(NULL /* not looking one up yet */, NULL, NULL,
							  pathArg, options->pguri,
							  options->host,
							  options->port[0] != '\0' ? options->port : NULL,
							  options->user, &target))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * Deliberately NOT ensure_empty_dir(): that function rmtree()s an
	 * *existing* directory before recreating it (right for a base backup's
	 * own one-shot destination in cli_basebackup.c, catastrophic here --
	 * this is the route's top-level directory, potentially already holding
	 * captured WAL and prior base backups on a re-run). Just make sure it
	 * exists; never touch what's already in it.
	 */
	if (!directory_exists(target.path) &&
		pg_mkdir_p((char *) target.path, 0700) == -1)
	{
		log_error("Failed to create \"%s\": %m", target.path);
		return false;
	}

	/*
	 * cli_resolve_upstream() above already refused to succeed without a
	 * resolved host (--pguri, --host, or an existing route's own
	 * "upstream"), so --receivewal pull always has somewhere to pull from by
	 * the time it's written below -- no separate check needed here.
	 */
	if (!write_route_section(configPath, options->cluster, &target,
							 options->pguri, options->hostname,
							 options->receivewalPull, options->force))
	{
		/* errors have already been logged */
		return false;
	}

	ensure_tls_certificate(options->pgdata, configPath, options->cluster,
						   options->hostname, options->sslSelfSigned);

	uint64_t systemIdentifier = 0;

	if (!cli_fetch_systemid_run(&target, options->force, &systemIdentifier))
	{
		/* errors have already been logged -- including, per this call's own
		 * comment, a role lacking REPLICATION: Postgres refuses a
		 * replication-mode connection for that at the backend level */
		return false;
	}

	log_info("cluster register complete: route \"%s\" is ready (no base "
			 "backup taken here -- \"pg_walserver serve\" bootstraps the "
			 "route's first base backup automatically, once, the next "
			 "time it starts or reloads this route; run \"pg_walserver "
			 "basebackup\" by hand at any time to take another one)",
			 options->cluster);

	return true;
}


/*
 * ws_cluster_drop_run, without purge, marks routeKey's own [section] as
 * disabled (routes_set_property(configPath, routeKey, "disabled", "true"),
 * routes.c) rather than removing it outright: an operator who only meant
 * to stop archiving a cluster, or is about to re-register it under a
 * different upstream, should never lose its archive by accident, and the
 * section's own "path" must stay on record for --purge to find later --
 * dropping the section right away, the way this used to work, would
 * orphan the route's own on-disk data (captured WAL, base backups) with
 * nothing left in the config file pointing back at it. A disabled route
 * is otherwise inert -- see routes.h's own comment on WsRoute's
 * "disabled" field for the full list of what stops.
 *
 * With purge, the route's own directory is rmtree()'d and its [section]
 * is fully removed (routes_drop_section()) -- this works the same way
 * whether the route was already disabled (the common case: "drop" once
 * to stop it, "drop --purge" later once its data is no longer needed) or
 * still active (an operator skipping straight to full removal).
 */
bool
ws_cluster_drop_run(const char *pgdata, const char *configFile,
					const char *routeKey, bool purge)
{
	if (routeKey == NULL || routeKey[0] == '\0')
	{
		log_error("cluster drop requires a cluster name "
				  "(\"pg_walserver cluster drop <name> ...\")");
		return false;
	}

	if (pgdata == NULL || pgdata[0] == '\0')
	{
		log_error("cluster drop requires --pgdata (this instance's own "
				  "data root)");
		return false;
	}

	char configPath[MAXPGPATH] = { 0 };

	config_file_path(pgdata, configFile, configPath, sizeof(configPath));

	WsRoute *routes = NULL;
	int routeCount = 0;

	if (!routes_load(configPath, &routes, &routeCount))
	{
		/* errors have already been logged */
		return false;
	}

	const WsRoute *route = routes_find_exact(routes, routeCount, routeKey);

	if (route == NULL)
	{
		log_error("No route \"%s\" in \"%s\"", routeKey, configPath);
		routes_free(routes);
		return false;
	}

	char routePath[MAXPGPATH] = { 0 };
	bool alreadyDisabled = route->disabled;

	strlcpy(routePath, route->path, sizeof(routePath));
	routes_free(routes);

	if (!purge)
	{
		if (alreadyDisabled)
		{
			log_info("Route \"%s\" is already dropped (disabled); its own "
					 "data under \"%s\" is still in place -- pass --purge "
					 "to remove it, or \"pg_walserver cluster prune\" to "
					 "remove every dropped route at once",
					 routeKey, routePath);
			return true;
		}

		if (!routes_set_property(configPath, routeKey, "disabled", "true"))
		{
			/* errors have already been logged */
			return false;
		}

		log_info("Route \"%s\" dropped (disabled) in \"%s\"; its own data "
				 "under \"%s\" was left in place -- pass --purge to remove "
				 "it too, or run \"pg_walserver cluster enable %s\" to "
				 "bring it back",
				 routeKey, configPath, routePath, routeKey);

		return true;
	}

	if (!routes_drop_section(configPath, routeKey))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * Reload BEFORE rmtree(), not after: routeKey is now gone from the
	 * config file, so an already-running "serve" that picks this up
	 * stops the route's own embedded receivewal worker/bootstrap backup
	 * (if either was actually running -- always true unless the route
	 * was already disabled first) before its directory is removed out
	 * from under it. Reloading only after rmtree(), the way this used to
	 * work, let a still-live child keep writing into a directory that no
	 * longer existed until the next SIGHUP finally caught up to it.
	 */
	if (!alreadyDisabled)
	{
		ws_cluster_reload_running_server(pgdata);
	}

	if (!rmtree_retrying(routePath))
	{
		log_warn("Route \"%s\" was dropped from \"%s\", but removing "
				 "its own directory \"%s\" failed -- remove it by "
				 "hand", routeKey, configPath, routePath);
	}
	else
	{
		log_info("Removed \"%s\" (--purge)", routePath);
	}

	return true;
}


/*
 * ws_cluster_enable_run clears routeKey's own "disabled" property
 * (routes_set_property(configPath, routeKey, "disabled", "false"),
 * routes.c) -- the dedicated, symmetric counterpart to "cluster drop"
 * (without --purge): no connection URI to re-supply, unlike re-running
 * "cluster register" to the same end (write_route_section()'s own
 * "already configured" no-op path also clears "disabled", but only ever
 * as a side effect of an otherwise-complete register call, which needs
 * --pguri/--host again even though the route's own upstream is already
 * on file). Refuses a route that doesn't exist, or one that is already
 * active, the same "nothing to do, say so, don't error" shape "cluster
 * drop" itself uses for an already-disabled route.
 */
bool
ws_cluster_enable_run(const char *pgdata, const char *configFile,
					  const char *routeKey)
{
	if (routeKey == NULL || routeKey[0] == '\0')
	{
		log_error("cluster enable requires a cluster name "
				  "(\"pg_walserver cluster enable <name> ...\")");
		return false;
	}

	if (pgdata == NULL || pgdata[0] == '\0')
	{
		log_error("cluster enable requires --pgdata (this instance's own "
				  "data root)");
		return false;
	}

	char configPath[MAXPGPATH] = { 0 };

	config_file_path(pgdata, configFile, configPath, sizeof(configPath));

	WsRoute *routes = NULL;
	int routeCount = 0;

	if (!routes_load(configPath, &routes, &routeCount))
	{
		/* errors have already been logged */
		return false;
	}

	const WsRoute *route = routes_find_exact(routes, routeCount, routeKey);

	if (route == NULL)
	{
		log_error("No route \"%s\" in \"%s\"", routeKey, configPath);
		routes_free(routes);
		return false;
	}

	bool alreadyEnabled = !route->disabled;

	routes_free(routes);

	if (alreadyEnabled)
	{
		log_info("Route \"%s\" is already active in \"%s\"", routeKey,
				 configPath);
		return true;
	}

	if (!routes_set_property(configPath, routeKey, "disabled", "false"))
	{
		/* errors have already been logged */
		return false;
	}

	log_info("Route \"%s\" enabled again in \"%s\"", routeKey, configPath);

	return true;
}


/*
 * ws_cluster_prune_run purges every disabled ("dropped") route at once --
 * routes_drop_section() plus rmtree() for each, the same work "cluster
 * drop --purge <name>" does for one route by name, applied to every route
 * currently disabled, the "ala docker" bulk equivalent of "docker
 * container prune"/"docker system prune". Never touches an active
 * (non-disabled) route -- only an already-disabled one is ever eligible,
 * so (unlike ws_cluster_drop_run()'s own --purge path) there is no live
 * embedded receivewal worker/bootstrap backup left to stop first: an
 * already-running "serve", reloaded once at the very end, is only
 * catching up on registration bookkeeping it should already agree with.
 * Always returns true: no dropped routes to prune is an ordinary state
 * to report, not a failure; a route whose own rmtree() fails is warned
 * about (same as a single "drop --purge") and pruning continues with
 * the rest rather than aborting outright.
 */
bool
ws_cluster_prune_run(const char *pgdata, const char *configFile)
{
	if ((pgdata == NULL || pgdata[0] == '\0') &&
		(configFile == NULL || configFile[0] == '\0'))
	{
		log_error("cluster prune requires --pgdata or --config");
		return false;
	}

	char configPath[MAXPGPATH] = { 0 };

	config_file_path(pgdata, configFile, configPath, sizeof(configPath));

	WsRoute *routes = NULL;
	int routeCount = 0;

	if (!routes_load(configPath, &routes, &routeCount))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * Copy the disabled routes' own key/path aside before touching the
	 * config file: routes_drop_section() rewrites it on disk, and this
	 * loop's own routes array must stay a stable, already-loaded snapshot
	 * throughout (the same reason ws_cluster_drop_run() above copies
	 * routePath out before its own routes_free()).
	 */
	int pruned = 0;

	for (int i = 0; i < routeCount; i++)
	{
		if (!routes[i].disabled)
		{
			continue;
		}

		char routeKey[NAMEDATALEN + 16] = { 0 };
		char routePath[MAXPGPATH] = { 0 };

		strlcpy(routeKey, routes[i].key, sizeof(routeKey));
		strlcpy(routePath, routes[i].path, sizeof(routePath));

		if (!routes_drop_section(configPath, routeKey))
		{
			log_warn("Failed to remove route \"%s\" from \"%s\" -- "
					 "skipping it", routeKey, configPath);
			continue;
		}

		if (!rmtree_retrying(routePath))
		{
			log_warn("Route \"%s\" was dropped from \"%s\", but removing "
					 "its own directory \"%s\" failed -- remove it by "
					 "hand", routeKey, configPath, routePath);
		}
		else
		{
			log_info("Removed \"%s\" (\"%s\", dropped)", routePath, routeKey);
		}

		pruned++;
	}

	routes_free(routes);

	if (pruned > 0)
	{
		ws_cluster_reload_running_server(pgdata);
	}

	if (pruned == 0)
	{
		fformat(stdout, "No dropped clusters to prune.\n");
	}
	else
	{
		fformat(stdout, "Pruned %d dropped cluster%s.\n",
				pruned, pruned == 1 ? "" : "s");
	}

	return true;
}


/*
 * ws_cluster_list_run prints one row per registered route: its key, path,
 * upstream, hostname (or "-"), and "receivewal" setting -- the
 * registration itself, as the config file records it, never the
 * operational/data-layer facts :ref:`pg_walserver_list`'s own "list
 * clusters" already reports (backup/WAL presence, WAL range). Prints a
 * clean "none" message, not an error, when the chosen view (active
 * routes by default, dropped/disabled ones with showDisabled) has
 * nothing to show.
 */
bool
ws_cluster_list_run(const char *pgdata, const char *configFile,
					bool showUpstream, bool showDisabled)
{
	if ((pgdata == NULL || pgdata[0] == '\0') &&
		(configFile == NULL || configFile[0] == '\0'))
	{
		log_error("cluster list requires --pgdata or --config");
		return false;
	}

	char configPath[MAXPGPATH] = { 0 };

	config_file_path(pgdata, configFile, configPath, sizeof(configPath));

	WsRoute *routes = NULL;
	int routeCount = 0;

	if (!routes_load(configPath, &routes, &routeCount))
	{
		/* errors have already been logged */
		return false;
	}

	int matching = 0;

	for (int i = 0; i < routeCount; i++)
	{
		if (routes[i].disabled == showDisabled)
		{
			matching++;
		}
	}

	if (matching == 0)
	{
		if (showDisabled)
		{
			fformat(stdout, "No dropped clusters under \"%s\".\n", configPath);
		}
		else if (routeCount == 0)
		{
			fformat(stdout, "No clusters registered yet under \"%s\" -- see "
							"\"pg_walserver cluster register\".\n", configPath);
		}
		else
		{
			fformat(stdout, "No active clusters under \"%s\" (pass --disabled to "
							"see dropped ones).\n", configPath);
		}

		routes_free(routes);
		return true;
	}

	if (showUpstream)
	{
		/*
		 * UPSTREAM values are full connection strings/URIs, often far
		 * wider than any other column -- a wide row layout is unreadable
		 * once they're included, so pivot to one key: value block per
		 * cluster instead (only reached with --upstream, an explicit
		 * opt-in; the default table below never prints this column).
		 */
		bool first = true;

		for (int i = 0; i < routeCount; i++)
		{
			if (routes[i].disabled != showDisabled)
			{
				continue;
			}

			if (!first)
			{
				fformat(stdout, "\n");
			}
			first = false;

			fformat(stdout, "cluster:    %s\n", routes[i].key);
			fformat(stdout, "receivewal: %s\n",
					routes[i].receivewalPull ? "pull" : "none");
			fformat(stdout, "upstream:   %s\n",
					routes[i].upstream[0] != '\0' ? routes[i].upstream : "-");
			fformat(stdout, "hostname:   %s\n",
					routes[i].hostname[0] != '\0' ? routes[i].hostname : "-");
			fformat(stdout, "path:       %s\n", routes[i].path);
		}

		routes_free(routes);

		return true;
	}

	fformat(stdout, "%-20s %-10s %-24s %s\n",
			"CLUSTER", "RECEIVEWAL", "HOSTNAME", "PATH");
	fformat(stdout, "%-20s %-10s %-24s %s\n",
			"--------------------", "----------",
			"------------------------", "----");

	for (int i = 0; i < routeCount; i++)
	{
		if (routes[i].disabled != showDisabled)
		{
			continue;
		}

		fformat(stdout, "%-20s %-10s %-24s %s\n",
				routes[i].key,
				routes[i].receivewalPull ? "pull" : "none",
				routes[i].hostname[0] != '\0' ? routes[i].hostname : "-",
				routes[i].path);
	}

	routes_free(routes);

	return true;
}


/*
 * ws_cluster_set_upstream_run changes routeKey's own "upstream" property
 * (routes_set_property(), routes.c) -- the way to point an already-
 * registered cluster at a new upstream after a failover (or a planned
 * move), without dropping and re-registering it. An already-running
 * "serve" for the same --pgdata is reloaded immediately afterward
 * (cli_root.c's own cli_cluster_reload_running_server(), the exact same
 * shape "cluster register" and "reload" itself already use): reload's
 * own reconciliation (receivewal.c's own ws_receivewal_reload()) already
 * detects an "upstream" change on its own and restarts this route's
 * embedded receivewal worker against the new one -- no separate "move the
 * receivewal worker" step needed here, reload already does it. With
 * forceBasebackup, also takes a fresh base backup against the *new*
 * upstream right away (cli_basebackup_run(), cli_basebackup.c) once the
 * property is written -- the common "failover just happened, the old
 * upstream's own base backups are no longer usable against the new
 * timeline, get a current one immediately" case -- rather than leaving a
 * stale backup in place until the next scheduled "pg_walserver
 * basebackup" call.
 */
bool
ws_cluster_set_upstream_run(const char *pgdata, const char *configFile,
							const char *routeKey, const char *newUpstream,
							bool forceBasebackup)
{
	if (routeKey == NULL || routeKey[0] == '\0')
	{
		log_error("cluster set-upstream requires a cluster name "
				  "(\"pg_walserver cluster set-upstream <name> ...\")");
		return false;
	}

	if (pgdata == NULL || pgdata[0] == '\0')
	{
		log_error("cluster set-upstream requires --pgdata (this "
				  "instance's own data root)");
		return false;
	}

	if (newUpstream == NULL || newUpstream[0] == '\0')
	{
		log_error("cluster set-upstream requires --pguri");
		return false;
	}

	char configPath[MAXPGPATH] = { 0 };

	config_file_path(pgdata, configFile, configPath, sizeof(configPath));

	WsRoute *routes = NULL;
	int routeCount = 0;

	if (!routes_load(configPath, &routes, &routeCount))
	{
		/* errors have already been logged */
		return false;
	}

	const WsRoute *route = routes_find_exact(routes, routeCount, routeKey);

	if (route == NULL)
	{
		log_error("No route \"%s\" in \"%s\"", routeKey, configPath);
		routes_free(routes);
		return false;
	}

	if (route->disabled)
	{
		log_error("Route \"%s\" is dropped (disabled) -- run "
				  "\"pg_walserver cluster enable %s\" to bring it back "
				  "before changing its upstream", routeKey, routeKey);
		routes_free(routes);
		return false;
	}

	char routePath[MAXPGPATH] = { 0 };

	strlcpy(routePath, route->path, sizeof(routePath));
	routes_free(routes);

	if (!routes_set_property(configPath, routeKey, "upstream", newUpstream))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * The upstream's own major version, and system identifier, may well
	 * have changed too (a failover to a different PostgreSQL version
	 * during an upgrade, most notably) -- re-fetch both against the new
	 * upstream right away, --force since the whole point of this command
	 * is that they are now expected to change, not an error to refuse.
	 */
	WsUpstreamTarget target = { 0 };

	if (cli_resolve_upstream(pgdata, configFile, routeKey, NULL, NULL, NULL,
							 NULL, NULL, &target))
	{
		uint64_t systemIdentifier = 0;

		if (!cli_fetch_systemid_run(&target, true, &systemIdentifier))
		{
			log_warn("Route \"%s\"'s own system identifier/upstream "
					 "version could not be refreshed against the new "
					 "upstream -- run \"pg_walserver fetch-systemid "
					 "--cluster %s --force\" by hand", routeKey, routeKey);
		}
	}

	log_info("Route \"%s\"'s own upstream is now \"%s\"", routeKey,
			 newUpstream);

	if (forceBasebackup)
	{
		WsUpstreamTarget backupTarget = { 0 };

		if (!cli_resolve_upstream(pgdata, configFile, routeKey, NULL, NULL,
								  NULL, NULL, NULL, &backupTarget) ||
			!cli_basebackup_run(&backupTarget, NULL, 0))
		{
			log_error("Route \"%s\"'s own upstream was updated, but the "
					  "base backup that --force-basebackup asked for "
					  "against it did not complete -- see the error(s) "
					  "logged above; run \"pg_walserver basebackup "
					  "--cluster %s\" by hand", routeKey, routeKey);
			return false;
		}
	}

	return true;
}


/* -----------------------------------------------------------------------
 * pg_walserver cluster register <name> | drop <name> | list | set-upstream <name>
 * ----------------------------------------------------------------------- */

/*
 * The pidfile/SIGHUP reload logic that used to be a private static
 * function right here now lives in cli_cluster.c as ws_cluster_reload_
 * running_server() (cli_cluster.h), shared with commands there that need
 * to reload a running server at a point other than "after the whole
 * command finished" (ws_cluster_drop_run()'s own --purge path, in
 * particular).
 */


static WsClusterRegisterOptions clusterRegisterOptions = { 0 };

static struct option clusterRegisterLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ "path", required_argument, NULL, 'P' },
	{ "pguri", required_argument, NULL, 'u' },
	{ "host", required_argument, NULL, 'h' },
	{ "port", required_argument, NULL, 'p' },
	{ "user", required_argument, NULL, 'U' },
	{ "hostname", required_argument, NULL, 'n' },
	{ "receivewal", required_argument, NULL, 'c' },
	{ "no-receivewal", no_argument, NULL, 'N' },
	{ "force", no_argument, NULL, 'F' },
	{ "ssl-self-signed", no_argument, NULL, 's' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_cluster_register_getopt parses "pg_walserver cluster register"'s
 * own flags (everything except the cluster name itself, a positional
 * argument left in argv for cli_cluster_register_command_run() below)
 * into the file-scope statics above.
 */
static int
cli_cluster_register_getopt(int argc, char **argv)
{
	optind = 0;
	clusterRegisterOptions = (WsClusterRegisterOptions) {
		0
	};
	ws_prefill_pgdata_from_env(clusterRegisterOptions.pgdata);

	/*
	 * The embedded receivewal worker is on by default now: running
	 * "cluster register" with no receivewal-related flag at all writes
	 * "receivewal = pull" (see write_route_section(), cli_cluster.c).
	 * --receivewal none / --no-receivewal are the explicit opt-out for a
	 * push-only (archive_command-only) route; --receivewal pull still
	 * works too, a no-op given this default.
	 */
	clusterRegisterOptions.receivewalPull = true;

	int c;

	while ((c = getopt_long(argc, argv, "D:f:P:u:h:p:U:n:c:NFs",
							clusterRegisterLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(clusterRegisterOptions.pgdata, optarg,
						sizeof(clusterRegisterOptions.pgdata));
				break;
			}

			case 'f':
			{
				strlcpy(clusterRegisterOptions.configFile, optarg,
						sizeof(clusterRegisterOptions.configFile));
				break;
			}

			case 'P':
			{
				strlcpy(clusterRegisterOptions.path, optarg,
						sizeof(clusterRegisterOptions.path));
				break;
			}

			case 'u':
			{
				strlcpy(clusterRegisterOptions.pguri, optarg,
						sizeof(clusterRegisterOptions.pguri));
				break;
			}

			case 'h':
			{
				strlcpy(clusterRegisterOptions.host, optarg,
						sizeof(clusterRegisterOptions.host));
				break;
			}

			case 'p':
			{
				strlcpy(clusterRegisterOptions.port, optarg,
						sizeof(clusterRegisterOptions.port));
				break;
			}

			case 'U':
			{
				strlcpy(clusterRegisterOptions.user, optarg,
						sizeof(clusterRegisterOptions.user));
				break;
			}

			case 'n':
			{
				strlcpy(clusterRegisterOptions.hostname, optarg,
						sizeof(clusterRegisterOptions.hostname));
				break;
			}

			case 'c':
			{
				if (streq(optarg, "pull"))
				{
					clusterRegisterOptions.receivewalPull = true;
				}
				else if (streq(optarg, "none"))
				{
					clusterRegisterOptions.receivewalPull = false;
				}
				else
				{
					log_fatal("Invalid --receivewal value \"%s\": recognized "
							  "values are \"pull\" (the default) and "
							  "\"none\"", optarg);
					exit(1);
				}
				break;
			}

			case 'N':
			{
				clusterRegisterOptions.receivewalPull = false;
				break;
			}

			case 'F':
			{
				clusterRegisterOptions.force = true;
				break;
			}

			case 's':
			{
				clusterRegisterOptions.sslSelfSigned = true;
				break;
			}

			default:
			{
				commandline_print_usage(&ws_root, stderr);
				exit(1);
			}
		}
	}

	return optind;
}


/*
 * cli_cluster_register_command_run reads the one positional argument
 * "cluster register" takes -- the cluster's own name, left in argv once
 * cli_cluster_register_getopt() has consumed every flag -- then runs
 * ws_cluster_register_run() against it and the options that getopt call
 * parsed above, and exit()s with its own result.
 */
static void
cli_cluster_register_command_run(int argc, char **argv)
{
	if (argc != 1)
	{
		log_fatal("cluster register requires exactly one argument: the "
				  "cluster's own name (\"pg_walserver cluster register "
				  "<name> ...\")");
		commandline_print_usage(&ws_root, stderr);
		exit(1);
	}

	strlcpy(clusterRegisterOptions.cluster, argv[0],
			sizeof(clusterRegisterOptions.cluster));

	if (!ws_cluster_register_run(&clusterRegisterOptions))
	{
		exit(1);
	}

	ws_cluster_reload_running_server(clusterRegisterOptions.pgdata);

	exit(0);
}


static char clusterDropPgdata[MAXPGPATH] = { 0 };
static char clusterDropConfigFile[MAXPGPATH] = { 0 };
static char clusterDropRoute[NAMEDATALEN + 16] = { 0 };
static bool clusterDropPurge = false;

static struct option clusterDropLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ "purge", no_argument, NULL, 'P' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_cluster_drop_getopt parses "pg_walserver cluster drop"'s own flags
 * (everything except the cluster name itself, a positional argument left
 * in argv for cli_cluster_drop_command_run() below) into the file-scope
 * statics above.
 */
static int
cli_cluster_drop_getopt(int argc, char **argv)
{
	optind = 0;
	clusterDropPgdata[0] = '\0';
	clusterDropConfigFile[0] = '\0';
	clusterDropRoute[0] = '\0';
	clusterDropPurge = false;
	ws_prefill_pgdata_from_env(clusterDropPgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:f:P", clusterDropLongOptions,
							NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(clusterDropPgdata, optarg, sizeof(clusterDropPgdata));
				break;
			}

			case 'f':
			{
				strlcpy(clusterDropConfigFile, optarg,
						sizeof(clusterDropConfigFile));
				break;
			}

			case 'P':
			{
				clusterDropPurge = true;
				break;
			}

			default:
			{
				commandline_print_usage(&ws_root, stderr);
				exit(1);
			}
		}
	}

	return optind;
}


/*
 * cli_cluster_drop_command_run reads the one positional argument
 * "cluster drop" takes -- the cluster's own name -- then runs ws_
 * cluster_drop_run() against it and the options cli_cluster_drop_getopt
 * parsed above, and exit()s with its own result.
 */
static void
cli_cluster_drop_command_run(int argc, char **argv)
{
	if (argc != 1)
	{
		log_fatal("cluster drop requires exactly one argument: the "
				  "cluster's own name (\"pg_walserver cluster drop "
				  "<name> ...\")");
		commandline_print_usage(&ws_root, stderr);
		exit(1);
	}

	strlcpy(clusterDropRoute, argv[0], sizeof(clusterDropRoute));

	if (!ws_cluster_drop_run(clusterDropPgdata, clusterDropConfigFile,
							 clusterDropRoute, clusterDropPurge))
	{
		exit(1);
	}

	ws_cluster_reload_running_server(clusterDropPgdata);

	exit(0);
}


static char clusterEnablePgdata[MAXPGPATH] = { 0 };
static char clusterEnableConfigFile[MAXPGPATH] = { 0 };
static char clusterEnableRoute[NAMEDATALEN + 16] = { 0 };

static struct option clusterEnableLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_cluster_enable_getopt parses "pg_walserver cluster enable"'s own
 * flags (everything except the cluster name itself, a positional
 * argument left in argv for cli_cluster_enable_command_run() below) into
 * the file-scope statics above.
 */
static int
cli_cluster_enable_getopt(int argc, char **argv)
{
	optind = 0;
	clusterEnablePgdata[0] = '\0';
	clusterEnableConfigFile[0] = '\0';
	clusterEnableRoute[0] = '\0';
	ws_prefill_pgdata_from_env(clusterEnablePgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:f:", clusterEnableLongOptions,
							NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(clusterEnablePgdata, optarg,
						sizeof(clusterEnablePgdata));
				break;
			}

			case 'f':
			{
				strlcpy(clusterEnableConfigFile, optarg,
						sizeof(clusterEnableConfigFile));
				break;
			}

			default:
			{
				commandline_print_usage(&ws_root, stderr);
				exit(1);
			}
		}
	}

	return optind;
}


/*
 * cli_cluster_enable_command_run reads the one positional argument
 * "cluster enable" takes -- the cluster's own name -- then runs ws_
 * cluster_enable_run() against it and the options cli_cluster_enable_
 * getopt parsed above, and exit()s with its own result.
 */
static void
cli_cluster_enable_command_run(int argc, char **argv)
{
	if (argc != 1)
	{
		log_fatal("cluster enable requires exactly one argument: the "
				  "cluster's own name (\"pg_walserver cluster enable "
				  "<name> ...\")");
		commandline_print_usage(&ws_root, stderr);
		exit(1);
	}

	strlcpy(clusterEnableRoute, argv[0], sizeof(clusterEnableRoute));

	if (!ws_cluster_enable_run(clusterEnablePgdata, clusterEnableConfigFile,
							   clusterEnableRoute))
	{
		exit(1);
	}

	ws_cluster_reload_running_server(clusterEnablePgdata);

	exit(0);
}


static char clusterListPgdata[MAXPGPATH] = { 0 };
static char clusterListConfigFile[MAXPGPATH] = { 0 };
static bool clusterListShowUpstream = false;
static bool clusterListShowDisabled = false;

static struct option clusterListLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ "upstream", no_argument, NULL, 'u' },
	{ "disabled", no_argument, NULL, 'x' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_cluster_list_getopt parses "pg_walserver cluster list"'s own flags
 * into the file-scope statics above.
 */
static int
cli_cluster_list_getopt(int argc, char **argv)
{
	optind = 0;
	clusterListPgdata[0] = '\0';
	clusterListConfigFile[0] = '\0';
	clusterListShowUpstream = false;
	clusterListShowDisabled = false;
	ws_prefill_pgdata_from_env(clusterListPgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:f:ux", clusterListLongOptions,
							NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(clusterListPgdata, optarg, sizeof(clusterListPgdata));
				break;
			}

			case 'f':
			{
				strlcpy(clusterListConfigFile, optarg,
						sizeof(clusterListConfigFile));
				break;
			}

			case 'u':
			{
				clusterListShowUpstream = true;
				break;
			}

			case 'x':
			{
				clusterListShowDisabled = true;
				break;
			}

			default:
			{
				commandline_print_usage(&ws_root, stderr);
				exit(1);
			}
		}
	}

	return optind;
}


/*
 * cli_cluster_list_command_run runs "pg_walserver cluster list" against
 * the options cli_cluster_list_getopt parsed above, then exit()s with
 * its own result.
 */
static void
cli_cluster_list_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	exit(ws_cluster_list_run(clusterListPgdata, clusterListConfigFile,
							 clusterListShowUpstream,
							 clusterListShowDisabled) ? 0 : 1);
}


static char clusterPrunePgdata[MAXPGPATH] = { 0 };
static char clusterPruneConfigFile[MAXPGPATH] = { 0 };

static struct option clusterPruneLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_cluster_prune_getopt parses "pg_walserver cluster prune"'s own
 * flags into the file-scope statics above.
 */
static int
cli_cluster_prune_getopt(int argc, char **argv)
{
	optind = 0;
	clusterPrunePgdata[0] = '\0';
	clusterPruneConfigFile[0] = '\0';
	ws_prefill_pgdata_from_env(clusterPrunePgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:f:", clusterPruneLongOptions,
							NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(clusterPrunePgdata, optarg, sizeof(clusterPrunePgdata));
				break;
			}

			case 'f':
			{
				strlcpy(clusterPruneConfigFile, optarg,
						sizeof(clusterPruneConfigFile));
				break;
			}

			default:
			{
				commandline_print_usage(&ws_root, stderr);
				exit(1);
			}
		}
	}

	return optind;
}


/*
 * cli_cluster_prune_command_run runs "pg_walserver cluster prune" against
 * the options cli_cluster_prune_getopt parsed above, then exit()s with
 * its own result.
 */
static void
cli_cluster_prune_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	exit(ws_cluster_prune_run(clusterPrunePgdata,
							  clusterPruneConfigFile) ? 0 : 1);
}


static char clusterSetUpstreamPgdata[MAXPGPATH] = { 0 };
static char clusterSetUpstreamConfigFile[MAXPGPATH] = { 0 };
static char clusterSetUpstreamRoute[NAMEDATALEN + 16] = { 0 };
static char clusterSetUpstreamUpstream[MAXCONNINFO] = { 0 };
static bool clusterSetUpstreamForceBasebackup = false;

static struct option clusterSetUpstreamLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ "pguri", required_argument, NULL, 'u' },
	{ "force-basebackup", no_argument, NULL, 'F' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_cluster_set_upstream_getopt parses "pg_walserver cluster
 * set-upstream"'s own flags (everything except the cluster name itself,
 * a positional argument left in argv for cli_cluster_set_upstream_
 * command_run() below) into the file-scope statics above.
 */
static int
cli_cluster_set_upstream_getopt(int argc, char **argv)
{
	optind = 0;
	clusterSetUpstreamPgdata[0] = '\0';
	clusterSetUpstreamConfigFile[0] = '\0';
	clusterSetUpstreamRoute[0] = '\0';
	clusterSetUpstreamUpstream[0] = '\0';
	clusterSetUpstreamForceBasebackup = false;
	ws_prefill_pgdata_from_env(clusterSetUpstreamPgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:f:u:F",
							clusterSetUpstreamLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(clusterSetUpstreamPgdata, optarg,
						sizeof(clusterSetUpstreamPgdata));
				break;
			}

			case 'f':
			{
				strlcpy(clusterSetUpstreamConfigFile, optarg,
						sizeof(clusterSetUpstreamConfigFile));
				break;
			}

			case 'u':
			{
				strlcpy(clusterSetUpstreamUpstream, optarg,
						sizeof(clusterSetUpstreamUpstream));
				break;
			}

			case 'F':
			{
				clusterSetUpstreamForceBasebackup = true;
				break;
			}

			default:
			{
				commandline_print_usage(&ws_root, stderr);
				exit(1);
			}
		}
	}

	return optind;
}


/*
 * cli_cluster_set_upstream_command_run reads the one positional argument
 * "cluster set-upstream" takes -- the cluster's own name -- then runs
 * ws_cluster_set_upstream_run() against it and the options cli_cluster_
 * set_upstream_getopt parsed above, and exit()s with its own result.
 */
static void
cli_cluster_set_upstream_command_run(int argc, char **argv)
{
	if (argc != 1)
	{
		log_fatal("cluster set-upstream requires exactly one argument: "
				  "the cluster's own name (\"pg_walserver cluster "
				  "set-upstream <name> ...\")");
		commandline_print_usage(&ws_root, stderr);
		exit(1);
	}

	strlcpy(clusterSetUpstreamRoute, argv[0], sizeof(clusterSetUpstreamRoute));

	if (!ws_cluster_set_upstream_run(clusterSetUpstreamPgdata,
									 clusterSetUpstreamConfigFile,
									 clusterSetUpstreamRoute,
									 clusterSetUpstreamUpstream,
									 clusterSetUpstreamForceBasebackup))
	{
		exit(1);
	}

	ws_cluster_reload_running_server(clusterSetUpstreamPgdata);

	exit(0);
}
