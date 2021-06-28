.. _pg_autoctl_do_tmux:

pg_autoctl do tmux
==================

pg_autoctl do tmux - Set of facilities to handle tmux interactive sessions

Synopsis
--------

pg_autoctl do tmux provides the following commands::

   pg_autoctl do tmux
    script   Produce a tmux script for a demo or a test case (debug only)
    session  Run a tmux session for a demo or a test case
    stop     Stop pg_autoctl processes that belong to a tmux session
    wait     Wait until a given node has been registered on the monitor
    clean    Clean-up a tmux session processes and root dir


Description
-----------

An easy way to get started with pg_auto_failover in a localhost only
formation with three nodes is to run the following command::

  $ PG_AUTOCTL_DEBUG=1 pg_autoctl do tmux session \
       --root /tmp/pgaf \
	   --first-pgport 9000 \
	   --nodes 4 \
	   --layout tiled

This requires the command ``tmux`` to be available in your PATH. The
``pg_autoctl do tmux session`` commands prepares a self-contained root
directory where to create pg_auto_failover nodes and their configuration,
then prepares a tmux script, and then runs the script with a command such as::

  /usr/local/bin/tmux -v start-server ; source-file /tmp/pgaf/script-9000.tmux

The tmux session contains a single tmux window multiple panes:

 - one pane for the monitor
 - one pane per Postgres nodes, here 4 of them
 - one pane for running ``watch pg_autoctl show state``
 - one extra pane for an interactive shell.

Usually the first two commands to run in the interactive shell, once the
formation is stable (one node is primary, the other ones are all secondary),
are the following::

  $ pg_autoctl get formation settings
  $ pg_autoctl perform failover
