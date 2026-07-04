.. _pgaftest:

pgaftest — Integration Test Framework
======================================

``pgaftest`` is the integration-test binary for pg_auto_failover.  It reads
``.pgaf`` specification files that describe a cluster topology and a sequence
of named test steps, then drives a Docker Compose stack to verify the cluster
behaves correctly.

The same specification file works in two modes:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Command
     - Effect
   * - ``pgaftest run spec.pgaf``
     - Headless CI: start → setup → sequence → teardown → TAP output + exit code
   * - ``pgaftest setup spec.pgaf``
     - Interactive: start cluster, run setup, then drop into a shell or tmux session

Quick start
-----------

.. code-block:: bash

   # Build the pg_auto_failover Docker image once
   docker build -t pg_auto_failover:pg17 .

   # Run a spec file headlessly
   PGAF_IMAGE=pg_auto_failover:pg17 pgaftest run tests/tap/specs/basic_operation.pgaf

   # Start a cluster interactively
   PGAF_IMAGE=pg_auto_failover:pg17 pgaftest setup tests/tap/specs/basic_operation.pgaf

   # Run one named step against the live cluster
   # (work dir is auto-derived: $TMPDIR/pgaftest/basic_operation)
   pgaftest step fail_primary tests/tap/specs/basic_operation.pgaf

   # Tear down
   pgaftest down tests/tap/specs/basic_operation.pgaf

.. _pgaftest_spec:

Spec file format
----------------

A ``.pgaf`` file contains four kinds of top-level blocks:

1. ``cluster { }`` — topology declaration
2. ``setup { }`` — commands to run after ``docker compose up``
3. ``teardown { }`` — commands to run at the end of a run (or on ``pgaftest down``)
4. ``step <name> { }`` — named test step
5. ``sequence`` — ordered list of step names for CI mode

cluster { }
~~~~~~~~~~~

The ``cluster`` block describes the Docker Compose topology.  ``pgaftest``
generates:

* A ``docker-compose.yml`` with one service per node
* A ``pg_autoctl_node.ini`` file per node (see :ref:`node_spec_format`)
* Each container mounts its ini file at ``/etc/pgaf/node.ini`` and runs the
  same command: ``pg_autoctl node run /etc/pgaf/node.ini``

The topology follows a three-level hierarchy:
``cluster`` → ``monitor`` + ``formation(s)`` → ``nodes``

**Syntax**

.. code-block:: text

   cluster {
       # Cluster-level options (all optional)
       monitor [port N]               # expose monitor on host port; 0 = auto
       image   "pg_auto_failover:pg17"  # Docker image; "" = build from source
       ssl     self-signed             # self-signed | cert | off
       auth    trust                   # trust | md5 | scram

       # One or more formation blocks
       formation [name] [num-sync N] {
           nodename [kind] [option...]
           ...
       }
   }

**Cluster-level options**

``monitor [port N]``
  Declares the monitor service.  The optional ``port N`` exposes the
  monitor's Postgres port on host port ``N`` so the test runner can connect
  directly with libpq.  When omitted, a random free port is chosen.

``image``
  Docker image to use for every container.  Overrides the ``PGAF_IMAGE``
  environment variable.  When neither is set, a ``build:`` stanza pointing at
  the project root is used instead.

``ssl``
  SSL mode written into the ``[options]`` section of each node's ini file.
  Allowed values: ``self-signed`` (default), ``cert``, ``off``.

``auth``
  Authentication method.  Passed as ``--auth`` to ``pg_autoctl create``.
  Default: ``trust``.

**Formation block**

.. code-block:: text

   formation [name] [num-sync N] {
       ...nodes...
   }

``name``
  Formation name on the monitor.  Defaults to ``default`` when omitted.

``num-sync N``
  Sets ``number-sync-standbys`` on this formation after the cluster comes up.
  When absent, the monitor default (0) applies.

**Node lines** (inside a formation block)

.. code-block:: text

   nodename [kind] [option...]

``nodename``
  The service name — used as the Docker Compose service name and as the
  ``hostname`` in the container.

``kind`` (optional, default ``postgres``)
  * ``postgres`` — standalone Postgres node
  * ``coordinator`` — Citus coordinator
  * ``worker`` — Citus worker

Options:

* ``async`` — makes this node asynchronous (``replication_quorum = false``)
* ``candidate-priority N`` — integer 0–100; default 50.  ``0`` prevents this
  node from ever being elected primary.  Can also be written ``candidate-priority=N``.
* ``replication-quorum false`` — same as ``async``; can also be
  written ``replication-quorum=false``.
* ``group N`` — Citus group id (required for ``worker`` kind).
  Can also be written ``group=N``.
* ``no-monitor`` — standalone node: does not register with a monitor.
* ``listen`` — bind Postgres to all interfaces (``0.0.0.0``).
* ``citus-secondary`` — marks this node as a Citus secondary.
* ``citus-cluster-name NAME`` — sets the Citus cluster name.
* ``port N`` — override the default Postgres port (5432).
* ``debian-cluster NAME`` — Debian-style Postgres cluster name.  The node
  will use the Debian ``/var/lib/postgresql/<version>/<NAME>`` layout that
  results from ``pg_createcluster``.  Requires the ``debian`` Dockerfile
  build target; see ``monitor image-target`` below.
* ``ssl MODE`` — per-node SSL mode override (overrides the cluster-level ``ssl``).
* ``auth-method METHOD`` — per-node auth method override.
* ``launch deferred`` — do not start this node automatically at ``compose up``
  time.  Use ``compose start <node>`` inside a step to start it later.  This
  is how tests that add nodes mid-scenario are written.
* ``volume <name> <containerPath>`` — mount a Docker named volume into the
  container at ``<containerPath>``.  The volume is created automatically.
  Multiple ``volume`` lines are allowed.

**Multi-option nodes — block syntax**

When a node needs several options the block form keeps the spec readable:

.. code-block:: text

   node coordinator1b {
       coordinator
       candidate-priority 0
       citus-secondary
       citus-cluster-name readonly
   }

   node worker1b {
       worker group 1
       candidate-priority 0
       citus-secondary
       citus-cluster-name readonly
   }

   node node1 {
       volume extra_a "/extra_volumes/extra_a"
       volume extra_b "/extra_volumes/extra_b"
   }

The block form requires the ``node`` keyword before the name.  Flat (single-
line) form does not use the keyword.  ``pgaftest indent`` automatically
promotes a node to block form whenever the flat line would exceed 72 characters
or the node has any ``volume`` entries.

**Monitor options**

.. code-block:: text

   cluster {
       monitor image-target testrun
       monitor debian-cluster main
       ...
   }

``monitor image-target <name>``
  Use the specified Dockerfile build stage for the monitor container instead
  of the default ``run`` stage.  Useful when the monitor needs extra tools
  (e.g. ``testrun`` for ``make installcheck``, ``test`` for the build
  environment).

``monitor debian-cluster <name>``
  Mark the monitor container as Debian-style: use the ``debian`` Dockerfile
  target and set ``PGDATA`` to ``/var/lib/postgresql/<version>/<name>``.

``monitor <name> initially stopped``
  Declare a second monitor service named ``<name>``.  The container is
  created and initialized at ``compose up`` time (so its data volume is
  ready), but ``pgaftest`` immediately stops it.  Use this for
  monitor-replacement scenarios where the second monitor must exist on disk
  but must not be reachable until the test deliberately starts it with
  ``compose start <name>``.

  .. code-block:: text

     cluster {
         monitor
         monitor monitor2 initially stopped
         formation {
             node1
             node2
         }
     }

**Examples**

Minimal two-node HA cluster:

.. code-block:: text

   cluster {
       monitor
       formation {
           node1
           node2
       }
   }

Three-node cluster, ``node3`` is a non-candidate async standby:

.. code-block:: text

   cluster {
       monitor
       formation {
           node1
           node2
           node3  async  candidate-priority 0
       }
   }

Named formation with ``num-sync``:

.. code-block:: text

   cluster {
       monitor
       formation default num-sync 1 {
           node1
           node2
           node3
       }
   }

Citus cluster (one coordinator, two worker HA pairs in separate formations):

.. code-block:: text

   cluster {
       image "pg_auto_failover:pg17-citus"
       monitor
       formation coordinators {
           coord  coordinator
       }
       formation workers num-sync 1 {
           worker1  worker  group 1
           worker2  worker  group 1
           worker3  worker  group 2
           worker4  worker  group 2
       }
   }

Custom image and non-default auth:

.. code-block:: text

   cluster {
       image "myregistry.example.com/pgaf:latest"
       ssl   self-signed
       auth  scram-sha-256
       monitor port 15432
       formation {
           node1
           node2
       }
   }

setup { } and teardown { }
~~~~~~~~~~~~~~~~~~~~~~~~~~

These blocks contain the same DSL commands as named steps (see below).
``setup`` runs after ``docker compose up -d``, before the first step.
``teardown`` runs at the end of ``pgaftest run`` (always, even on failure)
and at the end of ``pgaftest down`` in interactive mode.

A typical setup block waits for all nodes to reach their initial states:

.. code-block:: text

   setup {
       wait until node1 state = primary   timeout 120s
       wait until node2 state = secondary timeout 120s
   }

step <name> { }
~~~~~~~~~~~~~~~

Named blocks of commands that can be run individually (``pgaftest step``) or
collectively via the ``sequence`` directive.

.. code-block:: text

   step my_step {
       <command>
       <command>
       ...
   }

sequence
~~~~~~~~

Declares the ordered list of steps to run in CI mode (``pgaftest run``).
Each step name appears on its own line or space-separated:

.. code-block:: text

   sequence
       create_table
       fail_primary
       check_failover
       restart_old_primary

Step commands (DSL reference)
------------------------------

All commands are available inside ``setup``, ``teardown``, and ``step`` blocks.

``exec <service> <command...>``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Run a command inside a running container via ``docker compose exec``.
Fails the step if the command exits with a non-zero status.

.. code-block:: text

   exec node1  pg_autoctl perform failover
   exec monitor  psql -c "SELECT count(*) FROM pgautofailover.node"

``exec-fails <service> <command...>``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Like ``exec`` but asserts the command **fails** (exits non-zero).
Fails the step if the command unexpectedly succeeds.  Use this to verify
that a command is correctly rejected by pg_autoctl.

.. code-block:: text

   # enable maintenance on a primary without --allow-failover must fail
   exec-fails node1  pg_autoctl enable maintenance

   # failover with a single node (no standby) must fail
   exec-fails monitor  pg_autoctl perform failover

``wait until <node> state is <state>  [timeout Ns]``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Poll until ``<node>`` reaches ``<state>``, then continue.  The timeout
defaults to 90 seconds.  Fails the current step if the timeout expires.
Both ``is`` and ``=`` are accepted as the state operator.

.. code-block:: text

   wait until node1 state is primary        timeout 120s
   wait until node2 state is secondary      timeout 60s
   wait until node2 state is demote_timeout timeout 90s

**Multi-node form** — wait for several nodes simultaneously using ``and``:

.. code-block:: text

   wait until node1 state is primary
       and node2 state is secondary
       timeout 90s

   wait until coordinator1a state is primary
       and coordinator1b state is secondary
       and worker1a state is primary
       timeout 120s

Each ``and <node> state is <state>`` condition must be satisfied at the same
time for the wait to succeed.

**Pass-through states** — assert that a node visits intermediate states on
the way to the target:

.. code-block:: text

   wait until node2 state is primary
       passing through stop_replication, draining
       timeout 120s

The runner observes monitor LISTEN/NOTIFY notifications; a pass-through state
is satisfied when a notification for that state is received before the target
state notification arrives.

**State names** — all pg_auto_failover FSM states are first-class tokens in
the spec DSL.  Both ``_`` and ``-`` spellings are accepted:

.. list-table::
   :header-rows: 1
   :widths: 30 30 40

   * - Token
     - Aliases
     - Description
   * - ``init``
     -
     - Node has just been created
   * - ``single``
     -
     - Single-node cluster (no standby yet)
   * - ``primary``
     -
     - Running as primary
   * - ``wait_primary``
     - ``wait-primary``
     - Waiting for a standby to connect
   * - ``wait_standby``
     - ``wait-standby``
     - Primary waiting for first standby
   * - ``secondary``
     -
     - Running as synchronous standby
   * - ``catchingup``
     -
     - Standby catching up after promotion or restart
   * - ``draining``
     -
     - Primary draining before demotion
   * - ``demote_timeout``
     - ``demote-timeout``
     - Former primary waiting for demotion timer
   * - ``demoted``
     -
     - Former primary; postgres stopped
   * - ``maintenance``
     -
     - Node paused for maintenance
   * - ``prepare_maintenance``
     - ``prepare-maintenance``
     - Transition into maintenance
   * - ``wait_maintenance``
     - ``wait-maintenance``
     - Standby waiting while primary enters maintenance
   * - ``join_primary``
     - ``join-primary``
     - New node joining as primary
   * - ``apply_settings``
     - ``apply-settings``
     - Applying configuration changes
   * - ``report_lsn``
     - ``report-lsn``
     - Standby reporting its LSN for promotion selection
   * - ``fast_forward``
     - ``fast-forward``
     - Standby fast-forwarding to primary's LSN
   * - ``join_secondary``
     - ``join-secondary``
     - New node joining as secondary
   * - ``prepare_promotion``
     - ``prepare-promotion``
     - Standby preparing to be promoted
   * - ``stop_replication``
     - ``stop-replication``
     - Standby stopping replication before promotion
   * - ``dropped``
     -
     - Node has been dropped from the formation

**Dual-source polling** — the runner queries two sources in each polling
round, succeeding as soon as either returns a match:

1. **Monitor** (``pg_autoctl inspect monitor node-state``): checks the
   ``reportedstate`` column in ``pgautofailover.node``.  This is the primary
   source; it captures fast FSM transitions correctly because the monitor's
   view lags by exactly one keeper heartbeat.

2. **Node-local FSM** (``pg_autoctl inspect fsm node-state``): checks the
   keeper's on-disk ``current_role`` in the state file inside the container.
   This source is consulted **only** when the monitor reports ``health < 0``
   (node is unhealthy / unreachable) for the target node.  A network-
   partitioned primary self-assigns ``demote_timeout`` locally after
   ``network_partition_timeout`` seconds (default 20 s) even though it cannot
   report that state back to the monitor.  Gating the node-local check on the
   monitor's health value avoids false positives from stale on-disk state
   while correctly observing partition-driven transitions.

.. note::

   The dual-source strategy lets tests write ``wait until node2 state =
   demote_timeout`` after a ``network disconnect node2`` without any special
   syntax.  The runner detects the partition automatically.

``wait until <state1>, <state2>, ... [in group N] [timeout Ns]``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Wait until the formation as a whole has at least one node in each of the
listed states simultaneously.  Useful for asserting cluster-wide convergence
after a failover.

.. code-block:: text

   # Wait for primary + secondary simultaneously
   wait until primary, secondary timeout 120s

   # Wait for specific group
   wait until primary, secondary in group 1 timeout 90s

``assert <node> state = <state>``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Immediately check that ``<node>`` is in ``<state>`` on the monitor.  Fails
the step if the state does not match.  No polling.

.. code-block:: text

   assert node2 state = primary
   assert node1 assigned-state = secondary

``assert <node> assigned-state = <state>``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Like ``assert state`` but checks the ``assigned_state`` column on the monitor
rather than ``reportedstate``.

``sql <service> { SQL }``
~~~~~~~~~~~~~~~~~~~~~~~~~

Run SQL inside ``<service>``'s container and capture the output.  Use a
multi-line ``{ ... }`` block for multi-statement SQL.

.. code-block:: text

   sql node1 {
       CREATE TABLE t1 (a int);
       INSERT INTO t1 VALUES (1), (2), (3);
   }
   sql node2 { SELECT count(*) FROM t1; }

``expect { text }``
~~~~~~~~~~~~~~~~~~~~

Compare the output of the most recent ``sql`` command against ``text``.
Fails the step when the output does not match.  Also fails if the
preceding ``sql`` raised a SQL error (use ``expect error`` for that).

Single-value form:

.. code-block:: text

   sql node2 { SELECT count(*) FROM t1; }
   expect { 3 }

Multi-line form (matches ``psql --tuples-only --no-align`` output, one row per line):

.. code-block:: text

   sql node1 { SELECT a, b FROM t ORDER BY a; }
   expect {
       1	hello
       2	world
   }

Inline tuple form — each ``{ value }`` group corresponds to one output row.
This is equivalent to the multi-line form above.

.. code-block:: text

   sql node1 { SELECT count(*) FROM t GROUP BY status; }
   expect { { 2 } { 5 } }

``expect error [SQLSTATE]``
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Assert that the most recent ``sql`` command raised a SQL error.
Fails the step if the query succeeded.  The optional ``SQLSTATE`` argument
(a 5-character PostgreSQL error code) constrains the check to a specific
error class.

.. code-block:: text

   # writes to a hot-standby must be rejected (25006 = read_only_sql_transaction)
   sql node2 { INSERT INTO t1 VALUES (42); }
   expect error 25006

   # any SQL error is acceptable (no SQLSTATE constraint)
   sql node1 { SELECT 1/0; }
   expect error

PostgreSQL SQLSTATE codes of interest:

* ``25006`` — ``read_only_sql_transaction`` (writes to a standby)
* ``42P01`` — ``undefined_table``
* ``23505`` — ``unique_violation``

The SQLSTATE is extracted from psql's verbose error output
(``VERBOSITY=verbose``).  ``ON_ERROR_STOP=1`` is always set so psql
exits non-zero on SQL errors.

``network disconnect <node>``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Disconnect ``<node>``'s container from the Docker network, simulating a
network partition.

.. code-block:: text

   network disconnect node1

``network connect <node>``
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Reconnect a previously disconnected container.

.. code-block:: text

   network connect node1

``sleep Ns``
~~~~~~~~~~~~~

Wait ``N`` seconds (for example ``sleep 5s`` or ``sleep 30s``).

``compose down``
~~~~~~~~~~~~~~~~

Run ``docker compose down --volumes``.  Used inside ``teardown { }`` to clean
up after a test run.

.. code-block:: text

   teardown {
       compose down
   }

``compose start <service>``
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Start a stopped container.  Equivalent to ``docker compose start <service>``.
Pairs with ``compose stop`` and ``launch deferred`` node declarations.

.. code-block:: text

   compose start node2

``compose stop <service>``
~~~~~~~~~~~~~~~~~~~~~~~~~~

Stop a container cleanly (SIGTERM + grace period).  Equivalent to
``docker compose stop <service>``.  Use this to simulate a process crash or
clean node shutdown.

.. code-block:: text

   compose stop node1
   wait until node2 state is wait_primary  timeout 90s

``compose kill <service>``
~~~~~~~~~~~~~~~~~~~~~~~~~~

Kill a container immediately with SIGKILL.  Equivalent to
``docker compose kill <service>``.  Use when a clean shutdown would allow
the node to complete its shutdown sequence — which some tests intentionally
want to avoid.

.. code-block:: text

   compose kill node1

``set monitor <service>``
~~~~~~~~~~~~~~~~~~~~~~~~~

Switch the runner's active monitor to ``<service>``.  After this command:

* LISTEN/NOTIFY reconnects to ``<service>`` — subsequent ``wait until`` and
  formation-state checks use the new monitor.
* ``monitor_get_node_state`` queries ``<service>`` for implicit post-exec
  state verification.

Use this in monitor-replacement tests immediately after starting the new
monitor container, so that all subsequent runner operations target it instead
of the original (stopped) monitor.

.. code-block:: text

   step switch_to_new_monitor {
       compose start monitor2
       sleep 5s
       exec monitor2  pg_autoctl inspect pgsetup wait
       set monitor monitor2
   }

   step reconnect_node1 {
       exec node1  pg_autoctl enable monitor postgresql://autoctl_node@monitor2/pg_auto_failover
       wait until node1 state is single  timeout 60s
   }

``logs <svc> [not] contains "<pattern>"``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Assert that the container log output of ``<svc>`` contains (or does not
contain) the literal string ``<pattern>``.  The runner runs
``docker compose logs --no-color <svc>`` and pipes through ``grep -qF``.

.. code-block:: text

   logs node2  contains "password=****"
   logs node2  not contains "plaintext_secret"

``logs <svc> [not] matches "<pattern>"``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Like ``logs … contains`` but uses PCRE via ``grep -qP``.  Use this for
regular-expression patterns, including negative lookahead assertions.

.. code-block:: text

   logs node2  not matches "^(?!primary_conninfo|Failed to find).*streaming_password"

The example asserts that no log line contains ``streaming_password`` unless
the line also starts with ``primary_conninfo`` or ``Failed to find`` — the
two expected contexts where the password legitimately appears.

``stop postgres <node>``
~~~~~~~~~~~~~~~~~~~~~~~~~

Stop the Postgres server inside ``<node>``'s container without stopping the
``pg_autoctl`` supervisor.  Equivalent to calling
``pg_autoctl manual service pgctl off`` inside the container.  The supervisor detects
the outage and reacts according to the FSM.

.. code-block:: text

   exec node1   pg_autoctl enable maintenance
   wait until node1 state is maintenance  timeout 60s
   stop postgres node1

``start postgres <node>``
~~~~~~~~~~~~~~~~~~~~~~~~~~

Restart the Postgres server inside ``<node>``'s container via the
``pg_autoctl`` supervisor (``pg_autoctl manual pgctl on``).

.. code-block:: text

   start postgres node1

``promote <node>[, <node>, ...]``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Assign ``pg_autoctl perform promotion`` to the listed nodes, one per group.
Useful after setup to designate which node becomes primary in each group.

.. code-block:: text

   promote node1
   promote coordinator1a, worker1a, worker2a

``assert <node> stays <state> while { }``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Assert that ``<node>`` remains in ``<state>`` throughout the execution of all
commands in the ``while { }`` body.  The state is checked before and after
each inner command; any deviation fails the step.

.. code-block:: text

   assert node1 stays secondary while {
       exec node2  pg_autoctl enable maintenance
       wait until node2 state is maintenance  timeout 60s
       exec node2  pg_autoctl disable maintenance
       wait until node2 state is secondary    timeout 60s
   }

``%CIDR%`` macro
~~~~~~~~~~~~~~~~~

The literal token ``%CIDR%`` is expanded to the Docker network CIDR (e.g.
``172.31.0.0/16``) in any ``exec`` or ``exec-fails`` command argument.  The
expanded value is logged on a separate line.  Use it to scope HBA rules to
the test network instead of ``0.0.0.0/0``:

.. code-block:: text

   exec monitor  bash -c "echo 'host all all %CIDR% trust' >> /var/lib/postgres/pgaf/pg_hba.conf"

TAP output
----------

In CI mode (``pgaftest run``) the runner emits
`TAP <https://testanything.org>`_ (Test Anything Protocol) output to stdout.
One TAP line is emitted per step:

.. code-block:: text

   1..5
   ok 1 - create_table
   ok 2 - fail_primary
   not ok 3 - check_failover
   # FAIL: wait until node2 state = primary: timed out after 120s (current: demoted)
   ok 4 - restart_old_primary
   ok 5 - verify_replication

Each failing step logs a ``# FAIL: …`` diagnostic line with the exact command
that failed.  Standard TAP consumers (``prove``, GitHub Actions, Jenkins) can
parse this output directly.

The exit code is ``0`` when all steps passed and non-zero otherwise.

Complete example — basic failover test
---------------------------------------

The following is a complete spec file that creates a two-node cluster,
verifies replication, triggers a failover, and checks recovery.

.. code-block:: text

   # tests/tap/specs/basic_operation.pgaf
   #
   # Two-node HA: create primary + secondary, test failover and recovery.

   cluster {
       monitor
       formation {
           node1
           node2
       }
   }

   setup {
       wait until node1 state = primary   timeout 120s
       wait until node2 state = secondary timeout 120s
   }

   teardown {
       compose down
   }

   # ---------------------------------------------------------------
   # Verify that data written on the primary appears on the standby.
   # ---------------------------------------------------------------

   step create_table {
       sql node1 {
           CREATE TABLE t1(a int);
           INSERT INTO t1 VALUES (1), (2), (3);
       }
       sql node2 { SELECT count(*) FROM t1; }
       expect { 3 }
   }

   # ---------------------------------------------------------------
   # Trigger a failover by stopping the primary's pg_autoctl process.
   # ---------------------------------------------------------------

   step fail_primary {
       exec node1  pg_autoctl stop
   }

   step check_failover {
       wait until node2 state = wait_primary  timeout 120s
       wait until node2 state = primary       timeout 120s
       assert node2 state = primary
   }

   step restart_old_primary {
       exec node1  pg_autoctl node run /etc/pgaf/node.ini
       wait until node1 state = secondary  timeout 120s
   }

   step verify_replication {
       sql node2 {
           INSERT INTO t1 VALUES (4), (5);
       }
       sql node1 { SELECT count(*) FROM t1; }
       expect { 5 }
   }

   sequence
       create_table
       fail_primary
       check_failover
       restart_old_primary
       verify_replication

Running the test suite
----------------------

A ``schedule`` file lists the spec files to run, one per line:

.. code-block:: text

   # tests/tap/schedule
   tests/tap/specs/basic_operation.pgaf
   tests/tap/specs/multi_standbys.pgaf

Run all specs in the schedule:

.. code-block:: bash

   PGAF_IMAGE=pg_auto_failover:pg17 \
     pgaftest run --schedule tests/tap/schedule

Run a single spec file:

.. code-block:: bash

   PGAF_IMAGE=pg_auto_failover:pg17 \
     pgaftest run tests/tap/specs/basic_operation.pgaf

Interactive session (useful for debugging):

.. code-block:: bash

   # Start the cluster and drop into a shell
   PGAF_IMAGE=pg_auto_failover:pg17 \
     pgaftest setup tests/tap/specs/basic_operation.pgaf

   # In a separate terminal, run individual steps
   # (work dir auto-derived to $TMPDIR/pgaftest/basic_operation)
   pgaftest step create_table   tests/tap/specs/basic_operation.pgaf
   pgaftest step fail_primary   tests/tap/specs/basic_operation.pgaf
   pgaftest step check_failover tests/tap/specs/basic_operation.pgaf

   # Tear down when done
   pgaftest down tests/tap/specs/basic_operation.pgaf

Environment variables
---------------------

``PGAF_IMAGE``
  Docker image tag to use for all containers.  Overrides the ``image``
  directive in the ``cluster { }`` block.  When neither is set, ``pgaftest``
  uses a ``build:`` stanza pointing at the project root, which triggers a
  local Docker build.

  .. code-block:: bash

     export PGAF_IMAGE=pg_auto_failover:pg17

How pgaftest and pg_autoctl node relate
----------------------------------------

``pgaftest`` uses ``pg_autoctl node run /etc/pgaf/node.ini`` as the container
command, which means every container benefits from the :ref:`live
reconfiguration <pg_autoctl_node>` feature: editing a node's ini file
on the host and saving it causes the running supervisor inside the container
to converge the changed settings within seconds (inotify) or within
``NODESPEC_WATCH_INTERVAL_SECS`` (10 s, on platforms without inotify).

This is particularly useful during interactive sessions.  For example, to
change ``node2``'s candidate priority without restarting the container:

.. code-block:: bash

   # Edit the generated ini file in the work directory
   # (auto-derived from the spec name, e.g. $TMPDIR/pgaftest/basic_operation)
   WORKDIR=${TMPDIR:-/tmp}/pgaftest/basic_operation
   sed -i 's/candidate_priority = 50/candidate_priority = 0/' \
       $WORKDIR/node2.ini

   # The supervisor inside node2 picks up the change automatically.
   # Verify on the monitor:
   docker compose exec monitor \
     pg_autoctl get node candidate-priority --pgdata /var/lib/postgres/pgaf

pg_autoctl inspect commands used by pgaftest
--------------------------------------------

``pgaftest`` talks to the containers exclusively through ``pg_autoctl``
subcommands via ``docker compose exec``.  No psql scripts, no port
forwarding.  The relevant commands are:

``pg_autoctl inspect monitor node-state``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Runs on the **monitor** container.  Queries ``pgautofailover.node`` and
prints one line::

   <reportedstate>|<goalstate>|<health>

``health`` is the integer from the monitor's health-check column:
``1`` = healthy, ``-1`` = unhealthy (keeper unreachable).

With ``--name <node>`` and ``--state <target>`` the command exits 0 only
when ``reportedstate`` equals ``<target>``.  ``--timeout N`` retries for up
to ``N`` seconds using exponential back-off.

.. code-block:: bash

   # One-shot query (prints reported|goal|health)
   docker compose exec -T monitor \
     pg_autoctl inspect monitor node-state --name node2

   # Wait up to 90 s for node2 to report "secondary"
   docker compose exec -T monitor \
     pg_autoctl inspect monitor node-state \
       --name node2 --state secondary --timeout 90

``pg_autoctl inspect fsm node-state``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Runs on a **data node** container.  Reads the keeper's on-disk state file
and prints::

   <current_role>|<assigned_role>

``current_role`` is the keeper's authoritative local state.  It is updated
by the keeper even when the node cannot reach the monitor — for example, a
partitioned primary self-assigns ``demote_timeout`` after
``network_partition_timeout`` (default 20 s) without any monitor contact.

With ``--state <target>`` exits 0 when ``current_role`` equals ``<target>``.
``--timeout N`` retries for up to ``N`` seconds.

.. code-block:: bash

   # Check that a partitioned primary has self-assigned demote_timeout
   docker compose exec -T node2 \
     pg_autoctl inspect fsm node-state --state demote_timeout --timeout 60

``pg_autoctl inspect monitor formation-states``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Runs on the **monitor** container.  Waits until the formation has at least
one node in each of the listed states simultaneously, then exits 0.

.. code-block:: bash

   docker compose exec -T monitor \
     pg_autoctl inspect monitor formation-states \
       --timeout 120 primary secondary

See also
--------

* :ref:`pg_autoctl_node` — the ``pg_autoctl node run`` command and node spec
  file format used by every container that ``pgaftest`` starts
* :ref:`pg_autoctl_create_postgres`
* :ref:`pg_autoctl_perform_failover`
