-- Copyright (c) Microsoft Corporation. All rights reserved.
-- Licensed under the PostgreSQL License.

\x on

select *
  from pgautofailover.register_node('default', 'localhost', 9876, 'postgres');

select *
  from pgautofailover.register_node('default', 'localhost', 9877, 'postgres');

select *
  from pgautofailover.node_active('default', 'localhost', 9876,
                                  current_group_role => 'single');

select *
  from pgautofailover.register_node('default', 'localhost', 9877, 'postgres');

table pgautofailover.formation;

-- dump the pgautofailover.node table, omitting the timely columns
select formationid, nodeid, groupid, nodehost, nodeport,
       goalstate, reportedstate, reportedpgisrunning, reportedrepstate
  from pgautofailover.node;

select * from pgautofailover.get_primary('unknown formation');
select * from pgautofailover.get_primary(group_id => -10);
select * from pgautofailover.get_primary();

select * from pgautofailover.get_primary('default', 0);
select * from pgautofailover.get_other_nodes('localhost', 9876);

select pgautofailover.remove_node('localhost', 9876);

table pgautofailover.formation;

-- dump the pgautofailover.node table, omitting the timely columns
select formationid, nodeid, groupid, nodehost, nodeport,
       goalstate, reportedstate, reportedpgisrunning, reportedrepstate
  from pgautofailover.node;

select * from pgautofailover.node_active('default', 'localhost', 9877);

-- should fail as there's no primary at this point
select pgautofailover.perform_failover();
