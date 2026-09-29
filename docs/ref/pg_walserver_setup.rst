.. _pg_walserver_setup:

pg_walserver setup
===================

pg_walserver setup - Configure pg_walserver itself (port, TLS, auth-timeout)

Synopsis
--------

::

  pg_walserver setup --pgdata <path> [--config-file <path>] [--port <port>]
      [--ssl-cert-file <path>] [--ssl-key-file <path>] [--ssl-ca-file <path>]
      [--auth-timeout <seconds>]

Configures *this pg_walserver instance itself*: writes whichever of
``--port``/``--ssl-cert-file``/``--ssl-key-file``/``--ssl-ca-file``/
``--auth-timeout`` was given as plain ``key = value`` lines at the very
top of the config file, ahead of any ``[cluster]`` section. A later
``pg_walserver serve --pgdata <path>``, with none of these flags
repeated, already picks them up as its own defaults -- an explicit flag
given directly to ``serve`` still always wins over whatever ``setup``
persisted here.

``setup`` is deliberately not about any one archived cluster: that is
:ref:`pg_walserver_register`/:ref:`pg_walserver_drop`/
:ref:`pg_walserver_set_upstream`'s job now, split out of what used to
be this same command. ``setup`` configures the server; those configure
what it serves.

Options
-------

--pgdata

  This instance's own data root, created if missing. Defaults to
  ``PGDATA``; the config file itself lives at
  ``<pgdata>/pg_walserver.ini`` unless ``--config-file`` overrides it.

--config-file

  Where the config file itself lives, independent of ``--pgdata`` (or
  the ``PG_WALSERVER_CONFIG_FILE`` environment variable) -- a
  Debian-style deployment's own split, e.g.
  ``/etc/pg_walserver/pg_walserver.ini`` for config, ``--pgdata`` at
  ``/var/lib/pg_walserver`` for data. Every other ``pg_walserver``
  command takes this same flag.

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
  23:56:59 13 INFO  Set "port = 6543" in "/var/lib/archiver/pg_walserver.ini"
  23:56:59 13 INFO  setup complete: "/var/lib/archiver/pg_walserver.ini" is ready

Running it again with no flag at all is a no-op, and says so::

  archive$ pg_walserver setup --pgdata /var/lib/archiver
  23:57:01 19 INFO  setup: "/var/lib/archiver/pg_walserver.ini" already exists; no --port/--ssl-*/--auth-timeout flag was given, nothing to persist -- "pg_walserver serve"'s own built-in defaults apply unless overridden on its own command line

Debian-style deployment: config under ``/etc``, independent of the data
root under ``/var``::

  archive$ pg_walserver setup --pgdata /var/lib/pg_walserver \
      --config-file /etc/pg_walserver/pg_walserver.ini --port 6543
  00:08:11 18 INFO  Set "port = 6543" in "/etc/pg_walserver/pg_walserver.ini"
  00:08:11 18 INFO  setup complete: "/etc/pg_walserver/pg_walserver.ini" is ready

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_register`
* :ref:`pg_walserver_drop`
* :ref:`pg_walserver_set_upstream`
* :ref:`pg_walserver_serve`
* :ref:`pg_walserver_create_cert`
