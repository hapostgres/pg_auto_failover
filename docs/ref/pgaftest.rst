.. _pgaftest:

pgaftest
========

``pgaftest`` is the pg_auto_failover integration test runner.  It reads
``.pgaf`` spec files that describe a cluster topology and a set of test steps,
spins up the cluster using Docker Compose, and drives the steps to completion.

Two modes of operation are supported:

- **CI mode** (``pgaftest run``): headless TAP output, non-zero exit on failure.
- **Interactive mode** (``pgaftest setup``): cluster stays up, shell or tmux
  session opened for hands-on exploration.

.. contents::
   :local:
   :depth: 2


Synopsis
--------

::

   pgaftest run    [options] <spec.pgaf>
   pgaftest run    --schedule <file> [options]
   pgaftest setup  [options] <spec.pgaf> [--tmux]
   pgaftest step   <step-name>  [--work-dir <dir>]
   pgaftest show   <spec.pgaf>
   pgaftest prepare <spec.pgaf> [<output-dir>]
   pgaftest down   [--work-dir <dir>]


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
    Use ``pgaftest down`` to clean up manually.

.. _pgaftest_setup:

``pgaftest setup``
~~~~~~~~~~~~~~~~~~

Start a cluster interactively.  Docker Compose is started and the
``setup {}`` block runs.  Control is then handed back to you.

::

   pgaftest setup tests/tap/specs/basic_operation.pgaf
   pgaftest setup docs/tutorial/interactive_tutorial.pgaf --tmux

Options:

``--tmux``
    Open a three-pane tmux session immediately after the cluster is ready.
    The setup block runs in the bottom pane so you can watch the logs pane
    while the cluster initialises.  The three panes are:

    - **top** — ``docker compose logs -f`` (live container output)
    - **middle** — ``pg_autoctl watch`` on the monitor
    - **bottom** — interactive ``bash`` in the first data node (once setup
      completes)

``--work-dir <dir>``
    Working directory (default: ``$TMPDIR/pgaftest/<spec-name>``).

.. _pgaftest_step:

``pgaftest step``
~~~~~~~~~~~~~~~~~

Run a single named step against a cluster that was started with
``pgaftest setup``.

::

   pgaftest step stop_primary --work-dir /tmp/pgaftest/basic_operation

The step name must match a ``step <name> {}`` block in the spec.  The spec
file is read from ``<work-dir>/spec.pgaf``.

.. _pgaftest_down:

``pgaftest down``
~~~~~~~~~~~~~~~~~

Run the ``teardown {}`` block (if any) and then ``docker compose down
--volumes --remove-orphans``.

::

   pgaftest down --work-dir /tmp/pgaftest/basic_operation
   pgaftest down tests/tap/specs/basic_operation.pgaf

When called with only ``--work-dir``, the project name is derived from the
last path component of the working directory, matching the name that
``pgaftest setup`` used when creating the stack.

.. _pgaftest_show:

``pgaftest show``
~~~~~~~~~~~~~~~~~

Print the ``docker-compose.yml`` that would be generated from the spec,
without running anything.

::

   pgaftest show tests/tap/specs/basic_operation.pgaf

.. _pgaftest_prepare:

``pgaftest prepare``
~~~~~~~~~~~~~~~~~~~~

Write ``docker-compose.yml``, per-node ``.ini`` files, and a ``Makefile``
to an output directory for manual inspection or customisation.

::

   pgaftest prepare tests/tap/specs/basic_operation.pgaf ./my-cluster


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

The multi-node form ``wait until primary, secondary`` waits until at least
one node in each named state is present in the formation.

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

``expect`` matches the output of the immediately preceding ``sql`` command.
Fields are separated by whitespace; rows are wrapped in ``{ }``.

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
    Override the Docker image used for all node containers.  Useful in CI to
    avoid rebuilding the image on every run::

      PGAF_IMAGE=pgaf:run pgaftest run tests/tap/specs/basic_operation.pgaf

``PGAFTEST_COMPOSE_SERVICE``
    Name of the service inside which ``pgaftest`` itself runs when executing
    inside a container.  Used by the CI workflow to locate the binary.

``PGAFTEST_HOST_WORK_DIR``
    Host-side working directory when ``pgaftest`` runs inside a container.
    Bind-mounted so compose files written by the binary are visible to the
    Docker daemon on the host.

``TMPDIR``
    Base directory for auto-derived working directories
    (default: ``/tmp``).  Working directories are placed under
    ``$TMPDIR/pgaftest/<spec-name>``.


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


Examples
--------

**Run a single spec**::

   pgaftest run tests/tap/specs/basic_operation.pgaf

**Run the full schedule**::

   pgaftest run --schedule tests/tap/schedule

**Bring up an interactive cluster, watch it in tmux**::

   pgaftest setup docs/tutorial/interactive_tutorial.pgaf --tmux

**Run a named step against a live cluster**::

   pgaftest step stop_primary --work-dir /tmp/pgaftest/basic_operation

**Tear down a cluster started with** ``--no-cleanup`` **or** ``setup``::

   pgaftest down --work-dir /tmp/pgaftest/basic_operation

**Print the generated docker-compose.yml without running anything**::

   pgaftest show tests/tap/specs/basic_operation.pgaf


See also
--------

- :ref:`tutorial` — Docker Compose tutorial with ``pgaftest`` alternative
- :ref:`citus_quickstart` — Citus tutorial with ``pgaftest`` alternative
- :ref:`pg_autoctl_watch`
