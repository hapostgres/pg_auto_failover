-- Copyright (c) Microsoft Corporation. All rights reserved.
-- Licensed under the PostgreSQL License.

-- This only tests that names are assigned properly

\x on

-- create a citus formation
select *
  from pgautofailover.create_formation('citus', 'citus', 'citus', true, 0);

-- register the first coordinator
select *
  from pgautofailover.register_node('citus', 'localhost', 9876,
                                    dbname => 'citus',
                                    desired_group_id => 0,
                                    node_kind => 'coordinator');

select *
  from pgautofailover.set_node_system_identifier(4, 6862008014275870855);

-- coordinator_1 reports single
select *
  from pgautofailover.node_active('citus', 4, 0,
                                  current_group_role => 'single');

-- register first worker
select *
  from pgautofailover.register_node('citus', 'localhost', 9878,
                                    dbname => 'citus',
                                    desired_group_id => 1,
                                    node_kind => 'worker');

-- event summary: which MonitorFSM[] rule (if any) produced each of this
-- test's own state-change events. Exercises pgautofailover.last_events()
-- against a real scenario -- its own SELECT list didn't match
-- pgautofailover.event's column set for a long time, breaking it outright,
-- and nothing in this suite ever called it to notice (see monitor.sql's
-- own minimal-repro coverage). eventid/eventtime omitted: eventid is a
-- database-wide sequence shared by every test in this schedule (see
-- regress_schedule's own comment) and eventtime is a live timestamp --
-- neither is a stable value to pin in this file's own expected output.
select reportedstate, goalstate, rule_pos, rule_section, description
  from pgautofailover.last_events('citus', count => 100);
