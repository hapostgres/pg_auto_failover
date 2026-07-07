/*
 * src/bin/pgaftest/compose_gen.c
 *   Generate a docker-compose.yml and per-node pg_autoctl_node.ini files
 *   from a TestCluster spec.
 *
 * Design principle — uniform containers
 * ======================================
 * Every container in the generated compose file uses:
 *   - the same Docker image (PGAF_IMAGE env var or a local build)
 *   - the same command:  pg_autoctl node run /etc/pgaf/node.ini
 *   - the same config file location inside the container
 *
 * Node-specific behaviour (monitor vs postgres vs coordinator vs worker,
 * candidate-priority, replication-quorum, formation, group) is encoded in
 * the per-node pg_autoctl_node.ini file, which is bind-mounted into the
 * container at /etc/pgaf/node.ini.
 *
 * This means:
 *   - No bespoke container command per node type.
 *   - The running supervisor watches /etc/pgaf/node.ini via inotify/mtime
 *     and converges mutable settings when the file is edited externally.
 *   - Changing candidate-priority or replication-quorum for a node is as
 *     simple as editing the ini file on the host — the container picks it
 *     up without a restart.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "compose_gen.h"
#include "pgsetup.h"
#include "log.h"
#include "file_utils.h"   /* sformat */
#include "string_utils.h"

/*
 * Monitor URI used by data nodes to register.
 * The monitor hostname is always "monitor" inside the compose network.
 */
#define MONITOR_PGURI \
	"postgresql://autoctl_node@monitor/pg_auto_failover"

/* Fixed path inside every container */
#define NODE_INI_PATH "/etc/pgaf/node.ini"

/* Fixed PGDATA location inside every container */
#define NODE_PGDATA "/var/lib/postgres/pgaf"

/* Debian-style PGDATA prefix: /var/lib/postgresql/<version>/<cluster> */
#define DEBIAN_PGDATA_PREFIX "/var/lib/postgresql"

static const char *
debian_pg_version(void)
{
	const char *v = getenv("PGVERSION"); /* IGNORE-BANNED */
	return (v && v[0]) ? v : "17";
}


/* SSL cert directory inside every container when ssl needs CA-signed certs */
#define SSL_DIR_IN_CONTAINER "/etc/pgaf/ssl"

/*
 * Shell snippet prepended to the container start command when CA-signed certs
 * are in use.
 *
 * - Server cert/key copied to /var/lib/postgres/ (volume root, writable) so
 *   PostgreSQL can open the key file as the container user.
 * - Client cert/key copied to ~/.postgresql/ so libpq uses them automatically
 *   for all outbound connections (cert auth to monitor and peers).
 * - CA cert copied to ~/.postgresql/root.crt so libpq trusts server certs.
 */
#define SSL_COPY_CERTS_CMD \
	"cp " SSL_DIR_IN_CONTAINER "/server/server.crt /var/lib/postgres/server.crt" \
							   " && cp " SSL_DIR_IN_CONTAINER \
	"/server/server.key /var/lib/postgres/server.key" \
	" && chmod 0600 /var/lib/postgres/server.key" \
	" && mkdir -p /var/lib/postgres/.postgresql" \
	" && cp " \
	SSL_DIR_IN_CONTAINER \
	"/client/postgresql.crt /var/lib/postgres/.postgresql/postgresql.crt" \
	" && cp " \
	SSL_DIR_IN_CONTAINER \
	"/client/postgresql.key /var/lib/postgres/.postgresql/postgresql.key" \
	" && chmod 0600 /var/lib/postgres/.postgresql/postgresql.key" \
	" && cp " \
	SSL_DIR_IN_CONTAINER "/ca.crt /var/lib/postgres/.postgresql/root.crt" \
						 " &&"


/*
 * ssl_needs_certs returns true when the ssl mode requires CA-signed server
 * certificates to be generated on the host: verify-ca and verify-full.
 */
static bool
ssl_needs_certs(const char *ssl)
{
	return strcmp(ssl, "verify-ca") == 0 || strcmp(ssl, "verify-full") == 0;
}


/*
 * run_openssl runs a single openssl command (argv-style) and returns true on
 * success.  The last element of argv[] must be NULL.
 */
static bool
run_openssl(const char *const *argv)
{
	char cmd[4096] = "openssl";
	int pos = strlen(cmd);

	for (int i = 1; argv[i]; i++)
	{
		pos += sformat(cmd + pos, sizeof(cmd) - pos, " %s", argv[i]);
		if (pos >= (int) sizeof(cmd) - 1)
		{
			log_error("openssl command too long");
			return false;
		}
	}

	log_debug("Running: %s", cmd);
	int rc = system(cmd);
	if (rc != 0)
	{
		log_error("Command failed (exit %d): %s", rc, cmd);
		return false;
	}
	return true;
}


/*
 * compose_gen_write_ssl_certs generates the CA and per-service server
 * certificates in <workDir>/ssl/ using openssl.  Called from
 * runner_compose_generate() before compose_gen_write() so the volume paths
 * already exist when the compose file is written.
 *
 * Layout on the host:
 *   ssl/ca.crt              — shared CA certificate
 *   ssl/ca.key              — CA private key (stays on host)
 *   ssl/<svc>/server.crt    — server certificate for service <svc>
 *   ssl/<svc>/server.key    — server private key for service <svc>
 *
 * The CA cert and the per-service dir are bind-mounted into each container so
 * pg_autoctl can find them at the paths recorded in the ini [ssl] section.
 */
bool
compose_gen_write_ssl_certs(const TestCluster *cluster, const char *workDir)
{
	if (!ssl_needs_certs(cluster->ssl))
	{
		return true;
	}

	char sslDir[MAXPGPATH];
	sformat(sslDir, sizeof(sslDir), "%s/ssl", workDir);

	/* Create ssl/ directory */
	if (mkdir(sslDir, 0755) != 0 && errno != EEXIST)
	{
		log_error("Failed to create %s: %m", sslDir);
		return false;
	}

	char caKey[MAXPGPATH], caCrt[MAXPGPATH];
	sformat(caKey, sizeof(caKey), "%s/ca.key", sslDir);
	sformat(caCrt, sizeof(caCrt), "%s/ca.crt", sslDir);

	/* Generate CA key + self-signed cert with basicConstraints=CA:TRUE */
	bool caRegenerated = false;
	if (access(caCrt, F_OK) != 0)
	{
		const char *genCA[] = {
			"openssl", "req", "-new", "-x509", "-nodes", "-text",
			"-days", "3650",
			"-keyout", caKey,
			"-out", caCrt,
			"-subj", "/CN=root.pgautofailover.ca",
			"-addext", "basicConstraints=critical,CA:TRUE",
			"-addext", "keyUsage=critical,keyCertSign,cRLSign",
			NULL
		};
		if (!run_openssl(genCA))
		{
			return false;
		}

		chmod(caKey, 0600); /* CA key stays private; not mounted into containers */
		log_info("Generated CA certificate at %s", caCrt);
		caRegenerated = true;
	}

	/*
	 * Generate server cert for each service: monitor + all nodes.
	 * We build a flat list of service names to iterate.
	 */
	const char *services[PGAF_MAX_NODES + 1]; /* +1 for monitor */
	int svcCount = 0;

	if (cluster->withMonitor)
	{
		services[svcCount++] = "monitor";
	}

	if (cluster->secondMonitorName[0])
	{
		services[svcCount++] = cluster->secondMonitorName;
	}

	for (int fi = 0; fi < cluster->formationCount; fi++)
	{
		const TestFormation *form = &cluster->formations[fi];
		for (int ni = 0; ni < form->nodeCount; ni++)
		{
			services[svcCount++] = form->nodes[ni].name;
		}
	}

	char serialFile[MAXPGPATH];
	sformat(serialFile, sizeof(serialFile), "%s/ca.srl", sslDir);

	for (int i = 0; i < svcCount; i++)
	{
		const char *svc = services[i];
		char svcDir[MAXPGPATH];
		sformat(svcDir, sizeof(svcDir), "%s/%s", sslDir, svc);

		if (mkdir(svcDir, 0755) != 0 && errno != EEXIST)
		{
			log_error("Failed to create %s: %m", svcDir);
			return false;
		}

		char srvKey[MAXPGPATH], srvCrt[MAXPGPATH], srvCsr[MAXPGPATH];
		sformat(srvKey, sizeof(srvKey), "%s/server.key", svcDir);
		sformat(srvCrt, sizeof(srvCrt), "%s/server.crt", svcDir);
		sformat(srvCsr, sizeof(srvCsr), "%s/server.csr", svcDir);

		if (access(srvCrt, F_OK) == 0 && !caRegenerated)
		{
			continue; /* already generated and CA hasn't changed */
		}
		char cn[128];
		sformat(cn, sizeof(cn), "/CN=%s.pgautofailover.ca", svc);

		const char *genReq[] = {
			"openssl", "req", "-new", "-nodes", "-text",
			"-out", srvCsr, "-keyout", srvKey,
			"-subj", cn,
			NULL
		};
		if (!run_openssl(genReq))
		{
			return false;
		}

		/* 0644: container user must read this for the cp-and-chmod step */
		chmod(srvKey, 0644);

		const char *signSrv[] = {
			"openssl", "x509", "-req", "-in", srvCsr,
			"-text", "-days", "365",
			"-CA", caCrt, "-CAkey", caKey, "-CAcreateserial",
			"-out", srvCrt,
			NULL
		};
		if (!run_openssl(signSrv))
		{
			return false;
		}

		log_info("Generated server certificate for %s at %s", svc, srvCrt);
	}

	/*
	 * Generate client certificate with CN=autoctl_node, used by pg_autoctl
	 * processes to authenticate to PostgreSQL when ssl.auth = cert.
	 * Placed in ssl/client/ and mounted read-only into each container;
	 * the container start command copies it to ~/.postgresql/ with correct
	 * ownership so libpq can use it for all outbound connections.
	 */
	char clientDir[MAXPGPATH], clientCrt[MAXPGPATH],
		 clientKey[MAXPGPATH], clientCsr[MAXPGPATH];
	sformat(clientDir, sizeof(clientDir), "%s/client", sslDir);
	sformat(clientCrt, sizeof(clientCrt), "%s/postgresql.crt", clientDir);
	sformat(clientKey, sizeof(clientKey), "%s/postgresql.key", clientDir);
	sformat(clientCsr, sizeof(clientCsr), "%s/postgresql.csr", clientDir);

	if (mkdir(clientDir, 0755) != 0 && errno != EEXIST)
	{
		log_error("Failed to create %s: %m", clientDir);
		return false;
	}

	if (access(clientCrt, F_OK) != 0 || caRegenerated)
	{
		const char *genReq[] = {
			"openssl", "req", "-new", "-nodes", "-text",
			"-out", clientCsr, "-keyout", clientKey,
			"-subj", "/CN=autoctl_node",
			NULL
		};
		if (!run_openssl(genReq))
		{
			return false;
		}

		/* 0644: container user must read this for the cp-and-chmod step */
		chmod(clientKey, 0644);

		const char *signClient[] = {
			"openssl", "x509", "-req", "-in", clientCsr,
			"-text", "-days", "365",
			"-CA", caCrt, "-CAkey", caKey, "-CAcreateserial",
			"-out", clientCrt,
			NULL
		};
		if (!run_openssl(signClient))
		{
			return false;
		}

		log_info("Generated client certificate at %s", clientCrt);
	}

	return true;
}


/*
 * image_stanza writes either `image:` (when PGAF_IMAGE is set or the cluster
 * spec provides an image) or a `build:` stanza.
 */
static void
write_image_stanza_target(FILE *f, const TestCluster *cluster,
						  const char *contextDir, const char *target)
{
	/*
	 * The cluster image (from `image "..."` in the spec) is for data services
	 * only.  The pgaftest service always builds from --target pgaftest so that
	 * the test runner binary is present regardless of which cluster image is
	 * used.
	 */
	bool isTestRunner = (strcmp(target, "pgaftest") == 0);

	/* cluster-level image overrides PGAF_IMAGE env var */
	const char *img = cluster->image[0] ? cluster->image : getenv("PGAF_IMAGE"); /* IGNORE-BANNED */

	if (img && *img && !isTestRunner)
	{
		fformat(f, "    image: \"%s\"\n", img);
	}
	else
	{
		fformat(f,
				"    build:\n"
				"      context: \"%s\"\n"
				"      target: %s\n",
				contextDir, target);
	}
}


static void
write_image_stanza(FILE *f, const TestCluster *cluster, const char *contextDir)
{
	write_image_stanza_target(f, cluster, contextDir, "run");
}


/* -----------------------------------------------------------------------
 * compose_gen_write — the docker-compose.yml
 * ----------------------------------------------------------------------- */

/*
 * compose_gen_write generates the docker-compose.yml.
 *
 * Every service uses:
 *   command: pg_autoctl node run /etc/pgaf/node.ini
 *
 * The per-node ini file is bind-mounted from the workdir.
 */

/*
 * Pick a random free TCP port in the range [32768, 60999].
 * Tries up to 64 candidates; exits with a log_fatal if none found.
 *
 * Ports already handed out in this process are remembered in a static array
 * so that consecutive calls never return the same port even though the OS
 * releases the probe socket between calls.
 */
static int
pick_free_port(void)
{
	static int reserved[64];
	static int reserved_count = 0;

	srand((unsigned) time(NULL) ^ (unsigned) getpid());
	for (int attempt = 0; attempt < 64; attempt++)
	{
		int port = 32768 + rand() % (60999 - 32768 + 1);

		/* skip ports already given out this process */
		bool already_used = false;
		for (int i = 0; i < reserved_count; i++)
		{
			if (reserved[i] == port)
			{
				already_used = true;
				break;
			}
		}
		if (already_used)
		{
			continue;
		}

		int fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0)
		{
			continue;
		}

		int opt = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

		struct sockaddr_in addr = {
			.sin_family = AF_INET,
			.sin_port = htons((uint16_t) port),
			.sin_addr = { .s_addr = htonl(INADDR_LOOPBACK) },
		};
		int rc = bind(fd, (struct sockaddr *) &addr, sizeof(addr));
		close(fd);

		if (rc == 0)
		{
			if (reserved_count < 64)
			{
				reserved[reserved_count++] = port;
			}
			return port;
		}
	}
	log_fatal("Could not find a free TCP port for monitor exposure");
	exit(1);
}


bool
compose_gen_write(TestCluster *cluster,
				  const char *path,
				  const char *projectName,
				  const char *contextDir,
				  const char *specFile)
{
	FILE *f = fopen(path, "w"); /* IGNORE-BANNED */
	if (!f)
	{
		log_error("Failed to open \"%s\" for writing: %m", path);
		return false;
	}

	fformat(f, "# Generated by pgaftest — do not edit manually\n");
	fformat(f, "# Project: %s\n\n", projectName);
	fformat(f, "services:\n");

	/* ---- monitor (optional) ---- */
	if (cluster->withMonitor)
	{
		/* allocate a random free host port for the monitor's postgres */
		if (cluster->monitorHostPort == 0)
		{
			cluster->monitorHostPort = pick_free_port();
		}

		fformat(f, "  monitor:\n");
		if (cluster->monitorDebianCluster[0])
		{
			write_image_stanza_target(f, cluster, contextDir, "debian");
		}
		else if (cluster->monitorImageTarget[0])
		{
			write_image_stanza_target(f, cluster, contextDir,
									  cluster->monitorImageTarget);
		}
		else
		{
			write_image_stanza(f, cluster, contextDir);
		}

		char monitor_pgdata[MAXPGPATH];
		if (cluster->monitorDebianCluster[0])
		{
			sformat(monitor_pgdata, sizeof(monitor_pgdata),
					DEBIAN_PGDATA_PREFIX "/%s/%s",
					debian_pg_version(), cluster->monitorDebianCluster);
		}
		else
		{
			strlcpy(monitor_pgdata, NODE_PGDATA, sizeof(monitor_pgdata));
		}

		fformat(f,
				"    hostname: monitor\n"
				"    volumes:\n"
				"      - monitor_data:/var/lib/postgres:rw\n"
				"      - ./monitor.ini:" NODE_INI_PATH ":ro\n");
		if (ssl_needs_certs(cluster->ssl))
		{
			fformat(f,
					"      - ./ssl/ca.crt:" SSL_DIR_IN_CONTAINER "/ca.crt:ro\n"
																 "      - ./ssl/monitor:"
					SSL_DIR_IN_CONTAINER "/server:ro\n"
										 "      - ./ssl/client:"
					SSL_DIR_IN_CONTAINER "/client:ro\n");
		}
		if (cluster->bindSource)
		{
			fformat(f,
					"      - %s:/usr/src/pg_auto_failover:rw\n", contextDir);
		}
		fformat(f,
				"    environment:\n"
				"      PGDATA: %s\n",
				monitor_pgdata);
		if (cluster->extensionVersion[0])
		{
			fformat(f,
					"      PG_AUTOCTL_EXTENSION_VERSION: \"%s\"\n",
					cluster->extensionVersion);
		}
		fformat(f,
				"    ports:\n"
				"      - \"%d:5432\"\n",
				cluster->monitorHostPort);

		fformat(f,
				"    command: [\"/bin/sh\", \"-c\","
				" \"%s rm -f /tmp/pg_autoctl%s/pg_autoctl.pid"
				" && exec pg_autoctl node run " NODE_INI_PATH "\"]\n"
															  "    stop_grace_period: 60s\n\n",
				ssl_needs_certs(cluster->ssl) ? SSL_COPY_CERTS_CMD : "",
				monitor_pgdata);
	}

	/* ---- second monitor (initially stopped, for replace-monitor tests) ---- */
	if (cluster->secondMonitorName[0])
	{
		if (cluster->secondMonitorHostPort == 0)
		{
			cluster->secondMonitorHostPort = pick_free_port();
		}

		const char *svc = cluster->secondMonitorName;
		fformat(f, "  %s:\n", svc);
		write_image_stanza(f, cluster, contextDir);
		fformat(f,
				"    hostname: %s\n"
				"    volumes:\n"
				"      - %s_data:/var/lib/postgres:rw\n"
				"      - ./%s.ini:" NODE_INI_PATH ":ro\n",
				svc, svc, svc);
		if (ssl_needs_certs(cluster->ssl))
		{
			fformat(f,
					"      - ./ssl/ca.crt:" SSL_DIR_IN_CONTAINER "/ca.crt:ro\n"
																 "      - ./ssl/%s:"
					SSL_DIR_IN_CONTAINER "/server:ro\n"
										 "      - ./ssl/client:"
					SSL_DIR_IN_CONTAINER "/client:ro\n",
					svc);
		}
		if (cluster->bindSource)
		{
			fformat(f,
					"      - %s:/usr/src/pg_auto_failover:rw\n", contextDir);
		}
		fformat(f,
				"    environment:\n"
				"      PGDATA: " NODE_PGDATA "\n");
		fformat(f,
				"    ports:\n"
				"      - \"%d:5432\"\n",
				cluster->secondMonitorHostPort);
		fformat(f,
				"    command: [\"/bin/sh\", \"-c\","
				" \"%s rm -f /tmp/pg_autoctl" NODE_PGDATA "/pg_autoctl.pid"
														  " && exec pg_autoctl node run "
				NODE_INI_PATH "\"]\n"
							  "    stop_grace_period: 60s\n\n",
				ssl_needs_certs(cluster->ssl) ? SSL_COPY_CERTS_CMD : "");
	}

	/* ---- data nodes — iterate all formations ---- */

	/*
	 * Non-default formations are listed in [formation <name>] sections of the
	 * monitor.ini.  pg_autoctl node run reads them and creates each one after
	 * the monitor is initialized, before starting the supervisor.
	 * Data nodes retry registration until their formation exists.
	 */
	const TestNode *firstNode = NULL;

	for (int fi = 0; fi < cluster->formationCount; fi++)
	{
		const TestFormation *form = &cluster->formations[fi];

		for (int ni = 0; ni < form->nodeCount; ni++)
		{
			const TestNode *n = &form->nodes[ni];
			fformat(f, "  %s:\n", n->name);
			if (n->debianCluster[0])
			{
				write_image_stanza_target(f, cluster, contextDir, "debian");
			}
			else
			{
				write_image_stanza(f, cluster, contextDir);
			}

			/* debian PGDATA: /var/lib/postgresql/<ver>/<cluster> */
			char node_pgdata[MAXPGPATH];
			if (n->debianCluster[0])
			{
				sformat(node_pgdata, sizeof(node_pgdata),
						DEBIAN_PGDATA_PREFIX "/%s/%s",
						debian_pg_version(), n->debianCluster);
			}
			else
			{
				strlcpy(node_pgdata, NODE_PGDATA, sizeof(node_pgdata));
			}

			fformat(f,
					"    hostname: %s\n"
					"    volumes:\n"
					"      - %s_data:/var/lib/postgres:rw\n"
					"      - ./%s.ini:" NODE_INI_PATH ":%s\n",
					n->name,
					n->name,
					n->name,
					n->launchDeferred ? "rw" : "ro");
			if (ssl_needs_certs(cluster->ssl))
			{
				fformat(f,
						"      - ./ssl/ca.crt:" SSL_DIR_IN_CONTAINER "/ca.crt:ro\n"
																	 "      - ./ssl/%s:"
						SSL_DIR_IN_CONTAINER "/server:ro\n"
											 "      - ./ssl/client:"
						SSL_DIR_IN_CONTAINER "/client:ro\n",
						n->name);
			}
			for (int vi = 0; vi < n->volumeCount; vi++)
			{
				fformat(f, "      - %s_%s:%s:rw\n",
						n->volumes[vi].name, n->name, n->volumes[vi].path);
			}
			fformat(f,
					"    environment:\n"
					"      PGDATA: %s\n"
					"      PGUSER: demo\n"
					"      PGDATABASE: demo\n"
					"      PG_AUTOCTL_TEST_DELAY: \"1\"\n"
					"    expose:\n"
					"      - 5432\n",
					node_pgdata);

			/*
			 * No depends_on: keeper retry loops handle monitor not yet ready,
			 * and the formation wait in keeper_register_and_init handles the
			 * case where the formation hasn't been created yet.
			 */

			/* per-node ssl override may differ from cluster default */
			const char *node_ssl = n->ssl[0] ? n->ssl : cluster->ssl;
			fformat(f,
					"    command: [\"/bin/sh\", \"-c\","
					" \"%s rm -f /tmp/pg_autoctl%s/pg_autoctl.pid"
					" && exec pg_autoctl node run " NODE_INI_PATH "\"]\n"
																  "    stop_grace_period: 60s\n",
					ssl_needs_certs(node_ssl) ? SSL_COPY_CERTS_CMD : "",
					node_pgdata);

			/*
			 * With a monitor: the first data node gets a healthcheck so that
			 * subsequent nodes use service_healthy depends_on.  This ensures
			 * node1 has registered with the monitor (and become the initial
			 * primary) before any other node starts, making the initial
			 * election deterministic.
			 *
			 * Without a monitor (no-monitor nodes): the FSM is driven
			 * manually via pg_autoctl manual fsm assign, so postgres is not
			 * running when the container starts.  No healthcheck; subsequent
			 * nodes use service_started so they launch as soon as node1 has
			 * started (they don't need postgres to be ready yet).
			 */
			if (!firstNode && cluster->withMonitor)
			{
				fformat(f,
						"    healthcheck:\n"
						"      test: [\"CMD\", \"pg_autoctl\", \"status\","
						" \"--pgdata\", \"%s\"]\n"
						"      interval: 2s\n"
						"      timeout: 5s\n"
						"      retries: 30\n"
						"      start_period: 15s\n",
						node_pgdata);
			}

			if (firstNode)
			{
				fformat(f,
						"    depends_on:\n"
						"      %s:\n"
						"        condition: %s\n",
						firstNode->name,
						cluster->withMonitor ? "service_healthy" : "service_started");
			}
			fformat(f, "\n");

			if (!firstNode)
			{
				firstNode = n;
			}
		}
	}

	/*
	 * ---- pgaftest service ----
	 *
	 * When specFile is provided, include a pgaftest service that runs the
	 * spec from inside the compose network.  Benefits:
	 *   - direct TCP to monitor/nodes — LISTEN/NOTIFY works, no pg_hba issue
	 *   - pg_autoctl commands run locally (just need --monitor <pguri>)
	 *   - compose stop/start and network disconnect/connect via docker socket
	 *
	 * CI usage: docker compose up --exit-code-from pgaftest
	 */
	if (specFile)
	{
		/* Determine which service pgaftest must wait for before starting. */
		/* It needs the cluster fully ready: monitor healthy + all nodes started. */
		fformat(f, "  pgaftest:\n");
		write_image_stanza_target(f, cluster, contextDir, "pgaftest");

		/* Build monitor pguri for PG_AUTOCTL_MONITOR */
		char monitorPguri[512];
		if (cluster->monitorPassword[0])
		{
			sformat(monitorPguri, sizeof(monitorPguri),
					"postgres://autoctl_node:%s@monitor/pg_auto_failover",
					cluster->monitorPassword);
		}
		else
		{
			sformat(monitorPguri, sizeof(monitorPguri),
					"postgres://autoctl_node@monitor/pg_auto_failover");
		}

		/*
		 * pgaftest has its own ping loop that waits for the monitor before
		 * starting, so no depends_on is needed.
		 */

		/* Derive work dir (directory containing the compose file). */
		char workDir[MAXPGPATH];
		strlcpy(workDir, path, sizeof(workDir));
		char *slash = strrchr(workDir, '/');
		if (slash)
		{
			*slash = '\0';
		}
		else
		{
			strlcpy(workDir, ".", sizeof(workDir));
		}

		fformat(f,
				"    user: root\n"
				"    volumes:\n"
				"      - %s:/spec.pgaf:ro\n"
				"      - /var/run/docker.sock:/var/run/docker.sock\n"
				"      - %s:%s:ro\n",
				specFile, workDir, workDir);
		if (ssl_needs_certs(cluster->ssl))
		{
			fformat(f,
					"      - %s/ssl/ca.crt:/root/.postgresql/root.crt:ro\n"
					"      - %s/ssl/client/postgresql.crt:/root/.postgresql/postgresql.crt:ro\n"
					"      - %s/ssl/client/postgresql.key:/root/.postgresql/postgresql.key:ro\n",
					workDir, workDir, workDir);
		}
		fformat(f,
				"    environment:\n"
				"      PGAFTEST_COMPOSE_SERVICE: \"1\"\n"
				"      PGAFTEST_HOST_WORK_DIR: \"%s\"\n"
				"      COMPOSE_PROJECT_NAME: \"%s\"\n",
				workDir, projectName);
		if (cluster->withMonitor)
		{
			fformat(f, "      PG_AUTOCTL_MONITOR: \"%s\"\n", monitorPguri);
		}
		fformat(f, "    command: [\"pgaftest\", \"run\", \"/spec.pgaf\"]\n\n");
	}

	/* ---- volumes ---- */
	fformat(f, "volumes:\n");
	if (cluster->withMonitor)
	{
		fformat(f, "  monitor_data:\n");
	}
	if (cluster->secondMonitorName[0])
	{
		fformat(f, "  %s_data:\n", cluster->secondMonitorName);
	}
	for (int fi = 0; fi < cluster->formationCount; fi++)
	{
		const TestFormation *form = &cluster->formations[fi];
		for (int ni = 0; ni < form->nodeCount; ni++)
		{
			const TestNode *n = &form->nodes[ni];
			fformat(f, "  %s_data:\n", n->name);
			for (int vi = 0; vi < n->volumeCount; vi++)
			{
				fformat(f, "  %s_%s:\n", n->volumes[vi].name, n->name);
			}
		}
	}

	fclose(f);
	log_info("Wrote docker-compose.yml to \"%s\"", path);
	return true;
}


/* -----------------------------------------------------------------------
 * compose_gen_write_monitor_ini
 * ----------------------------------------------------------------------- */
bool
compose_gen_write_monitor_ini(const TestCluster *cluster, const char *dir)
{
	if (!cluster->withMonitor)
	{
		return true;
	}

	char path[1024];
	sformat(path, sizeof(path), "%s/monitor.ini", dir);

	FILE *f = fopen(path, "w"); /* IGNORE-BANNED */
	if (!f)
	{
		log_error("Failed to open \"%s\" for writing: %m", path);
		return false;
	}

	char mon_pgdata[MAXPGPATH];
	if (cluster->monitorDebianCluster[0])
	{
		sformat(mon_pgdata, sizeof(mon_pgdata),
				DEBIAN_PGDATA_PREFIX "/%s/%s",
				debian_pg_version(), cluster->monitorDebianCluster);
	}
	else
	{
		strlcpy(mon_pgdata, NODE_PGDATA, sizeof(mon_pgdata));
	}

	fformat(f,
			"# Generated by pgaftest — do not edit manually\n"
			"# Monitor node configuration\n"
			"\n"
			"[node]\n"
			"kind     = monitor\n"
			"hostname = monitor\n"
			"port     = 5432\n"
			"\n"
			"[postgresql]\n"
			"pgdata = %s\n"
			"\n"
			"[options]\n"
			"ssl        = %s\n"
			"auth       = %s\n"
			"pg_hba_lan = false\n",
			mon_pgdata,
			cluster->ssl,
			cluster->auth);

	if (ssl_needs_certs(cluster->ssl))
	{
		fformat(f,
				"\n"
				"[ssl]\n"
				"ca_file   = " SSL_DIR_IN_CONTAINER "/ca.crt\n"

		        /*
		         * The server cert/key are copied from the read-only bind-mount to
		         * /var/lib/postgres/ (the volume root, writable by the container
		         * user) in the start command so the key is owned by the container
		         * user and PostgreSQL can open it.
		         */
													"cert_file = /var/lib/postgres/server.crt\n"
													"key_file  = /var/lib/postgres/server.key\n");
	}

	if (cluster->monitorPassword[0])
	{
		fformat(f,
				"\n"
				"[pg_auto_failover]\n"
				"autoctl_node_password = %s\n",
				cluster->monitorPassword);
	}

	/* emit [formation <name>] for every non-default formation */
	for (int fi = 0; fi < cluster->formationCount; fi++)
	{
		const TestFormation *form = &cluster->formations[fi];
		if (strcmp(form->name, "default") == 0)
		{
			continue;
		}

		/* kind defaults to "ha" — the monitor uses that default when absent */
		fformat(f, "\n[formation %s]\n", form->name);
	}

	fclose(f);

	/* world-writable so the container's docker user can update it at runtime */
	(void) chmod(path, 0666);
	log_info("Wrote monitor.ini to \"%s\"", path);
	return true;
}


/* -----------------------------------------------------------------------
 * compose_gen_write_second_monitor_ini
 * ----------------------------------------------------------------------- */
bool
compose_gen_write_second_monitor_ini(const TestCluster *cluster, const char *dir)
{
	if (!cluster->secondMonitorName[0])
	{
		return true;
	}

	const char *name = cluster->secondMonitorName;
	char path[1024];
	sformat(path, sizeof(path), "%s/%s.ini", dir, name);

	FILE *f = fopen(path, "w"); /* IGNORE-BANNED */
	if (!f)
	{
		log_error("Failed to open \"%s\" for writing: %m", path);
		return false;
	}

	fformat(f,
			"# Generated by pgaftest — do not edit manually\n"
			"# Second monitor (replacement): %s\n"
			"\n"
			"[node]\n"
			"kind     = monitor\n"
			"hostname = %s\n"
			"port     = 5432\n"
			"\n"
			"[postgresql]\n"
			"pgdata = " NODE_PGDATA "\n"
									"\n"
									"[options]\n"
									"ssl        = %s\n"
									"auth       = %s\n"
									"pg_hba_lan = false\n",
			name, name,
			cluster->ssl,
			cluster->auth);

	if (ssl_needs_certs(cluster->ssl))
	{
		fformat(f,
				"\n"
				"[ssl]\n"
				"ca_file   = " SSL_DIR_IN_CONTAINER "/ca.crt\n"

		        /*
		         * The server cert/key are copied from the read-only bind-mount to
		         * /var/lib/postgres/ (the volume root, writable by the container
		         * user) in the start command so the key is owned by the container
		         * user and PostgreSQL can open it.
		         */
													"cert_file = /var/lib/postgres/server.crt\n"
													"key_file  = /var/lib/postgres/server.key\n");
	}

	fclose(f);
	(void) chmod(path, 0666);
	log_info("Wrote %s.ini to \"%s\"", name, path);
	return true;
}


/* -----------------------------------------------------------------------
 * compose_gen_write_node_ini
 * ----------------------------------------------------------------------- */
bool
compose_gen_write_node_ini(const TestCluster *cluster,
						   const TestFormation *formation,
						   const TestNode *node,
						   int nodeId,
						   const char *dir)
{
	char path[1024];
	sformat(path, sizeof(path), "%s/%s.ini", dir, node->name);

	FILE *f = fopen(path, "w"); /* IGNORE-BANNED */
	if (!f)
	{
		log_error("Failed to open \"%s\" for writing: %m", path);
		return false;
	}

	const char *kindStr;
	switch (node->kind)
	{
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

	/* Effective ssl/auth: node-level overrides cluster-level default */
	const char *eff_ssl = node->ssl[0] ? node->ssl : cluster->ssl;
	const char *eff_auth = node->auth[0] ? node->auth : cluster->auth;

	int pg_port = node->pgPort > 0 ? node->pgPort : 5432;

	fformat(f,
			"# Generated by pgaftest — do not edit manually\n"
			"# Node: %s  (formation: %s)\n"
			"\n"
			"[node]\n"
			"kind     = %s\n"
			"name     = %s\n"
			"hostname = %s\n"
			"port     = %d\n",
			node->name, formation->name,
			kindStr,
			node->name,
			node->name,
			pg_port);

	if (node->debianCluster[0])
	{
		fformat(f, "debian_cluster = %s\n", node->debianCluster);
	}

	if (node->debianCluster[0])
	{
		fformat(f,
				"\n"
				"[postgresql]\n"
				"pgdata = " DEBIAN_PGDATA_PREFIX "/%s/%s\n"
												 "\n",
				debian_pg_version(), node->debianCluster);
	}
	else
	{
		fformat(f,
				"\n"
				"[postgresql]\n"
				"pgdata = " NODE_PGDATA "\n"
										"\n");
	}

	if (node->noMonitor)
	{
		fformat(f, "[monitor]\nno_monitor = true\n");
		if (nodeId > 0)
		{
			fformat(f, "node_id = %d\n", nodeId);
		}
		fformat(f, "\n");
	}
	else if (cluster->monitorPassword[0])
	{
		fformat(f,
				"[monitor]\n"
				"pguri = postgresql://autoctl_node:%s@monitor/pg_auto_failover\n"
				"\n",
				cluster->monitorPassword);
	}
	else
	{
		fformat(f,
				"[monitor]\n"
				"pguri = " MONITOR_PGURI "\n"
										 "\n");
	}

	fformat(f,
			"[formation]\n"
			"name  = %s\n"
			"group = %d\n"
			"\n"
			"[settings]\n"
			"candidate_priority = %d\n"
			"replication_quorum = %s\n",
			formation->name,
			node->group,
			node->candidatePriority,
			node->replicationQuorum ? "true" : "false");

	/* Citus role/cluster settings live in their own [citus] section */
	if (node->citusSecondary || node->citusClusterName[0])
	{
		fformat(f, "\n[citus]\n");
		if (node->citusSecondary)
		{
			fformat(f, "role         = secondary\n");
		}
		if (node->citusClusterName[0])
		{
			fformat(f, "cluster_name = %s\n", node->citusClusterName);
		}
	}

	fformat(f,
			"\n"
			"[options]\n"
			"ssl        = %s\n"
			"auth       = %s\n"
			"pg_hba_lan = %s\n",
			eff_ssl,
			eff_auth,
			node->noMonitor ? "false" : "true");

	if (ssl_needs_certs(eff_ssl))
	{
		fformat(f,
				"\n"
				"[ssl]\n"
				"ca_file   = " SSL_DIR_IN_CONTAINER "/ca.crt\n"

		        /*
		         * The server cert/key are copied from the read-only bind-mount to
		         * /var/lib/postgres/ (the volume root, writable by the container
		         * user) in the start command so the key is owned by the container
		         * user and PostgreSQL can open it.
		         */
													"cert_file = /var/lib/postgres/server.crt\n"
													"key_file  = /var/lib/postgres/server.key\n");
	}

	if (node->listen)
	{
		fformat(f, "listen     = 0.0.0.0\n");
	}
	if (node->launchDeferred)
	{
		fformat(f, "\n[launch]\nmode = deferred\n");
	}

	/*
	 * Write pg_auto_failover role passwords when configured.  The
	 * replication.password key already exists in the [replication] section;
	 * monitor_password and the cluster-level autoctl_node_password go into a
	 * dedicated [pg_auto_failover] section so that pg_autoctl picks them up
	 * via the new OPTION_AUTOCTL_NODE_PASSWORD / OPTION_AUTOCTL_MONITOR_PASSWORD
	 * ini options.
	 */
	{
		const char *eff_monitor_pw =
			node->monitorPassword[0] ? node->monitorPassword : "";
		const char *eff_replication_pw =
			node->replicationPassword[0] ? node->replicationPassword : "";

		/*
		 * cluster->monitorPassword is the autoctl_node password on the monitor.
		 * It is already embedded in the monitor URI above; it does not belong
		 * in the node's own [pg_auto_failover] section.
		 *
		 * node->monitorPassword is the pgautofailover_monitor health-check role
		 * password on this data node — set by the monitor when it connects to
		 * run health checks.
		 */
		if (eff_replication_pw[0])
		{
			fformat(f,
					"\n"
					"[replication]\n"
					"password = %s\n",
					eff_replication_pw);
		}

		if (eff_monitor_pw[0])
		{
			fformat(f,
					"\n"
					"[pg_auto_failover]\n"
					"monitor_password = %s\n",
					eff_monitor_pw);
		}
	}

	fclose(f);
	(void) chmod(path, 0666);
	log_info("Wrote %s.ini to \"%s\"", node->name, path);
	return true;
}


/* -----------------------------------------------------------------------
 * Helpers used by test_runner.c
 * ----------------------------------------------------------------------- */
void
compose_network_name(const char *projectName, char *buf, int buflen)
{
	sformat(buf, buflen, "%s_default", projectName);
}


void
compose_container_name(const char *projectName, const char *service,
					   char *buf, int buflen)
{
	sformat(buf, buflen, "%s-%s-1", projectName, service);
}
