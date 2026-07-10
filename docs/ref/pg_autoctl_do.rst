.. _pg_autoctl_do:

pg_autoctl do
=============

pg_autoctl do - Internal QA tooling (tmux sessions, demo app)

The ``pg_autoctl do`` command group contains development and QA tooling that
is not intended for production use. For read-only diagnostics see
:ref:`pg_autoctl_inspect`; for manual cluster recovery operations see
:ref:`pg_autoctl_manual`.

.. toctree::
   :maxdepth: 1

   pg_autoctl_do_tmux
   pg_autoctl_do_demo

``pg_autoctl do`` provides the following commands::

    pg_autoctl do
    + tmux  Set of facilities to handle tmux interactive sessions
    + demo  Use a demo application for pg_auto_failover

    pg_autoctl do tmux
      script   Produce a tmux script for a demo or a test case (debug only)
      session  Run a tmux session for a demo or a test case
      stop     Stop pg_autoctl processes that belong to a tmux session
      wait     Wait until a given node has been registered on the monitor
      clean    Clean-up a tmux session processes and root dir

    pg_autoctl do demo
      run      Run the pg_auto_failover demo application
      uri      Grab the application connection string from the monitor
      ping     Attempt to connect to the application URI
      summary  Display a summary of the previous demo app run
