.. _disaster_recovery:

Disaster Recovery
=================

When implementing High-Availability with Postgres, both the availabitility
of the data and of the service must be taken into account. The availability
of the service can be handled by automated failover. The availability of the
data is what the disaster recovery plan is all about.

To implement disaster recovery, Postgres requires archiving of both the
PGDATA directory (base backups) and the WAL (write-ahead log, splitted in
segments of 16MB by default).

The disaster recovery setup usually intersects with the failover mechanisms
in the following ways:

  - when creating a secondary Postgres instance, if an archive is available,
    it is possible to fetch an archive of the PGDATA from there rather than
    using a ``pg_basebackup`` connection to the current primary node,

  - the secondary nodes can be setup to use the Postgres ``restore_command``
    mechanism, improving the robustness of the system by providing an
    alternative source for the WAL files when connection to the primary has
    been lost,

  - archiving WAL files can be done from the ``archive_command`` mechanism
    and integrated in a way that allows for ``archive_mode = 'always'``,
    which makes it possible for any node in a pg_auto_failover group to
    archive the WAL files,

  - at times of a failover, if extra actions are necessary, then
    pg_auto_failover can automate them.

	TODO: less hand waving; will come later when we have all the pieces
	integrated and played with failovers for a while.

At the moment, pg_auto_failover knows to integrate the WAL-G archiving
method. See :ref:`pg_autoctl_create_archiver-policy` for details.

Archiving
---------

When using pg_auto_failover, it is possible to create one or more archiver
policies for each formation.

An archiver policy allows to implement :ref:`disaster_recovery` and is used
in the following 5 ways:

  1. Archiving the Postgres WAL files with the archive_command support.
  2. Restoring the Postgres WAL files with the restore_command support.
  3. Archiving copies of the PGDATA directory, aka base backups.
  4. Restoring copies of the PGDATA directory from the archive.
  5. Maintaining a disaster recovery oriented archive retention policy.

This integration is entirely automated with pg_auto_failover.
