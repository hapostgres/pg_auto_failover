# pg_auto_failover

[![Documentation Status](https://readthedocs.org/projects/pg-auto-failover/badge/?version=main)](https://pg-auto-failover.readthedocs.io/en/main/?badge=main)

pg_auto_failover is an extension and service for PostgreSQL that monitors
and manages automated failover for a Postgres cluster. It is optimized for
simplicity and correctness and supports Postgres 10 and newer.

pg_auto_failover supports several Postgres architectures and implements a
safe automated failover for your Postgres service. It is possible to get
started with only two data nodes which will be given the roles of primary
and secondary by the monitor.

![pg_auto_failover Architecture with 2 nodes](docs/tikz/arch-single-standby.svg?raw=true "pg_auto_failover Architecture with 2 nodes")

The pg_auto_failover Monitor implements a state machine and relies on
in-core PostgreSQL facilities to deliver HA. For example, when the
**secondary** node is detected to be unavailable, or when its lag is too
much, then the Monitor removes it from the `synchronous_standby_names`
setting on the **primary** node. Until the **secondary** is back to being
monitored healthy, failover and switchover operations are not allowed,
preventing data loss.

pg_auto_failover consists of the following parts:

  - a PostgreSQL extension named `pgautofailover`
  - a PostgreSQL service to operate the pg_auto_failover monitor
  - a pg_auto_failover keeper to operate your PostgreSQL instances, see `pg_autoctl run`

## Multiple Standbys

It is possible to implement a production architecture with any number of
Postgres nodes, for better data availability guarantees.

![pg_auto_failover Architecture with 3 nodes](docs/tikz/arch-multi-standby.svg?raw=true "pg_auto_failover Architecture with 3 nodes")

By default, pg_auto_failover uses synchronous replication and every node
that reaches the secondary state is added to synchronous_standby_names on
the primary. With pg_auto_failover 1.4 it is possible to remove a node from
the _replication quorum_ of Postgres.

## Citus HA

Starting with pg_auto_failover 2.0 it's now possible to also implement High
Availability for a Citus cluster.

![pg_auto_failover Architecture with Citus](docs/tikz/arch-citus.svg?raw=true "pg_auto_failover Architecture with Citus")

## Documentation

Please check out project
[documentation](https://pg-auto-failover.readthedocs.io/en/main/) for
tutorial, manual pages, detailed design coverage, and troubleshooting
information.

## Installing pg_auto_failover from packages

Note that pg_auto_failover packages are also found in Postgres PGDG package
repositories. If you're using those repositories already, you can install
the packages from there.

### Ubuntu or Debian:

Binary packages for debian and derivatives (ubuntu) are available from
[apt.postgresql.org](https://wiki.postgresql.org/wiki/Apt) repository,
install by following the linked documentation and then::

```bash
$ sudo apt-get install pg-auto-failover-cli
$ sudo apt-get install postgresql-14-auto-failover
```

When using debian, two packages are provided for pg_auto_failover: the
monitor Postgres extension is packaged separately and depends on the
Postgres version you want to run for the monitor itself. The monitor's
extension package is named `postgresql-14-auto-failover` when targeting
Postgres 14.

Then another package is prepared that contains the `pg_autoctl` command, and
the name of the package is `pg-auto-failover-cli`. That's the package that
is needed for the Postgres nodes.

To avoid debian creating a default Postgres data directory and service,
follow these steps before installing the previous packages.

```bash
$ curl https://www.postgresql.org/media/keys/ACCC4CF8.asc | apt-key add -
$ echo "deb http://apt.postgresql.org/pub/repos/apt buster-pgdg main" > /etc/apt/sources.list.d/pgdg.list

# bypass initdb of a "main" cluster
$ echo 'create_main_cluster = false' | sudo tee -a /etc/postgresql-common/createcluster.conf
$ apt-get update
$ apt-get install -y --no-install-recommends postgresql-14
```

### Other installation methods

Please see our extended documentation chapter [Installing
pg_auto_failover](https://pg-auto-failover.readthedocs.io/en/main/install.html)
for details.

## Trying pg_auto_failover on your local computer

The main documentation for pg_auto_failover includes the following 3 tutorial:

  - The main [pg_auto_failover
    Tutorial](https://pg-auto-failover.readthedocs.io/en/main/tutorial.html)
    uses docker compose on your local computer to start multiple Postgres
    nodes and implement your first failover.

  - The complete [pg_auto_failover Azure VM
    Tutorial](https://pg-auto-failover.readthedocs.io/en/main/azure-tutorial.html)
    guides you into creating an Azure network and then Azure VMs in that
    network, to then provisioning those VMs, and then running Postgres nodes
    with pg_auto_failover and then introducing hard failures and witnessing
    an automated failover.

  - The [Citus Cluster Quick
    Start](https://pg-auto-failover.readthedocs.io/en/main/citus-quickstart.html)
    tutorial uses docker compose to create a full Citus cluster and guide
    you to a worker failover and then a coordinator failover.

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
