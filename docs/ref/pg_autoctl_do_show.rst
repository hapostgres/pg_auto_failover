.. _pg_autoctl_do_show:

pg_autoctl do show
==================

pg_autoctl do show - Show some debug level information

Synopsis
--------

The commands :ref:`pg_autoctl_create_monitor` and
:ref:`pg_autoctl_create_postgres` both implement some level of automated
detection of the node network settings when the option ``--hostname`` is not
used.

Adding to those commands, when a new node is registered to the monitor,
other nodes also edit their Postgres HBA rules to allow the new node to
connect, unless the option ``--skip-pg-hba`` has been used.

The debug sub-commands for ``pg_autoctl do show`` can be used to see in
details the network discovery done by ``pg_autoctl``.

pg_autoctl do show provides the following commands::

    pg_autoctl do show
      ipaddr    Print this node's IP address information
      cidr      Print this node's CIDR information
      lookup    Print this node's DNS lookup information
      hostname  Print this node's default hostname
      reverse   Lookup given hostname and check reverse DNS setup

pg_autoctl do show ipaddr
-------------------------

Connects to an external IP address and uses ``getsockname(2)`` to retrieve
the current address to which the socket is bound.

The external IP address defaults to ``8.8.8.8``, the IP address of a Google
provided public DNS server, or to the monitor IP address or hostname in the
context of :ref:`pg_autoctl_create_postgres`.

::

   $ pg_autoctl do show ipaddr
   16:42:40 62631 INFO  ipaddr.c:107: Connecting to 8.8.8.8 (port 53)
   192.168.1.156

pg_autoctl do show cidr
-----------------------

Connects to an external IP address in the same way as the previous command
``pg_autoctl do show ipaddr`` and then matches the local socket name with
the list of local network interfaces. When a match is found, uses the
netmask of the interface to compute the CIDR notation from the IP address.

The computed CIDR notation is then used in HBA rules.

::

   $ pg_autoctl do show cidr
   16:43:19 63319 INFO  Connecting to 8.8.8.8 (port 53)
   192.168.1.0/24


pg_autoctl do show hostname
---------------------------

Uses either its first (and only) argument or the result of
``gethostname(2)`` as the candidate hostname to use in HBA rules, and then
check that the hostname resolves to an IP address that belongs to one of the
machine network interfaces.

When the hostname forward-dns lookup resolves to an IP address that is local
to the node where the command is run, then a reverse-lookup from the IP
address is made to see if it matches with the candidate hostname.

::

   $ pg_autoctl do show hostname
   DESKTOP-IC01GOOS.europe.corp.microsoft.com

   $ pg_autoctl -vv do show hostname 'postgres://autoctl_node@localhost:5500/pg_auto_failover'
   13:45:00 93122 INFO  cli_do_show.c:256: Using monitor hostname "localhost" and port 5500
   13:45:00 93122 INFO  ipaddr.c:107: Connecting to ::1 (port 5500)
   13:45:00 93122 DEBUG cli_do_show.c:272: cli_show_hostname: ip ::1
   13:45:00 93122 DEBUG cli_do_show.c:283: cli_show_hostname: host localhost
   13:45:00 93122 DEBUG cli_do_show.c:294: cli_show_hostname: ip ::1
   localhost

pg_autoctl do show lookup
-------------------------

Checks that the given argument is an hostname that resolves to a local IP
address, that is an IP address associated with a local network interface.

::

   $ pg_autoctl do show lookup DESKTOP-IC01GOOS.europe.corp.microsoft.com
   DESKTOP-IC01GOOS.europe.corp.microsoft.com: 192.168.1.156

pg_autoctl do show reverse
--------------------------

Implements the same DNS checks as Postgres HBA matching code: first does a
forward DNS lookup of the given hostname, and then a reverse-lookup from all
the IP addresses obtained. Success is reached when at least one of the IP
addresses from the forward lookup resolves back to the given hostname (as
the first answer to the reverse DNS lookup).

::

   $ pg_autoctl do show reverse DESKTOP-IC01GOOS.europe.corp.microsoft.com
   16:44:49 64910 FATAL Failed to find an IP address for hostname "DESKTOP-IC01GOOS.europe.corp.microsoft.com" that matches hostname again in a reverse-DNS lookup.
   16:44:49 64910 INFO  Continuing with IP address "192.168.1.156"

   $ pg_autoctl -vv do show reverse DESKTOP-IC01GOOS.europe.corp.microsoft.com
   16:44:45 64832 DEBUG ipaddr.c:719: DESKTOP-IC01GOOS.europe.corp.microsoft.com has address 192.168.1.156
   16:44:45 64832 DEBUG ipaddr.c:733: reverse lookup for "192.168.1.156" gives "desktop-ic01goos.europe.corp.microsoft.com" first
   16:44:45 64832 DEBUG ipaddr.c:719: DESKTOP-IC01GOOS.europe.corp.microsoft.com has address 192.168.1.156
   16:44:45 64832 DEBUG ipaddr.c:733: reverse lookup for "192.168.1.156" gives "desktop-ic01goos.europe.corp.microsoft.com" first
   16:44:45 64832 DEBUG ipaddr.c:719: DESKTOP-IC01GOOS.europe.corp.microsoft.com has address 2a01:110:10:40c::2ad
   16:44:45 64832 DEBUG ipaddr.c:728: Failed to resolve hostname from address "192.168.1.156": nodename nor servname provided, or not known
   16:44:45 64832 DEBUG ipaddr.c:719: DESKTOP-IC01GOOS.europe.corp.microsoft.com has address 2a01:110:10:40c::2ad
   16:44:45 64832 DEBUG ipaddr.c:728: Failed to resolve hostname from address "192.168.1.156": nodename nor servname provided, or not known
   16:44:45 64832 DEBUG ipaddr.c:719: DESKTOP-IC01GOOS.europe.corp.microsoft.com has address 100.64.34.213
   16:44:45 64832 DEBUG ipaddr.c:728: Failed to resolve hostname from address "192.168.1.156": nodename nor servname provided, or not known
   16:44:45 64832 DEBUG ipaddr.c:719: DESKTOP-IC01GOOS.europe.corp.microsoft.com has address 100.64.34.213
   16:44:45 64832 DEBUG ipaddr.c:728: Failed to resolve hostname from address "192.168.1.156": nodename nor servname provided, or not known
   16:44:45 64832 FATAL cli_do_show.c:333: Failed to find an IP address for hostname "DESKTOP-IC01GOOS.europe.corp.microsoft.com" that matches hostname again in a reverse-DNS lookup.
   16:44:45 64832 INFO  cli_do_show.c:334: Continuing with IP address "192.168.1.156"
