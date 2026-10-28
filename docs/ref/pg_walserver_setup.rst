.. _pg_walserver_setup:

pg_walserver setup
===================

pg_walserver setup - Configure pg_walserver itself (port, TLS, auth-timeout, HBA)

Synopsis
--------

::

  pg_walserver setup --pgdata <path> [--config <path>] [--port <port>]
      [--ssl-cert-file <path>] [--ssl-key-file <path>] [--ssl-ca-file <path>]
      [--auth-timeout <seconds>] [--no-hba] [--no-cert]

Configures *this pg_walserver instance itself*: writes whichever of
``--port``/``--ssl-cert-file``/``--ssl-key-file``/``--ssl-ca-file``/
``--auth-timeout`` was given as plain ``key = value`` lines at the very
top of the config file, ahead of any ``[cluster]`` section. A later
``pg_walserver serve --pgdata <path>``, with none of these flags
repeated, already picks them up as its own defaults -- an explicit flag
given directly to ``serve`` still always wins over whatever ``setup``
persisted here.

Unconditionally, unless skipped with ``--no-cert``/``--no-hba``, and
never overwriting a file that already exists:

* a self-signed TLS certificate is created (the same facility
  ``pg_walserver cluster register`` itself uses);
* ``<pgdata>/pg_walserver_hba.conf`` is created with one real, active
  rule open to this machine's own local network, auto-discovered the
  same way ``pg_autoctl`` discovers its own LAN CIDR to admit a new
  node -- adapted here to ``setup`` having no monitor/upstream address
  to discover it *from* the way ``pg_autoctl`` does: it walks the
  local network interfaces directly instead, using the first
  non-loopback one found. The rule admits any role with the right
  ``scram-sha-256`` password, never one specific role name -- ``setup``
  cannot know which role an operator will later choose in
  ``cluster register --pguri ...``. Review and narrow both defaults
  (the CIDR, and, once you know it, the role name) before running on a
  reachable network. When no usable local interface is found, the file
  is still created, with the same commented-out placeholder
  ``pg_walserver serve``'s own bootstrap path already falls back to --
  never a hard error.

``setup`` is deliberately not about any one archived cluster: that is
:ref:`pg_walserver_cluster`'s job now (``register``/``drop``/``list``/
``set-upstream``), split out of what used to be this same command.
``setup`` configures the server; ``cluster`` configures what it
serves. Also deliberately not here: the password file
(``pg_walserver_passwd``, written by ``scram-secret``) -- that needs an
operator-chosen secret ``setup`` cannot fabricate safely.

Options
-------

--pgdata

  This instance's own data root, created if missing. Defaults to
  ``PGDATA``; the config file itself lives at
  ``<pgdata>/pg_walserver.ini`` unless ``--config`` overrides it.

--config

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

--no-hba

  Skip auto-creating ``<pgdata>/pg_walserver_hba.conf``.

--no-cert

  Skip auto-creating the self-signed TLS certificate.

Every ``--port``/``--ssl-*``/``--auth-timeout`` flag here is optional
and independent: only whichever ones are given get written, and each
becomes ``serve``'s own default from then on.

Examples
--------

Configure a fresh ``--pgdata`` with a non-default port, before anything
else has been set up -- the certificate and HBA file are created too::

  archive$ pg_walserver setup --pgdata /var/lib/archiver --port 6543
  11:37:49 182868 INFO  Set "port = 6543" in "/var/lib/archiver/pg_walserver.ini"
  11:37:49 182868 INFO  setup complete: "/var/lib/archiver/pg_walserver.ini" is ready
  11:37:49 182868 INFO   /usr/bin/openssl req -new -x509 -days 365 -nodes -text -out /var/lib/archiver/server.crt -keyout /var/lib/archiver/server.key -subj "/CN=dev-runner"
  11:37:50 182868 INFO  Created a self-signed certificate for "/var/lib/archiver" ("/var/lib/archiver/server.crt"/"/var/lib/archiver/server.key", CN=dev-runner) -- replace it with a real one before running on a reachable network
  11:37:50 182868 INFO  Creating the default pg_walserver HBA file "/var/lib/archiver/pg_walserver_hba.conf"
  11:37:50 182868 INFO  HBA: admitting "10.1.0.0/24" (this machine's own local network, auto-discovered) in "/var/lib/archiver/pg_walserver_hba.conf" -- review and adjust it before running on a reachable network

Running it again with no flag at all only re-checks the config file
(already there, nothing new to persist); the certificate and HBA file
are also already there, so neither step repeats either::

  archive$ pg_walserver setup --pgdata /var/lib/archiver
  11:37:50 182888 INFO  setup: "/var/lib/archiver/pg_walserver.ini" already exists; no --port/--ssl-*/--auth-timeout flag was given, nothing to persist -- "pg_walserver serve"'s own built-in defaults apply unless overridden on its own command line

Debian-style deployment: config under ``/etc``, independent of the data
root under ``/var``, with the certificate/HBA auto-provisioning skipped
because this particular host already provisions both some other way::

  archive$ pg_walserver setup --pgdata /var/lib/pg_walserver \
      --config /etc/pg_walserver/pg_walserver.ini --port 6543 \
      --no-hba --no-cert
  11:37:50 182890 INFO  Set "port = 6543" in "/etc/pg_walserver/pg_walserver.ini"
  11:37:50 182890 INFO  setup complete: "/etc/pg_walserver/pg_walserver.ini" is ready

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_cluster`
* :ref:`pg_walserver_serve`
* :ref:`pg_walserver_create_cert`
