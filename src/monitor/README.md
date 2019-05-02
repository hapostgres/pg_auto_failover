# pg_auto_failover: the Monitor

This directory contains the pg_auto_failover monitor. The monitor handles
the group state machines of one or more groups of servers. Each group of
servers implements a single Highly Available PostgreSQL Service. Several
groups can be organized as part of the same formation.

The pg_auto_failover monitor is implemented as a PostgreSQL extension.

