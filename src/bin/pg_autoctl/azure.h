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

#include "postgres_fe.h"
#include "pqexpbuffer.h"

#include "defaults.h"
#include "parsing.h"

/* global variables from azure.c */
extern bool dryRun;
extern PQExpBuffer azureScript;
extern char azureCLI[MAXPGPATH];

/* command line parsing */
typedef struct AzureOptions
{
	char prefix[NAMEDATALEN];
	char region[NAMEDATALEN];
	char location[NAMEDATALEN];

	int nodes;
	int cidr;
	bool fromSource;
	bool appNode;
	bool monitor;
	bool all;
	bool watch;
} AzureOptions;

#define MAX_VMS_PER_REGION 28   /* monitor, then pg ndoes [a-z], then app */

typedef struct AzureVMipAddresses
{
	char name[NAMEDATALEN];
	char public[BUFSIZE];
	char private[BUFSIZE];
} AzureVMipAddresses;

typedef struct AzureRegionResources
{
	char filename[MAXPGPATH];   /* on-disk configuration file path */
	char prefix[NAMEDATALEN];   /* ha-demo-dim- */
	char region[NAMEDATALEN];   /* nickname, such as paris */
	char location[NAMEDATALEN]; /* francecentral, eastus, etc */

	char group[NAMEDATALEN];    /* ha-demo-dim-paris */
	char vnet[BUFSIZE];         /* ha-demo-dim-paris-vnet */
	char nsg[BUFSIZE];          /* ha-demo-dim-paris-nsg */
	char rule[BUFSIZE];         /* ha-demo-dim-paris-ssh-and-pg */
	char subnet[BUFSIZE];       /* ha-demo-dim-paris-subnet */

	char vnetPrefix[BUFSIZE];   /* 10.%d.0.0/16 */
	char subnetPrefix[BUFSIZE]; /* 10.%d.%d.0/24 */
	char ipAddress[BUFSIZE];    /* our IP address as seen from the outside */

	int monitor;                /* do we want a monitor in that region? */
	int appNodes;               /* application nodes count */
	int nodes;                  /* node count */

	bool fromSource;

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

bool az_group_delete(const char *group);

bool azure_create_vm(AzureRegionResources *azRegion,
					 const char *name,
					 const char *image,
					 const char *username);

bool azure_create_vms(AzureRegionResources *azRegion,
					  const char *image,
					  const char *username);

bool azure_prepare_target_versions(KeyVal *env);
bool azure_provision_vm(const char *group, const char *name, bool fromSource);
bool azure_provision_vms(AzureRegionResources *azRegion, bool fromSource);

bool azure_fetch_ip_addresses(const char *group,
							  AzureVMipAddresses *vmArray);

bool azure_resource_list(const char *group);
bool azure_show_ip_addresses(const char *group);
bool azure_vm_ssh(const char *group, const char *vm);
bool azure_vm_ssh_command(const char *group,
						  const char *vm,
						  bool tty,
						  const char *command);

bool azure_create_region(AzureRegionResources *azRegion);
bool azure_drop_region(AzureRegionResources *azRegion);
bool azure_provision_nodes(AzureRegionResources *azRegion);

bool azure_deploy_monitor(AzureRegionResources *azRegion);
bool azure_deploy_postgres(AzureRegionResources *azRegion, int vmIndex);
bool azure_deploy_vm(AzureRegionResources *azRegion, const char *vmName);

bool azure_create_nodes(AzureRegionResources *azRegion);

bool azure_ls(AzureRegionResources *azRegion);
bool azure_show_ips(AzureRegionResources *azRegion);
bool azure_ssh(AzureRegionResources *azRegion, const char *vm);
bool azure_ssh_command(AzureRegionResources *azRegion,
					   const char *vm, bool tty, const char *command);

bool azure_sync_source_dir(AzureRegionResources *azRegion);

/* src/bin/pg_autoctl/cli_do_tmux_azure.c */
bool tmux_azure_start_or_attach_session(AzureRegionResources *azRegion);
bool tmux_azure_kill_session(AzureRegionResources *azRegion);

#endif  /* AZURE_H */
