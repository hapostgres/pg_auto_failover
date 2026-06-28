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
 * Write a docker-compose.yml to `path` for the given cluster spec.
 * The monitor's port 5432 is exposed on `monitorHostPort` so the
 * test runner can query it directly with libpq.
 * Returns true on success.
 */
bool compose_gen_write(const TestCluster *cluster,
                       const char *path,
                       const char *projectName,
                       int monitorHostPort,
                       const char *contextDir);

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
                                const char *dir);

/* Return the docker network name for a project */
void compose_network_name(const char *projectName, char *buf, int buflen);

/* Return the container name for a service in a project */
void compose_container_name(const char *projectName,
                            const char *service,
                            char *buf, int buflen);

#endif /* COMPOSE_GEN_H */
