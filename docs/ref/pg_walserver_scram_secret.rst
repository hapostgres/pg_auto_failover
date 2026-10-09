.. _pg_walserver_scram_secret:

pg_walserver scram-secret
==========================

pg_walserver scram-secret - Print one pg_walserver_passwd line for a user

Synopsis
--------

::

  PGPASSWORD=<password> pg_walserver scram-secret --user <name>

Prints one ``pg_walserver_passwd`` line (``<user>:<SCRAM-SHA-256
secret>``) to standard output. The password is read from the
``PGPASSWORD`` environment variable, never from the command line, so it
never appears in the process list or shell history.

Options
-------

--user

  Role name the printed line authenticates. Defaults to
  ``pgautofailover_replicator``.

Examples
--------

::

  archive$ PGPASSWORD='s3kr3t' pg_walserver scram-secret --user archiver_repl
  archiver_repl:SCRAM-SHA-256$4096:jA1sipJv8X8XINl50x/Vqg==$+J+rarRxXDGK6MiP8kvudmVePrrDjPy0G2GUhXvByXM=:tD+kWPTc+VktrHxJ5Nyqkrs0azQdg0pRpHnL3dmIZY8=

Append it directly to the credential store ``scram-sha-256`` HBA rules
check against::

  archive$ PGPASSWORD='s3kr3t' pg_walserver scram-secret --user archiver_repl \
      >> /var/lib/archiver/pg_walserver_passwd

See Also
--------

* :ref:`pg_walserver`
