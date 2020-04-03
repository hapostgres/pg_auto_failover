/*
 * src/bin/pg_autoctl/cli_do_show.c
 *     Implementation of a CLI which lets you run operations on the local
 *     postgres server directly.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <signal.h>

#include "postgres_fe.h"

#include "cli_common.h"
#include "cli_do_root.h"
#include "commandline.h"
#include "config.h"
#include "defaults.h"
#include "file_utils.h"
#include "fsm.h"
#include "ipaddr.h"
#include "keeper_config.h"
#include "keeper.h"
#include "monitor.h"
#include "monitor_config.h"
#include "pgctl.h"
#include "primary_standby.h"


static void cli_show_ipaddr(int argc, char **argv);
static void cli_show_cidr(int argc, char **argv);
static void cli_show_lookup(int argc, char **argv);
static void cli_show_nodename(int argc, char **argv);

static CommandLine do_show_ipaddr_command =
	make_command("ipaddr",
				 "Print this node's IP address information", "", "",
				 NULL, cli_show_ipaddr);

static CommandLine do_show_cidr_command =
	make_command("cidr",
				 "Print this node's CIDR information", "", "",
				 NULL, cli_show_cidr);

static CommandLine do_show_lookup_command =
	make_command("lookup",
				 "Print this node's DNS lookup information",
				 "<nodename>", "",
				 NULL, cli_show_lookup);

static CommandLine do_show_nodename_command =
	make_command("nodename",
				 "Print this node's default nodename", "", "",
				 NULL, cli_show_nodename);

CommandLine *do_show_subcommands[] = {
	&do_show_ipaddr_command,
	&do_show_cidr_command,
	&do_show_lookup_command,
	&do_show_nodename_command,
	NULL
};

CommandLine do_show_commands =
	make_command_set("show",
					 "Show some debug level information", NULL, NULL,
					 NULL, do_show_subcommands);


/*
 * cli_show_ipaddr displays the LAN IP address of the current node, as used
 * when computing the CIDR address range to open in the HBA file.
 */
static void
cli_show_ipaddr(int argc, char **argv)
{
	char ipAddr[BUFSIZE];

	if (!fetchLocalIPAddress(ipAddr, BUFSIZE,
							 DEFAULT_INTERFACE_LOOKUP_SERVICE_NAME,
							 DEFAULT_INTERFACE_LOOKUP_SERVICE_PORT))
	{
		log_warn("Failed to determine network configuration.");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	fformat(stdout, "%s\n", ipAddr);
}


/*
 * cli_show_cidr displays the LAN CIDR that pg_autoctl grants connections to in
 * the HBA file for setting up Postgres streaming replication and connections
 * to the monitor.
 */
static void
cli_show_cidr(int argc, char **argv)
{
	char ipAddr[BUFSIZE];
	char cidr[BUFSIZE];

	if (!fetchLocalIPAddress(ipAddr, BUFSIZE,
							 DEFAULT_INTERFACE_LOOKUP_SERVICE_NAME,
							 DEFAULT_INTERFACE_LOOKUP_SERVICE_PORT))
	{
		log_warn("Failed to determine network configuration.");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	if (!fetchLocalCIDR(ipAddr, cidr, BUFSIZE))
	{
		log_warn("Failed to determine network configuration.");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	fformat(stdout, "%s\n", cidr);
}


/*
 * cli_check_nodename checks that the --nodename argument is either an IP
 * address that exists on the local list of interfaces, or a hostname that a
 * DNS lookup solves to an IP address we have on the local machine.
 *
 */
static void
cli_show_lookup(int argc, char **argv)
{
	char *nodename;
	IPType ipType = IPTYPE_NONE;

	if (argc != 1)
	{
		commandline_print_usage(&do_show_lookup_command, stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}
	nodename = argv[0];
	ipType = ip_address_type(nodename);

	if (ipType == IPTYPE_NONE)
	{
		char localIpAddress[BUFSIZE];

		if (!findHostnameLocalAddress(nodename, localIpAddress, BUFSIZE))
		{
			log_fatal("Failed to check nodename \"%s\", see above for details",
					  nodename);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		fformat(stdout, "%s: %s\n", nodename, localIpAddress);
	}
	else
	{
		/* an IP address has been given, we do a reverse lookup */
		char *ipAddr = nodename;
		char hostname[_POSIX_HOST_NAME_MAX];
		char localIpAddress[BUFSIZE];

		/* reverse DNS lookup to fetch the hostname */
		if (!findHostnameFromLocalIpAddress(ipAddr,
											hostname, _POSIX_HOST_NAME_MAX))
		{
			/* errors already logged, keep the ipAddr, show exit failure */
			fformat(stdout, "%s\n", ipAddr);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		/* DNS lookup of the given hostname to make sure we get back here */
		if (!findHostnameLocalAddress(hostname, localIpAddress, BUFSIZE))
		{
			log_fatal("Failed to check nodename \"%s\", see above for details",
					  hostname);

			/* keep ipAddr and show exit failure */
			fformat(stdout, "%s\n", ipAddr);

			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		fformat(stdout, "%s: %s\n", localIpAddress, hostname);
	}
}


/*
 * cli_show_nodename shows the default --nodename we would use. It's the
 * reverse DNS entry for the local IP address we probe.
 */
static void
cli_show_nodename(int argc, char **argv)
{
	char ipAddr[BUFSIZE];
	char localIpAddress[BUFSIZE];
	char hostname[_POSIX_HOST_NAME_MAX];

	/* fetch the default local address used when connecting remotely */
	if (!fetchLocalIPAddress(ipAddr, BUFSIZE,
							 DEFAULT_INTERFACE_LOOKUP_SERVICE_NAME,
							 DEFAULT_INTERFACE_LOOKUP_SERVICE_PORT))
	{
		log_warn("Failed to determine network configuration.");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
	log_debug("cli_show_nodename: ip %s", ipAddr);

	/* do a reverse DNS lookup from this local address to an hostname */
	if (!findHostnameFromLocalIpAddress(ipAddr, hostname, _POSIX_HOST_NAME_MAX))
	{
		/* the nodename is going to be the ipAddr in that case */
		fformat(stdout, "%s\n", ipAddr);

		/* still indicate it was a failure */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
	log_debug("cli_show_nodename: host %s", hostname);

	/* do a lookup of the host name and see that we get a local address back */
	if (!findHostnameLocalAddress(hostname, localIpAddress, BUFSIZE))
	{
		/* the nodename is going to be the ipAddr in that case */
		fformat(stdout, "%s\n", ipAddr);

		/* still indicate it was a failure */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
	log_debug("cli_show_nodename: ip %s", localIpAddress);

	fformat(stdout, "%s\n", hostname);
}
