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
#include "pgsetup.h"
#include "primary_standby.h"


static void cli_show_ipaddr(int argc, char **argv);
static void cli_show_cidr(int argc, char **argv);
static void cli_show_lookup(int argc, char **argv);
static void cli_show_hostname(int argc, char **argv);
static void cli_show_reverse(int argc, char **argv);
static void cli_show_version(int arg, char **argv);

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
				 "<hostname>", "",
				 NULL, cli_show_lookup);

static CommandLine do_show_hostname_command =
	make_command("hostname",
				 "Print this node's default hostname",
				 "[postgres://monitor/uri]", "",
				 NULL, cli_show_hostname);

static CommandLine do_show_reverse_command =
	make_command("reverse",
				 "Lookup given hostname and check reverse DNS setup", "", "",
				 NULL, cli_show_reverse);

static CommandLine do_show_version_command =
	make_command("version",
				 "Run pg_autoctl version --json and parses the output", "", "",
				 NULL, cli_show_version);

CommandLine *do_show_subcommands[] = {
	&do_show_ipaddr_command,
	&do_show_cidr_command,
	&do_show_lookup_command,
	&do_show_hostname_command,
	&do_show_reverse_command,
	&do_show_version_command,
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
	bool mayRetry = false;

	if (!fetchLocalIPAddress(ipAddr, BUFSIZE,
							 DEFAULT_INTERFACE_LOOKUP_SERVICE_NAME,
							 DEFAULT_INTERFACE_LOOKUP_SERVICE_PORT,
							 LOG_WARN, &mayRetry))
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
	bool mayRetry = false;

	if (!fetchLocalIPAddress(ipAddr, BUFSIZE,
							 DEFAULT_INTERFACE_LOOKUP_SERVICE_NAME,
							 DEFAULT_INTERFACE_LOOKUP_SERVICE_PORT,
							 LOG_WARN, &mayRetry))
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
 * cli_check_hostname checks that the --hostname argument is either an IP
 * address that exists on the local list of interfaces, or a hostname that a
 * DNS lookup solves to an IP address we have on the local machine.
 *
 */
static void
cli_show_lookup(int argc, char **argv)
{
	if (argc != 1)
	{
		commandline_print_usage(&do_show_lookup_command, stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}
	char *hostname = argv[0];
	IPType ipType = ip_address_type(hostname);

	if (ipType == IPTYPE_NONE)
	{
		char localIpAddress[BUFSIZE];

		if (!findHostnameLocalAddress(hostname, localIpAddress, BUFSIZE))
		{
			log_fatal("Failed to check hostname \"%s\", see above for details",
					  hostname);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		fformat(stdout, "%s: %s\n", hostname, localIpAddress);
	}
	else
	{
		/* an IP address has been given, we do a reverse lookup */
		char *ipAddr = hostname;
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
			log_fatal("Failed to check hostname \"%s\", see above for details",
					  hostname);

			/* keep ipAddr and show exit failure */
			fformat(stdout, "%s\n", ipAddr);

			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		fformat(stdout, "%s: %s\n", localIpAddress, hostname);
	}
}


/*
 * cli_show_hostname shows the default --hostname we would use. It's the
 * reverse DNS entry for the local IP address we probe.
 */
static void
cli_show_hostname(int argc, char **argv)
{
	char ipAddr[BUFSIZE];
	char localIpAddress[BUFSIZE];
	char hostname[_POSIX_HOST_NAME_MAX];

	char monitorHostname[_POSIX_HOST_NAME_MAX];
	int monitorPort = pgsetup_get_pgport();

	bool mayRetry = false;

	/*
	 * When no argument is used, use hostname(3) and 5432, as we would for a
	 * monitor (pg_autoctl create monitor).
	 */
	if (argc == 0)
	{
		if (ipaddrGetLocalHostname(monitorHostname, sizeof(hostname)))
		{
			/* we found our hostname(3), use the default pg port */
			fformat(stdout, "%s\n", monitorHostname);
			exit(EXIT_CODE_QUIT);
		}
		else
		{
			/* use the default host/port to find the default local IP address */
			strlcpy(monitorHostname,
					DEFAULT_INTERFACE_LOOKUP_SERVICE_NAME,
					_POSIX_HOST_NAME_MAX);

			monitorPort = DEFAULT_INTERFACE_LOOKUP_SERVICE_PORT;
		}
	}

	/*
	 * When one argument is given, it is expected to be the monitor Postgres
	 * connection string, and we then act as a keeper node.
	 */
	else if (argc == 1)
	{
		if (!hostname_from_uri(argv[0],
							   monitorHostname, _POSIX_HOST_NAME_MAX,
							   &monitorPort))
		{
			log_fatal("Failed to determine monitor hostname when parsing "
					  "Postgres URI \"%s\"", argv[0]);
			exit(EXIT_CODE_BAD_ARGS);
		}

		log_info("Using monitor hostname \"%s\" and port %d",
				 monitorHostname,
				 monitorPort);
	}
	else
	{
		commandline_print_usage(&do_show_hostname_command, stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* fetch the default local address used when connecting remotely */
	if (!fetchLocalIPAddress(ipAddr, BUFSIZE,
							 monitorHostname, monitorPort,
							 LOG_WARN, &mayRetry))
	{
		log_warn("Failed to determine network configuration.");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
	log_debug("cli_show_hostname: ip %s", ipAddr);

	/* do a reverse DNS lookup from this local address to an hostname */
	if (!findHostnameFromLocalIpAddress(ipAddr, hostname, _POSIX_HOST_NAME_MAX))
	{
		/* the hostname is going to be the ipAddr in that case */
		fformat(stdout, "%s\n", ipAddr);

		/* still indicate it was a failure */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
	log_debug("cli_show_hostname: host %s", hostname);

	/* do a lookup of the host name and see that we get a local address back */
	if (!findHostnameLocalAddress(hostname, localIpAddress, BUFSIZE))
	{
		/* the hostname is going to be the ipAddr in that case */
		fformat(stdout, "%s\n", ipAddr);

		/* still indicate it was a failure */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
	log_debug("cli_show_hostname: ip %s", localIpAddress);

	fformat(stdout, "%s\n", hostname);
}


/*
 * cli_show_reverse does a forward DNS lookup of the given hostname, and then a
 * reverse DNS lookup for every of the forward DNS results. Success is reached
 * when at last one of the IP addresses from the forward lookup resolves back
 * to the given hostname.
 */
static void
cli_show_reverse(int argc, char **argv)
{
	char ipaddr[BUFSIZE] = { 0 };
	bool foundHostnameFromAddress = false;

	if (argc != 1)
	{
		commandline_print_usage(&do_show_reverse_command, stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	char *hostname = argv[0];
	IPType ipType = ip_address_type(hostname);

	if (ipType != IPTYPE_NONE)
	{
		log_error("Hostname must not be an IP address");
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!resolveHostnameForwardAndReverse(hostname, ipaddr, sizeof(ipaddr),
										  &foundHostnameFromAddress) ||
		!foundHostnameFromAddress)
	{
		log_fatal("Failed to find an IP address for hostname \"%s\" that "
				  "matches hostname again in a reverse-DNS lookup.",
				  hostname);
		log_info("Continuing with IP address \"%s\"", ipaddr);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	log_info("Hostname \"%s\" resolves to IP address %s and back",
			 hostname,
			 ipaddr);
}


/*
 * cli_show_version runs pg_autoctl version --json and parses the version
 * string.
 */
static void
cli_show_version(int arg, char **argv)
{
	Keeper keeper = { 0 };
	KeeperVersion version = { 0 };

	log_debug("cli_show_version");

	if (!keeper_pg_autoctl_get_version_from_disk(&keeper, &version))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	log_info("pg_autoctl \"%s\"", version.pg_autoctl_version);
	log_info("pgautofailover \"%s\"", version.required_extension_version);
}
