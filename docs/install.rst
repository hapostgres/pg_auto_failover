.. _install:

Installing pg_auto_failover
===========================

We provide native system packages for pg_auto_failover on most popular Linux distributions.

Use the steps below to install pg_auto_failover on PostgreSQL 11. At the
current time pg_auto_failover is compatible with both PostgreSQL 10 and
PostgreSQL 11.

Ubuntu or Debian
----------------

.. code-block:: bash

  # add the required packages to your system
  curl https://install.citusdata.com/community/deb.sh | sudo bash

  # install pg_auto_failover
  sudo apt-get install postgresql-11-auto-failover

  # confirm installation
  /usr/bin/pg_autoctl --version

Fedora, CentOS, or Red Hat
--------------------------

.. code-block:: bash

  # add the required packages to your system
  curl https://install.citusdata.com/community/rpm.sh | sudo bash

  # install pg_auto_failover
  sudo yum install -y pg-auto-failover10_11

  # confirm installation
  /usr/pgsql-11/bin/pg_autoctl --version

Installing a pgautofailover Systemd unit
----------------------------------------

The command ``pg_autoctl show systemd`` outputs a systemd unit file that you
can use to setup a boot-time registered service for pg_auto_failover on your
machine.

Here's a sample output from the command:

.. code-block::

   $ pg_autoctl show systemd
   13:44:34 INFO  HINT: to complete a systemd integration, run the following commands:
   13:44:34 INFO  pg_autoctl -q show systemd --pgdata /var/lib/postgresql/monitor | sudo tee /etc/systemd/system/pgautofailover.service
   13:44:34 INFO  sudo systemctl daemon-reload
   13:44:34 INFO  sudo systemctl start pgautofailover
   [Unit]
   Description = pg_auto_failover
   
   [Service]
   WorkingDirectory = /var/lib/postgresql/monitor
   Environment = "PGDATA=/var/lib/postgresql/monitor"
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
