.. _pg_autoctl_create_archiver-policy:

pg_autoctl create archiver-policy
=================================

pg_autoctl create archiver-policy - Create an archiving policy for a given
formation

Synopsis
--------

This command registers a new archiver-policy on the monitor, with the
specified kind::

  usage: pg_autoctl create archiver-policy --formation --method --target --config filename [ ... ]

  --monitor          pg_auto_failover Monitor Postgres URL
  --formation        pg_auto_failover formation
  --target           archiving target name (default)
  --method           archiving method to use for this policy (wal-g)
  --config           archiving method configuration file, in JSON
  --backup-interval  how often to archive PGDATA
  --backup-max-count how many archives of PGDATA to keep
  --backup-max-age   how long to keep a PGDATA archive

Description
-----------

The pg_auto_failover solution can handle WAL archiving and disaster
recovery. For this, the commands :ref:`pg_autoctl_archive_wal` and
:ref:`pg_autoctl_restore_wal` are provided, and are automatically installed
as the archive_command_ and the restore_command_ in the Postgres
configuration.

.. _archive_command: https://www.postgresql.org/docs/current/runtime-config-wal.html#GUC-ARCHIVE-COMMAND

.. _restore_command: https://www.postgresql.org/docs/current/runtime-config-wal.html#GUC-RESTORE-COMMAND

When using pg_auto_failover, it is possible to create one or more archiver
policies for each formation.

An archiver policy allows to implement :ref:`disaster_recovery` and is used
in the following 5 ways:

  1. Archiving the Postgres WAL files with the archive_command support.
  2. Restoring the Postgres WAL files with the restore_command support.
  3. Archiving copies of the PGDATA directory, aka base backups.
  4. Restoring copies of the PGDATA directory from the archive.
  5. Maintaining a disaster recovery oriented archive retention policy.

Options
-------

The following options are available to ``pg_autoctl create archiver-policy``:

--monitor

  Postgres URI used to connect to the monitor. Must use the ``autoctl_node``
  username and target the ``pg_auto_failover`` database name. It is possible
  to show the Postgres URI from the monitor node using the command
  :ref:`pg_autoctl_show_uri`.

--formation

  Name of the formation for which to create an archiver-policy.

--target

  Name of the archiver policy target. Can be any name that is useful to
  differenciate your archiving policy configuration. Examples would include
  *minio* or *east-us-1* or *francecentral*.

--method

  Name of the archive method to use to implement this policy. At the moment,
  pg_auto_failover only has support for the *wal-g* method, which is also
  the default value for this argument, so you may omit specifying it.

--config

  Configuration file to use for the archiver policy. The configuration file
  is expected to be in the JSON format, allowing rich querying on the
  monitor directly.

--backup-interval

  How often should pg_auto_failover take base backups of PGDATA. Defaults to
  every 6 hours.

--backup-max-count

  How many base backups of PGDATA should be kept, at most.

  Retention policy allows dropping an archived backup when either the max
  count or the max age is reached, whichever comes first.

--backup-max-age

  How old a base backup is allowed to be before it's dropped.

  Retention policy allows dropping an archived backup when either the max
  count or the max age is reached, whichever comes first.

Example
-------

To see an example of an archiver-policy using our only supported method
WAL-G, we need to first have a compatible storage for it. For this, we can
use the `minio`__ solution and have a local deployment of a Cloud Blob
Storage API.

__ https://min.io

::

   $ docker run -e MINIO_REGION_NAME="home" -p 9000:9000 -p 9001:9001 minio/minio server /data --console-address ":9001"

Now that the minio server is running, we need to create a bucket. Here we
can use the `mc` command line tool.

::

   $ mc alias set local http://localhost:9000 minioadmin minioadmin
   $ mc mb local/pgaf

Now we have a running instance of minio and a bucket named pgaf for
pg_auto_failover archiving needs, we can write and test a WAL-G
configuration file that's using it.

::

   $ cat wal-g.json
   {
       "WALG_S3_PREFIX": "s3://pgaf/default/0/",
       "AWS_ENDPOINT": "http://localhost:9000",
       "AWS_S3_FORCE_PATH_STYLE": true,
       "AWS_ACCESS_KEY_ID": "minioadmin",
       "AWS_REGION": "home",
       "AWS_SECRET_ACCESS_KEY": "minioadmin"
   }

This setup can now be registered as an archive policy.

::

   $ pg_autoctl create archiver-policy --target minio --config ~/dev/temp/wal-g.json


At this point, the already set-up ``archive_command`` in Postgres
automatically starts archiving WAL files. See :ref:`pg_autoctl_archive_wal`
for details.
