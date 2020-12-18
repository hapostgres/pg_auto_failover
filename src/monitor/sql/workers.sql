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
