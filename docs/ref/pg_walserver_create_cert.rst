.. _pg_walserver_create_cert:

pg_walserver create-cert
=========================

pg_walserver create-cert - Create a self-signed TLS certificate for --pgdata

Synopsis
--------

::

  pg_walserver create-cert --pgdata <path> --hostname <name> [--force]

Creates a self-signed TLS certificate for ``--pgdata``, written as
``<pgdata>/server.crt`` and ``<pgdata>/server.key``. ``pg_walserver
setup`` already creates one automatically the first time a second
named route needs one; run this by hand to create the first
certificate ahead of time, or to replace one with ``--force``.

Options
-------

--pgdata

  This instance's own top-level storage root. Defaults to ``PGDATA``.
  The certificate is written as ``<pgdata>/server.crt`` and
  ``<pgdata>/server.key``.

--hostname

  The certificate's own CN/subject.

--force

  Overwrite an already-existing ``server.crt``/``server.key``.

Examples
--------

::

  archive$ pg_walserver create-cert --pgdata /var/lib/archiver \
      --hostname mycluster.archive.example.com
  INFO   /usr/bin/openssl req -new -x509 -days 365 -nodes -text \
         -out /var/lib/archiver/server.crt -keyout /var/lib/archiver/server.key \
         -subj "/CN=mycluster.archive.example.com"
  INFO  Created a self-signed certificate for "/var/lib/archiver"
        ("/var/lib/archiver/server.crt"/"/var/lib/archiver/server.key",
        CN=mycluster.archive.example.com) -- replace it with a real one
        before running on a reachable network

This is self-signed, exactly like the certificate ``setup`` creates
automatically: it establishes TLS/SNI routing, but a real deployment
should replace it with one issued by a trusted CA before running on a
reachable network.

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_setup`
