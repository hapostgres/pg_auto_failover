.. _pg_autoctl_create_archiver:

pg_autoctl create archiver
===========================

pg_autoctl create archiver - Initialize a pg_auto_failover archiver node

Synopsis
--------

The command ``pg_autoctl create archiver`` registers a new **Archiver**
identity on the monitor and attaches it to one or more formations for
Archiving & Disaster Recovery. See :ref:`archiving_architecture` for what
an archiver actually does once running, and :ref:`archiving_operations`
for the operational side of this command.

::

  usage: pg_autoctl create archiver

    --pgdata            path to the archiver's local data/cache directory
    --pgctl             path to pg_ctl (used to locate pg_receivewal)
    --monitor           pg_auto_failover Monitor Postgres URL
    --hostname           hostname to advertise for this archiver
    --name                archiver name (default: derived from hostname)
    --formation          formation to attach to, may be repeated
                         (default: "default")
    --region             data-centre or availability-zone label for this
                         archiver (default: "default")
    --basebackup-policy   base-backup production/retention policy to attach
                         (default: "default")
    --run                create node then run pg_autoctl service

Description
-----------

Unlike ``pg_autoctl create postgres`` and the other node kinds, this
command does not initialize a PostgreSQL data directory: ``--pgdata``
here names the archiver's local cache directory for captured WAL segments
and base backups, and no ``initdb`` ever runs against it. Once
registered, the archiver starts one WAL-capture process per group of
every formation it is attached to (every worker of a Citus formation
included, not just the coordinator), each following its own group's
current primary and reconnecting on its own across any later promotion,
and reports its progress to the monitor the same way an ordinary standby
reports replication state.

``--formation`` may be given more than once, to attach the same archiver
to several formations from the start -- there is currently no separate
command to attach an already-running archiver to a further formation
later on, so every formation (and, for a Citus formation gaining a new
worker group afterwards, that new group too) needs to be covered by a
repeated ``--formation`` up front. See :ref:`archiving_architecture`'s own
"Several formations" and "A Citus formation" sections for the process
model this produces.

``--basebackup-policy`` attaches a named base-backup production/retention
policy (see :ref:`pg_autoctl_create_basebackup_policy`) to every formation
given, formation-wide. A formation that never gets a policy of its own
this way, or via :ref:`pg_autoctl_set_basebackup_policy`, falls back to
the schema's own built-in ``default`` policy.

Options
-------

The following options are available to ``pg_autoctl create archiver``:

--pgdata

  Path to the archiver's local cache directory for captured WAL segments
  and base backups. Despite the flag's name shared with every other node
  kind, this is never a real Postgres data directory. Defaults to the
  environment variable ``PGDATA``.

--pgctl

  Path to the ``pg_ctl`` tool, used only to locate the ``pg_receivewal``
  binary the archiver runs alongside it. Same discovery rules as
  :ref:`pg_autoctl_create_postgres`'s own ``--pgctl``.

--monitor

  Postgres URI used to connect to the monitor. Must use the
  ``autoctl_node`` username and target the ``pg_auto_failover`` database
  name. It is possible to show the Postgres URI from the monitor node
  using the command :ref:`pg_autoctl_show_uri`.

--hostname

  Hostname or IP address other nodes and clients use to reach this
  archiver -- in particular, what a standby's ``primary_conninfo`` or a
  ``restore_command`` would point at when using this archiver as a
  disaster-recovery source. Same discovery rules as
  :ref:`pg_autoctl_create_postgres`'s own ``--hostname`` when not
  provided.

--name

  Archiver name used on the monitor. Defaults to ``--hostname`` when not
  provided.

--formation

  Formation to attach this archiver to. May be repeated to attach the
  same archiver to several formations at once; defaults to the
  ``default`` formation when not given at all.

--region

  Free-form label identifying the data-centre or availability zone this
  archiver runs in. Purely informational, same convention as
  :ref:`pg_autoctl_create_postgres`'s own ``--region``: displayed by
  ``pg_autoctl watch``'s archivers section, does not affect any placement
  or quorum decision on its own. More than one archiver can be attached
  to the very same formation at once -- distinct regions across them is
  the intended shape for geographically-redundant disaster-recovery
  coverage of one formation.

--basebackup-policy

  Name of an existing base-backup policy (see
  :ref:`pg_autoctl_create_basebackup_policy`) to attach to every
  ``--formation`` given, formation-wide.

--run

  Immediately run the ``pg_autoctl`` archiver service after having
  created this node, instead of requiring a separate ``pg_autoctl run``
  invocation afterwards.

See Also
--------

:ref:`pg_autoctl_node_run` provides a declarative alternative to this
command: describe the node once in a ``pg_autoctl_node.ini`` file and run
``pg_autoctl node run`` — it creates the archiver if absent and starts
the supervisor in one step. See :ref:`pg_autoctl_node` for the full
reference.

:ref:`archiving_architecture` covers what runs once an archiver is
started, and :ref:`archiving_operations` covers the rest of the
day-to-day commands (attaching a policy after the fact, watching what's
captured, rebuilding a node from an archiver's cache).
