Failover and Fault Tolerance
============================

At the heart of the pg_auto_failover implementation is a State Machine. The
state machine is driven by the monitor, and its transitions are implemented
in the keeper service, which then reports success to the monitor.

The keeper is allowed to retry transitions as many times as needed until
they succeed, and reports also failures to reach the assigned state to the
monitor node. The monitor also implements frequent health-checks targeting
the registered PostgreSQL nodes.

When the monitor detects something is not as expected, it takes action by
assigning a new goal state to the keeper, that is responsible for
implementing the transition to this new state, and then reporting.

Unhealthy Nodes
---------------

The pg_auto_failover monitor is responsible for running regular health-checks with
every PostgreSQL node it manages. A health-check is successful when it is
able to connect to the PostgreSQL node using the PostgreSQL protocol
(libpq), imitating the ``pg_isready`` command.

How frequent those health checks are (20s by default), the PostgreSQL
connection timeout in use (5s by default), and how many times to retry in
case of a failure before marking the node unhealthy (2 by default) are GUC
variables that you can set on the Monitor node itself. Remember, the monitor
is implemented as a PostgreSQL extension, so the setup is a set of
PostgreSQL configuration settings::

   SELECT name, setting
     FROM pg_settings
    WHERE name ~ 'pgautofailover\.health';
                   name                   | setting
 -----------------------------------------+---------
  pgautofailover.health_check_max_retries | 2
  pgautofailover.health_check_period      | 20000
  pgautofailover.health_check_retry_delay | 2000
  pgautofailover.health_check_timeout     | 5000
 (4 rows)

The pg_auto_failover keeper also reports if PostgreSQL is running as expected. This
is useful for situations where the PostgreSQL server / OS is running fine
and the keeper (``pg_autoctl run``) is still active, but PostgreSQL has failed.
Situations might include *File System is Full* on the WAL disk, some file
system level corruption, missing files, etc.

Here's what happens to your PostgreSQL service in case of any single-node
failure is observed:

  - Primary node is monitored unhealthy

    When the primary node is unhealthy, and only when the secondary node is
    itself in good health, then the primary node is asked to transition to
    the DRAINING state, and the attached secondary is asked to transition
    to the state PREPARE_PROMOTION. In this state, the secondary is asked to
    catch-up with the WAL traffic from the primary, and then report
    success.

    The monitor then continues orchestrating the promotion of the standby: it
    stops the primary (implementing STONITH in order to prevent any data
    loss), and promotes the secondary into being a primary now.

    Depending on the exact situation that triggered the primary unhealthy,
    it's possible that the secondary fails to catch-up with WAL from it, in
    that case after the PREPARE\_PROMOTION\_CATCHUP\_TIMEOUT the standby
    reports success anyway, and the failover sequence continues from the
    monitor.

  - Secondary node is monitored unhealthy

    When the secondary node is unhealthy, the monitor assigns to it the
    state CATCHINGUP, and assigns the state WAIT\_PRIMARY to the primary
    node. When implementing the transition from PRIMARY to WAIT\_PRIMARY,
    the keeper disables synchronous replication.

    When the keeper reports an acceptable WAL difference in the two nodes
    again, then the replication is upgraded back to being synchronous. While
    a secondary node is not in the SECONDARY state, secondary promotion is
    disabled.

  - Monitor node has failed

    Then the primary and secondary node just work as if you didn't have setup
    pg_auto_failover in the first place, as the keeper fails to report local state
    from the nodes. Also, health checks are not performed. It means that no
    automated failover may happen, even if needed.

.. _network_partitions:

Network Partitions
------------------

Adding to those simple situations, pg_auto_failover is also resilient to Network
Partitions. Here's the list of situation that have an impact to pg_auto_failover
behavior, and the actions taken to ensure High Availability of your
PostgreSQL service:

  - Primary can't connect to Monitor

    Then it could be that either the primary is alone on its side of a
    network split, or that the monitor has failed. The keeper decides
    depending on whether the secondary node is still connected to the
    replication slot, and if we have a secondary, continues to serve
    PostgreSQL queries.

    Otherwise, when the secondary isn't connected, and after the
    NETWORK\_PARTITION\_TIMEOUT has elapsed, the primary considers it might
    be alone in a network partition: that's a potential split brain situation
    and with only one way to prevent it. The primary stops, and reports a new
    state of DEMOTE\_TIMEOUT.

    The network\_partition\_timeout can be setup in the keeper's
    configuration and defaults to 20s.

  - Monitor can't connect to Primary

    Once all the retries have been done and the timeouts are elapsed, then
    the primary node is considered unhealthy, and the monitor begins the
    failover routine. This routine has several steps, each of them allows to
    control our expectations and step back if needed.

    For the failover to happen, the secondary node needs to be healthy and
    caught-up with the primary. Only if we timeout while waiting for the WAL
    delta to resorb (30s by default) then the secondary can be promoted with
    uncertainty about the data durability in the group.

  - Monitor can't connect to Secondary

    As soon as the secondary is considered unhealthy then the monitor
    changes the replication setting to asynchronous on the primary, by
    assigning it the WAIT\_PRIMARY state. Also the secondary is assigned the
    state CATCHINGUP, which means it can't be promoted in case of primary
    failure.

    As the monitor tracks the WAL delta between the two servers, and they
    both report it independently, the standby is eligible to promotion again
    as soon as it's caught-up with the primary again, and at this time it is
    assigned the SECONDARY state, and the replication will be switched back to
    synchronous.

Failure handling and network partition detection
------------------------------------------------

If a node cannot communicate to the monitor, either because the monitor is
down or because there is a problem with the network, it will simply remain
in the same state until the monitor comes back.

If there is a network partition, it might be that the monitor and secondary
can still communicate and the monitor decides to promote the secondary since
the primary is no longer responsive. Meanwhile, the primary is still
up-and-running on the other side of the network partition. If a primary
cannot communicate to the monitor it starts checking whether the secondary
is still connected. In PostgreSQL, the secondary connection automatically
times out after 30 seconds. If last contact with the monitor and the last
time a connection from the secondary was observed are both more than 30
seconds in the past, the primary concludes it is on the losing side of a
network partition and shuts itself down. It may be that the secondary and
the monitor were actually down and the primary was the only node that was
alive, but we currently do not have a way to distinguish such a situation.
As with consensus algorithms, availability can only be correctly preserved
if at least 2 out of 3 nodes are up.

In asymmetric network partitions, the primary might still be able to talk to
the secondary, while unable to talk to the monitor. During failover, the
monitor therefore assigns the secondary the `stop_replication` state, which
will cause it to disconnect from the primary. After that, the primary is
expected to shut down after at least 30 and at most 60 seconds. To factor in
worst-case scenarios, the monitor waits for 90 seconds before promoting the
secondary to become the new primary.
