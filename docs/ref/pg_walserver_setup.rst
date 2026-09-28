.. _pg_walserver_setup:

pg_walserver setup
===================

pg_walserver setup - Configure pg_walserver itself (port, TLS, auth-timeout)

Synopsis
--------

::

  pg_walserver setup --pgdata <path> [--port <port>]
      [--ssl-cert-file <path>] [--ssl-key-file <path>] [--ssl-ca-file <path>]
      [--auth-timeout <seconds>]

Configures *this pg_walserver instance itself*: writes whichever of
``--port``/``--ssl-cert-file``/``--ssl-key-file``/``--ssl-ca-file``/
``--auth-timeout`` was given as plain ``key = value`` lines at the very
top of ``pg_walserver.ini``, ahead of any ``[cluster]`` section. A
later ``pg_walserver serve --pgdata <path>``, with none of these flags
repeated, already picks them up as its own defaults -- an explicit flag
given directly to ``serve`` still always wins over whatever ``setup``
persisted here.

``setup`` is deliberately not about any one archived cluster: that is
:ref:`pg_walserver_cluster`'s job now (``register``/``drop``/``list``/
``set-upstream``), split out of what used to be this same command.
``setup`` configures the server; ``cluster`` configures what it serves.

Options
-------

--pgdata

  Where ``<pgdata>/pg_walserver.ini`` lives, created if missing.
  Defaults to ``PGDATA``.

--port

  ``serve``'s own default port when its own ``--port`` isn't given.
  Defaults to ``6543``.

--ssl-cert-file, --ssl-key-file, --ssl-ca-file

  ``serve``'s own defaults when its own equivalent flag isn't given.

--auth-timeout

  ``serve``'s own default auth-timeout, in seconds, when its own
  ``--auth-timeout`` isn't given.

Every flag here is optional and independent: only whichever ones are
given get written, and each becomes ``serve``'s own default from then
on.

Examples
--------

Configure a fresh ``--pgdata`` with a non-default port, before anything
else has been set up::

  archive$ pg_walserver setup --pgdata /var/lib/archiver --port 6543
  21:38:22 2660205 INFO  Set "port = 6543" in "/var/lib/archiver/pg_walserver.ini"
  21:38:22 2660205 INFO  setup complete: "/var/lib/archiver" is ready

Running it again with no flag at all is a no-op, and says so::

  archive$ pg_walserver setup --pgdata /var/lib/archiver
  21:38:22 2660205 INFO  setup: "/var/lib/archiver" already exists; no --port/--ssl-*/--auth-timeout flag was given, nothing to persist -- "pg_walserver serve"'s own built-in defaults apply unless overridden on its own command line

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_cluster`
* :ref:`pg_walserver_serve`
* :ref:`pg_walserver_create_cert`
