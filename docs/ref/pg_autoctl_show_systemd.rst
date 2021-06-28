.. _pg_autoctl_show_systemd:

pg_autoctl show systemd
=======================

pg_autoctl show systemd - Print systemd service file for this node

Synopsis
--------

This command outputs a configuration unit that is suitable for registering
``pg_autoctl`` as a systemd service.

Examples
--------

::

   $ pg_autoctl show systemd --pgdata node1
   17:38:29 99778 INFO  HINT: to complete a systemd integration, run the following commands:
   17:38:29 99778 INFO  pg_autoctl -q show systemd --pgdata "node1" | sudo tee /etc/systemd/system/pgautofailover.service
   17:38:29 99778 INFO  sudo systemctl daemon-reload
   17:38:29 99778 INFO  sudo systemctl enable pgautofailover
   17:38:29 99778 INFO  sudo systemctl start pgautofailover
   [Unit]
   Description = pg_auto_failover

   [Service]
   WorkingDirectory = /Users/dim
   Environment = 'PGDATA=node1'
   User = dim
   ExecStart = /Applications/Postgres.app/Contents/Versions/12/bin/pg_autoctl run
   Restart = always
   StartLimitBurst = 0
   ExecReload = /Applications/Postgres.app/Contents/Versions/12/bin/pg_autoctl reload

   [Install]
   WantedBy = multi-user.target

To avoid the logs output, use the ``-q`` option:

::

   $ pg_autoctl show systemd --pgdata node1 -q
   [Unit]
   Description = pg_auto_failover

   [Service]
   WorkingDirectory = /Users/dim
   Environment = 'PGDATA=node1'
   User = dim
   ExecStart = /Applications/Postgres.app/Contents/Versions/12/bin/pg_autoctl run
   Restart = always
   StartLimitBurst = 0
   ExecReload = /Applications/Postgres.app/Contents/Versions/12/bin/pg_autoctl reload

   [Install]
   WantedBy = multi-user.target
