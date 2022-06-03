.. _pg_autoctl_enable_ssl:

pg_autoctl enable ssl
=====================

pg_autoctl enable ssl - Enable SSL configuration on this node

Synopsis
--------

It is possible to manage Postgres SSL settings with the ``pg_autoctl``
command, both at :ref:`pg_autoctl_create_postgres` time and then again to
change your mind and update the SSL settings at run-time.

::

   usage: pg_autoctl enable ssl  [ --pgdata ] [ --json ]

  --pgdata      path to data directory
  --ssl-self-signed setup network encryption using self signed certificates (does NOT protect against MITM)
  --ssl-mode        use that sslmode in connection strings
  --ssl-ca-file     set the Postgres ssl_ca_file to that file path
  --ssl-crl-file    set the Postgres ssl_crl_file to that file path
  --no-ssl          don't enable network encryption (NOT recommended, prefer --ssl-self-signed)
  --server-key      set the Postgres ssl_key_file to that file path
  --server-cert     set the Postgres ssl_cert_file to that file path


Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.

--ssl-self-signed

  Generate SSL self-signed certificates to provide network encryption. This
  does not protect against man-in-the-middle kinds of attacks. See
  :ref:`security` for more about our SSL settings.

--ssl-mode

  SSL Mode used by ``pg_autoctl`` when connecting to other nodes,
  including when connecting for streaming replication.

--ssl-ca-file

  Set the Postgres ``ssl_ca_file`` to that file path.

--ssl-crl-file

  Set the Postgres ``ssl_crl_file`` to that file path.

--no-ssl

  Don't enable network encryption. This is not recommended, prefer
  ``--ssl-self-signed``.

--server-key

  Set the Postgres ``ssl_key_file`` to that file path.

--server-cert

  Set the Postgres ``ssl_cert_file`` to that file path.

Environment
-----------

PGDATA

  Postgres directory location. Can be used instead of the ``--pgdata``
  option.

PG_AUTOCTL_MONITOR

  Postgres URI to connect to the monitor node, can be used instead of the
  ``--monitor`` option.

XDG_CONFIG_HOME

  The pg_autoctl command stores its configuration files in the standard
  place XDG_CONFIG_HOME. See the `XDG Base Directory Specification`__.

  __ https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
  
XDG_DATA_HOME

  The pg_autoctl command stores its internal states files in the standard
  place XDG_DATA_HOME, which defaults to ``~/.local/share``. See the `XDG
  Base Directory Specification`__.

  __ https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html

  
