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
named route needs one, or right away for the very first route with
its own ``--ssl-self-signed`` flag -- the common case, and the one
that needs no separate call to this command at all. Run this by hand
instead only to create a certificate ahead of either of those, or to
replace one with ``--force``.

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

  archive$ pg_walserver create-cert --hostname mycluster.archive.example.com --force
  21:12:53 2588243 INFO   /usr/bin/openssl req -new -x509 -days 365 -nodes -text -out /var/lib/archiver/server.crt -keyout /var/lib/archiver/server.key -subj "/CN=mycluster.archive.example.com"
  21:12:53 2588243 INFO  Created a self-signed certificate for "/var/lib/archiver" ("/var/lib/archiver/server.crt"/"/var/lib/archiver/server.key", CN=mycluster.archive.example.com) -- replace it with a real one before running on a reachable network

This is self-signed, exactly like the certificate ``setup`` creates
automatically: it establishes TLS/SNI routing, but a real deployment
should replace it with one issued by a trusted CA before running on a
reachable network.

How the certificate is created
-------------------------------

The same facility ``pg_autoctl --ssl-self-signed`` itself uses creates
this certificate: a plain ``openssl req -new -x509`` call, one self-signed
certificate/key pair valid 365 days, with the certificate's subject CN
set to ``--hostname``, exactly as PostgreSQL's own `Creating
Certificates`__ page describes. Self-signed certificates protect
against eavesdropping; they do not protect against a Man-In-The-Middle
or impersonation attack -- see PostgreSQL's own `SSL Support`__ page
for what that distinction means in practice.

__ https://www.postgresql.org/docs/current/ssl-tcp.html#SSL-CERTIFICATE-CREATION
__ https://www.postgresql.org/docs/current/libpq-ssl.html

Using your own certificate
----------------------------

For a certificate issued by a trusted CA, ``create-cert`` is not
involved at all: just place ``server.crt`` and ``server.key`` directly
at ``<pgdata>/server.crt``/``<pgdata>/server.key`` (or point
``--ssl-cert-file``/``--ssl-key-file`` at wherever they already live --
see :ref:`pg_walserver_serve`), then start or reload ``serve``.
``pg_walserver`` reads whatever files are there; it has no notion of
"which kind" of certificate is installed.

Verifying a *client* certificate (``clientcert=verify-full`` HBA rule,
see :ref:`pg_walserver`'s "Access control") is a separate, optional
step on top of this: it needs the CA's own root certificate at
``--ssl-ca-file`` to validate client certificates against, which is
unrelated to whichever certificate ``serve`` itself presents to
connecting clients.

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_cluster`
* :ref:`pg_walserver_serve`
