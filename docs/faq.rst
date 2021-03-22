Frequently Asked Questions
==========================

Those questions have been asked in `GitHub issues`__ for the project by
several people. If you have more questions, feel free to open a new issue,
and your question and its answer might make it to this FAQ.

__ https://github.com/citusdata/pg_auto_failover/issues_

The secondary is blocked in the CATCHING_UP state, what should I do?
--------------------------------------------------------------------

In the pg_auto_failover design, the following two things are needed for the
monitor to be able to orchestrate nodes integration completely:

 1. Health Checks must be successful

    The monitor runs periodic health checks with all the nodes registered
    in the system. Those *health checks* are Postgres connections from the
    monitor to the registered Postgres nodes, and use the ``hostname`` and
    ``port`` as registered.

    The ``pg_autoctl show state`` commands column *Reachable* contains
    "yes" when the monitor could connect to a specific node, "no" when this
    connection failed, and "unknown" when no connection has been attempted
    yet, since the last startup time of the monitor.

    The *Reachable* column from ``pg_autoctl show state`` command output
    must show a "yes" entry before a new standby node can be orchestrated
    up to the "secondary" goal state.

 2. pg_autoctl service must be running

    The pg_auto_failover monitor works by assigning goal states to
    individual Postgres nodes. The monitor will not assign a new goal state
    until the current one has been reached.

    To implement a transition from the current state to the goal state
    assigned by the monitor, the pg_autoctl service must be running on
    every node.

When your new standby node stays in the "catchingup" state for a long time,
please check that the node is reachable from the monitor given its
``hostname`` and ``port`` known on the monitor, and check that the
``pg_autoctl run`` command is running for this node.

When things are not obvious, the next step is to go read the logs. Both the
output of the ``pg_autoctl`` command and the Postgres logs are relevant. See
the :ref:`logs` question for details.

.. _logs:

Should I read the logs? Where are the logs?
-------------------------------------------

Yes. If anything seems strange to you, please do read the logs.

As maintainers of the ``pg_autoctl`` tool, we can't foresee everything that
may happen to your production environment. Still, a lot of efforts is spent
on having a meaningful output. So when you're in a situation that's hard to
understand, please make sure to read the ``pg_autoctl`` logs and the
Postgres logs.

When using systemd integration, the ``pg_autoctl`` logs are then handled
entirely by the journal facility of systemd. Please then refer to
``journalctl`` for viewing the logs.

The Postgres logs are to be found in the ``$PGDATA/log`` directory with the
default configuration deployed by ``pg_autoctl create ...``. When a custom
Postgres setup is used, please refer to your actual setup to find Postgres
logs.

The state of the system is blocked, what should I do?
-----------------------------------------------------

This question is a general case situation that is similar in nature to the
previous situation, reached when adding a new standby to a group of Postgres
nodes. Please check the same two elements: the monitor health checks are
successful, and the ``pg_autoctl run`` command is running.

When things are not obvious, the next step is to go read the logs. Both the
output of the ``pg_autoctl`` command and the Postgres logs are relevant. See
the :ref:`logs` question for details.

The monitor is a SPOF in pg_auto_failover design, how should we handle that?
----------------------------------------------------------------------------

When using pg_auto_failover, the monitor is needed to make decisions and
orchestrate changes in all the registered Postgres groups. Decisions are
transmitted to the Postgres nodes by the monitor assigning nodes a goal
state which is different from their current state.

Consequences of the monitor being unavailable
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Nodes contact the monitor each second and call the ``node_active`` stored
procedure, which returns a goal state that is possibly different from the
current state.

The monitor only assigns Postgres nodes with a new goal state when a cluster
wide operation is needed. In practice, only the following operations require
the monitor to assign a new goal state to a Postgres node:

 - a new node is registered
 - a failover needs to happen, either triggered automatically or manually
 - a node is being put to maintenance
 - a node replication setting is being changed.

When the monitor node is not available, the ``pg_autoctl`` processes on the
Postgres nodes will fail to contact the monitor every second, and log about
this failure. Adding to that, no orchestration is possible.

The Postgres streaming replication does not need the monitor to be available
in order to deliver its service guarantees to your application, so your
Postgres service is still available when the monitor is not available.

To repair your installation after having lost a monitor, the following
scenarios are to be considered.

The monitor node can be brought up again without data having been lost
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This is typically the case in Cloud Native environments such as Kubernetes,
where you could have a service migrated to another pod and re-attached to
its disk volume. This scenario is well supported by pg_auto_failover, and no
intervention is needed.

It is also possible to use synchronous archiving with the monitor so that
it's possible to recover from the current archives and continue operating
without intervention on the Postgres nodes, except for updating their monitor URI. This requires an archiving setup
that uses synchronous replication so that any transaction committed on the
monitor is known to have been replicated in your WAL archive.

At the moment, you have to take care of that setup yourself. Here's a quick
summary of what needs to be done:

  1. Schedule base backups

     Use ``pg_basebackup`` every once in a while to have a full copy of the
     monitor Postgres database available.

  2. Archive WAL files in a synchronous fashion

     Use ``pg_receivewal --sync ...`` as a service to keep a WAL archive in
     sync with the monitor Postgres instance at all time.

  3. Prepare a recovery tool on top of your archiving strategy

     Write a utility that knows how to create a new monitor node from your
     most recent pg_basebackup copy and the WAL files copy.

     Bonus points if that tool/script is tested at least once a day, so that
     you avoid surprises on the unfortunate day that you actually need to
     use it in production.

A future version of pg_auto_failover will include this facility, but the
current versions don't.

The monitor node can only be built from scratch again
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

If you don't have synchronous archiving for the monitor set-up, then you
might not be able to restore a monitor database with the expected up-to-date
node metadata. Specifically we need the nodes state to be in sync with what
each ``pg_autoctl`` process has received the last time they could contact
the monitor, before it has been unavailable.

It is possible to register nodes that are currently running to a new monitor
without restarting Postgres on the primary. For that, the procedure
mentionned in :ref:`replacing_monitor_online` must be followed, using the
following commands::

  $ pg_autoctl disable monitor
  $ pg_autoctl enable monitor
