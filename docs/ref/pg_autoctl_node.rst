.. _pg_autoctl_node:

pg_autoctl node
===============

pg_autoctl node - Declarative node lifecycle from a single configuration file

Synopsis
--------

``pg_autoctl node`` manages the full lifecycle of a pg_auto_failover node —
creation, startup, and live reconfiguration — driven by a single
``pg_autoctl_node.ini`` file rather than long sequences of flags::

    pg_autoctl node
      run    Create (if needed) and run a node described by a pg_autoctl_node.ini file
      apply  Apply mutable settings from a pg_autoctl_node.ini to a running node
      start  Start a node waiting in launch=deferred mode (idempotent)
      show   Dump current node configuration as a pg_autoctl_node.ini file
      check  Validate a pg_autoctl_node.ini file without creating anything

.. toctree::
   :maxdepth: 1

   pg_autoctl_node_run

Description
-----------

``pg_autoctl node`` is the recommended entry point for container and
Kubernetes environments. The complete node description lives in one ini file
that can be version-controlled, templated, and mounted into a container.

A single command starts the node from scratch or resumes an existing one::

    pg_autoctl node run /etc/pgaf/node.ini

This makes ``pg_autoctl node run`` a natural ``CMD`` or ``command:`` for a
Docker or Kubernetes workload — the same image and the same entry-point
work for every node type (monitor, primary, standby, Citus coordinator,
Citus worker). Per-node differences live entirely in the mounted ini file.

The ``pg_autoctl_node.ini`` File
--------------------------------

The file uses ``.ini`` sections. A typical data node::

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
    ssl     = self-signed
    auth    = trust
    pg_hba_lan = true

A monitor node omits ``[monitor]`` entirely (or leaves ``pguri`` empty)::

    [node]
    kind = monitor
    hostname = monitor.internal
    port = 5432

    [postgresql]
    pgdata = /var/lib/postgresql/monitor

    [formation default]
    kind = pgsql

Section reference
~~~~~~~~~~~~~~~~~

``[node]``
    ``kind`` — one of ``postgres``, ``monitor``, ``coordinator``, ``worker``.
    ``name``, ``hostname``, ``port``.

``[postgresql]``
    ``pgdata`` — path to the Postgres data directory.

``[monitor]``
    ``pguri`` — connection string to the pg_auto_failover monitor.
    ``no_monitor = true`` for :ref:`disabled-monitor` mode.
    ``node_id`` — required with ``no_monitor``.

``[formation]``
    ``name`` — formation name, default ``"default"``.
    ``group`` — Citus group id (0 = coordinator).

``[settings]``  *(mutable — changes take effect without restart)*
    ``candidate_priority`` — failover weight 0–100, default 50.
    ``replication_quorum`` — sync quorum participant, default ``true``.

``[options]``  *(create-time only — ignored on restart)*
    ``ssl`` — ``self-signed``, ``verify-ca``, ``verify-full``, or ``off``.
    ``auth`` — ``trust``, ``md5``, ``scram``, or ``cert``.
    ``pg_hba_lan`` — add LAN-range entries to ``pg_hba.conf``.

``[ssl]``  *(for ``verify-ca`` / ``verify-full`` modes)*
    ``ssl_ca_file``, ``ssl_cert_file``, ``ssl_key_file``.

``[launch]``  *(optional — for ordered startup)*
    ``mode = deferred`` — hold the node in a wait loop until
    ``pg_autoctl node start`` writes ``mode = immediate``.
    Useful in orchestrators that need fine-grained control over
    the order in which nodes join the formation.

``[formation <name>]``  *(monitor kind only — repeat for each non-default formation)*
    ``kind`` — ``pgsql`` (default) or ``citus``.

Live Reconfiguration
--------------------

The supervisor that ``pg_autoctl node run`` exec's into watches the ini file
for changes. When it detects a write (via inotify on Linux, mtime polling
elsewhere) it re-reads the ``[settings]`` section and applies any changes
without restarting the node or interrupting replication.

Fields that are **mutable** and applied live:

- ``candidate_priority``
- ``replication_quorum``

Fields that are **immutable** (require a node restart to take effect):
``kind``, ``pgdata``, ``hostname``, ``port``, ``monitor.pguri``, all
``[options]`` and ``[ssl]`` values.

Changing an immutable field while the node is running is logged as a warning;
the new value will take effect the next time the node is started.

Docker and Kubernetes Usage
---------------------------

The fixed default path ``/etc/pgaf/node.ini`` lets every container image
use the same entry-point::

    CMD ["pg_autoctl", "node", "run", "/etc/pgaf/node.ini"]

Per-node configuration is then a bind-mount (Docker) or a ConfigMap
volume (Kubernetes), keeping the image itself fully generic.

**Docker Compose example:**

.. code-block:: yaml

    services:
      monitor:
        image: hapostgres/pg_auto_failover:latest
        volumes:
          - ./config/monitor.ini:/etc/pgaf/node.ini:ro
        command: ["pg_autoctl", "node", "run", "/etc/pgaf/node.ini"]

      node1:
        image: hapostgres/pg_auto_failover:latest
        volumes:
          - node1-data:/var/lib/postgresql/data
          - ./config/node1.ini:/etc/pgaf/node.ini:ro
        command: ["pg_autoctl", "node", "run", "/etc/pgaf/node.ini"]
        depends_on: [monitor]

      node2:
        image: hapostgres/pg_auto_failover:latest
        volumes:
          - node2-data:/var/lib/postgresql/data
          - ./config/node2.ini:/etc/pgaf/node.ini:ro
        command: ["pg_autoctl", "node", "run", "/etc/pgaf/node.ini"]
        depends_on: [monitor]

    volumes:
      node1-data:
      node2-data:

To promote ``node2`` to a higher failover priority without restarting it,
edit ``node2.ini`` and change ``candidate_priority = 80``, then write the
file. The supervisor picks up the change within seconds.

**Kubernetes StatefulSet example:**

.. code-block:: yaml

    apiVersion: apps/v1
    kind: StatefulSet
    metadata:
      name: pg-node
    spec:
      replicas: 2
      template:
        spec:
          containers:
          - name: pg-autoctl
            image: hapostgres/pg_auto_failover:latest
            command: ["pg_autoctl", "node", "run", "/etc/pgaf/node.ini"]
            volumeMounts:
            - name: node-spec
              mountPath: /etc/pgaf
          volumes:
          - name: node-spec
            configMap:
              name: pg-autoctl-node-spec

Updating the ConfigMap triggers the supervisor's file watcher and mutable
settings converge automatically; immutable changes require a pod restart.

Relationship to ``pg_autoctl create`` and ``pg_autoctl run``
------------------------------------------------------------

``pg_autoctl node run`` is a thin layer on top of the existing machinery —
it translates the ini file into the same flags and exec's into the same
supervisor that ``pg_autoctl create ... --run`` or ``pg_autoctl run`` would
start. There is no hidden API: every behaviour described here maps directly
to documented ``pg_autoctl`` operations.

You can always switch between approaches:

- Use ``pg_autoctl node show --pgdata <dir>`` to generate an ini file from
  an existing node that was created with ``pg_autoctl create``.
- Use ``pg_autoctl create`` and ``pg_autoctl run`` directly when you prefer
  explicit flag-based management.

Both approaches share the same state files, configuration, and monitor
protocol — only the entry point differs.

See Also
--------

:ref:`pg_autoctl_node_run`, :ref:`pg_autoctl_create_postgres`,
:ref:`pg_autoctl_run`, :ref:`pg_autoctl_set`
