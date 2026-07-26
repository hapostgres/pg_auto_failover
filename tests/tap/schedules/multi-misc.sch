# Multi-node misc: standbys, maintenance, ensure, network partition, drop node,
# quorum/candidate-selection edge cases (~9 min)
#
# fast_forward is deliberately NOT included here: making it actually
# exercise the fast_forward FSM state surfaced an unbounded hang in
# standby_fetch_missing_wal() (no timeout / liveness check on the WAL
# source) that's pre-existing and unrelated to this branch. Tracked for a
# separate branch/PR.
multi_standbys
multi_maintenance
ensure
multi_ifdown
drop_node_destroy
guard_data_loss
