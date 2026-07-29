-- Copyright (c) Microsoft Corporation. All rights reserved.
-- Licensed under the PostgreSQL License.
--
-- Plain dump of the monitor's declarative dispatch table, via the
-- pgautofailover.fsm view (pgautofailover.dump_fsm() ordered by pos). This
-- is a static, compile-time-fixed table -- unaffected by any node/formation
-- state -- so its expected output changes only when a row is added,
-- removed, or edited in MonitorFSM[] (group_state_machine.c), giving that
-- change an explicit, reviewable regression diff.
--
-- \x on: with the *_conditions columns added, a plain tabular row is far
-- wider than a terminal (or this file's own diff-ability), and reads far
-- worse than one field-per-line.

\x on

SELECT pos, section,
       active_node_current_state, other_node_current_state, candidate_node_current_state,
       active_node_conditions, other_node_conditions, candidate_node_conditions,
       group_conditions,
       active_node_assigned_state, other_node_assigned_state, has_extra_action,
       comment
  FROM pgautofailover.fsm
 ORDER BY pos;
