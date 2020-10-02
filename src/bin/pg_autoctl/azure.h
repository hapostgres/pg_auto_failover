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

#include "defaults.h"

/* global variables from azure.c */
extern bool dryRun;
extern PQExpBuffer azureScript;
extern char azureCLI[MAXPGPATH];

#define MAX_VMS_PER_REGION 27   /* monitor, then [a-z] */

typedef struct AzureVMipAddresses
{
	char name[NAMEDATALEN];
	char public[BUFSIZE];
	char private[BUFSIZE];
} AzureVMipAddresses;


typedef struct AzureRegionResources
{
	char prefix[NAMEDATALEN];
	char region[NAMEDATALEN];
	char group[NAMEDATALEN];

	char vnet[BUFSIZE];
	char nsg[BUFSIZE];
	char rule[BUFSIZE];
	char subnet[BUFSIZE];

	bool monitor;               /* do we want a monitor in that region? */
	int nodes;                  /* node count */

	AzureVMipAddresses vmArray[MAX_VMS_PER_REGION];
} AzureRegionResources;


bool azure_psleep(int count, bool force);
bool azure_get_remote_ip(char *ipAddress, size_t ipAddressSize);

bool azure_create_group(const char *name, const char *location);
bool azure_create_vnet(const char *group, const char *name, const char *prefix);
bool azure_create_nsg(const char *group, const char *name);

bool azure_create_nsg_rule(const char *group,
						   const char *nsgName,
						   const char *name,
						   const char *prefixes);

bool azure_create_subnet(const char *group,
						 const char *vnet,
						 const char *name,
						 const char *prefixes,
						 const char *nsg);

bool azure_create_vm(AzureRegionResources *azRegion,
					 const char *name,
					 const char *image,
					 const char *username);

bool azure_create_vms(AzureRegionResources *azRegion,
					  const char *image,
					  const char *username);

bool azure_provision_vm(const char *group, const char *name);
bool azure_provision_vms(AzureRegionResources *azRegion);

bool azure_resource_list(const char *group);
bool azure_show_ip_addresses(const char *group);
bool azure_vm_ssh(const char *group, const char *vm);
bool azure_vm_ssh_command(const char *group,
						  const char *vm,
						  bool tty,
						  const char *command);

bool azure_create_region(const char *prefix,
						 const char *region,
						 const char *location,
						 int cidr,
						 bool monitor,
						 int nodes);

bool azure_provision_nodes(const char *prefix,
						   const char *region,
						   bool monitor,
						   int nodes);

bool azure_create_nodes(const char *prefix,
						const char *region,
						bool monitor,
						int nodes);

bool azure_create_service(const char *prefix,
						  const char *name,
						  bool monitor,
						  int nodes);

bool azure_ls(const char *prefix, const char *name);
bool azure_show_ips(const char *prefix, const char *name);
bool azure_ssh(const char *prefix, const char *name, const char *vm);
bool azure_ssh_command(const char *prefix, const char *name, const char *vm,
					   bool tty, const char *command);

#endif  /* AZURE_H */
