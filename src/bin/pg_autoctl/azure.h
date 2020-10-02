/*
 * src/bin/pg_autoctl/azure.h
 *     Implementation of a CLI which lets you call `az` cli commands to prepare
 *     a pg_auto_failover demo or QA environment.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef AZURE_H
#define AZURE_H

#include <stdbool.h>

/* global variables from azure.c */
extern bool dryRun;
extern PQExpBuffer azureScript;
extern char azureCLI[MAXPGPATH];

bool azure_psleep(int count, bool force);
bool azure_get_remote_ip(char *ipAddress, size_t ipAddressSize);

bool azure_create_group(const char *name, const char *location);
bool azure_create_vnet(const char *group, const char *name, const char *prefix);
bool azure_create_nsg(const char *group, const char *name);

bool azure_create_nsg_rule(const char *group,
						   const char *nsgName,
						   const char *name,
						   const char *prefixes,
						   const char *ports);

bool azure_create_subnet(const char *group,
						 const char *vnet,
						 const char *name,
						 const char *prefixes,
						 const char *nsg);

bool azure_create_vm(const char *group,
					 const char *name,
					 const char *vnet,
					 const char *subnet,
					 const char *image,
					 const char *username);

bool azure_create_vms(int count,
					  bool monitor,
					  const char *group,
					  const char *vnet,
					  const char *subnet,
					  const char *image,
					  const char *username);

bool azure_provision_vm(const char *group, const char *name);
bool azure_provision_vms(int count, bool monitor, const char *group);

bool azure_create_region(const char *prefix,
						 const char *name,
						 const char *location,
						 int cidr,
						 bool monitor,
						 int nodes);


#endif  /* AZURE_H */
