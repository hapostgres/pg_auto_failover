# pg_auto_failover

[![Slack Status](http://slack.citusdata.com/badge.svg)](https://slack.citusdata.com)
[![Documentation Status](https://readthedocs.org/projects/pg-auto-failover/badge/?version=master)](https://pg-auto-failover.readthedocs.io/en/master/?badge=master)

pg_auto_failover is an extension and service for PostgreSQL that monitors
and manages automated failover for a Postgres cluster. It is optimized for
simplicity and correctness and supports Postgres 10 and newer.

pg_auto_failover supports several Postgres architectures and implements a
safe automated failover for your Postgres service. It is possible to get
started with only two data nodes which will be given the roles of primary
and secondary by the monitor.

![pg_auto_failover Architecture with 2 nodes](docs/tikz/arch-single-standby.svg?raw=true "pg_auto_failover Architecture with 2 nodes")

The pg_auto_failover Monitor implements a state machine and relies on in-core
PostgreSQL facilities to deliver HA. For example. when the **secondary** node
is detected to be unavailable, or when its lag is too much, then the
Monitor removes it from the `synchronous_standby_names` setting on the
**primary** node. Until the **secondary** is back to being monitored healthy,
failover and switchover operations are not allowed, preventing data loss.

pg_auto_failover consists of the following parts:

  - a PostgreSQL extension named `pgautofailover`
  - a PostgreSQL service to operate the pg_auto_failover monitor
  - a pg_auto_failover keeper to operate your PostgreSQL instances, see `pg_autoctl run`

Starting with pg_auto_failover version 1.4, it is possible to implement a
production architecture with any number of Postgres nodes, for better data
availability guarantees.

![pg_auto_failover Architecture with 3 nodes](docs/tikz/arch-multi-standby.svg?raw=true "pg_auto_failover Architecture with 3 nodes")

By default, pg_auto_failover uses synchronous replication and every node
that reaches the secondary state is added to synchronous_standby_names on
the primary. With pg_auto_failover 1.4 it is possible to remove a node from
the _replication quorum_ of Postgres.

## Dependencies

At runtime, pg_auto_failover depends on only Postgres. Postgres versions 10,
11, 12, and 13 are currently supported.

At buildtime. pg_auto_failover depends on Postgres server development
package like any other Postgres extensions (the server development package
for Postgres 11 when using debian or Ubuntu is named
`postgresql-server-dev-11`), and then `libssl-dev` and `libkrb5-dev` are
needed to for the client side when building with all the `libpq`
authentication options.

## Documentation

Please check out project
[documentation](https://pg-auto-failover.readthedocs.io/en/master/) for how
to guides and troubleshooting information.

## Installing pg_auto_failover from packages

Ubuntu or Debian:

```bash
# Add the repository to your system
curl https://install.citusdata.com/community/deb.sh | sudo bash

# Install pg_auto_failover
sudo apt-get install postgresql-11-auto-failover

# Confirm installation
/usr/bin/pg_autoctl --version
```

Fedora, CentOS, or Red Hat:

```bash
# Add the repository to your system
curl https://install.citusdata.com/community/rpm.sh | sudo bash

# Install pg_auto_failover
sudo yum install -y pg-auto-failover10_11

# Confirm installation
/usr/pgsql-11/bin/pg_autoctl --version
```

## Building pg_auto_failover from source

To build the project, make sure you have installed the build-dependencies,
then just type `make`. You can install the resulting binary using `make
install`.

Build dependencies example on debian for Postgres 11:

~~~ bash
$ sudo apt-get install postgresql-server-dev-11 libssl-dev libkrb5-dev
~~~

Then build pg_auto_failover from sources with the following instructions:

~~~ bash
$ make
$ sudo make install -j10
~~~

For this to work though, the PostgreSQL client (libpq) and server
(postgresql-server-dev) libraries must be available in your standard include
and link paths.

The `make install` step will deploy the `pgautofailover` PostgreSQL extension in
the PostgreSQL directory for extensions as pointed by `pg_config`, and
install the `pg_autoctl` binary command in the directory pointed to by
`pg_config --bindir`, alongside other PostgreSQL tools such as `pg_ctl` and
`pg_controldata`.

## Trying pg_auto_failover on your local computer

Once the building and installation is done, follow those steps:

  1. Install and run a monitor

     ~~~ bash
	 $ export PGDATA=./monitor
	 $ export PGPORT=5000
	 $ pg_autoctl create monitor --ssl-self-signed --hostname localhost --auth trust --run
     ~~~

  2. Get the Postgres URI (connection string) for the monitor node:

     ~~~ bash
     $ pg_autoctl show uri --monitor --pgdata ./monitor
	 postgres://autoctl_node@localhost:5000/pg_auto_failover?sslmode=require
     ~~~

     The following two steps are going to use the option `--monitor` which
     expects that connection string. So copy/paste your actual Postgres URI
     for the monitor in the next steps.

  3. Install and run a primary PostgreSQL instance:

     ~~~ bash
	 $ export PGDATA=./node_1
	 $ export PGPORT=5001
     $ pg_autoctl create postgres \
         --hostname localhost \
         --auth trust \
         --ssl-self-signed \
         --monitor 'postgres://autoctl_node@localhost:5000/pg_auto_failover?sslmode=require' \
         --run
     ~~~

  4. Install and run a secondary PostgreSQL instance, using exactly the same
     command, but with a different PGDATA and PGPORT, because we're running
     everything on the same host:

     ~~~ bash
	 $ export PGDATA=./node_2
	 $ export PGPORT=5002
     $ pg_autoctl create postgres \
         --hostname localhost \
         --auth trust \
         --ssl-self-signed \
         --monitor 'postgres://autoctl_node@localhost:5000/pg_auto_failover?sslmode=require' \
         --run
     ~~~

  4. See the state of the new system:

     ~~~ bash
	 $ pg_autoctl show state
	   Name |  Node |      Host:Port |       LSN | Reachable |       Current State |      Assigned State
     -------+-------+----------------+-----------+-----------+---------------------+--------------------
     node_1 |     1 | localhost:5001 | 0/30000D8 |       yes |             primary |             primary
     node_2 |     2 | localhost:5002 | 0/30000D8 |       yes |           secondary |           secondary
     ~~~

That's it! You now have a running pg_auto_failover setup with two PostgreSQL nodes
using Streaming Replication to implement fault-tolerance.

## Your first failover

Now that we have two nodes setup and running, we can initiate a manual
failover, also named a switchover. It is possible to trigger such an
operation without any node having to actually fail when using
pg_auto_failover.

The command `pg_autoctl perform switchover` can be used to force
pg_auto_failover to orchestrate a failover. Because all the nodes are
actually running fine (meaning that `pg_autoctl` actively reports the local
state of each node to the monitor), the failover process does not have to
carefuly implement timeouts to make sure to avoid split-brain.

~~~ bash
$ pg_autoctl perform switchover
19:06:41 63977 INFO  Listening monitor notifications about state changes in formation "default" and group 0
19:06:41 63977 INFO  Following table displays times when notifications are received
    Time |   Name |  Node |      Host:Port |       Current State |      Assigned State
---------+--------+-------+----------------+---------------------+--------------------
19:06:43 | node_1 |     1 | localhost:5001 |             primary |            draining
19:06:43 | node_2 |     2 | localhost:5002 |           secondary |   prepare_promotion
19:06:43 | node_2 |     2 | localhost:5002 |   prepare_promotion |   prepare_promotion
19:06:43 | node_2 |     2 | localhost:5002 |   prepare_promotion |    stop_replication
19:06:43 | node_1 |     1 | localhost:5001 |             primary |      demote_timeout
19:06:43 | node_1 |     1 | localhost:5001 |            draining |      demote_timeout
19:06:43 | node_1 |     1 | localhost:5001 |      demote_timeout |      demote_timeout
19:06:44 | node_2 |     2 | localhost:5002 |    stop_replication |    stop_replication
19:06:44 | node_2 |     2 | localhost:5002 |    stop_replication |        wait_primary
19:06:44 | node_1 |     1 | localhost:5001 |      demote_timeout |             demoted
19:06:44 | node_1 |     1 | localhost:5001 |             demoted |             demoted
19:06:44 | node_2 |     2 | localhost:5002 |        wait_primary |        wait_primary
19:06:45 | node_1 |     1 | localhost:5001 |             demoted |          catchingup
19:06:46 | node_1 |     1 | localhost:5001 |          catchingup |          catchingup
19:06:47 | node_1 |     1 | localhost:5001 |          catchingup |           secondary
19:06:47 | node_2 |     2 | localhost:5002 |        wait_primary |             primary
19:06:47 | node_1 |     1 | localhost:5001 |           secondary |           secondary
19:06:48 | node_2 |     2 | localhost:5002 |             primary |             primary
~~~

The promotion of the secondary node is finished when the node reaches the
goal state *wait_primary*. At this point, the application that connects to
the secondary is allowed to proceed with write traffic.

Because this is a switchover and no nodes have failed, `node_1` that used to
be the primary completes its cycle and joins as a secondary within the same
operation. The Postgres tool `pg_rewind` is used to implement that
transition.

And there you have done a full failover from your `node_1`, former primary, to
your `node_2`, new primary. We can have a look at the state now:

~~~
$ pg_autoctl show state
  Name |  Node |      Host:Port |       LSN | Reachable |       Current State |      Assigned State
-------+-------+----------------+-----------+-----------+---------------------+--------------------
node_1 |     1 | localhost:5001 | 0/3001648 |       yes |           secondary |           secondary
node_2 |     2 | localhost:5002 | 0/3001648 |       yes |             primary |             primary
~~~

## Cleaning-up your local setup

You can use the commands `pg_autoctl stop`, `pg_autoctl drop node
--destroy`, and `pg_autoctl drop monitor --destroy` if you want to get rid
of everything set-up so far.

## Formations and Groups

In the previous example, the options `--formation` and `--group` are not
used. This means we've been using the default values: the default formation
is named *default* and the default group id is zero (0).

It's possible to add other services to the same running monitor by using
another formation.

## Installing pg_auto_failover on-top of an existing Postgres setup

The `pg_autoctl create postgres --pgdata ${PGDATA}` step can be used with an
existing Postgres installation running at `${PGDATA}`, only with the primary
node.

On a secondary node, it is possible to re-use an existing data directory
when it has the same `system_identifier` as the other node(s) already
registered in the same formation and group.

## Application and Connection Strings

To retrieve the connection string to use at the application level, use the
following command:

~~~ bash
$ pg_autoctl show uri --formation default --pgdata ...
postgres://localhost:5002,localhost:5001/postgres?target_session_attrs=read-write&sslmode=require
~~~

You can use that connection string from within your application, adjusting
the username that is used to connect. By default, pg_auto_failover edits the
Postgres HBA rules to allow the `--username` given at `pg_autoctl create
postgres` time to connect to this URI from the database node itself.

To allow application servers to connect to the Postgres database, edit your
`pg_hba.conf` file as documented in [the pg_hba.conf
file](https://www.postgresql.org/docs/current/auth-pg-hba-conf.html) chapter
of the PostgreSQL documentation.

## Reporting Security Issues

Security issues and bugs should be reported privately, via email, to the Microsoft Security
Response Center (MSRC) at [secure@microsoft.com](mailto:secure@microsoft.com). You should
receive a response within 24 hours. If for some reason you do not, please follow up via
email to ensure we received your original message. Further information, including the
[MSRC PGP](https://technet.microsoft.com/en-us/security/dn606155) key, can be found in
the [Security TechCenter](https://technet.microsoft.com/en-us/security/default).

## Authors

* [Dimitri Fontaine](https://github.com/dimitri)
* [Nils Dijk](https://github.com/thanodnl)
* [Marco Slot](https://github.com/marcoslot)
* [Louise Grandjonc](https://github.com/louiseGrandjonc)
* [Joe Nelson](https://github.com/begriffs)
* [Hadi Moshayedi](https://github.com/pykello)
* [Lukas Fittl](https://github.com/lfittl)
* [Murat Tuncer](https://github.com/mtuncer)
* [Jelte Fennema](https://github.com/JelteF)

## License

Copyright (c) Microsoft Corporation. All rights reserved.

This project is licensed under the PostgreSQL License, see LICENSE file for details.

This project includes bundled third-party dependencies, see NOTICE file for details.
