.. _pg_autoctl_inspect_show:

pg_autoctl inspect show
=======================

pg_autoctl inspect show - Network and hostname diagnostics

Synopsis
--------

The commands :ref:`pg_autoctl_create_monitor` and
:ref:`pg_autoctl_create_postgres` both implement automated detection of node
network settings when the option ``--hostname`` is not used. When a new node
is registered to the monitor, other nodes also update their HBA rules to
allow the new node to connect.

``pg_autoctl inspect show`` exposes the network discovery logic so that
operators can verify how ``pg_autoctl`` sees the local host.

::

    pg_autoctl inspect show
      ipaddr    Print this node's IP address information
      cidr      Print this node's CIDR information
      lookup    Print this node's DNS lookup information
      hostname  Print this node's default hostname
      reverse   Lookup given hostname and check reverse DNS setup

pg_autoctl inspect show ipaddr
------------------------------

Connects to an external IP address and uses ``getsockname(2)`` to retrieve
the current address to which the socket is bound. The external IP defaults
to ``8.8.8.8``, or to the monitor IP/hostname in the context of
:ref:`pg_autoctl_create_postgres`.

::

   $ pg_autoctl inspect show ipaddr
   192.168.1.156

pg_autoctl inspect show cidr
-----------------------------

Connects to an external IP address in the same way as ``inspect show ipaddr``
and then matches the local socket name with the list of local network
interfaces. When a match is found, uses the netmask of the interface to
compute the CIDR notation from the IP address. The computed CIDR is used in
HBA rules.

::

   $ pg_autoctl inspect show cidr
   192.168.1.0/24

pg_autoctl inspect show hostname
---------------------------------

Uses either its first argument or the result of ``gethostname(2)`` as the
candidate hostname for HBA rules, then checks that the hostname resolves to
an IP address that belongs to one of the machine's network interfaces.

::

   $ pg_autoctl inspect show hostname
   node1.example.com

pg_autoctl inspect show lookup
-------------------------------

Checks that the given argument is a hostname that resolves to a local IP
address (an IP address associated with a local network interface).

::

   $ pg_autoctl inspect show lookup node1.example.com
   node1.example.com: 192.168.1.156

pg_autoctl inspect show reverse
--------------------------------

Implements the same DNS checks as Postgres HBA matching code: first does a
forward DNS lookup of the given hostname, then a reverse-lookup from all the
IP addresses obtained. Success is reached when at least one IP address from
the forward lookup resolves back to the given hostname.

::

   $ pg_autoctl inspect show reverse node1.example.com
   node1.example.com: 192.168.1.156
