# Node lifecycle, monitor operations, and Debian/tablespace layouts (~30 min).
# Merged from former node, monitor, and node-extra schedules to reduce CI job
# count and GitHub Actions runner queue pressure.
create_standby_with_pgdata
launch_deferred_set_metadata
fsm_step_report_advance
maintenance_and_drop
auth
monitor_disabled
replace_monitor
extension_update
debian_clusters
tablespaces
replication_stall_3dc
demote_timeout_wait_primary_deadlock
timeline_fork_report_lsn_deadlock
timeline_fork_3node_auto_detect
