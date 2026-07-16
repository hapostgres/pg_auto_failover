.. _pgaftest:

pgaftest
========

``pgaftest`` is the pg_auto_failover integration test runner.  It reads
``.pgaf`` spec files that describe a cluster topology and a set of test steps,
spins up the cluster using Docker Compose, and drives the steps to completion.

Two modes of operation are supported:

- **CI mode** (``pgaftest run``): headless TAP output, non-zero exit on failure.
- **Interactive mode** (``pgaftest cluster setup`` / ``pgaftest tmux``): cluster
  stays up, shell or tmux session opened for hands-on exploration.

.. contents::
   :local:
   :depth: 2


Host commands vs. container commands
--------------------------------------

``pgaftest`` commands fall into two groups depending on where they must be
run.

**Host commands** orchestrate Docker Compose itself — they generate YAML,
bring stacks up and down, or operate on local spec files.  Run these on the
machine that owns the Docker socket (developer workstation or CI runner).

**Container commands** operate against an already-running stack from inside
the ``pgaftest`` service container, which has the Docker CLI and the host
Docker socket bind-mounted (Docker-out-of-Docker, DooD).  These are the
commands you type in the interactive shell that ``pgaftest tmux`` drops you
into.

The compose stack layout and the relationship between host and container
commands is shown below:

.. code-block:: text

   Host (developer workstation / CI runner)
   ├── Docker daemon
   │   └── Compose project: <spec-name>
   │       ├── monitor    (pgautofailover monitor, port 5432)
   │       ├── node1      (pg_autoctl run, primary)
   │       ├── node2      (pg_autoctl run, secondary)
   │       └── setup      (pgaftest binary, /var/run/docker.sock bind-mounted)
   │           ↑ interactive shell lives here (pgaftest step, show, sql, network)
   └── pgaftest tmux     ← runs here, on the host
       └── tmux session
           ├── top pane:    docker compose logs -f
           ├── middle pane: pg_autoctl watch (live FSM state table)
           └── bottom pane: bash inside the setup container
                            (pgaftest step/show/sql/network available,
                             PGAFTEST_SPEC set automatically)

**On the host** (developer workstation or CI runner):

.. code-block:: text

   pgaftest run <spec.pgaf>               CI mode: up → setup → sequence → teardown → down, TAP output
   pgaftest run --schedule <file>         Run every spec listed in the schedule file
   pgaftest tmux <spec.pgaf>             Bring up the stack and open a 3-pane tmux session
   pgaftest cluster setup <spec.pgaf>    Bring up the stack without opening tmux
   pgaftest cluster prepare <spec.pgaf>  Write compose YAML and .ini files without starting anything
   pgaftest cluster down [<spec.pgaf>]   Run teardown{} and docker compose down --volumes
   pgaftest show compose <spec.pgaf>     Print the generated docker-compose.yml (dry run)
   pgaftest show spec <spec.pgaf>        Print the spec file source
   pgaftest indent <spec.pgaf>           Parse and rewrite with canonical indentation
   pgaftest help                         Print the full command tree

**Inside the container** (bottom pane of the tmux session):

.. code-block:: text

   pgaftest step                         Run the next pending step (auto-advance)
   pgaftest step <name>                  Run a specific named step
   pgaftest show steps                   List sequence steps with progress markers (* next, ! failed)
   pgaftest show step                    Preview the DSL commands the next step will execute
   pgaftest show state                   Step X/N header + pg_autoctl show state output
   pgaftest sql <node> { <query> }       Run SQL on a named node and print the result
   pgaftest network disconnect <node>    Sever a node's network connection (simulate partition)
   pgaftest network connect <node>       Restore a node's network connection
   pgaftest cluster down                 Run teardown{} and docker compose down


Typical interactive session
----------------------------

Start the cluster on the host and drop into a tmux session::

   pgaftest tmux tests/tap/specs/basic_operation.pgaf

The top pane streams ``docker compose logs -f``.  The middle pane runs
``pg_autoctl watch``.  The bottom pane is an interactive shell inside the
``pgaftest`` service container.  From there::

   # See which steps are available and where you are
   pgaftest show steps

   # Preview what the next step will do
   pgaftest show step

   # Check cluster state
   pgaftest show state

   # Run the next step (auto-advance)
   pgaftest step

   # Or run a specific step by name
   pgaftest step stop_primary

   # Inject a network partition manually
   pgaftest network disconnect node1

   # Check the cluster reacted correctly
   pgaftest show state

   # Restore the network and continue
   pgaftest network connect node1
   pgaftest step

   # Tear down when done
   pgaftest cluster down

The ``pg_autoctl watch`` output in the top tmux pane updates in real time
throughout this session.


Docker-out-of-Docker (DooD) architecture
-----------------------------------------

The ``setup`` service container runs with the host Docker socket bind-mounted
so it can reach sibling containers via ``docker compose exec``.  Direct libpq
connections are used for monitor queries where possible, falling back to
``docker exec`` for everything else.

For implementation details — how the state file works, which operations use
direct libpq vs. docker exec, environment variables, and how to build
``pgaftest`` — see ``src/bin/pgaftest/README.md`` in the source tree.


Synopsis
--------

::

   pgaftest run             [options] <spec.pgaf>
   pgaftest run             --schedule <file> [options]
   pgaftest tmux            [options] <spec.pgaf>
   pgaftest cluster setup   [options] <spec.pgaf>
   pgaftest cluster prepare <spec.pgaf> [<output-dir>]
   pgaftest cluster down    [<spec.pgaf>] [--work-dir <dir>]
   pgaftest step            [<step-name>] [--work-dir <dir>]
   pgaftest show            compose|spec|steps|step|state [<spec.pgaf>]
   pgaftest sql             <node> { <query> }
   pgaftest network         disconnect|connect <node>
   pgaftest indent          <spec.pgaf>
   pgaftest help


Sub-commands
------------

.. _pgaftest_run:

``pgaftest run``
~~~~~~~~~~~~~~~~

Run a spec in CI mode.  Docker Compose is started, the ``setup {}`` block
runs, each step in ``sequence`` runs in order, ``teardown {}`` always runs,
and the stack is removed.  Output is TAP: ``ok N - step_name`` / ``not ok``.

::

   pgaftest run tests/tap/specs/basic_operation.pgaf

Options:

``--schedule <file>``
    Run every spec listed in the file (one name or path per line).

``--expected <dir>``
    Compare TAP output against ``.out`` files in the directory.

``--work-dir <dir>``
    Working directory for compose files and state (default:
    ``$TMPDIR/pgaftest/<spec-name>``).

``--no-cleanup``
    Leave the compose stack running after the run for post-mortem inspection.
    Use ``pgaftest cluster down`` to clean up manually.

.. _pgaftest_tmux:

``pgaftest tmux``
~~~~~~~~~~~~~~~~~

Generate compose YAML, bring the stack up, run ``setup {}``, then launch a
tmux session for interactive exploration::

   pgaftest tmux tests/tap/specs/basic_operation.pgaf

The session has three panes:

- **top** — ``docker compose logs -f`` (streaming container output)
- **middle** — ``pg_autoctl watch`` (live FSM state table)
- **bottom** — interactive shell inside the ``pgaftest`` service container,
  with ``PGAFTEST_SPEC`` set so all ``pgaftest`` commands find the spec
  automatically

Options:

``--work-dir <dir>``
    Working directory (default: ``$TMPDIR/pgaftest/<spec-name>``).

.. _pgaftest_cluster:

``pgaftest cluster``
~~~~~~~~~~~~~~~~~~~~

Sub-commands for managing the Docker Compose stack lifecycle.

``pgaftest cluster setup [options] <spec.pgaf>``
    Generate compose YAML, bring the stack up, run ``setup {}``, then return
    control for interactive use (no tmux window).  Use ``pgaftest tmux`` for
    the tmux variant.

    Options:

    ``--work-dir <dir>``
        Working directory (default: ``$TMPDIR/pgaftest/<spec-name>``).

``pgaftest cluster prepare <spec.pgaf> [<output-dir>]``
    Write ``docker-compose.yml``, per-node ``.ini`` files, and a ``Makefile``
    to an output directory for manual inspection or customisation without
    starting anything::

       pgaftest cluster prepare tests/tap/specs/basic_operation.pgaf ./my-cluster

``pgaftest cluster down [<spec.pgaf>] [--work-dir <dir>]``
    Run the ``teardown {}`` block (if any) and then ``docker compose down
    --volumes --remove-orphans``::

       pgaftest cluster down tests/tap/specs/basic_operation.pgaf
       pgaftest cluster down --work-dir /tmp/pgaftest/basic_operation

.. _pgaftest_step:

``pgaftest step``
~~~~~~~~~~~~~~~~~

Run a step against a live cluster (container command)::

   # Run the next pending step (auto-advance using the state file)
   pgaftest step

   # Run a specific step by name
   pgaftest step stop_primary

When called without a step name, ``pgaftest step`` reads the state file to
find the next step to run, or retries the last step if it failed.  On success
the cursor advances; on failure it stays so the next invocation retries.

Inside the ``pgaftest`` container the spec file and work directory are
resolved automatically from the ``PGAFTEST_SPEC`` and
``PGAFTEST_HOST_WORK_DIR`` environment variables.

.. _pgaftest_show:

``pgaftest show``
~~~~~~~~~~~~~~~~~

Sub-commands for inspecting a spec or a running session:

``pgaftest show compose [<spec.pgaf>]``
    Render the generated ``docker-compose.yml`` to stdout without starting
    anything.

``pgaftest show spec [<spec.pgaf>]``
    Print the spec file source.

``pgaftest show steps [<spec.pgaf>]``
    List all steps in the sequence with progress markers (container command)::

        * test_001_verify_sync_replication    ← next to run
          test_002_cut_replication_link       ← completed
        ! test_003_primary_becomes_wait_primary ← failed, will retry

``pgaftest show step [<spec.pgaf>]``
    Print the DSL commands that will run on the next ``pgaftest step``
    invocation, so you can review what is about to happen (container command).

``pgaftest show state [<spec.pgaf>]``
    Print a ``Step X/N: name`` progress header followed by
    ``pg_autoctl show state`` output for the whole formation (container
    command).


Spec file format
----------------

A ``.pgaf`` spec file has four top-level sections.  Only ``cluster {}`` is
required; the others are optional.

.. code-block:: text

   # comments start with #

   cluster {
       ...             # topology declaration
   }

   setup {
       ...             # commands to run after compose up
   }

   teardown {
       ...             # commands to run on cleanup
   }

   step <name> {
       ...             # named test step
   }

   sequence
       step1
       step2
       ...

The ``cluster {}`` block
~~~~~~~~~~~~~~~~~~~~~~~~

Declares the Docker Compose topology.  Every cluster must have at least one
formation with at least one node.

.. code-block:: text

   cluster {
       monitor                       # required for multi-node HA
       image "pg_auto_failover:pg17" # optional; default: build from source

       ssl   self-signed             # self-signed | cert | off (default: self-signed)
       auth  trust                   # trust | md5 | scram     (default: trust)

       formation {                   # optional name; default name: "default"
           node1
           node2
           node3  async  candidate-priority 0
       }

       # Citus: multiple formations
       formation workers num-sync 1 {
           w1  worker  group 1
           w2  worker  group 1
       }
   }

Node modifiers:

============================================  =============================================
``async``                                     Mark as async standby (no sync quorum)
``candidate-priority <N>``                    Failover priority 0–100 (default: 50)
``launch deferred``                           Container starts with ``sleep infinity``;
                                              use ``exec node  pg_autoctl node start``
``coordinator`` / ``worker group <N>``        Citus role
``no-monitor``                                Standalone node (no monitor)
``listen``                                    Bind all interfaces (``--listen 0.0.0.0``)
``auth <method>``                             Per-node auth override
``ssl <mode>``                                Per-node SSL override
``volume <name> <path>``                      Mount a named Docker volume at ``<path>``
============================================  =============================================

Commands inside ``setup``, ``teardown``, and ``step`` blocks
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**State waits**

.. code-block:: text

   wait until <node> state is <state>  [timeout <N>s]
   wait until primary, secondary       [timeout <N>s]
   wait until <node> stopped           [timeout <N>s]

**Assertions**

.. code-block:: text

   assert <node> state is <state>
   assert <node> assigned-state is <state>
   assert <node> stays <state> while {
       # commands that must not trigger a transition
   }

**Execute commands in a container**

.. code-block:: text

   exec        <service>  <command...>   # must exit 0
   exec-fails  <service>  <command...>   # must exit non-zero

**SQL**

.. code-block:: text

   sql <service> { SELECT ... }
   expect { <expected output> }
   expect error [<SQLSTATE>]

**Network**

.. code-block:: text

   network disconnect <node>
   network connect    <node>

**Compose lifecycle**

.. code-block:: text

   compose stop  <service>
   compose start <service>
   compose kill  <service>
   compose down

**PostgreSQL control**

.. code-block:: text

   stop postgres  <node>
   start postgres <node>

**Failover**

.. code-block:: text

   # Promote a specific node to primary via the monitor SQL API.
   # The monitor picks the transition path; all other nodes adjust.
   promote <node> [, <node2>, ...]

   # Untargeted failover via the monitor SQL API: the monitor picks the
   # best secondary.  Formation and group default to "default" and 0.
   perform failover
   perform failover group <N>
   perform failover in formation <name>
   perform failover in formation <name> group <N>

   # Targeted switchover: ask a node to hand off its primary role.
   # This triggers a graceful transition FROM that node TO another.
   # Use when you want the named node to STOP being primary.
   exec <node>  pg_autoctl perform switchover

**Monitor targeting** (replace-monitor tests)

.. code-block:: text

   set monitor <service>

**Log grep**

.. code-block:: text

   logs <service> [not] <pattern>

**Sleep**

.. code-block:: text

   sleep <N>s


Environment variables
---------------------

``PGAF_IMAGE``
    Override the Docker image used for cluster node containers.  Defaults to
    ``pg_auto_failover:pg<version>`` — the image produced by ``make build``
    (or ``make build-pg17``, etc.).  Set this when using a custom tag::

      PGAF_IMAGE=myregistry/pgaf:pg17 pgaftest run tests/tap/specs/basic_operation.pgaf

``PGAFTEST_IMAGE``
    Override the Docker image used for the ``setup`` service container.
    Defaults to ``pgaf:pgaftest`` — the image produced by
    ``make build-pgaftest``.

``PGAF_BUILD``
    When set to any non-empty value, the generated ``docker-compose.yml``
    emits inline ``build:`` stanzas (``target: run`` / ``target: pgaftest``)
    instead of ``image:`` references.  Use this when iterating on the
    Dockerfile without tagging an image::

      PGAF_BUILD=1 pgaftest tmux tests/tap/specs/basic_operation.pgaf

``PGAFTEST_SPEC``
    Path to the spec file inside the container.  Set automatically by
    ``pgaftest setup`` in the generated Compose environment; allows all
    container commands to find the spec without an explicit argument.

``PGAFTEST_IN_CONTAINER``
    Set to ``"1"`` inside the pgaftest service container.  Used to detect
    whether the binary is running in the container vs. the host.

``PGAFTEST_HOST_WORK_DIR``
    Host-side working directory, set automatically in the container
    environment.  Used by container commands to locate the compose project
    (``docker-compose.yml``, node ``*.ini`` files, SSL certificates).

``TMPDIR``
    Base directory for auto-derived working directories (default: ``/tmp``).
    Working directories are placed under ``$TMPDIR/pgaftest/<spec-name>``.


TAP output
----------

In ``run`` mode, pgaftest produces TAP__ output on standard output.  Each
named step in ``sequence`` becomes one test point, with elapsed time::

  # multi_standbys.pgaf
  ok    1        - test_002_candidate_priority                       1227 ms
  ok    2        - test_003_replication_quorum                       1624 ms
  ok    3        - test_004_001_add_three_standbys                   1011 ms
  ok    4        - test_004_002_add_three_standbys                   1019 ms
  ok    5        - test_004_003_add_three_standbys                   1014 ms
  ok    6        - test_005_number_sync_standbys                     1445 ms
  ok    7        - test_006_number_sync_standbys_trigger             7176 ms
  ok    8        - test_007_create_t1                                 552 ms
  ok    9        - test_008_set_candidate_priorities                  670 ms
  ok    10       - test_009_failover                                 3406 ms
  ok    11       - test_010_read_from_nodes                           707 ms
  ok    12       - test_011_write_into_new_primary                    402 ms
  ok    13       - test_012_fail_primary                            54196 ms
  ok    14       - test_013_restart_node2                            3648 ms
  ok    15       - test_014_001_fail_set_properties                   764 ms
  ok    16       - test_014_002_fail_two_standby_nodes               3306 ms
  ok    17       - test_014_003_unblock_writes                        417 ms
  ok    18       - test_014_004_restart_nodes                        1116 ms
  ok    19       - test_015_002_fail_two_standby_nodes              20471 ms
  ok    20       - test_015_003_set_properties                        364 ms
  ok    21       - test_015_004_restart_nodes                        3852 ms
  ok    22       - test_016_001_set_candidate_priorities_to_zero      700 ms
  ok    23       - test_016_002_trigger_failover                     2765 ms
  ok    24       - test_016_003_set_candidate_priority_to_one        3818 ms
  ok    25       - test_016_004_reset_candidate_priority             1231 ms
  ok    26       - test_016_005_perform_promotion                    3119 ms
  ok    27       - test_017_remove_old_primary                        998 ms
  1..27
  # All 27 tests passed.

On failure::

  # guard_data_loss.pgaf
  ok    1        - test_001_kill_primary_and_one_standby    22427 ms
  ok    2        - test_002_verify_stuck                    10063 ms
  not ok3        - test_003_allow_data_loss_failover        90935 ms
  1..3
  # 1 test failed.

__ https://testanything.org/tap-specification.html


See also
--------

- :ref:`tutorial` — Docker Compose tutorial with ``pgaftest`` alternative
- :ref:`citus_quickstart` — Citus tutorial with ``pgaftest`` alternative
- :ref:`pg_autoctl_watch`
