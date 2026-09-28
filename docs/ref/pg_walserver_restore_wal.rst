.. _pg_walserver_restore_wal:

pg_walserver restore-wal
=========================

pg_walserver restore-wal - Fetch one WAL/.backup file from a pg_walserver route (restore_command)

Synopsis
--------

::

  pg_walserver restore-wal <filename> <destination-path> --cluster <name>
      --host <host> [--port <port>] [--user <name>] [--sslmode <mode>]

Fetches one WAL segment or ``.backup`` history file from a route, for
use as PostgreSQL's own ``restore_command``::

  restore_command = 'pg_walserver restore-wal %f %p --cluster mycluster \
                        --host archive.example.com --user archiver_repl'

Options
-------

Accepts the same ``--cluster``, ``--host``, ``--port``, ``--user``,
``--sslmode`` options as :ref:`pg_walserver_archive_wal`.

Examples
--------

::

  restore$ PGPASSWORD=s3kr3t pg_walserver restore-wal \
      000000010000000000000002 /var/lib/postgres/pitr/pg_wal/RECOVERYXLOG \
      --cluster mycluster --host archive --port 6543 --user archiver_repl --sslmode require
  INFO  Fetched "000000010000000000000002" (16777216 bytes) to
        "/var/lib/postgres/pitr/pg_wal/RECOVERYXLOG"

A raw ``FETCH_FILE`` via ``psql``, or any other replication-protocol
client, also works for a one-off fetch::

  restore$ PGPASSWORD=s3kr3t psql "host=archive port=6543 dbname=mycluster \
      user=archiver_repl replication=true sslmode=require" \
      -c "FETCH_FILE '000000010000000000000002'" > /tmp/000000010000000000000002

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_archive_wal`
