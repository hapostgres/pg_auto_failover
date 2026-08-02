# Keeper/monitor FSM edge-gap coverage, split out of node.sch (~14 min):
# node.sch's own comment already measured ~30 min for its original 14 specs
# against the CI step's 20-minute timeout before these were ever added, and
# adding these on top pushed every PG version over the limit (CI run
# 83400372049: all 6 node schedule jobs timed out at 20 minutes, stalling
# around spec #20/28 -- a cumulative time-budget overrun, not a single
# stuck test). Run on PG17 only, same rationale as multi-alternate/
# multi-misc/multi-async/citus-1/citus-2 in ci.yml: this is FSM logic, not
# version-specific code paths.
keeper_fsm_gap_209_wait_maintenance
keeper_fsm_gap_211_wait_maintenance
keeper_fsm_gap_209_wait_standby
keeper_fsm_gap_211_wait_standby
keeper_fsm_gap_211_primary_priority_zero
keeper_fsm_gap_new_node_joins_report_lsn_group
keeper_fsm_gap_candidate_fast_forward_left_alone
keeper_fsm_gap_priority_zero_fast_forward_left_alone
keeper_fsm_gap_primary_left_alone_mid_maintenance_handoff
keeper_fsm_gap_priority_zero_candidate_left_alone_mid_promotion
keeper_fsm_gap_priority_zero_primary_left_alone_mid_demotion
keeper_fsm_gap_priority_zero_losing_candidate_left_alone_mid_handoff
keeper_fsm_gap_stop_replication_report_lsn_priority
keeper_fsm_gap_stop_replication_report_lsn_new_node
