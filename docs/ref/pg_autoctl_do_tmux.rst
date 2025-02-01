.. _pg_autoctl_do_tmux:

pg_autoctl do tmux
==================

pg_autoctl do tmux - Set of facilities to handle tmux interactive sessions

Synopsis
--------

pg_autoctl do tmux provides the following commands::

  pg_autoctl do tmux
  + compose  Set of facilities to handle docker compose sessions
    script   Produce a tmux script for a demo or a test case (debug only)
    session  Run a tmux session for a demo or a test case
    stop     Stop pg_autoctl processes that belong to a tmux session
    wait     Wait until a given node has been registered on the monitor
    clean    Clean-up a tmux session processes and root dir

  pg_autoctl do tmux compose
    config   Produce a docker compose configuration file for a demo
    script   Produce a tmux script for a demo or a test case (debug only)
    session  Run a tmux session for a demo or a test case


Description
-----------

An easy way to get started with pg_auto_failover in a localhost only
formation with three nodes is to run the following command::

  $ PG_AUTOCTL_DEBUG=1 pg_autoctl do tmux session \
       --root /tmp/pgaf    \
       --first-pgport 9000 \
       --nodes 4           \
       --layout tiled

This requires the command ``tmux`` to be available in your PATH. The
``pg_autoctl do tmux session`` commands prepares a self-contained root
directory where to create pg_auto_failover nodes and their configuration,
then prepares a tmux script, and then runs the script with a command such as::

  /usr/local/bin/tmux -v start-server ; source-file /tmp/pgaf/script-9000.tmux

The tmux session contains a single tmux window multiple panes:

 - one pane for the monitor
 - one pane per Postgres nodes, here 4 of them
 - one pane for running ``pg_autoctl watch``
 - one extra pane for an interactive shell.

Usually the first two commands to run in the interactive shell, once the
formation is stable (one node is primary, the other ones are all secondary),
are the following::

  $ pg_autoctl get formation settings
  $ pg_autoctl perform failover

Using docker compose to run a distributed system
------------------------------------------------

The same idea can also be implemented with docker compose to run the nodes,
and still using tmux to have three control panels this time:

 - one pane for the docker compose cumulative logs of all the nodes
 - one pane for running ``pg_autoctl watch``
 - one extra pane for an interactive shell.

For this setup, you can use the following command::

  PG_AUTOCTL_DEBUG=1 pg_autoctl do tmux compose session \
         --root ./tmux/citus           \
         --first-pgport 5600           \
         --nodes 3                     \
         --async-nodes 0               \
         --node-priorities 50,50,0     \
         --sync-standbys -1            \
         --citus-workers 4             \
         --citus-secondaries 0         \
         --citus                       \
         --layout even-vertical

The ``pg_autoctl do tmux compose session`` command also takes care of
creating external docker volumes and referencing them for each node in the
docker compose file.

.. _pg_autoctl_do_tmux_session:

pg_autoctl do tmux session
--------------------------

This command runs a tmux session for a demo or a test case.

::

   usage: pg_autoctl do tmux session [option ...]

     --root              path where to create a cluster
     --first-pgport      first Postgres port to use (5500)
     --nodes             number of Postgres nodes to create (2)
     --async-nodes       number of async nodes within nodes (0)
     --node-priorities   list of nodes priorities (50)
     --sync-standbys     number-sync-standbys to set (0 or 1)
     --skip-pg-hba       use --skip-pg-hba when creating nodes
     --citus             start a Citus formation
     --citus-workers     number of Citus workers to create (2)
     --citus-secondaries number of Citus secondaries to create (0)
     --layout            tmux layout to use (even-vertical)
     --binpath           path to the pg_autoctl binary (current binary path)


.. _pg_autoctl_do_tmux_compose_session:

pg_autoctl do tmux compose session
----------------------------------

This command runs a tmux session for a demo or a test case. It generates a
docker compose file and then uses docker compose to drive many nodes.

::

   usage: pg_autoctl do tmux compose session [option ...]

     --root            path where to create a cluster
     --first-pgport    first Postgres port to use (5500)
     --nodes           number of Postgres nodes to create (2)
     --async-nodes     number of async nodes within nodes (0)
     --node-priorities list of nodes priorities (50)
     --sync-standbys   number-sync-standbys to set (0 or 1)
     --skip-pg-hba     use --skip-pg-hba when creating nodes
     --layout          tmux layout to use (even-vertical)
     --binpath         path to the pg_autoctl binary (current binary path)
