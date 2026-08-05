-- Copyright (c) Microsoft Corporation. All rights reserved.
-- Licensed under the PostgreSQL License.
--
-- Regression tests for the Archiving & Disaster Recovery schema and its
-- monitor API (milestone 1: schema + monitor API only -- no
-- service_archiver process involved, everything here is exercised via
-- direct SQL calls against the schema alone). See
-- ~/dev/temp/archiving-disaster-recovery.md for the full design.

\x on

-- A dedicated formation, like every other test in this schedule: 'default'
-- is the seed formation CREATE EXTENSION itself creates, and by this point
-- in regress_schedule it may already have real nodes registered into it by
-- earlier tests, so it's the one name this file must NOT reuse. The
-- 'default' basebackup_policy row (also a CREATE EXTENSION seed) is shared
-- on purpose: this file's own focus is exercising it, not creating another.
-- Two ordinary nodes stand in for a group's primary+secondary, inserted
-- directly rather than through register_node()/node_active(): the ordinary
-- node FSM has its own dedicated coverage elsewhere, this file's own focus
-- is the archiver schema layered on top of it.
SELECT pgautofailover.create_formation('archiving_test', 'pgsql', 'postgres',
                                        true, 1);

INSERT INTO pgautofailover.node
       (formationid, groupid, nodename, nodehost, nodeport, sysidentifier,
        goalstate, reportedstate)
VALUES ('archiving_test', 0, 'node1', 'node1.local', 5432, 111,
        'primary', 'primary'),
       ('archiving_test', 0, 'node2', 'node2.local', 5432, 111,
        'secondary', 'secondary');

-- ── register_archiver ────────────────────────────────────────────────────

SELECT pgautofailover.register_archiver('archiver1', 'archiver1.local')
       AS archiverid \gset

SELECT archiverid, archivername, hostname, region, basebackuppolicyid,
       autoregister, maxresidentreplay
  FROM pgautofailover.archiver;

-- the mandatory 'local' storage target is created in the same call
SELECT archiverstorageid, archiverid, storagemethod, storagepath, rcloneconfigid
  FROM pgautofailover.archiver_storage;

-- ── archiver_add_formation: the budget setup's own fan-out ─────────────────

SELECT * FROM pgautofailover.archiver_add_formation(:archiverid, 'archiving_test');

SELECT nodeid, formationid, groupid, nodename, nodehost, nodeport,
       goalstate, reportedstate, haspgdata
  FROM pgautofailover.node
 WHERE haspgdata = false;

SELECT archivernodeid, archiverid, kind, nodeid
  FROM pgautofailover.archiver_node
 WHERE kind = 'wal-receiver';

SELECT nodeid FROM pgautofailover.node
 WHERE formationid = 'archiving_test' AND groupid = 0 AND haspgdata = false \gset

-- calling archiver_add_formation() again for the same (archiver, formation)
-- must be a safe no-op -- no error, no duplicate node/archiver_node rows --
-- since a real archiver's own reconciler calls this periodically to pick up
-- newly-added groups (e.g. a Citus formation growing a worker), not just
-- once at creation time
SELECT * FROM pgautofailover.archiver_add_formation(:archiverid, 'archiving_test');

SELECT count(*) AS should_still_be_one FROM pgautofailover.node
 WHERE formationid = 'archiving_test' AND groupid = 0 AND haspgdata = false;

-- ── list_archiver_memberships: what an archiver process discovers ──────────

SELECT * FROM pgautofailover.list_archiver_memberships(:archiverid);

-- a second formation attached to the same archiver shows up alongside the
-- first -- this is the multi-membership case: one archiver, several
-- (formation, group) rows, each its own WAL stream and base-backup schedule
SELECT pgautofailover.create_formation('archiving_test_2', 'pgsql', 'postgres',
                                        true, 1);
INSERT INTO pgautofailover.node
       (formationid, groupid, nodename, nodehost, nodeport, sysidentifier,
        goalstate, reportedstate)
VALUES ('archiving_test_2', 0, 'node3', 'node3.local', 5432, 222,
        'primary', 'primary');
SELECT * FROM pgautofailover.archiver_add_formation(:archiverid, 'archiving_test_2');
SELECT formation_id, group_id
  FROM pgautofailover.list_archiver_memberships(:archiverid)
 ORDER BY formation_id;

SELECT pgautofailover.archiver_remove_formation(:archiverid, 'archiving_test_2');

-- a second archiver serving the same formation/group shares the same
-- (nodehost, nodeport) = (its own hostname, 0) with the first -- the
-- node_nodehost_nodeport_haspgdata_idx partial unique index (scoped to
-- haspgdata rows only) must not reject this. Registered with an explicit,
-- distinct region from archiver1's own default -- this is the intended
-- shape for geographically-redundant DR coverage of the same formation
-- (see archiver.region's own comment); get_archivers() below must surface
-- both regions distinctly.
SELECT pgautofailover.register_archiver('archiver2', 'archiver1.local',
                                         region => 'eu-west')
       AS archiverid2 \gset
SELECT * FROM pgautofailover.archiver_add_formation(:archiverid2, 'archiving_test');

SELECT archiver_id, archiver_name, region
  FROM pgautofailover.get_archivers('archiving_test')
 ORDER BY archiver_id;

-- ── WAL capture confirmation: wal_archived() / report_wal_received() ───────

SELECT pgautofailover.report_wal_received(
           :nodeid, '000000010000000000000001', '0/1000000');

-- default archiver_quorum is 1: a single archiver's report already satisfies it
SELECT pgautofailover.wal_archived('archiving_test', 0, '000000010000000000000001');

-- bump the formation-wide default to 2: the same segment, reported by only
-- one archiver, no longer satisfies quorum
SELECT pgautofailover.set_archiver_policy('archiving_test', NULL, 2, NULL, NULL);
SELECT pgautofailover.wal_archived('archiving_test', 0, '000000010000000000000001');

-- a group-specific override takes precedence over the formation-wide default
SELECT pgautofailover.set_archiver_policy('archiving_test', 0, 1, NULL, NULL);
SELECT * FROM pgautofailover.get_archiver_policy('archiving_test', 0);
-- group 1 has no override of its own: falls back to the formation default (2)
SELECT * FROM pgautofailover.get_archiver_policy('archiving_test', 1);

-- ── base backup lifecycle ───────────────────────────────────────────────────

SELECT pgautofailover.report_basebackup_started(
           :archiverid, 'archiving_test', 0, 'base_20260804', 1, '0/500000', 'live')
       AS basebackupid \gset

SELECT pgautofailover.report_basebackup_completed(
           :basebackupid, '0/1000000', 123456789,
           '/var/lib/pgaf-archiver/backups/base_20260804');

SELECT basebackupid, status, startlsn, endlsn, sizebytes
  FROM pgautofailover.basebackup;

SELECT basebackupid, formationid, groupid, status
  FROM pgautofailover.get_latest_basebackup('archiving_test', 0);

-- nothing to prune yet: the captured segment's LSN isn't older than this
-- backup's own startlsn
SELECT pgautofailover.prune_archiver_wal('archiving_test', 0);

-- report_basebackup_deleted() marks status='deleted' (never a real DELETE)
-- and prunes -- with no 'complete' backup left for this group, there's no
-- anchor point to replay forward from, so nothing prunes either
SELECT pgautofailover.report_basebackup_deleted(:basebackupid);
SELECT basebackupid, status, deletedat IS NOT NULL AS was_deleted
  FROM pgautofailover.basebackup;

-- ── rclone_config + archiver_storage ─────────────────────────────────────

SELECT pgautofailover.create_rclone_config(
           'minio-test', '[minio]' || chr(10) || 'type = s3')
       AS rcloneconfigid \gset

SELECT pgautofailover.archiver_add_storage(:archiverid, 'minio-test')
       AS archiverstorageid \gset

SELECT archiverstorageid, storagemethod, rcloneconfigid
  FROM pgautofailover.archiver_storage
 WHERE archiverid = :archiverid
 ORDER BY archiverstorageid;

-- the mandatory local target cannot be removed
SELECT archiverstorageid AS local_storageid FROM pgautofailover.archiver_storage
 WHERE archiverid = :archiverid AND storagemethod = 'local' \gset

SELECT pgautofailover.archiver_remove_storage(:local_storageid);

-- the non-local target can be
SELECT pgautofailover.archiver_remove_storage(:archiverstorageid);
SELECT count(*) AS remaining_storage_targets FROM pgautofailover.archiver_storage
 WHERE archiverid = :archiverid;

-- ── warm-standby archiver_node + maxresidentreplay cap ──────────────────────

SELECT pgautofailover.create_archiver_node(
           :archiverid, 'warm-standby', '/var/lib/pgaf-archiver/standby',
           NULL, NULL, 'archiving_test', 0, 'continuous')
       AS archivernodeid1 \gset

-- default maxresidentreplay is 1: a second resident warm-standby on the
-- same archiver must be refused
SELECT pgautofailover.create_archiver_node(
           :archiverid, 'warm-standby', '/var/lib/pgaf-archiver/standby2',
           NULL, NULL, 'archiving_test', 0, 'continuous');

-- ── PITR lifecycle ───────────────────────────────────────────────────────

SELECT pgautofailover.create_archiver_node(
           :archiverid, 'pitr', '/var/lib/pgaf-archiver/pitr-recovery',
           NULL, NULL, NULL, NULL, NULL, NULL, 'restoring')
       AS pitrnodeid \gset

SELECT pgautofailover.report_pitr_status(
           :pitrnodeid, 'create',
           '{"restore_target_time": "2026-08-04 00:00:00+00"}'::jsonb,
           NULL, NULL, 'not paused');

SELECT pgautofailover.set_archiver_node_pitr_status(:pitrnodeid, 'paused');

SELECT pgautofailover.report_pitr_status(
           :pitrnodeid, 'status', NULL, '0/900000'::pg_lsn, '2026-08-04 00:00:05+00', 'paused');

SELECT archivernodeid, archiverid, pitrstatus, lastoperation,
       observedlsn, observedpausestate
  FROM pgautofailover.pitr_node_status;

-- ── PITR command queue: pops and clears exactly once ────────────────────────

SELECT pgautofailover.pitr_queue_command(:pitrnodeid, 'promote', NULL);
SELECT pgautofailover.pitr_next_command(:pitrnodeid);
SELECT pgautofailover.pitr_next_command(:pitrnodeid);

-- ── archiver_remove_formation cleans up the ARCHIVING node row ──────────────

SELECT pgautofailover.archiver_remove_formation(:archiverid, 'archiving_test');

SELECT count(*) AS should_be_zero FROM pgautofailover.node
 WHERE haspgdata = false AND nodeid = :nodeid;

SELECT count(*) AS should_also_be_zero FROM pgautofailover.archiver_node
 WHERE archiverid = :archiverid AND kind = 'wal-receiver';
