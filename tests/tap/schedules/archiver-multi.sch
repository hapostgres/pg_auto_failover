# Archiving & Disaster Recovery: dynamic multi-formation attach and
# geo-redundant region coverage. Kept out of archiver.sch (WAL capture,
# base backups, rebuild-from-archiver): these specs exercise the
# reconciler's own membership-diffing and the region column's SQL/CLI
# round-trip -- monitor-side and CLI logic, not pg_walsender's wire
# protocol -- so PG17-only matches node-fsm-gaps.sch's own rationale
# ("this is FSM/logic coverage, not version-specific code paths") rather
# than archiver.sch's all-versions one. Also keeps this schedule light:
# archiver_multi_formation.pgaf alone has a mandatory 50s sleep (one
# reconciler tick, ARCHIVER_RECONCILER_INTERVAL_SECONDS) plus several
# more, and archiver.sch already learned the hard way (this same PR,
# CI run 84233594160) what happens when a schedule's own runtime creeps
# past the 20-minute step timeout.
archiver_multi_formation
archiver_budget_architecture_regions
archiver_two_regions
