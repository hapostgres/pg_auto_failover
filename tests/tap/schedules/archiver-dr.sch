# Archiving & Disaster Recovery: operational behaviour and quorums --
# archiver_quorum (two archivers) and replication-quorum participation
# (the archiver as a synchronous-commit quorum member), then the archiver's
# lifecycle (monitor down, detach/re-attach, restart, HBA keywords, backup
# now, local WAL retention).
#
# Kept apart from archiver.sch / archiver-multi.sch for the CI step
# timeout (20 minutes per schedule): archiver_quorum ~5 min, archiver_
# lifecycle ~8-10 min. PG17 only, like archiver-multi.sch: this is
# monitor/keeper logic rather than version-sensitive wire protocol.
archiver_quorum
archiver_lifecycle
archiver_serve_port
archiver_fsm_quorum
archiver_fsm_maintenance
archiver_wal_segsize
archiver_archive_command
archiver_archive_command_none
