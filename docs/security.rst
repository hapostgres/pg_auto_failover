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

To setup pg_auto_failover with password for connections, you can use one of
the password based authentication methods supported by Postgres, such as
``password`` or ``scram-sha-256``. We recommend the latter, as in the
following example::

  $ pg_autoctl create monitor --auth scram-sha-256 ...

The ``pg_autoctl`` does not set the password for you. The first step is to
set the database user password in the monitor database thanks to the
following command::

  $ psql postgres://monitor.host/pg_auto_failover
  > alter user autoctl_node password 'h4ckm3';

Now that the monitor is ready with our password set for the ``autoctl_node``
user, we can use the password in the monitor connection string used when
creating Postgres nodes.

On the primary node, we can create the Postgres setup as usual, and then set
our replication password, that we will use if we are demoted and then
re-join as a standby::

  $ pg_autoctl create postgres       \
         --auth scram-sha-256        \
         ...                         \
         --monitor postgres://autoctl_node:h4ckm3@monitor.host/pg_auto_failover

  $ pg_autoctl config set replication.password h4ckm3m0r3

The second Postgres node is going to be initialized as a secondary and
``pg_autoctl`` then calls ``pg_basebackup`` at create time. We need to have
the replication password already set at this time, and we can achieve that
the following way::

  $ export PGPASSWORD=h4ckm3m0r3
  $ pg_autoctl create postgres       \
         --auth scram-sha-256        \
         ...                         \
         --monitor postgres://autoctl_node:h4ckm3@monitor.host/pg_auto_failover

  $ pg_autoctl config set replication.password h4ckm3m0r3

Note that you can use `The Password File`__ mechanism as discussed in the
Postgres documentation in order to maintain your passwords in a separate
file, not in your main pg_auto_failover configuration file. This also avoids
using passwords in the environment and in command lines.

__ https://www.postgresql.org/docs/current/libpq-pgpass.html

Encryption of network communications
------------------------------------

Postgres knows how to use SSL to enable network encryption of all
communications, including authentication with passwords and the whole data
set when streaming replication is used.

To enable SSL on the server an SSL certificate is needed. It could be as
simple as a self-signed certificate, and ``pg_autoctl`` creates such a
certificate for you when using ``--ssl-self-signed`` command line option::

  $ pg_autoctl create monitor --ssl-self-signed ...      \
                              --auth scram-sha-256 ...   \
                              --ssl-mode require         \
                              ...

  $ pg_autoctl create postgres --ssl-self-signed ...      \
                               --auth scram-sha-256 ...   \
                               ...

  $ pg_autoctl create postgres --ssl-self-signed ...      \
                               --auth scram-sha-256 ...   \
                               ...

In that example we setup SSL connections to encrypt the network traffic, and
we still have to setup an authentication mechanism exactly as in the
previous sections of this document. Here ``scram-sha-256`` has been
selected, and the password will be sent over an encrypted channel.

When using the ``--ssl-self-signed`` option, ``pg_autoctl`` creates a
self-signed certificate, as per the Postgres documentation at the `Creating
Certificates`__ page.

__ https://www.postgresql.org/docs/current/ssl-tcp.html#SSL-CERTIFICATE-CREATION

The certificate subject CN defaults to the ``--hostname`` parameter, which
can be given explicitely or computed by ``pg_autoctl`` as either your
hostname when you have proper DNS resolution, or your current IP address.

Self-signed certificates provide protection against eavesdropping; this
setup does NOT protect against Man-In-The-Middle attacks nor Impersonation
attacks. See PostgreSQL documentation page `SSL Support`__ for details.

__ https://www.postgresql.org/docs/current/libpq-ssl.html

Using your own SSL certificates
-------------------------------

In many cases you will want to install certificates provided by your local
security department and signed by a trusted Certificate Authority. In that
case one solution is to use ``--skip-pg-hba`` and do the whole setup
yourself.

It is still possible to give the certificates to pg_auto_failover and have
it handle the setup for you, including the creation of and signing of client
certificates for the ``autoctl_node`` and ``pgautofailover_replication``
users::

  $ pg_autoctl create monitor --ssl-ca-file root.crt   \
                              --ssl-crl-file root.crl  \
                              --server-cert server.crt  \
                              --server-key server.key  \
                              --ssl-mode verify-full \
                              ...

  $ pg_autoctl create postgres --ssl-ca-file root.crt   \
                               --server-cert server.crt  \
                               --server-key server.key  \
                               --ssl-mode verify-full \
                               ...

  $ pg_autoctl create postgres --ssl-ca-file root.crt   \
                               --server-cert server.crt  \
                               --server-key server.key  \
                               --ssl-mode verify-full \
                               ...

The option ``--ssl-mode`` can be used to force connection strings used by
``pg_autoctl`` to contain your prefered ssl mode. It defaults to ``require``
when using ``--ssl-self-signed`` and to ``allow`` when ``--no-ssl`` is used.
Here, we set ``--ssl-mode`` to ``validate-ca`` which requires SSL Certificates
Authentication, covered next.

The default ``--ssl-mode`` when providing your own certificates (signed by
your trusted CA) is then ``verify-full``. This setup applies to the client
connection where the server identity is going to be checked against the root
certificate provided with ``--ssl-ca-file`` and the revocation list
optionally provided with the ``--ssl-crl-file``. Both those files are used
as the respective parameters ``sslrootcert`` and ``sslcrl`` in pg_autoctl
connection strings to both the monitor and the streaming replication primary
server.

SSL Certificates Authentication
-------------------------------

Given those files, it is then possible to use certificate based
authentication of client connections. For that, it is necessary to prepare
client certificates signed by your root certificate private key and using
the target user name as its CN, as per Postgres documentation for
`Certificate Authentication`__:

    The cn (Common Name) attribute of the certificate will be compared to
    the requested database user name, and if they match the login will be
    allowed

__ https://www.postgresql.org/docs/current/auth-cert.html

For enabling the `cert` authentication method with pg_auto_failover, you
need to prepare a `Client Certificate`__ for the user ``postgres`` and used
by pg_autoctl when connecting to the monitor, to place in
``~/.postgresql/postgresql.crt`` along with its key
``~/.postgresql/postgresql.key``, in the home directory of the user that
runs the pg_autoctl service (which defaults to ``postgres``).

__ https://www.postgresql.org/docs/current/libpq-ssl.html#LIBPQ-SSL-CLIENTCERT

Then you need to create a user name map as documented in Postgres page `User
Name Maps`__ so that your certificate can be used to authenticate pg_autoctl
users.

__ https://www.postgresql.org/docs/current/auth-username-maps.html

The ident map in ``pg_ident.conf`` on the pg_auto_failover monitor should
then have the following entry, to allow ``postgres`` to connect as the
``autoctl_node`` user for ``pg_autoctl`` operations::

  # MAPNAME       SYSTEM-USERNAME         PG-USERNAME

  # pg_autoctl runs as postgres and connects to the monitor autoctl_node user
  pgautofailover   postgres               autoctl_node

To enable streaming replication, the ``pg_ident.conf`` file on each Postgres
node should now allow the ``postgres`` user in the client certificate to
connect as the ``pgautofailover_replicator`` database user::

  # MAPNAME       SYSTEM-USERNAME         PG-USERNAME

  # pg_autoctl runs as postgres and connects to the monitor autoctl_node user
  pgautofailover  postgres                pgautofailover_replicator

Given that user name map, you can then use the ``cert`` authentication
method. As with the ``pg_ident.conf`` provisioning, it is best to now
provision the HBA rules yourself, using the ``--skip-pg-hba`` option::

  $ pg_autoctl create postgres --skip-pg-hba --ssl-ca-file ...

The HBA rule will use the authentication method ``cert`` with a map option,
and might then look like the following on the monitor::

  # allow certificate based authentication to the monitor
  hostssl pg_auto_failover autoctl_node 10.0.0.0/8 cert map=pgautofailover

Then your pg_auto_failover nodes on the 10.0.0.0 network are allowed to
connect to the monitor with the user ``autoctl_node`` used by
``pg_autoctl``, assuming they have a valid and trusted client certificate.

The HBA rule to use on the Postgres nodes to allow for Postgres streaming
replication connections looks like the following::

  # allow streaming replication for pg_auto_failover nodes
  hostssl replication pgautofailover_replicator 10.0.0.0/8 cert map=pgautofailover

Because the Postgres server runs as the ``postgres`` system user, the
connection to the primary node can be made with SSL enabled and will then
use the client certificates installed in the ``postgres`` home directory in
``~/.postgresql/postgresql.{key,cert}`` locations.

Postgres HBA provisioning
-------------------------

While pg_auto_failover knows how to manage the Postgres HBA rules that are
necessary for your stream replication needs and for its monitor protocol, it
will not manage the Postgres HBA rules that are needed for your
applications.

If you have your own HBA provisioning solution, you can include the rules
needed for pg_auto_failover and then use the ``--skip-pg-hba`` option to the
``pg_autoctl create`` commands.


Enable SSL connections on an existing setup
-------------------------------------------

Whether you upgrade pg_auto_failover from a previous version that did not
have support for the SSL features, or when you started with ``--no-ssl`` and
later change your mind, it is possible with pg_auto_failover to add SSL
settings on system that has already been setup without explicit SSL support.

In this section we detail how to upgrade to SSL settings.

Installing Self-Signed certificates on-top of an already existing
pg_auto_failover setup is done with one of the following pg_autoctl command
variants, depending if you want self-signed certificates or fully verified
ssl certificates::

  $ pg_autoctl enable ssl --ssl-self-signed --ssl-mode required

  $ pg_autoctl enable ssl --ssl-ca-file root.crt   \
                          --ssl-crl-file root.crl  \
                          --server-cert server.crt  \
                          --server-key server.key  \
                          --ssl-mode verify-full

The ``pg_autoctl enable ssl`` command edits the
``postgresql-auto-failover.conf`` Postgres configuration file to match the
command line arguments given and enable SSL as instructed, and then updates
the pg_autoctl configuration.

The connection string to connect to the monitor is also automatically
updated by the ``pg_autoctl enable ssl`` command. You can verify your new
configuration with::

  $ pg_autoctl config get pg_autoctl.monitor

Note that an already running pg_autoctl deamon will try to reload its
configuration after ``pg_autoctl enable ssl`` has finished. In some cases
this is not possible to do without a restart. So be sure to check the logs
from a running daemon to confirm that the reload succeeded. If it did not
you may need to restart the daemon to ensure the new connection string is
used.

The HBA settings are not edited, irrespective of the ``--skip-pg-hba`` that
has been used at creation time. That's because the ``host`` records match
either SSL or non-SSL connection attempts in Postgres HBA file, so the
pre-existing setup will continue to work. To enhance the SSL setup, you can
manually edit the HBA files and change the existing lines from ``host`` to
``hostssl`` to dissallow unencrypted connections at the server side.

In summary, to upgrade an existing pg_auto_failover setup to enable SSL:

  1. run the ``pg_autoctl enable ssl`` command on your monitor and then all
     the Postgres nodes,

  2. on the Postgres nodes, review your pg_autoctl logs to make sure that
     the reload operation has been effective, and review your Postgres
     settings to verify that you have the expected result,

  3. review your HBA rules setup to change the pg_auto_failover rules from
     ``host`` to ``hostssl`` to disallow insecure connections.
