.. _pg_walserver_basebackup:

pg_walserver basebackup
========================

pg_walserver basebackup - Take a base backup of a route's upstream

Synopsis
--------

::

  pg_walserver basebackup --pgdata <path> --cluster <name>
      [--path <dir>] [--upstream <conninfo> | --host <host> [--port <port>] [--user <name>]]

Takes a real base backup from a route's upstream into
``<path>/basebackups/<label>/``, then updates
``<path>/basebackups/.latest`` once the backup is verified complete.
``pg_walserver serve`` already takes a route's first base backup
automatically; run this by hand, or from a cron job, whenever a fresh
one is wanted afterward.

Options
-------

--pgdata

  Where ``<pgdata>/pg_walserver.ini`` lives. Defaults to ``PGDATA``.

--cluster

  The cluster name to back up, looked up in ``pg_walserver.ini``.

--path

  The route's own directory. Overrides the route's own ``path``
  property.

--upstream

  A libpq connection string to connect with. Overrides the route's own
  ``upstream`` property.

--host, --port, --user

  Override individual connection parameters. Default port ``5432``,
  default user ``pgautofailover_replicator``.

Examples
--------

::

  archive$ pg_walserver basebackup --pgdata /var/lib/archiver --cluster mycluster \
      --upstream "host=primary user=archiver_repl sslmode=require"
  INFO  Taking a base backup of primary:5432 into
        "/var/lib/archiver/mycluster/basebackups/basebackup-20260928T134237Z"
  INFO   /usr/bin/pg_basebackup -w -d 'application_name=pg_walserver-basebackup
         host=primary port=5432 user=archiver_repl sslmode=require' --pgdata
         /var/lib/archiver/mycluster/basebackups/basebackup-20260928T134237Z
         -U archiver_repl --verbose --progress --wal-method=stream
         --checkpoint=fast --label basebackup-20260928T134237Z
  INFO  pg_basebackup: initiating base backup, waiting for checkpoint to complete
  INFO  pg_basebackup: checkpoint completed
  INFO  pg_basebackup: write-ahead log start point: 0/4000028 on timeline 1
  INFO  pg_basebackup: starting background WAL receiver
  INFO  pg_basebackup: created temporary replication slot "pg_basebackup_745442"
  INFO  23712/23712 kB (100%), 1/1 tablespace
  INFO  pg_basebackup: write-ahead log end point: 0/4000120
  INFO  pg_basebackup: waiting for background process to finish streaming ...
  INFO  pg_basebackup: syncing data to disk ...
  INFO  pg_basebackup: base backup completed
  INFO  Base backup "basebackup-20260928T134237Z" is now the latest for
        "/var/lib/archiver/mycluster"

Carrying its own WAL, concurrently, rather than depending on
``restore_command`` to fetch any segment written during the backup
itself, is why ``pg_basebackup`` is always invoked with
``--wal-method=stream``.

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_archive_cleanup`
