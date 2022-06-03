.. _install:

Installing pg_auto_failover
===========================

We provide native system packages for pg_auto_failover on most popular Linux
distributions.

Use the steps below to install pg_auto_failover on PostgreSQL 11. At the
current time pg_auto_failover is compatible with both PostgreSQL 10 and
PostgreSQL 11.

Ubuntu or Debian
----------------

Postgres apt repository
~~~~~~~~~~~~~~~~~~~~~~~

Binary packages for debian and derivatives (ubuntu) are available from
`apt.postgresql.org`__ repository, install by following the linked
documentation and then::

  $ sudo apt-get install pg-auto-failover-cli
  $ sudo apt-get install postgresql-14-auto-failover

__ https://wiki.postgresql.org/wiki/Apt

The Postgres extension named "pgautofailover" is only necessary on the
monitor node. To install that extension, you can install the
``postgresql-14-auto-failover`` package when using Postgres 14. It's
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

Quick install
~~~~~~~~~~~~~

The following installation method downloads a bash script that automates
several steps. The full script is available for review at our `package cloud
installation instructions page`__ url.

__ https://packagecloud.io/citusdata/community/install#bash

.. code-block:: bash

  # add the required packages to your system
  curl https://install.citusdata.com/community/rpm.sh | sudo bash

  # install pg_auto_failover
  sudo yum install -y pg-auto-failover14_12

  # confirm installation
  /usr/pgsql-12/bin/pg_autoctl --version

Manual installation
~~~~~~~~~~~~~~~~~~~

If you'd prefer to install your repo on your system manually, follow the
instructions from `package cloud manual installation`__ page. This page will
guide you with the specific details to achieve the 3 steps:

  1. install the pygpgme yum-utils packages for your distribution
  2. install a new RPM reposiroty for CitusData packages
  3. update your local yum cache

Then when that's done, you can proceed with installing pg_auto_failover
itself as in the previous case:

.. code-block:: bash

  # install pg_auto_failover
  sudo yum install -y pg-auto-failover14_12

  # confirm installation
  /usr/pgsql-12/bin/pg_autoctl --version

__ https://packagecloud.io/citusdata/community/install#manual-rpm

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
