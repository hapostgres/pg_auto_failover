.. _install:

Installing pg_auto_failover
===========================

We provide native system packages for pg_auto_failover on most popular Linux
distributions.

Use the steps below to install pg_auto_failover on PostgreSQL 16. At the
current time pg_auto_failover is compatible with PostgreSQL 13 to 16.

Ubuntu or Debian
----------------

Postgres apt repository
~~~~~~~~~~~~~~~~~~~~~~~

Binary packages for debian and derivatives (ubuntu) are available from
`apt.postgresql.org`__ repository, install by following the linked
documentation and then::

  $ sudo apt-get install pg-auto-failover-cli
  $ sudo apt-get install postgresql-16-auto-failover

__ https://wiki.postgresql.org/wiki/Apt

The Postgres extension named "pgautofailover" is only necessary on the
monitor node. To install that extension, you can install the
``postgresql-16-auto-failover`` package when using Postgres 16. It's
available for other Postgres versions too.

Avoiding the default Postgres service
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When installing the debian Postgres package, the installation script will
initialize a Postgres data directory automatically, and register it to the
systemd services. When using pg_auto_failover, it is best to avoid that step.

To avoid automated creation of a Postgres data directory when installing the
debian package, follow those steps:

::

  $ curl https://www.postgresql.org/media/keys/ACCC4CF8.asc | apt-key add -
  $ echo "deb http://apt.postgresql.org/pub/repos/apt buster-pgdg main" > /etc/apt/sources.list.d/pgdg.list

  # bypass initdb of a "main" cluster
  $ echo 'create_main_cluster = false' | sudo tee -a /etc/postgresql-common/createcluster.conf
  $ apt-get update
  $ apt-get install -y --no-install-recommends postgresql-14

That way when it's time to :ref:`pg_autoctl_create_monitor` or
:ref:`pg_autoctl_create_postgres` there is no confusion about how to handle
the default Postgres service created by debian: it has not been created at
all.

Fedora, CentOS, or Red Hat
--------------------------

The Postgres community packaging team for RPM based system has worked on
supporting pg_auto_failover. Binary packages are available by following the
documentation at `PostgreSQL Yum Repository`__.

__ https://yum.postgresql.org

A single package named ``pg_auto_failover`` is available on the RPM based
systems, containing both the monitor Postgres extension and the pg_autoctl
command line.

Installing a pgautofailover Systemd unit
----------------------------------------

The command ``pg_autoctl show systemd`` outputs a systemd unit file that you
can use to setup a boot-time registered service for pg_auto_failover on your
machine.

Here's a sample output from the command:

.. code-block:: bash

   $ export PGDATA=/var/lib/postgresql/monitor
   $ pg_autoctl show systemd
   13:44:34 INFO  HINT: to complete a systemd integration, run the following commands:
   13:44:34 INFO  pg_autoctl -q show systemd --pgdata "/var/lib/postgresql/monitor" | sudo tee /etc/systemd/system/pgautofailover.service
   13:44:34 INFO  sudo systemctl daemon-reload
   13:44:34 INFO  sudo systemctl start pgautofailover
   [Unit]
   Description = pg_auto_failover

   [Service]
   WorkingDirectory = /var/lib/postgresql
   Environment = 'PGDATA=/var/lib/postgresql/monitor'
   User = postgres
   ExecStart = /usr/lib/postgresql/10/bin/pg_autoctl run
   Restart = always
   StartLimitBurst = 0

   [Install]
   WantedBy = multi-user.target

Copy/pasting the commands given in the hint output from the command will
enable the pgautofailer service on your system, when using systemd.

It is important that PostgreSQL is started by ``pg_autoctl`` rather than by
systemd itself, as it might be that a failover has been done during a
reboot, for instance, and that once the reboot complete we want the local
Postgres to re-join as a secondary node where it used to be a primary node.


Building pg_auto_failover from sources
--------------------------------------

To build the project, make sure you have installed the build-dependencies,
then just type `make`. You can install the resulting binary using `make
install`.

For this to work please consider adding both the binary and the source
repositories to your debian distribution by using the following apt sources,
as an example targetting the debian bullseye distribution:

::

   deb http://apt.postgresql.org/pub/repos/apt bullseye-pgdg main
   deb-src http://apt.postgresql.org/pub/repos/apt bullseye-pgdg main

Then we can install the build dependencies for Postgres, knowing that
pg_auto_failover uses the same build dependencies:

::

   $ sudo apt-get build-dep -y --no-install-recommends postgresql-14

Then build pg_auto_failover from sources with the following instructions:

::

   $ make -s clean && make -s -j12 all
   $ sudo make -s install
