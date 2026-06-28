.. _pg_autoctl_node:

pg_autoctl node
===============

pg_autoctl node - Declarative node lifecycle driven by a configuration file

Synopsis
--------

::

  pg_autoctl node
    run    <file.ini>           Create (if needed) and run from a node spec file
    apply  <file.ini>           Apply mutable settings to a running node
    show   [ --pgdata <dir> ]   Dump current config as a node spec file
    check  <file.ini>           Validate a node spec file without creating anything

Description
-----------

The ``pg_autoctl node`` command group replaces the manual composition of
``pg_autoctl create <kind> [options] --run`` and provides a single declarative
interface for the full lifecycle of a pg_auto_failover node:

1. **Create** — if ``PGDATA`` does not yet contain a ``pg_autoctl.cfg``, run
   the equivalent of ``pg_autoctl create <kind>`` using the options in the
   file.

2. **Run** — hand off to the pg_autoctl supervisor loop, which then manages
   the Postgres and service subprocesses.

3. **Watch** — the supervisor watches the node spec file for changes and
   automatically **converges mutable settings** (``candidate_priority``,
   ``replication_quorum``) without requiring a node restart.

The node spec file is a small INI file that collects all node parameters.
The default path inside a Docker/Kubernetes container is
``/etc/pgaf/node.ini``.  A Docker image built from the pg_auto_failover
project can use a single ``CMD``:

.. code-block:: dockerfile

   CMD ["pg_autoctl", "node", "run", "/etc/pgaf/node.ini"]

pg_autoctl node run
-------------------

::

  usage: pg_autoctl node run <file.ini>

  <file.ini>   Path to the pg_autoctl.ini file.
               Defaults to /etc/pgaf/node.ini when no argument is given.

**First start (node not yet created):**
Builds and executes the equivalent of::

  pg_autoctl create <kind> \
    --pgdata   <pgdata>    \
    --hostname <hostname>  \
    --pgport   <port>      \
    --monitor  <pguri>     \
    --formation <formation> \
    --ssl-self-signed      \   # or --no-ssl, depending on [options] ssl =
    --auth trust           \
    --pg-hba-lan           \
    --run

**Subsequent starts (node already created):**
Reads the spec file, applies any changed mutable settings (``candidate_priority``,
``replication_quorum``), then executes ``pg_autoctl run --pgdata <pgdata>``.

Before executing, ``pg_autoctl node run`` sets the environment variable
``PG_AUTOCTL_NODESPEC`` to the path of the spec file.  The supervisor process
(PID 1 in Docker/Kubernetes) reads this variable at start-up and initialises a
file watcher so that later edits to the spec file are picked up without
restarting the supervisor.

pg_autoctl node apply
---------------------

::

  usage: pg_autoctl node apply <file.ini>

Reads the node spec file and compares the mutable section (``[settings]``)
against the node's current configuration.  For each changed field it runs the
appropriate ``pg_autoctl set node`` sub-command:

.. list-table::
   :header-rows: 1

   * - INI field
     - pg_autoctl sub-command
   * - ``candidate_priority``
     - ``pg_autoctl set node candidate-priority``
   * - ``replication_quorum``
     - ``pg_autoctl set node replication-quorum``

Use this command when the supervisor is not running (for example, in a
scripted maintenance window) to reconcile the monitor's view of the node with
a freshly edited spec file.

pg_autoctl node show
--------------------

::

  usage: pg_autoctl node show [ --pgdata <dir> ]

Reads the local ``pg_autoctl.cfg`` configuration file and prints it in
``pg_autoctl.ini`` format on stdout.  Useful for bootstrapping a spec
file from an existing node that was created with ``pg_autoctl create``.

Example::

  $ pg_autoctl node show --pgdata /var/lib/postgres/pgaf > node.ini

pg_autoctl node check
---------------------

::

  usage: pg_autoctl node check <file.ini>

Parses and validates the spec file without creating or modifying anything.
Prints a summary of all resolved fields.  Exit code is non-zero when the file
is invalid or a required field is missing.

Example output::

  Node spec "/etc/pgaf/node.ini" is valid.
    kind               : postgres
    pgdata             : /var/lib/postgres/pgaf
    hostname           : node1
    port               : 5432
    monitor_pguri      : postgresql://autoctl_node@monitor/pg_auto_failover
    formation          : default
    group              : 0
    candidate_priority : 50
    replication_quorum : true
    ssl                : self-signed
    auth               : trust
    pg_hba_lan         : true

.. _node_spec_format:

Node spec file format (pg_autoctl.ini)
-------------------------------------------

The node spec file uses a simple INI syntax with five sections.  Lines
beginning with ``#`` are comments.

.. code-block:: ini

   # pg_autoctl.ini — describes a single pg_auto_failover node.
   # Feed to: pg_autoctl node run /etc/pgaf/node.ini
   # Edit and save to converge mutable settings on the running node.

   [node]
   kind     = postgres        # postgres | monitor | coordinator | worker
   hostname = node1           # reported to the monitor; default: system hostname
   port     = 5432            # Postgres port

   [postgresql]
   pgdata = /var/lib/postgres/pgaf

   [monitor]
   # Required for kind != monitor.  Empty for monitor nodes.
   pguri = postgresql://autoctl_node@monitor/pg_auto_failover

   [formation]
   name  = default            # formation name; default "default"
   group = 0                  # Citus group id; 0 = coordinator group

   [settings]
   # These fields are mutable.  Edit and save to apply without a restart.
   candidate_priority = 50    # 0–100; 0 = never a failover candidate
   replication_quorum = true  # participates in the synchronous replication quorum

   [options]
   # These fields are immutable after the node is created.
   # They are only used during the first-ever pg_autoctl create run.
   ssl        = self-signed   # self-signed | cert | off
   auth       = trust         # trust | md5 | scram
   pg_hba_lan = true          # add --pg-hba-lan at create time (allow LAN connections)

Sections and fields
~~~~~~~~~~~~~~~~~~~

``[node]``
^^^^^^^^^^

``kind``
  Node role.  One of: ``monitor``, ``postgres``, ``coordinator``, ``worker``.
  Determines which ``pg_autoctl create`` sub-command is used.  Immutable
  after the node is created.

``hostname``
  Hostname or IP address reported to the pg_auto_failover monitor.  Other
  nodes use this to reach Postgres.  Mutable in the ``[node]`` section but
  requires a monitor metadata update (``pg_autoctl set node metadata``) to
  take effect on a running node; the file watcher does **not** apply hostname
  changes automatically.  Defaults to the system hostname when empty.

``port``
  Postgres port number.  Defaults to ``5432``.  Immutable after creation.

``[postgresql]``
^^^^^^^^^^^^^^^^

``pgdata``
  Absolute path to the Postgres data directory.  Immutable after creation.

``[monitor]``
^^^^^^^^^^^^^

``pguri``
  Connection string the node uses to reach the pg_auto_failover monitor.
  Required for all ``kind`` values except ``monitor``.  Example::

    pguri = postgresql://autoctl_node@monitor/pg_auto_failover

``[formation]``
^^^^^^^^^^^^^^^

``name``
  Formation name on the monitor.  Defaults to ``default``.

``group``
  Citus group id.  Meaningful only for ``kind = coordinator`` (always ``0``)
  and ``kind = worker`` (``1``, ``2``, …).  Ignored for other node kinds.

``[settings]`` — mutable
^^^^^^^^^^^^^^^^^^^^^^^^

``candidate_priority``
  Failover candidate priority, ``0``–``100``.  ``0`` means the node is never
  promoted; ``100`` means it is the preferred candidate.  Default ``50``.

  Editing this field and saving the file causes the supervisor to call
  ``pg_autoctl set node candidate-priority`` within 10 seconds (or
  immediately on Linux systems that support inotify).

``replication_quorum``
  ``true`` or ``false``.  When ``true`` the node participates in the
  synchronous replication quorum controlled by
  ``synchronous_standby_names``.  Default ``true``.

  Editing this field and saving the file causes the supervisor to call
  ``pg_autoctl set node replication-quorum``.

``[options]`` — immutable after creation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``ssl``
  SSL mode used at create time.  One of:

  * ``self-signed`` — ``pg_autoctl`` generates a self-signed certificate
    and enables SSL (``--ssl-self-signed``).
  * ``cert`` — SSL is enabled; certificate files must already be in place.
  * ``off`` — no SSL (``--no-ssl``).

``auth``
  Authentication method passed as ``--auth`` to ``pg_autoctl create``.
  Common values: ``trust``, ``md5``, ``scram-sha-256``.

``pg_hba_lan``
  When ``true``, pass ``--pg-hba-lan`` at create time.  This adds a
  ``host all all <LAN-CIDR> <auth>`` rule to ``pg_hba.conf``, which is
  required when Postgres is accessed from other containers on the same
  Docker network.  Set to ``false`` for the monitor node (which does not
  serve data connections from other nodes).

Live reconfiguration
--------------------

The supervisor (the process that runs as PID 1 when ``pg_autoctl node run``
is used) watches the spec file for changes and converges the ``[settings]``
section automatically.

On **Linux** the supervisor uses ``inotify`` (``IN_CLOSE_WRITE`` and
``IN_MOVED_TO`` events) so changes are detected within milliseconds of the
file being closed after writing.

On **macOS and other platforms** the supervisor falls back to polling the
file's modification time every 10 seconds (``NODESPEC_WATCH_INTERVAL_SECS``).

Workflow — updating candidate priority in a running cluster
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: bash

   # 1. Edit the spec file on the Docker host
   sed -i 's/candidate_priority = 50/candidate_priority = 0/' node1.ini

   # 2. No further action required: the supervisor picks up the change
   #    within 10 s (mtime poll) or immediately (inotify on Linux).
   #    On the monitor, verify:
   docker compose exec monitor \
     pg_autoctl get node candidate-priority --pgdata /var/lib/postgres/pgaf

Immutable-field changes are logged as warnings but not applied — a node
restart is required.

Use in Docker Compose and Kubernetes
-------------------------------------

The primary motivation for the node spec file is to allow a **single
Docker image** with a **single command** to serve all node roles:

.. code-block:: dockerfile

   # One image, one CMD — role determined by the mounted ini file
   FROM pg_auto_failover:pg17
   CMD ["pg_autoctl", "node", "run", "/etc/pgaf/node.ini"]

In Docker Compose, each service mounts its own ini file at
``/etc/pgaf/node.ini`` (generated by ``pgaftest`` or written by hand):

.. code-block:: yaml

   services:
     monitor:
       image: pg_auto_failover:pg17
       volumes:
         - monitor_data:/var/lib/postgres:rw
         - ./monitor.ini:/etc/pgaf/node.ini:ro
       command: ["pg_autoctl", "node", "run", "/etc/pgaf/node.ini"]

     node1:
       image: pg_auto_failover:pg17
       volumes:
         - node1_data:/var/lib/postgres:rw
         - ./node1.ini:/etc/pgaf/node.ini:ro
       command: ["pg_autoctl", "node", "run", "/etc/pgaf/node.ini"]

In Kubernetes, use a ``ConfigMap`` to store the ini content and mount it as a
file into the Pod.  Editing the ``ConfigMap`` propagates the new file into the
Pod within the kubelet's sync period (typically 1 minute); the supervisor
detects the change and converges without a Pod restart.

.. code-block:: yaml

   apiVersion: v1
   kind: ConfigMap
   metadata:
     name: node1-spec
   data:
     node.ini: |
       [node]
       kind     = postgres
       hostname = node1.pg-cluster.svc.cluster.local
       port     = 5432

       [postgresql]
       pgdata = /var/lib/postgres/pgaf

       [monitor]
       pguri = postgresql://autoctl_node@monitor.pg-cluster.svc.cluster.local/pg_auto_failover

       [formation]
       name  = default
       group = 0

       [settings]
       candidate_priority = 50
       replication_quorum = true

       [options]
       ssl        = self-signed
       auth       = scram-sha-256
       pg_hba_lan = true

   ---
   apiVersion: apps/v1
   kind: StatefulSet
   # ...
   spec:
     template:
       spec:
         containers:
           - name: postgres
             image: pg_auto_failover:pg17
             command: ["pg_autoctl", "node", "run", "/etc/pgaf/node.ini"]
             volumeMounts:
               - name: node-spec
                 mountPath: /etc/pgaf
         volumes:
           - name: node-spec
             configMap:
               name: node1-spec

See also
--------

* :ref:`pg_autoctl_create_postgres`
* :ref:`pg_autoctl_create_monitor`
* :ref:`pg_autoctl_run`
* :ref:`pg_autoctl_set_node_candidate_priority`
* :ref:`pg_autoctl_set_node_replication_quorum`
* :ref:`pgaftest_spec` — ``pgaftest`` test spec files use the same ini format
  for the per-node files it generates inside ``cluster { }`` blocks
