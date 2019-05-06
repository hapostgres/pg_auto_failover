# pg_auto_failover

[![Slack Status](http://slack.citusdata.com/badge.svg)](https://slack.citusdata.com)
[![Documentation Status](https://readthedocs.org/projects/pg-auto-failover/badge/?version=latest)](https://pg-auto-failover.readthedocs.io/en/latest/?badge=latest)

pg_auto_failover is an extension and service for PostgreSQL that monitors and manages
automated failover for a Postgres cluster. It is optimized for simplicity and correctness and supports Postgres 10 and newer.

We set up one PostgreSQL server as a **monitor** node as well as a **primary** and **secondary** node for storing data. The monitor node tracks the health of the data nodes and implements a failover state machine. On the PostgreSQL nodes, the `pg_autoctl` program runs alongside PostgreSQL and runs the necessary commands to configure synchronous streaming replication.

![pg_auto_failover Architecture](docs/pg_auto_failover-arch.png?raw=true "pg_auto_failover Architecture")

The pg_auto_failover Monitor implements a state machine and relies on in-core
PostgreSQL facilities to deliver HA. For example. when the **secondary** node
is detected to be unavailable, or when its lag is too important, then the
Monitor removes it from the `synchronous_standby_names` setting on the
**primary** node. Until the **secondary** is back to being monitored healthy,
failover and switchover operations are not allowed, preventing data loss.

pg_auto_failover consists of the following parts:

  - a PostgreSQL extension named `pgautofailover`
  - a PostgreSQL service to operate the pg_auto_failover monitor
  - a pg_auto_failover keeper to operate your PostgreSQL instances, see `pg_autoctl run`

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

To build the project, just type `make`.

~~~ bash
$ make
$ make install
~~~

For this to work though, the PostgreSQL client (libpq) and server
(postgresql-server-dev) libraries must be available in your standard include
and link paths.

The `make install` step will deploy the `pgautofailover` PostgreSQL extension in
the PostgreSQL directory for extensions as pointed by `pg_config`, and
install the `pg_autoctl` binary command in the directory pointed to by
`pg_config --bindir`, alongside other PostgreSQL tools such as `pg_ctl` and
`pg_controldata`.

## Using pg_auto_failover

Once the building and installation is done, follow those steps:

  1. Install and start a pg_auto_failover monitor on your monitor machine:

     ~~~ bash
     $ pg_autoctl create monitor --pgdata /path/to/pgdata     \
                                 --nodename `pg_autoctl show ipaddr`
     ~~~

     Once this command is done, you should have a running PostgreSQL
     instance on the machine, installed in the directory pointed to by the
     `--pgdata` option, using the default port 5432.

     You may change the port using `--pgport`.

     The command also creates a `autoctl` user and database, and a
     `autoctl_node` user for the other nodes to use. In the `pg_auto_failover`
     database, the extension `pgautofailover` is installed, and some *background
     worker* jobs are active already, waiting until a PostgreSQL node gets
     registered to run health-checks on it.

  2. Install and start a primary PostgreSQL instance:

     ~~~ bash
     $ pg_autoctl create postgres --pgdata /path/to/pgdata     \
                                  --nodename `pg_autoctl show ipaddr`  \
                                  --monitor postgres://autoctl_node@host/pg_auto_failover
     ~~~

     This command is using lots of default parameters that you may want to
     override, see its `--help` output for details. The three parameters
     used above are mandatory, though:

       - `--pgdata` sets the PGDATA directory where to install PostgreSQL,
         and if not given as an command line option then the environment
         variable `PGDATA` is used, when defined.

       - `--nodename` is used by other nodes to connect to this one, so it
         should be a hostname or an IP address that is reachable by the
         other members (localhost probably won't work).
         
         The provided `--nodename` is going to be used by pg_auto_failover
         to grant connection privileges in the Postgres HBA file. When using
         a hostname rather than an IP address, please make sure that reverse
         DNS is then working with the provided hostname, because that's how
         Postgres will then match any connection attempt with the HBA rules
         for granting connections, as described in Postgres documentation
         for [the pg_hba.conf
         file](https://www.postgresql.org/docs/current/auth-pg-hba-conf.html),
         at the *address* field.

         In particular, this matching is done when setting-up replication
         from the primary to the secondary node by pg_auto_failover.

       - `--monitor` is the Postgres URI used to connect to the monitor that
         we deployed with the previous command, you should replace `host` in
         the connection string to point to the right host and port.

         Also, pg_auto_failover currently makes no provision on the monitor node
         with respect to database connection privileges, that are edited in
         PostgreSQL `pg_hba.conf` file. So please adjust the setup to allow
         for your keepers to be able to connect to the monitor.

     The initialisation step probes the given `--pgdata` directory for an
     existing PostgreSQL cluster, and when the directory doesn't exist it
     will go ahead and `pg_ctl initdb` one for you, after having registered
     the local node (`nodename:pgport`) to the pg_auto_failover monitor.

     Now that the installation is ready we can run the keeper service, which
     connects to the pg_auto_failover monitor every 5 seconds and implement the
     state transitions when needed:

     ~~~ bash
     $ pg_autoctl run --pgdata /path/to/pgdata
     ~~~


  3. Install and start a secondary PostgreSQL instance:

     ~~~ bash
     $ pg_autoctl create postgres --pgdata /path/to/pgdata     \
                                  --nodename `pg_autoctl show ipaddr`  \
                                  --monitor postgres://autoctl_node@host/pg_auto_failover
     ~~~

     This command is the same as in the previous section, because it's all
     about initializing a PostgreSQL node again. This time, the monitor has
     a node registered as a primary server already, in the state SINGLE.
     Given this current state, the monitor is assigning this new node the
     role of a standby, and `pg_autoctl create` makes that happen.

     The command waits until the primary has prepared the PostgreSQL
     replication, which means editing `pg_hba.conf` to allow for connecting
     the standby with the *replication* privilege, and creating a
     replication slot for the standby.

     Once everything is ready, the monitor assign the goal state CATCHINGUP
     to the secondary server, which can now `pg_basebackup` from the
     primary, install its `recovery.conf` and start the PostgreSQL service.

     Once the `pg_autoctl create` command is done, again, it's important
     to run the keeper's service:

     ~~~ bash
     $ pg_autoctl run --pgdata /path/to/pgdata
     ~~~

That's it! You now have a running pg_auto_failover setup with two PostgreSQL nodes
using Streaming Replication to implement fault-tolerance.

## Formations and Groups

In the previous example, the options `--formation` and `--group` are not
used. This means we've been using the default values: the default formation
is named *default* and the default group id is zero (0).

It's possible to add other services to the same running monitor by using
another formation.

## Contributing

This project welcomes contributions and suggestions. Most contributions require you to
agree to a Contributor License Agreement (CLA) declaring that you have the right to,
and actually do, grant us the rights to use your contribution. For details, visit
https://cla.microsoft.com.

When you submit a pull request, a CLA-bot will automatically determine whether you need
to provide a CLA and decorate the PR appropriately (e.g., label, comment). Simply follow the
instructions provided by the bot. You will only need to do this once across all repositories using our CLA.

This project has adopted the [Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/).
For more information see the [Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/)
or contact [opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional questions or comments.

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
* [Hadi Moshayedi](https://github.com/pykello)
* [Lukas Fittl](https://github.com/lfittl)

## License

Copyright (c) Microsoft Corporation. All rights reserved.

This project is licensed under the PostgreSQL License, see LICENSE file for details.

This project includes bundled third-party dependencies, see NOTICE file for details.
