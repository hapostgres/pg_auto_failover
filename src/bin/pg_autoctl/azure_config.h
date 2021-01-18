/*
 * src/bin/pg_autoctl/azure_config.h
 *     Implementation of a CLI which lets you call `az` cli commands to prepare
 *     a pg_auto_failover demo or QA environment.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef AZURE_CONFIG_H
#define AZURE_CONFIG_H

#include <stdbool.h>

#include "postgres_fe.h"
#include "pqexpbuffer.h"

#include "azure.h"
#include "defaults.h"

bool azure_config_read_file(AzureRegionResources *azRegion);
bool azure_config_write(FILE *stream, AzureRegionResources *azRegion);
bool azure_config_write_file(AzureRegionResources *azRegion);

void azure_config_prepare(AzureOptions *options, AzureRegionResources *azRegion);
bool azure_get_remote_ip(char *ipAddress, size_t ipAddressSize);

#endif  /* AZURE_CONFIG_H */
