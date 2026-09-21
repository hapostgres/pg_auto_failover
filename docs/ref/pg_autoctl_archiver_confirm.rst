.. _pg_autoctl_archiver_confirm:

pg_autoctl archiver confirm
===========================

pg_autoctl archiver confirm - archive_command: succeed once the archiver holds the given WAL file

Synopsis
--------

This command is meant to be used as Postgres ``archive_command``. It moves no
data: it only checks with the monitor that the archiver's ``pg_receivewal``
already holds the WAL segment. See :ref:`archiving_architecture`.

::

  usage: pg_autoctl archiver confirm  --pgdata <walfile>

    --pgdata   path to the node's PGDATA

Description
-----------

The exit code is 0 when the segment is confirmed, or when the group has no
archiver, and 1 otherwise so that Postgres retries. Names that are not 24
hexadecimal digits (timeline history, ``.backup``, ``.partial``) exit 0
without a check. When the monitor is unreachable, the answer cached in
``$PGDATA/pg_autoctl.archive-confirm`` is used: accepted only when the group
had no archiver.

New nodes are set up with::

  archive_command = '/path/to/pg_autoctl archiver confirm --pgdata PGDATA %f'

unless ``pg_autoctl create postgres --archive-confirm off`` is used.
