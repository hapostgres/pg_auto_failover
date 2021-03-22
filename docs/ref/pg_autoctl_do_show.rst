.. _pg_autoctl_do_show:

pg_autoctl do show
==================

pg_autoctl do show - Show some debug level information

Synopsis
--------

pg_autoctl do show provides the following commands::

    pg_autoctl do show
      ipaddr    Print this node's IP address information
      cidr      Print this node's CIDR information
      lookup    Print this node's DNS lookup information
      hostname  Print this node's default hostname
      reverse   Lookup given hostname and check reverse DNS setup

Description
-----------

The commands :ref:`pg_autoctl_create_monitor` and
:ref:`pg_autoctl_create_postgres` both implement some level of automated
detection of the node network settings when the option ``--hostname`` is not
used.

Adding to those commands, when a new node is registered to the monitor,
other nodes also edit their Postgres HBA rules to allow the new node to
connect, unless the option ``--skip-pg-hba`` has been used.

The debug sub-commands for ``pg_autoctl do show`` can be used to see in
details the network discovery done by ``pg_autoctl``.

Examples
--------

::

   $ pg_autoctl do show ipaddr
   16:42:40 62631 INFO  ipaddr.c:107: Connecting to 8.8.8.8 (port 53)
   192.168.1.156

   $ pg_autoctl do show cidr
   16:43:19 63319 INFO  Connecting to 8.8.8.8 (port 53)
   192.168.1.0/24

   $ pg_autoctl do show hostname
   DESKTOP-IC01GOOS.europe.corp.microsoft.com

   $ pg_autoctl do show lookup DESKTOP-IC01GOOS.europe.corp.microsoft.com
   DESKTOP-IC01GOOS.europe.corp.microsoft.com: 192.168.1.156

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
