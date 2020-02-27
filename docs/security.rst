.. _security:

Security settings for pg_auto_failover
======================================

In order to be able to orchestrate fully automated failovers,
pg_auto_failover needs to be able to establish the following Postgres
connections:

  - from the monitor node to each Postgres node to check the node's “health”
  - from each Postgres node to the monitor to implement our `node_active`
    protocol and fetch the current assigned state for this node
  - from the secondary node to the primary node for Postgres streaming
    replication.

Postgres Client authentication is controlled by a configuration file:
``pg_hba.conf``. This file contains a list of rules where each rule may
allow or reject a connection attempt.

For pg_auto_failover to work as intended, some HBA rules need to be added to
each node configuration. You can choose to provision the ``pg_hba.conf``
file yourself thanks to ``pg_autoctl`` options' ``--skip-pg-hba``, or you
can use the following options to control which kind of rules are going to be
added for you.

Postgres HBA rules
------------------

For your application to be able to connect to the current Postgres primary
servers, some application specific HBA rules have to be added to
``pg_hba.conf``. There is no provision for doing that in pg_auto_failover.

In other words, it is expected that you have to edit ``pg_hba.conf`` to open
connections for your application needs.

The trust security model
------------------------

As its name suggests the trust security model is not enabling any kind of
security validation. This setting is popular for testing deployments though,
as it makes it very easy to verify that everything works as intended before
putting security restrictions in place.

To enable a “trust” security model with pg_auto_failover, use the
``pg_autoctl`` option ``--auth trust`` when creating nodes::

  $ pg_autoctl create monitor --auth trust ...
  $ pg_autoctl create postgres --auth trust ...
  $ pg_autoctl create postgres --auth trust ...

When using ``--auth trust`` pg_autoctl adds new HBA rules in the monitor and
the Postgres nodes to enable connections as seen above.
  
Authentication with passwords
-----------------------------

  - use ``--auth scram-sha-256`` or ``--auth md5``
  - then ``alter user autoctl_node set password ...``
  - then set PGPASSWORD or edit ~/.pgpass
  - ``pg_autoctl config set replication.password ...``

See about this bootstrap problem for ``pg_autoctl create`` and the
replication password needed for ``pg_basebackup``; which you can't set early
enough at the moment.

Encryption of network communications
------------------------------------

Postgres knows how to use SSL to enable network encryption of all
communications, including authentication with passwords and the whole data
set when streaming replication is used.

To enable SSL on the server an SSL certificate is needed. It could be as
simple as a self-signed certificate, and ``pg_autoctl`` will create such a
certificate for you when setup with ssl support and without other
instruction::

  $ pg_autoctl create monitor --ssl --auth trust|scram-sha-256 ...
  $ pg_autoctl create postgres --ssl --auth trust|scram-sha-256 ...
  $ pg_autoctl create postgres --ssl --auth trust|scram-sha-256 ...

In that example we setup SSL connections to encrypt the network traffic, and
we still have to setup an authentication mechanism exactly as in the
previous sections of this document.

When using the ``--ssl`` option without providing any certificate, the
following command is used by ``pg_autoctl`` to create a self-signed
certificate, as per the Postgres documentation at the `Creating
Certificates`__ page::

  $ openssl req -new -x509 -days 365 -nodes -text -out server.crt \
    -keyout server.key -subj "/CN=${nodename}"

__ https://www.postgresql.org/docs/current/ssl-tcp.html#SSL-CERTIFICATE-CREATION

Note that you do NOT have to type that command yourself.

The certificate subject CN defaults to the ``--nodename`` parameter, which
can be given explicitely or computed by ``pg_autoctl`` as either your
hostname when you have proper DNS resolution, or your current IP address. It
is possible to use the ``--ssl-subj`` option to set the certificate subject.

Using SSL certificates for authentication
-----------------------------------------

Using a simple self-signed certificate as before does not allow for SSL
based authentication. To enable SSL based authentication, we need to have a
common root certificate (either self-signed or signed by a Certificate
Authority), as per the following excerpt of the Postgres documentation:

    To allow the client to verify the identity of the server, place a root
    certificate on the client and a leaf certificate signed by the root
    certificate on the server. To allow the server to verify the identity of
    the client, place a root certificate on the server and a leaf
    certificate signed by the root certificate on the client. One or more
    intermediate certificates (usually stored with the leaf certificate) can
    also be used to link the leaf certificate to the root certificate.

To create a self-signed root certificate with pg_auto_failover, use the
following command::

  $ pg_autoctl create cert --root --ssl-subj ".." root.crt

This command creates a self-signed certificate that can be used to sign both
client and server certificates. The default value for the certificate
subject is again ``"/CN=${nodename}"`` with nodename the same as the
``--nodename`` option.

On servers where you want to enable certificate based authentication you can
then use that root certificate to sign the server certificate and install it
as the Postgres ``ssl_ca_file``::

  $ pg_autoctl create monitor --ssl --root root.crt ...
  $ pg_autoctl create postgres --ssl --root root.crt ...
  $ pg_autoctl create postgres --ssl --root root.crt ...

The root certificate has to be installed on every node. It it used in the
following cases:

  - a client certificate is created for the user ``autoctl_node`` and used
    by pg_autoctl when connecting to the monitor, this client certificate is
    signed by the root certificate,

  - a server certificate is created for the local Postgres node, signed by
    the root certificate, to enable a chain of trust in between servers when
    setting up streaming replication,

  - a client certificate is created for the user
    ``pgautofailover_replicator`` and used by the standby nodes in their
    ``primary_conninfo`` connection string parameters ``sslcert`` and
    ``sslkey``,

  - the root certificate is used as the ``ssl_ca_file`` in every node
    Postgres configuration.

Given such a setting, the authentication used in the added HBA rules is
``cert``: pg_auto_failover connections are authenticated thanks to the SSL
certificates, without passwords.

You can omit ``--auth cert``, and you may still use the ``--skip-pg-hba``
option if you have other means to provision the HBA files on your systems.

Using your own SSL certificates for authentication
--------------------------------------------------

In many cases you will want to install certificates provided by your local
security department and signed by a trusted Certificate Authority. In that
case one solution is to use ``--skip-pg-hba`` and do the whole setup
yourself.

It is still possible to give the certificates to pg_auto_failover and have
it handle the setup for you, including the creation of and signing of client
certificates for the ``autoctl_node`` and ``pgautofailover_replication``
users::
  
  $ pg_autoctl create monitor --ssl --root root.crt --server-crt server.crt --server-key server.key ...
  $ pg_autoctl create postgres --ssl --root root.crt --server-crt server.crt --server-key server.key ...
  $ pg_autoctl create postgres  --ssl --root root.crt --server-crt server.crt --server-key server.key ...

When using your own certificates, pg_auto_failover still creates client
certificates for its users, signed with the given root certificate. On every
Postgres node:

  - a client certificate is created for the user ``autoctl_node`` and used
    by pg_autoctl when connecting to the monitor, this client certificate is
    signed by the root certificate,

  - a client certificate is created for the user
    ``pgautofailover_replicator`` and used by the standby nodes in their
    ``primary_conninfo`` connection string parameters ``sslcert`` and
    ``sslkey``,

  - the root certificate is used as the ``ssl_ca_file`` in every node
    Postgres configuration.

Given such a setting, the authentication used in the added HBA rules is
``cert``: pg_auto_failover connections are authenticated thanks to the SSL
certificates, without passwords.

You can omit ``--auth cert``, and you may still use the ``--skip-pg-hba``
option if you have other means to provision the HBA files on your systems.
    
Postgres HBA provisioning
-------------------------

While pg_auto_failover knows how to manage the Postgres HBA rules that are
necessary for your stream replication needs and for its monitor protocol, it
will not manage the Postgres HBA rules that are needed for your
applications.

If you have your own HBA provisioning solution, you can include the rules
needed for pg_auto_failover and then use the ``--skip-pg-hba`` option to the
``pg_autoctl create`` commands.

