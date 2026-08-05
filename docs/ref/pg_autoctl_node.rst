.. _pg_autoctl_node:

pg_autoctl node
===============

pg_autoctl node - Declarative node lifecycle from a single configuration file

Synopsis
--------

``pg_autoctl node`` manages the full lifecycle of a pg_auto_failover node —
creation, startup, and live reconfiguration — driven by a single
``pg_autoctl_node.ini`` file rather than long sequences of command-line flags::

    pg_autoctl node
      run    Create (if needed) and run a node described by a pg_autoctl_node.ini file
      apply  Apply mutable settings from a pg_autoctl_node.ini to a running node
      start  Start a node waiting in launch=deferred mode (idempotent)
      show   Dump current node configuration as a pg_autoctl_node.ini file
      check  Validate a pg_autoctl_node.ini file without creating anything

.. toctree::
   :maxdepth: 1

   pg_autoctl_node_run
   pg_autoctl_node_apply
   pg_autoctl_node_start
   pg_autoctl_node_show
   pg_autoctl_node_check

Description
-----------

``pg_autoctl node run <file>`` is the recommended entry-point for container
and Kubernetes deployments.  The complete node description lives in one ini
file that can be version-controlled, templated, and bind-mounted into a
container.  The same image and the same entry-point work for every node type
(monitor, primary, standby, Citus coordinator, Citus worker, archiver);
per-node differences live entirely in the mounted ini file.

The ``pg_autoctl_node.ini`` File
--------------------------------

The file uses ``.ini`` sections.  A typical data node::

    [node]
    kind     = postgres
    name     = node1
    hostname = node1.internal
    port     = 5432

    [postgresql]
    pgdata = /var/lib/postgresql/data

    [monitor]
    pguri = postgres://autoctl_node@monitor:5432/pg_auto_failover

    [formation]
    name = default

    [settings]
    candidate_priority = 50
    replication_quorum = true

    [options]
    ssl        = self-signed
    auth       = trust
    pg_hba_lan = true

A monitor node omits ``[monitor]`` entirely::

    [node]
    kind     = monitor
    hostname = monitor.internal
    port     = 5432

    [postgresql]
    pgdata = /var/lib/postgresql/monitor

    [formation default]
    kind = pgsql

Section and Property Reference
-------------------------------

The following sections and properties are supported in
``pg_autoctl_node.ini``.  Properties marked *mutable* can be changed on a
running node by editing the file; the supervisor converges the change without
restarting Postgres.  Immutable properties take effect only the next time the
node is created or started from scratch.

``[node]``
^^^^^^^^^^

``kind``

  Node role.  One of ``postgres``, ``monitor``, ``coordinator``,
  ``worker``, or ``archiver`` (see :ref:`archiving_architecture`).
  Required; immutable.

``name``

  Human-readable node name shown in ``pg_autoctl show state``.  Defaults to
  the value of ``hostname`` when not set.  Immutable.

``hostname``

  Address that other nodes (including the monitor) use to reach this node.
  Defaults to the auto-detected FQDN of the host.  Immutable.

``port``

  Postgres port number.  Defaults to ``5432``.  Immutable.

``[postgresql]``
^^^^^^^^^^^^^^^^

``pgdata``

  Path to the Postgres data directory.  Required; immutable.

``[monitor]``
^^^^^^^^^^^^^

``pguri``

  Connection string to the pg_auto_failover monitor.  Omit for monitor
  nodes.  **Mutable**: changing this value re-registers the node to the new
  monitor without restarting Postgres (``pg_autoctl disable monitor --force``
  followed by ``pg_autoctl enable monitor <new_uri>``).

``no_monitor``

  Set to ``true`` to run in :ref:`pg_autoctl_disable_monitor` (standalone)
  mode.  Immutable.

``node_id``

  Node identifier to use when ``no_monitor = true``.  Immutable.

``[formation]``
^^^^^^^^^^^^^^^

``name``

  Formation name.  Defaults to ``default``.  Immutable.

``group``

  Citus group identifier.  ``0`` means coordinator.  Defaults to ``0``.
  Immutable.  Not meaningful for ``kind = archiver`` -- see below.

For ``kind = archiver``, this section works the same way but with one
real difference in behavior worth knowing: an ordinary node's own
registration retries until its target formation exists (and, once it
does, applies immediately), while ``pg_autoctl create archiver`` (which
this section's ``name`` ultimately drives, once through ``[launch]``
below) has no such retry -- it attaches to whichever groups already exist
in that formation at the exact moment it runs, and never re-attaches to
groups added afterwards on its own. If the target formation (or, for a
Citus formation, all of its groups) might not exist yet when this node's
own container would otherwise start, use ``[launch]`` below to hold it
back until an operator or orchestrator confirms the formation is ready.

``[settings]``
^^^^^^^^^^^^^^

Settings in this section are applied live without restarting the node.

``candidate_priority``

  Failover weight, an integer from 0 to 100.  A value of ``0`` means this
  node is never promoted to primary.  Defaults to ``50``.  **Mutable**.

``replication_quorum``

  Whether this node participates in the synchronous replication quorum.
  Boolean, defaults to ``true``.  **Mutable**.

``region``

  Free-form label identifying the data-centre or availability zone this
  node runs in.  Defaults to ``default``.  **Mutable**: changes are applied
  via ``pg_autoctl set node region`` (see :ref:`pg_autoctl_set_node_region`).
  Purely informational: shown as its own column by ``pg_autoctl watch`` in
  the verbose and higher policies, and does not currently affect any
  failover or quorum decision.

``[options]``
^^^^^^^^^^^^^

``ssl``

  SSL mode.  One of ``self-signed``, ``verify-ca``, ``verify-full``, or
  ``off``.  Defaults to ``self-signed``.  **Mutable**: changes are applied via
  ``pg_autoctl enable ssl``; Postgres reloads SSL without a full restart.

``auth``

  Authentication method written into ``pg_hba.conf`` at create time.  One of
  ``trust``, ``md5``, ``scram``, or ``cert``.  Defaults to ``trust``.
  Immutable (create-time only).

``pg_hba_lan``

  When ``true``, add LAN-range entries to ``pg_hba.conf`` at create time.
  Defaults to ``true``.  Immutable (create-time only).

``[ssl]``
^^^^^^^^^

Certificate paths for ``verify-ca`` and ``verify-full`` SSL modes.  All
three are **mutable**: editing them and saving the file reconfigures Postgres
SSL live via ``pg_autoctl enable ssl``.

``ca_file``

  Path to the CA certificate file (``ssl_ca_file`` in ``postgresql.conf``).

``cert_file``

  Path to the server certificate file (``ssl_cert_file``).

``key_file``

  Path to the server private key file (``ssl_key_file``).

``[launch]``
^^^^^^^^^^^^

Two independent gates, both defaulting to ``immediate``.  ``pg_autoctl node
run`` checks ``create`` first (holding back node creation entirely), then
-- once the node exists, whether this run just created it or it already
existed -- checks ``run`` (holding back actually starting Postgres and the
supervisor).  Setting only one of the two is meaningful: ``create =
immediate`` with ``run = deferred`` creates the node right away but leaves
it stopped; ``create = deferred`` with ``run = immediate`` (the common
case, usually set together as ``create = deferred`` / ``run = deferred``)
waits before doing anything at all.

``create``

  When set to ``deferred``, ``pg_autoctl node run`` polls this file every
  0.5s and waits instead of running ``pg_autoctl create <kind> --run``
  immediately.  Call ``pg_autoctl node start`` to release it (clears both
  ``create`` and ``run`` together).  Defaults to ``immediate``.  See
  :ref:`pg_autoctl_node_start`.

``run``

  When set to ``deferred``, ``pg_autoctl node run`` polls this file every
  0.5s and waits (after any pending ``create`` has already resolved)
  instead of exec'ing into ``pg_autoctl run`` immediately.  Call
  ``pg_autoctl node start`` to release it.  Defaults to ``immediate``.  See
  :ref:`pg_autoctl_node_start`.

``[formation <name>]``
^^^^^^^^^^^^^^^^^^^^^^

Repeat this section for each non-default formation to create.  Applies to
monitor nodes only.

``kind``

  Formation kind.  One of ``pgsql`` or ``citus``.  Defaults to ``pgsql``.
  Immutable.

Password sections
^^^^^^^^^^^^^^^^^

These sections hold credentials that are kept out of the main ini file where
possible.

``[pg_auto_failover]``
  ``autoctl_node_password`` — password for the ``pgautofailover_monitor``
  role (monitor nodes) or the ``autoctl_node`` role (data nodes).

``[replication]``
  ``replication_password`` — password for the ``pgautofailover_replicator``
  role.

``[citus]``
  ``role = secondary`` and ``cluster_name`` for Citus secondary clusters.

Live Reconfiguration
--------------------

The supervisor that ``pg_autoctl node run`` exec's into watches the ini file
for changes.  When it detects a write it re-reads the file and converges any
**mutable** fields without restarting the node or interrupting replication:

``candidate_priority``, ``replication_quorum``, and ``region``
    Applied by calling ``pg_autoctl set node`` against the running node.

``ssl``, ``ca_file``, ``cert_file``, ``key_file``
    Applied by calling ``pg_autoctl enable ssl`` with the appropriate flags.
    Postgres reloads its SSL configuration without a full restart.

``monitor.pguri``
    Applied by calling ``pg_autoctl disable monitor --force`` (which removes
    the node from the old monitor) followed by
    ``pg_autoctl enable monitor <new_uri>`` (which registers the node to the
    new monitor).  Postgres keeps running throughout; only the monitoring
    relationship changes.

Changing an **immutable** field (``kind``, ``pgdata``, ``hostname``, ``port``,
``auth``, ``pg_hba_lan``) while the node is running is logged as a warning;
the value takes effect the next time the node is started.

The Deferred-Launch Pattern
----------------------------

::

    [launch]
    create = deferred
    run    = deferred

A node configured this way still runs the ordinary ``pg_autoctl node run``
command, but it starts a polling loop and waits rather than creating or
starting Postgres.  A sidecar container or init script then calls::

    pg_autoctl node start

which clears both flags (rewriting the ini file with ``create = immediate``
/ ``run = immediate``).  The waiting node detects the change within the
poll interval and proceeds.  This enables ordered startup without an
external orchestrator: the monitor container can be given the defaults
(``immediate``) while all data nodes start deferred, and each data node is
released with ``node start`` only after the monitor is confirmed ready --
or, for an :ref:`archiving_architecture` archiver that needs every group of
its target formation to already exist (a Citus formation's several worker
groups, in particular), only after every one of those groups is confirmed
registered.

See Also
--------

:ref:`pg_autoctl_node_run`, :ref:`pg_autoctl_create_postgres`,
:ref:`pg_autoctl_run`, :ref:`pg_autoctl_set`
