/*
 * src/bin/pgaftest/compose_gen.c
 *   Generate a docker-compose.yml and per-node pg_autoctl_node.ini files
 *   from a TestCluster spec.
 *
 * Design principle — uniform containers
 * ======================================
 * Every container in the generated compose file uses:
 *   - the same Docker image for cluster nodes (default: pg_auto_failover:pg<ver>,
 *     overridable via PGAF_IMAGE or `image "..."` in the spec)
 *   - the pgaftest runner image for the setup service (default: pgaf:pgaftest,
 *     overridable via PGAFTEST_IMAGE)
 *   - Set PGAF_BUILD=1 to emit inline `build:` stanzas instead of image refs
 *     (useful when iterating on the Dockerfile without tagging)
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
 * write_node_command emits the YAML command: stanza.
 *
 * When CA-signed SSL certs are in use, the server cert/key must be copied
 * from the read-only bind-mount (/etc/pgaf/ssl/server/) to the writable
 * volume root (/var/lib/postgres/) before pg_autoctl starts, because
 * PostgreSQL requires the key file to be owned by the process user.  The
 * client cert/key and CA cert are also copied so libpq can find them.
 * SSL_COPY_CERTS_CMD is a shell snippet ending with " &&" so we can
 * append the pg_autoctl invocation directly.
 *
 * When cluster->legacyStartup is set (e.g. upgrade tests using pgaf:current
 * which carries the v2.2 binary), the v2.3 "pg_autoctl node run <ini>" form
 * is not available.  Use the v2.2-style "pg_autoctl create <kind> --run"
 * instead.  isMonitor distinguishes the two node kinds; monitorUri is only
 * used when isMonitor is false.
 */
static void
write_node_command(FILE *f, const TestCluster *cluster, const char *iniPath)
{
	if (ssl_needs_certs(cluster->ssl))
	{
		fformat(f,
				"    command: [\"/bin/sh\", \"-c\", \""
				SSL_COPY_CERTS_CMD
				" pg_autoctl node run %s\"]\n"
				"    stop_grace_period: 60s\n",
				iniPath);
	}
	else
	{
		fformat(f,
				"    command: [\"pg_autoctl\", \"node\", \"run\","
				" \"%s\"]\n"
				"    stop_grace_period: 60s\n",
				iniPath);
	}
}


/*
 * ssl_args_for_legacy returns the ssl-related YAML array fragment (without
 * trailing comma) for a legacy "pg_autoctl create" command.
 *
 * "node run <ini>" reads ssl= from the ini; "create <kind> --run" needs the
 * flag spelled out on the command line.  Most cases are a single flag token;
 * verify-ca / verify-full would need cert paths too, but the only current
 * legacy-startup user (upgrade.pgaf) always uses the default self-signed.
 */
static const char *
ssl_args_for_legacy(const char *ssl)
{
	if (strcmp(ssl, "off") == 0)
	{
		return "\"--no-ssl\"";
	}

	/* self-signed is the default; also covers the empty/unset case */
	return "\"--ssl-self-signed\"";
}


static void
write_legacy_monitor_command(FILE *f, const char *pgdata, const char *auth,
							 const char *ssl)
{
	fformat(f,
			"    command: [\"pg_autoctl\", \"create\", \"monitor\","
			" \"--pgdata\", \"%s\","
			" \"--auth\", \"%s\","
			" %s,"
			" \"--run\"]\n"
			"    stop_grace_period: 60s\n",
			pgdata, auth, ssl_args_for_legacy(ssl));
}


static void
write_legacy_node_command(FILE *f, const char *pgdata, const char *monitorUri,
						  const char *auth, const char *ssl, const char *name)
{
	fformat(f,
			"    command: [\"pg_autoctl\", \"create\", \"postgres\","
			" \"--pgdata\", \"%s\","
			" \"--monitor\", \"%s\","
			" \"--name\", \"%s\","
			" \"--auth\", \"%s\","
			" %s,"
			" \"--run\"]\n"
			"    stop_grace_period: 60s\n",
			pgdata, monitorUri, name, auth, ssl_args_for_legacy(ssl));
}


/*
 * write_image_stanza_target — emit either `image:` or `build:` for a service.
 *
 * Resolution order for the run/debian/monitor targets (cluster data nodes):
 *   1. spec-level  `image "..."` declaration
 *   2. PGAF_IMAGE  env var
 *   3. pg_auto_failover:pg<version>  — produced by `make build`
 *
 * Resolution order for the pgaftest target (setup service):
 *   1. PGAFTEST_IMAGE env var
 *   2. pgaf:pgaftest                 — produced by `make build-pgaftest`
 *
 * Set PGAF_BUILD=1 to force an inline `build:` stanza for all targets
 * (useful when iterating on the Dockerfile without tagging an image).
 */
static void
write_image_stanza_target(FILE *f, const TestCluster *cluster,
						  const char *contextDir, const char *target)
{
	bool isTestRunner = (strcmp(target, "pgaftest") == 0);
	bool forceBuild = (getenv("PGAF_BUILD") != NULL); /* IGNORE-BANNED */

	if (!forceBuild)
	{
		const char *img = NULL;

		if (isTestRunner)
		{
			img = getenv("PGAFTEST_IMAGE"); /* IGNORE-BANNED */
			if (!img || !*img)
			{
				img = "pgaf:pgaftest";
			}
		}
		else
		{
			img = cluster->image[0] ? cluster->image : getenv("PGAF_IMAGE"); /* IGNORE-BANNED */
			if (!img || !*img)
			{
				static char defaultImg[64];
				sformat(defaultImg, sizeof(defaultImg),
						"pg_auto_failover:pg%s", debian_pg_version());
				img = defaultImg;
			}
		}

		fformat(f, "    image: \"%s\"\n", img);
		return;
	}

	fformat(f,
			"    build:\n"
			"      context: \"%s\"\n"
			"      target: %s\n"
			"      args:\n"
			"        PGVERSION: \"%s\"\n",
			contextDir, target, debian_pg_version());
}


static void
write_image_stanza(FILE *f, const TestCluster *cluster, const char *contextDir)
{
	write_image_stanza_target(f, cluster, contextDir, "run");
}


/* -----------------------------------------------------------------------
 * Static-IP / extra_hosts helpers
 *
 * Every compose stack gets a private /24 subnet derived from the project
 * name, and every service in it gets a fixed IP on that subnet. Name
 * resolution between services (monitor, data nodes) is done with Docker
 * Compose's `extra_hosts:` key, writing that same static IP-to-name mapping
 * directly into each service's /etc/hosts at container-creation time —
 * rather than via a DNS query at all. That sidesteps Docker's embedded
 * 127.0.0.11 resolver, which is known to drop DNS under heavy container
 * churn on GitHub Actions runners: an /etc/hosts entry can't time out or
 * get dropped mid-query the way a UDP DNS lookup can.
 *
 * IP layout (fourth octet):
 *   .2       monitor (when present)
 *   .3       second_monitor (when present)
 *   .4 …     data nodes, in the order they appear across all formations
 *
 * Subnet selection: hash the project name into one of 2048 /24 blocks
 * spread across 172.20.0.0/12 – 172.27.0.0/12 (second and third octets),
 * giving low collision probability when multiple tests run in parallel on
 * the same GHA runner.
 * ----------------------------------------------------------------------- */

/* Number of /24 subnets available: 8 class-B blocks × 256 class-C = 2048 */
#define PGAF_SUBNET_BASE_A 172
#define PGAF_SUBNET_RANGE_B 8    /* 172.20 … 172.27 */
#define PGAF_SUBNET_BASE_B 20

/*
 * djb2 hash — fast, low-collision for short ASCII strings.
 */
static unsigned long
djb2_hash(const char *s)
{
	unsigned long h = 5381;

	for (; *s; s++)
	{
		h = h * 33 ^ (unsigned char) *s;
	}
	return h;
}


/*
 * compose_subnet fills `buf` with the /24 subnet string for `projectName`,
 * e.g. "172.23.47.0/24".
 */
static void
compose_subnet(const char *projectName, char *buf, int buflen)
{
	unsigned long h = djb2_hash(projectName);
	int b = PGAF_SUBNET_BASE_B + (int) ((h >> 8) % PGAF_SUBNET_RANGE_B);
	int c = (int) (h % 256);

	sformat(buf, buflen, "%d.%d.%d.0/24", PGAF_SUBNET_BASE_A, b, c);
}


/*
 * compose_service_ip fills `buf` with the IP for a given last-octet offset.
 * offset 2 = monitor, 3 = secondMonitor, 4+ = data nodes.
 */
static void
compose_service_ip(const char *projectName, int offset, char *buf, int buflen)
{
	unsigned long h = djb2_hash(projectName);
	int b = PGAF_SUBNET_BASE_B + (int) ((h >> 8) % PGAF_SUBNET_RANGE_B);
	int c = (int) (h % 256);

	sformat(buf, buflen, "%d.%d.%d.%d", PGAF_SUBNET_BASE_A, b, c, offset);
}


/*
 * compose_gen_write_hosts writes the pgaf-hosts file: one "IP  name" line
 * per service (monitor + data nodes). This is no longer consumed by a DNS
 * server (see the extra_hosts note above) — pgaftest itself reads this file
 * back (runner_hosts_lookup in test_runner.c) to know each node's static IP
 * when reconnecting it to the network after a `network disconnect` step, so
 * the reconnect keeps the same address every other service's extra_hosts
 * entry already points at.
 */
bool
compose_gen_write_hosts(const TestCluster *cluster,
						const char *path,
						const char *projectName)
{
	FILE *f = fopen(path, "w"); /* IGNORE-BANNED */

	if (!f)
	{
		log_error("Failed to open \"%s\" for writing: %m", path);
		return false;
	}

	/* offset 2: monitor */
	if (cluster->withMonitor)
	{
		char ip[32];
		compose_service_ip(projectName, 2, ip, sizeof(ip));
		fformat(f, "%s  monitor\n", ip);
	}

	/* offset 3: second monitor */
	if (cluster->secondMonitorName[0])
	{
		char ip[32];
		compose_service_ip(projectName, 3, ip, sizeof(ip));
		fformat(f, "%s  %s\n", ip, cluster->secondMonitorName);
	}

	/* offset 4+: data nodes */
	int nodeOffset = 4;

	for (int fi = 0; fi < cluster->formationCount; fi++)
	{
		const TestFormation *form = &cluster->formations[fi];

		for (int ni = 0; ni < form->nodeCount; ni++)
		{
			const TestNode *n = &form->nodes[ni];
			char ip[32];
			compose_service_ip(projectName, nodeOffset++, ip, sizeof(ip));
			fformat(f, "%s  %s\n", ip, n->name);
		}
	}

	fclose(f); /* IGNORE-BANNED */
	log_info("Wrote pgaf-hosts to \"%s\"", path);
	return true;
}


/*
 * compose_write_extra_hosts writes an `extra_hosts:` block (the same
 * IP-to-name mapping as compose_gen_write_hosts, in Compose YAML form) to
 * the docker-compose.yml service currently being written. Every service
 * gets the full mapping — monitor, second monitor, and every data node —
 * regardless of whether it needs all of those peers, matching what every
 * service could previously resolve via the shared dnsmasq server.
 */
static void
compose_write_extra_hosts(FILE *f, const TestCluster *cluster,
						  const char *projectName)
{
	fformat(f, "    extra_hosts:\n");

	if (cluster->withMonitor)
	{
		char ip[32];
		compose_service_ip(projectName, 2, ip, sizeof(ip));
		fformat(f, "      - \"monitor:%s\"\n", ip);
	}

	if (cluster->secondMonitorName[0])
	{
		char ip[32];
		compose_service_ip(projectName, 3, ip, sizeof(ip));
		fformat(f, "      - \"%s:%s\"\n", cluster->secondMonitorName, ip);
	}

	int nodeOffset = 4;

	for (int fi = 0; fi < cluster->formationCount; fi++)
	{
		const TestFormation *form = &cluster->formations[fi];

		for (int ni = 0; ni < form->nodeCount; ni++)
		{
			const TestNode *n = &form->nodes[ni];
			char ip[32];
			compose_service_ip(projectName, nodeOffset++, ip, sizeof(ip));
			fformat(f, "      - \"%s:%s\"\n", n->name, ip);
		}
	}
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
				  const char *specFile,
				  const char *specDir,
				  bool interactive)
{
	FILE *f = fopen(path, "w"); /* IGNORE-BANNED */
	if (!f)
	{
		log_error("Failed to open \"%s\" for writing: %m", path);
		return false;
	}

	char subnet[32];

	compose_subnet(projectName, subnet, sizeof(subnet));

	/*
	 * Write pgaf-hosts next to docker-compose.yml. No longer read by any
	 * container at runtime (see compose_write_extra_hosts, which bakes the
	 * same mapping directly into the compose YAML); pgaftest itself reads
	 * this file back for `network connect` static-IP lookups.
	 * Skip when path is /dev/stdout (pgaftest show compose dry-run).
	 */
	if (strcmp(path, "/dev/stdout") != 0)
	{
		char hostsPath[MAXPGPATH];
		strlcpy(hostsPath, path, sizeof(hostsPath));
		char *slash = strrchr(hostsPath, '/');

		if (slash)
		{
			*slash = '\0';
			strlcat(hostsPath, "/pgaf-hosts", sizeof(hostsPath));
		}
		else
		{
			strlcpy(hostsPath, "pgaf-hosts", sizeof(hostsPath));
		}

		if (!compose_gen_write_hosts(cluster, hostsPath, projectName))
		{
			fclose(f); /* IGNORE-BANNED */
			return false;
		}
	}

	fformat(f, "# Generated by pgaftest — do not edit manually\n");
	fformat(f, "# Project: %s  subnet: %s\n\n", projectName, subnet);
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
		if (cluster->monitorImageTarget[0])
		{
			write_image_stanza_target(f, cluster, contextDir,
									  cluster->monitorImageTarget);
		}
		else
		{
			/*
			 * When PGAF_PREBUILT_IMAGE is set, use the pre-initialized monitor
			 * image (pg_autoctl node init already ran initdb at image-build
			 * time, so compose-up skips the slow initdb step).  Data nodes
			 * still use the plain run image; they need the live monitor URI and
			 * cannot be pre-initialized without it.
			 */
			const char *prebuiltImg = getenv("PGAF_PREBUILT_IMAGE"); /* IGNORE-BANNED */
			const char *runImg = cluster->image[0] ? cluster->image
								 : getenv("PGAF_IMAGE");                    /* IGNORE-BANNED */

			if (prebuiltImg && *prebuiltImg && runImg && *runImg)
			{
				fformat(f, "    image: \"%s\"\n", prebuiltImg);
			}
			else
			{
				write_image_stanza(f, cluster, contextDir);
			}
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
		if (specDir && specDir[0])
		{
			fformat(f, "      - %s:/etc/pgaf/specs:ro\n", specDir);
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


		if (cluster->legacyStartup)
		{
			write_legacy_monitor_command(f, monitor_pgdata, cluster->auth,
										 cluster->ssl);
		}
		else
		{
			write_node_command(f, cluster, NODE_INI_PATH);
		}
		fformat(f, "\n");

		/*
		 * Monitor healthcheck: data nodes use depends_on service_healthy so
		 * they do not start until the monitor is fully initialised.
		 */
		char monitorIp[32];
		compose_service_ip(projectName, 2, monitorIp, sizeof(monitorIp));

		fformat(f,
				"    healthcheck:\n"
				"      test: [\"CMD\", \"pg_autoctl\", \"status\","
				" \"--pgdata\", \"%s\"]\n"
				"      interval: 2s\n"
				"      timeout: 5s\n"
				"      retries: 150\n"
				"      start_period: 60s\n",
				monitor_pgdata);
		compose_write_extra_hosts(f, cluster, projectName);
		fformat(f,
				"    networks:\n"
				"      pgafnet:\n"
				"        ipv4_address: %s\n\n",
				monitorIp);
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
		char secondMonitorIp[32];
		compose_service_ip(projectName, 3, secondMonitorIp, sizeof(secondMonitorIp));
		fformat(f,
				"    ports:\n"
				"      - \"%d:5432\"\n",
				cluster->secondMonitorHostPort);
		write_node_command(f, cluster, NODE_INI_PATH);
		compose_write_extra_hosts(f, cluster, projectName);
		fformat(f,
				"    networks:\n"
				"      pgafnet:\n"
				"        ipv4_address: %s\n\n",
				secondMonitorIp);
	}

	/* ---- data nodes — iterate all formations ---- */

	/*
	 * Non-default formations are listed in [formation <name>] sections of the
	 * monitor.ini.  pg_autoctl node run reads them and creates each one after
	 * the monitor is initialized, before starting the supervisor.
	 * Data nodes retry registration until their formation exists.
	 */
	const TestNode *firstNode = NULL;

	/* IP offset 4 = first data node (2=monitor, 3=secondMonitor) */
	int nodeIpOffset = 4;

	/*
	 * Ordinal position of each node across every formation, in cluster{}
	 * declaration order (0 = first node declared).  Passed to the container
	 * as PG_AUTOCTL_TEST_DELAY so that pg_autoctl node run can stagger
	 * registration deterministically — see the PG_AUTOCTL_TEST_DELAY
	 * handling in cli_node.c for why this exists.  Unlike a name-derived
	 * index, this works uniformly for both "node1"/"node2" naming and
	 * Citus-style "worker1a"/"coordinator1b" naming, since it never parses
	 * the node name at all.
	 */
	int nodeOrdinal = 0;

	for (int fi = 0; fi < cluster->formationCount; fi++)
	{
		const TestFormation *form = &cluster->formations[fi];

		for (int ni = 0; ni < form->nodeCount; ni++)
		{
			const TestNode *n = &form->nodes[ni];
			int thisOffset = nodeIpOffset++;
			int thisOrdinal = nodeOrdinal++;
			fformat(f, "  %s:\n", n->name);
			write_image_stanza(f, cluster, contextDir);

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

			/*
			 * Debian-cluster nodes store data under /var/lib/postgresql/<ver>/<name>,
			 * so mount the volume there; regular nodes use /var/lib/postgres.
			 */
			const char *dataMount = n->debianCluster[0]
									? "/var/lib/postgresql"
									: "/var/lib/postgres";

			fformat(f,
					"    hostname: %s\n"
					"    volumes:\n"
					"      - %s_data:%s:rw\n"
					"      - ./%s.ini:" NODE_INI_PATH ":%s\n",
					n->name,
					n->name,
					dataMount,
					n->name,
					(n->launchDeferred || n->createDeferred) ? "rw" : "ro");
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
			if (specDir && specDir[0])
			{
				fformat(f,
						"      - %s:/etc/pgaf/specs:ro\n", specDir);
			}
			fformat(f,
					"    environment:\n"
					"      PGDATA: %s\n"
					"      PGUSER: demo\n"
					"      PGDATABASE: demo\n"
					"      PG_AUTOCTL_TEST_DELAY: \"%d\"\n"
					"    expose:\n"
					"      - 5432\n",
					node_pgdata, thisOrdinal);

			/*
			 * No depends_on: keeper retry loops handle monitor not yet ready,
			 * and the formation wait in keeper_register_and_init handles the
			 * case where the formation hasn't been created yet.
			 */

			if (cluster->legacyStartup)
			{
				char monitorUri[512];

				if (cluster->monitorPassword[0])
				{
					sformat(monitorUri, sizeof(monitorUri),
							"postgresql://autoctl_node:%s@monitor/pg_auto_failover",
							cluster->monitorPassword);
				}
				else
				{
					strlcpy(monitorUri,
							"postgresql://autoctl_node@monitor/pg_auto_failover",
							sizeof(monitorUri));
				}
				write_legacy_node_command(f, node_pgdata, monitorUri,
										  cluster->auth, cluster->ssl, n->name);
			}
			else
			{
				write_node_command(f, cluster, NODE_INI_PATH);
			}

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
			if (!firstNode && cluster->withMonitor &&
				!n->launchDeferred && !n->createDeferred)
			{
				fformat(f,
						"    healthcheck:\n"
						"      test: [\"CMD\", \"pg_autoctl\", \"status\","
						" \"--pgdata\", \"%s\"]\n"
						"      interval: 2s\n"
						"      timeout: 5s\n"
						"      retries: 150\n"
						"      start_period: 60s\n",
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
			else if (cluster->withMonitor &&
					 !n->launchDeferred && !n->createDeferred)
			{
				/* node1: wait for the monitor to be healthy before starting */
				fformat(f,
						"    depends_on:\n"
						"      monitor:\n"
						"        condition: service_healthy\n");
			}

			/*
			 * no-monitor or deferred: no depends_on needed — the keeper
			 * retry loops already handle the monitor/formation not being
			 * ready yet.
			 */
			char nodeIp[32];
			compose_service_ip(projectName, thisOffset, nodeIp, sizeof(nodeIp));
			compose_write_extra_hosts(f, cluster, projectName);
			fformat(f,
					"    networks:\n"
					"      pgafnet:\n"
					"        ipv4_address: %s\n\n",
					nodeIp);

			if (!firstNode && !n->launchDeferred && !n->createDeferred)
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
		/*
		 * In interactive mode the service is named "setup" and carries a
		 * profile so that `docker compose up` ignores it entirely — it is
		 * only invoked via `docker compose run setup`.  This avoids the need
		 * for --scale pgaftest=0 and prevents Compose from rebuilding cluster
		 * images on every `pgaftest tmux` invocation.
		 *
		 * In CI mode the service is named "pgaftest" with no profile so that
		 * `docker compose up --exit-code-from pgaftest` works as expected.
		 */
		const char *svcName = interactive ? "setup" : "pgaftest";
		fformat(f, "  %s:\n", svcName);
		if (interactive)
		{
			fformat(f, "    profiles: [setup]\n");
		}
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
		 * pgaftest waits for the first data node to be healthy so that the
		 * setup {} block's wait timeouts don't need to cover node
		 * initialisation from scratch.  Without this, pgaftest's setup timer
		 * starts as soon as the monitor is connectable, but node1 may not
		 * have finished initialising yet (especially in SSL cert clusters
		 * where each stage has its own 600s window).
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

		/*
		 * The setup service runs as root (image default): root always has
		 * access to /var/run/docker.sock for Docker-out-of-Docker regardless
		 * of the host socket GID (which differs between macOS Docker Desktop
		 * and Linux CI).  Working directory is /root so that pgaftest can
		 * maintain a local state file (current step, etc.) in a predictable,
		 * writable location without any volume mounts.
		 */
		fformat(f,
				"    working_dir: /root\n"
				"    volumes:\n"
				"      - %s:/root/spec.pgaf:ro\n"
				"      - /var/run/docker.sock:/var/run/docker.sock\n"
				"      - %s:%s\n",
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
				"      PGAFTEST_IN_CONTAINER: \"1\"\n"
				"      PGAFTEST_SPEC: \"/root/spec.pgaf\"\n"
				"      PGAFTEST_HOST_WORK_DIR: \"%s\"\n"
				"      COMPOSE_PROJECT_NAME: \"%s\"\n",
				workDir, projectName);
		if (cluster->withMonitor)
		{
			fformat(f, "      PG_AUTOCTL_MONITOR: \"%s\"\n", monitorPguri);
		}
		if (firstNode && cluster->withMonitor)
		{
			fformat(f,
					"    depends_on:\n"
					"      %s:\n"
					"        condition: service_healthy\n",
					firstNode->name);
		}

		/*
		 * This service resolves "monitor" and node names directly (e.g. via
		 * PG_AUTOCTL_MONITOR above), so it needs the same static-IP mapping
		 * as every other service and a place on pgafnet to reach them — no
		 * fixed IP of its own is needed since nothing resolves this service
		 * by name.
		 */
		compose_write_extra_hosts(f, cluster, projectName);
		fformat(f,
				"    networks:\n"
				"      - pgafnet\n");

		/*
		 * CI mode: run the full spec to completion (exit code drives CI).
		 * Interactive mode: no command — docker compose run supplies it.
		 */
		if (!interactive)
		{
			fformat(f,
					"    command: [\"pgaftest\", \"run\","
					" \"/var/lib/postgres/spec.pgaf\"]\n\n");
		}
		else
		{
			fformat(f, "\n");
		}
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

	/* ---- top-level networks block with static subnet ---- */
	fformat(f,
			"\nnetworks:\n"
			"  pgafnet:\n"
			"    ipam:\n"
			"      config:\n"
			"        - subnet: %s\n",
			subnet);

	fclose(f); /* IGNORE-BANNED */
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

	/*
	 * Emit [formation <name>] sections into monitor.ini.
	 *
	 * The default formation is created automatically at monitor init and does
	 * not need a section for kind/secondary, but we include it when num-sync
	 * is explicitly declared so that nodespec_apply can set it.
	 * Non-default formations always get a section so they are created at
	 * post-init time.
	 */
	for (int fi = 0; fi < cluster->formationCount; fi++)
	{
		const TestFormation *form = &cluster->formations[fi];
		bool isDefault = (strcmp(form->name, "default") == 0);

		/* derive kind from node types: any coordinator/worker → citus */
		bool isCitus = false;
		for (int ni = 0; ni < form->nodeCount && !isCitus; ni++)
		{
			PgInstanceKind k = form->nodes[ni].kind;
			if (k == NODE_KIND_CITUS_COORDINATOR || k == NODE_KIND_CITUS_WORKER)
			{
				isCitus = true;
			}
		}

		/* skip the default formation unless it has settings to apply */
		if (isDefault && !isCitus && !form->disableSecondary && form->numSync < 0)
		{
			continue;
		}

		fformat(f, "\n[formation %s]\n", form->name);

		if (!isDefault)
		{
			if (isCitus)
			{
				fformat(f, "kind = citus\n");
			}

			if (form->disableSecondary)
			{
				fformat(f, "secondary = false\n");
			}
		}

		if (form->numSync >= 0)
		{
			fformat(f, "num_sync_standbys = %d\n", form->numSync);
		}
	}

	fclose(f); /* IGNORE-BANNED */

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

	fclose(f); /* IGNORE-BANNED */
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
		if (ssl_needs_certs(eff_ssl))
		{
			fformat(f,
					"[monitor]\n"
					"pguri = postgresql://autoctl_node:%s@monitor/pg_auto_failover"
					"?sslmode=%s"
					"&sslrootcert=" SSL_DIR_IN_CONTAINER "/ca.crt"
														 "&sslcert=/var/lib/postgres/.postgresql/postgresql.crt"
														 "&sslkey=/var/lib/postgres/.postgresql/postgresql.key"
														 "\n\n",
					cluster->monitorPassword,
					eff_ssl);
		}
		else
		{
			fformat(f,
					"[monitor]\n"
					"pguri = postgresql://autoctl_node:%s@monitor/pg_auto_failover\n"
					"\n",
					cluster->monitorPassword);
		}
	}
	else if (ssl_needs_certs(eff_ssl))
	{
		/*
		 * For verify-ca / verify-full clusters, embed explicit SSL params in
		 * the monitor URI rather than relying on libpq auto-discovery of
		 * ~/.postgresql/.  Auto-discovery depends on $HOME being set correctly
		 * inside the container, which is fragile in the CI DinD environment.
		 */
		fformat(f,
				"[monitor]\n"
				"pguri = " MONITOR_PGURI
				"?sslmode=%s"
				"&sslrootcert=" SSL_DIR_IN_CONTAINER "/ca.crt"
													 "&sslcert=/var/lib/postgres/.postgresql/postgresql.crt"
													 "&sslkey=/var/lib/postgres/.postgresql/postgresql.key"
													 "\n\n",
				eff_ssl);
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

	if (node->debianCluster[0])
	{
		fformat(f, "debian_cluster = %s\n", node->debianCluster);
	}

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
	if (node->launchDeferred || node->createDeferred)
	{
		fformat(f, "\n[launch]\n");
		if (node->createDeferred)
		{
			fformat(f, "create = deferred\n");
		}
		if (node->launchDeferred)
		{
			fformat(f, "run = deferred\n");
		}
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

	fclose(f); /* IGNORE-BANNED */
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
	sformat(buf, buflen, "%s_pgafnet", projectName);
}


void
compose_container_name(const char *projectName, const char *service,
					   char *buf, int buflen)
{
	sformat(buf, buflen, "%s-%s-1", projectName, service);
}
