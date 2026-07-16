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

.. list-table::
   :header-rows: 1
   :widths: 40 12 48

   * - Command
     - Where
     - Purpose
   * - ``pgaftest run <spec.pgaf>``
     - **Host**
     - CI mode: full lifecycle — up, setup, sequence, teardown, down.
       Emits TAP to stdout.
   * - ``pgaftest cluster setup <spec.pgaf>``
     - **Host**
     - Generate compose YAML, bring the stack up, run ``setup{}``, then
       return control for interactive use.
   * - ``pgaftest tmux <spec.pgaf>``
     - **Host**
     - Same as ``cluster setup`` but launches a tmux session: top pane
       ``pg_autoctl watch``, bottom pane interactive shell in the
       ``pgaftest`` container.
   * - ``pgaftest cluster prepare <spec.pgaf> [<dir>]``
     - **Host**
     - Write compose YAML and ``.ini`` files to a directory without
       starting anything.  Useful for inspecting generated config.
   * - ``pgaftest cluster down <spec.pgaf>``
     - **Host**
     - Run the ``teardown{}`` block then ``docker compose down --volumes``.
   * - ``pgaftest show compose <spec.pgaf>``
     - **Host**
     - Dry-run: render the generated ``docker-compose.yml`` to stdout
       without starting anything.
   * - ``pgaftest show spec <spec.pgaf>``
     - **Host**
     - Print the spec file source.
   * - ``pgaftest indent <spec.pgaf>``
     - **Host**
     - Parse and rewrite the spec with canonical indentation in place.
   * - ``pgaftest help``
     - **Host**
     - Print the full command tree.
   * - ``pgaftest step [<name>]``
     - **Container**
     - Run the next pending step (auto-advance) or a specific named step
       against the live stack.  Records progress in a state file so
       repeated bare ``pgaftest step`` calls walk the sequence forward,
       retrying a failed step before advancing.
   * - ``pgaftest show steps``
     - **Container**
     - List the sequence steps with progress markers: ``*`` = next to run,
       ``!`` = last failed (will retry on next ``pgaftest step``), space
       prefix = completed.
   * - ``pgaftest show step``
     - **Container**
     - Print the DSL commands that will run when ``pgaftest step`` is called
       next, so you can review what is about to happen.
   * - ``pgaftest show state``
     - **Container**
     - Print a ``Step X/N: name`` progress header followed by
       ``pg_autoctl show state`` output for the whole formation.
   * - ``pgaftest sql <node> { <query> }``
     - **Container**
     - Run a SQL query on a named node and print the result to stdout.
   * - ``pgaftest network disconnect <node>``
     - **Container**
     - Disconnect a node from its Compose network, simulating a network
       partition.
   * - ``pgaftest network connect <node>``
     - **Container**
     - Reconnect a previously disconnected node.
   * - ``pgaftest cluster down``
     - **Container**
     - Run ``teardown{}`` and ``docker compose down`` via DooD from inside
       the container.


Typical interactive session
----------------------------

Start the cluster on the host and drop into a tmux session::

   pgaftest tmux tests/tap/specs/basic_failover.pgaf

``pg_autoctl watch`` fills the top pane.  The bottom pane is an interactive
shell inside the ``pgaftest`` service container.  From there::

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

``pgaftest cluster setup`` and ``pgaftest tmux`` generate a Compose file that
includes a ``setup`` service alongside the cluster nodes.  That service:

* runs as **root** — the Docker daemon socket at ``/var/run/docker.sock`` is
  always accessible by root regardless of the host socket GID, which differs
  between macOS Docker Desktop and Linux CI runners;
* sets ``working_dir: /root`` (root's home directory) so that ``pgaftest``
  can read and write a local state file in a predictable, writable location
  without any extra volume mounts;
* mounts the spec file read-only at ``/root/spec.pgaf``;
* bind-mounts the host Docker socket (``/var/run/docker.sock``) so that
  ``docker compose exec`` calls issued from inside the container target the
  host's Docker daemon and reach the sibling cluster containers;
* pre-sets ``PGAFTEST_SPEC=/root/spec.pgaf``, ``PGAFTEST_HOST_WORK_DIR``, and
  ``PGAFTEST_IN_CONTAINER=1`` in its environment so all container commands
  discover the spec file and Compose project without extra arguments, and so
  the binary knows it is running inside the container (used to pick the
  correct state-file location and to prefer direct libpq connections over
  ``docker compose exec`` for monitor queries).

In ``tmux`` mode the service runs ``sleep infinity`` to stay alive; the setup
block is driven by a separate ``docker compose run`` invocation.
In CI mode (``pgaftest run``) the same ``docker compose run setup`` invocation
runs ``pgaftest _setup_`` to execute the ``setup {}`` block.

**Direct monitor connections.**  Where possible, ``pgaftest`` uses a direct
libpq connection to the monitor — via its LISTEN/NOTIFY connection that is
established as part of the wait-for-state machinery — rather than shelling out
to ``docker compose exec``.  This applies to:

* polling node state during ``wait until <node> state = <s>`` (both the
  fast-path check and the periodic 5-second fallback inside the LISTEN loop);
* ``sql monitor { ... }`` commands in step bodies;
* ``promote <node>`` — calls ``pgautofailover.perform_promotion()`` directly;
* ``perform failover`` — calls ``pgautofailover.perform_failover()`` directly;
* ``pgaftest show state`` — when running inside the container, invokes
  ``pg_autoctl show state`` directly (it is in PATH and
  ``PG_AUTOCTL_MONITOR`` is set in the environment).

Docker exec is still used for non-monitor services (node ``exec`` commands,
``sql node1 { ... }``, network disconnect/connect, compose lifecycle).


Step state file
---------------

Container commands record progress in ``/root/pgaftest.state``
(``$HOME/pgaftest.state`` inside the setup container), a small JSON file
that tracks:

* ``current`` — index of the next step to run in the sequence;
* ``last_step`` — name of the most recently executed step;
* ``last_ok`` — whether that step succeeded.

On success ``current`` advances; on failure it stays pointing at the failed
step so the next bare ``pgaftest step`` retries it rather than skipping
ahead.

The file lives in ``/root`` (root's home directory inside the setup container)
rather than in the host-side bind-mounted work directory, so it is always
writable without any dependency on bind-mount ownership mapping.  It persists
for the lifetime of the container.

When ``pgaftest step`` is run on the host (outside the container), the state
file is written to ``$TMPDIR/pgaftest/<spec-name>/pgaftest.state`` alongside
the generated compose files.


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

The session has two panes:

- **top** — ``pg_autoctl watch`` on the monitor (live FSM state table)
- **bottom** — interactive shell in the ``pgaftest`` service container

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

   # Targeted switchover: the named node negotiates with the monitor and
   # hands off primary to the best available secondary.
   exec <node>  pg_autoctl perform switchover

   # Untargeted failover via the monitor SQL API: the monitor picks the
   # best secondary.  Formation and group default to "default" and 0.
   perform failover
   perform failover group <N>
   perform failover in formation <name>
   perform failover in formation <name> group <N>

   # Legacy targeted promotion via the monitor SQL API:
   promote <node> [, <node2>, ...]

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

In ``run`` mode, pgaftest produces `TAP version 13`__ output on standard
output.  Each named step in ``sequence`` becomes one test point::

  TAP version 13
  1..4
  ok 1 - stop_primary
  ok 2 - check_failover
  ok 3 - restart_node1
  ok 4 - verify_replication

On failure::

  not ok 2 - check_failover
  # DIAG: wait until node2 state is primary: timed out after 90s

__ https://testanything.org/tap-version-13-specification.html


See also
--------

- :ref:`tutorial` — Docker Compose tutorial with ``pgaftest`` alternative
- :ref:`citus_quickstart` — Citus tutorial with ``pgaftest`` alternative
- :ref:`pg_autoctl_watch`
