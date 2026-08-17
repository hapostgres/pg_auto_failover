# Archiving & Disaster Recovery: WAL capture, base backups, and rebuild-
# from-archiver. Split out of node.sch: adding these 4 specs pushed every
# PG version of that already-tight schedule over the CI step's 20-minute
# timeout (CI run 84233594160: PG16/PG18 timed out at 20 minutes, PG14/
# PG15/PG19 hit real failures before even getting there -- a cumulative
# time-budget overrun on top of real bugs, same pattern node-fsm-gaps.sch
# was split out for).
#
# Unlike node-fsm-gaps.sch, this schedule runs on every PG version rather
# than PG17 only: pg_walsender speaks the real Postgres replication wire
# protocol to real pg_basebackup/pg_receivewal clients, so its correctness
# is genuinely version-sensitive (this exact split was prompted by a
# version-specific bug: a hardcoded server_version made every non-PG16
# build fail "incompatible server version" against real pg_basebackup).
archiver_wal_capture
archiver_basebackup_generation
archiver_basebackup_policy
archiver_bootstrap_and_fast_forward
