-- Copyright (c) Microsoft Corporation. All rights reserved.
-- Licensed under the PostgreSQL License.

\x on

select *
  from pgautofailover.register_node('default', 'localhost', 9876, 'postgres');

select *
  from pgautofailover.set_node_system_identifier(1, 6852685710417058800);

-- node_1 reports single
select *
  from pgautofailover.node_active('default', 1, 0,
                                  current_group_role => 'single');

-- register node_2
select *
  from pgautofailover.register_node('default', 'localhost', 9877, 'postgres');

-- node_2 reports wait_standby already
select *
  from pgautofailover.node_active('default', 2, 0,
                                  current_group_role => 'wait_standby');

-- node_1 reports single again, and gets assigned wait_primary
select *
  from pgautofailover.node_active('default', 1, 0,
                                  current_group_role => 'single');

-- node_1 now reports wait_primary
select *
  from pgautofailover.node_active('default', 1, 0,
                                  current_group_role => 'wait_primary');

-- node_2 now reports wait_standby, gets assigned catchingup
select *
  from pgautofailover.node_active('default', 2, 0,
                                  current_group_role => 'wait_standby');

-- attempt to register node_3 now fails because node_1 is still in wait_primary
select *
  from pgautofailover.register_node('default', 'localhost', 9879, 'postgres');

select formationid, nodename, goalstate, reportedstate
  from pgautofailover.node;

table pgautofailover.formation;

-- dump the pgautofailover.node table, omitting the timely columns
select formationid, nodeid, groupid, nodehost, nodeport,
       goalstate, reportedstate, reportedpgisrunning, reportedrepstate
  from pgautofailover.node;

select * from pgautofailover.get_primary('unknown formation');
select * from pgautofailover.get_primary(group_id => -10);
select * from pgautofailover.get_primary();

select * from pgautofailover.get_primary('default', 0);
select * from pgautofailover.get_other_nodes(1);

select pgautofailover.remove_node(1);

table pgautofailover.formation;

-- dump the pgautofailover.node table, omitting the timely columns
select formationid, nodeid, groupid, nodehost, nodeport,
       goalstate, reportedstate, reportedpgisrunning, reportedrepstate
  from pgautofailover.node;

select *
  from pgautofailover.set_node_system_identifier(2, 6852685710417058800);

-- node_2 reports catchingup and gets assigned single, now alone
select *
  from pgautofailover.node_active('default', 2, 0,
                                  current_group_role => 'catchingup');

select * from pgautofailover.node_active('default', 2, 0);

-- should fail as there's no primary at this point
select pgautofailover.perform_failover();
