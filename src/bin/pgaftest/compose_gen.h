/*
 * src/bin/pgaftest/compose_gen.h
 *   Generate a docker-compose.yml from a TestCluster spec.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#ifndef COMPOSE_GEN_H
#define COMPOSE_GEN_H

#include <stdbool.h>
#include "test_spec.h"

/*
 * Generate CA and per-service server certificates in <workDir>/ssl/ using
 * openssl.  Only called when cluster->ssl is "verify-ca" or "verify-full".
 */
bool compose_gen_write_ssl_certs(const TestCluster *cluster,
								 const char *workDir);

/*
 * Write a docker-compose.yml to `path` for the given cluster spec.
 * Returns true on success.
 */

/* cluster is non-const: monitorHostPort is filled in if zero.
 * specFile is the absolute path to the .pgaf spec file; it is bind-mounted
 * into the pgaftest service at /spec.pgaf.  Pass NULL to omit the service.
 * specDir is the directory containing the spec (dirname(specFile)); it is
 * bind-mounted read-only at /etc/pgaf/specs in every data-node container so
 * that static JSON or other helper files shipped next to the spec can be
 * referenced in exec commands.  Pass NULL to omit the mount.
 * interactive: when true the pgaftest service uses `sleep infinity` instead
 * of `pgaftest run /spec.pgaf`, leaving it alive for an interactive shell. */
bool compose_gen_write(TestCluster *cluster,
					   const char *path,
					   const char *projectName,
					   const char *contextDir,
					   const char *specFile,
					   const char *specDir,
					   bool interactive);

/*
 * Write a pg_autoctl_node.ini for the second (replacement) monitor into `dir`.
 * Only called when cluster->secondMonitorName is set.
 */
bool compose_gen_write_second_monitor_ini(const TestCluster *cluster,
										  const char *dir);

/*
 * Write a pg_autoctl_node.ini file for the monitor node into `dir`.
 * The file is written as <dir>/monitor.ini and will be bind-mounted
 * into the container at PG_AUTOCTL_NODESPEC_PATH.
 */
bool compose_gen_write_monitor_ini(const TestCluster *cluster,
								   const char *dir);

/*
 * Write a pg_autoctl_node.ini file for a data node into `dir`.
 * The file is written as <dir>/<node->name>.ini.
 * `formation` supplies the formation name and is written into [formation].
 */
bool compose_gen_write_node_ini(const TestCluster *cluster,
								const TestFormation *formation,
								const TestNode *node,
								int nodeId,
								const char *dir);

/*
 * Write the pgaf-hosts file: static IP-to-name mapping for every service,
 * also embedded directly into docker-compose.yml as extra_hosts entries.
 * pgaftest itself reads this file back for `network connect` IP lookups.
 * path should be <workdir>/pgaf-hosts.
 */
bool compose_gen_write_hosts(const TestCluster *cluster,
							 const char *path,
							 const char *projectName);

/* Return the docker network name for a project */
void compose_network_name(const char *projectName, char *buf, int buflen);

/* Return the container name for a service in a project */
void compose_container_name(const char *projectName,
							const char *service,
							char *buf, int buflen);

#endif /* COMPOSE_GEN_H */
