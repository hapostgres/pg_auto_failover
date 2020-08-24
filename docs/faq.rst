Frequently Asked Questions
==========================

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

The state of the system is blocked, what should I do?
-----------------------------------------------------

This question is a general case situation that is similar in nature to the
previous situation, reached when adding a new standby to a group of Postgres
nodes. Please check the same two elements: the monitor health cheks are
successful, and the ``pg_autoctl run`` command is running.

The monitor is a SPOF in pg_auto_failover design, how should we handle that?
----------------------------------------------------------------------------

In the current pg_auto_failover design, there can be a single monitor node.
That is a *Single Point of Failure*. When the monitor node is not available,
there can be no state change in the system, which means that no failover can
happen.

Losing a monitor node when the system is stable has no availability impact,
as the Postgres streaming replication setup does not depend on the monitor
to operate normally. If after losing the monitor node then another Postgres
node becomes unavailable, then a failover will not happen as expected.

So while the impact of losing a monitor node is limited, we still have a
SPOF in current pg_auto_failover architecture. In the current version of
things it is expected that a backup and recovery mechanism (PITR) be
deployed alongside pg_auto_failover, and this should include the monitor.

We have several ideas how to best address this situation in a future version
of pg_auto_failover:

 - Resume operations on a new monitor.

   At the moment, it's only possible to register new nodes on a Postgres
   monitor when they are in very specific states: unknown, single, or
   wait_standby.

   We can implement a new protocol that allows registration of existing
   nodes with their current state, whatever that is. Given such a protocol,
   it would then be possible to replace the failed monitor by a new empty
   instance, and have node register themselves again in their current
   state, and the monitor would then be able to resume operations as
   intended, with a minimal downtime.

 - Integrate Disaster Recovery capabilities for the monitor.

   We are also thinking of integrating some Disaster Recovery facilities in
   pg_auto_failover.

   In most production setups, the PITR settings must be edited when a
   failover occurs and a new primary node is elected, so pg_auto_failover
   could integrate the necessary steps here, for instance.

   With PITR solution in the scope of pg_auto_failover we can automate the
   maintenance of a Disaster Recovery capability for the monitor itself.
   Even with a manual procedure to replace a failed monitor, we would have
   a much better answer to the current SPOF our design.

 - Implement a secondary monitor with manual failover.

   In addition to managing a PITR and Disaster Recovery solution for the
   monitor node, we could also integrate the management of a secondary node
   for the monitor.

   Because the question of “who monitors the monitor?” is recursive in
   nature, we would implement a manual switchover capability to the monitor
   node. Again, that would be an improvement over the current situation.

 - Design a distributed monitor system.

   Finally, a distributed decision making architecture is in being studied
   too. This would mean that 3 (or 5) monitor nodes are needed at all time,
   and those nodes would use the RAFT protocol, or the PAXOS protocol, to
   implement distributed consensus and membership management.

   This solution introduces non-trivial complexities to the design of
   pg_auto_failover, which is meant to be both simple and robust.
