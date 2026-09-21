-- Copyright (c) Microsoft Corporation. All rights reserved.
-- Licensed under the PostgreSQL License.
--
-- Regression tests for the Archiving & Disaster Recovery schema and its
-- monitor API (schema + monitor API only -- no service_archiver process
-- involved, everything here is exercised via direct SQL calls against
-- the schema alone).

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
           :nodeid, '000000010000000000000001', '0/1000000', 111);

-- bulk variant: two more segments in one call
SELECT pgautofailover.report_wal_received_bulk(
           :nodeid, 111,
           ARRAY['000000010000000000000002', '000000010000000000000003'],
           ARRAY['0/2000000', '0/3000000']::pg_lsn[]);

SELECT walfilename, lsn, systemidentifier
  FROM pgautofailover.archiver_wal
 ORDER BY walfilename;

-- default archiver_quorum is 1: a single archiver's report already satisfies it
SELECT pgautofailover.wal_archived('archiving_test', 0, '000000010000000000000001');

-- archive_confirmed(): a group with no archiver membership never blocks
SELECT pgautofailover.archive_confirmed('archiving_test', 42, '000000010000000000000001');
-- group 0 has an archiver: unreported segment is not confirmed, reported one is
SELECT pgautofailover.archive_confirmed('archiving_test', 0, '00000001000000000000000A');
SELECT pgautofailover.report_wal_received(
           :nodeid, '00000001000000000000000A', '0/A000000', 111);
SELECT pgautofailover.archive_confirmed('archiving_test', 0, '00000001000000000000000A');

-- bump the formation-wide default to 2: the same segment, reported by only
-- one archiver, no longer satisfies quorum
SELECT pgautofailover.set_archiver_policy('archiving_test', NULL, 2, NULL, NULL);
SELECT pgautofailover.wal_archived('archiving_test', 0, '000000010000000000000001');

-- with archiverquorum = 2, a segment reported by ONE archiver is not yet
-- archived, and becomes so as soon as a SECOND archiver reports it too
SELECT nodeid AS nodeid2 FROM pgautofailover.node
 WHERE formationid = 'archiving_test' AND groupid = 0
   AND haspgdata = false AND nodename = 'archiver-2-0' \gset

SELECT pgautofailover.report_wal_received(
           :nodeid, '000000010000000000000009', '0/9000000', 111);
SELECT pgautofailover.wal_archived('archiving_test', 0, '000000010000000000000009');
SELECT pgautofailover.report_wal_received(
           :nodeid2, '000000010000000000000009', '0/9000000', 111);
SELECT pgautofailover.wal_archived('archiving_test', 0, '000000010000000000000009');

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

-- ── get_group_hosts / get_group_hosts_hash: cache-invalidation fingerprint ──
-- (run after archiver_remove_formation above, so re-attach the archiver to
-- prove its ARCHIVING row -- haspgdata = false -- never shows up in hosts)

SELECT node_count, hosts, hash = md5('2:node1.local,node2.local') AS hash_matches
  FROM pgautofailover.get_group_hosts('archiving_test', 0);

SELECT pgautofailover.get_group_hosts_hash('archiving_test', 0)
       = (SELECT hash FROM pgautofailover.get_group_hosts('archiving_test', 0))
       AS hashes_agree;

SELECT count(*) AS attached_count
  FROM pgautofailover.archiver_add_formation(:archiverid, 'archiving_test');

SELECT node_count, hosts, hash = md5('2:node1.local,node2.local') AS hash_matches
  FROM pgautofailover.get_group_hosts('archiving_test', 0);

-- a group with no Postgres node: empty array, count 0, still a stable hash
SELECT node_count, hosts, hash = md5('0:') AS hash_matches
  FROM pgautofailover.get_group_hosts('archiving_test', 42);

SELECT pgautofailover.archiver_remove_formation(:archiverid, 'archiving_test');

-- ── hardening: SECURITY DEFINER functions are not executable by PUBLIC ──────

SELECT count(*) AS checked_functions,
       count(*) FILTER (WHERE has_function_privilege('public', p.oid, 'EXECUTE'))
         AS public_can_execute,
       count(*) FILTER (WHERE NOT has_function_privilege('autoctl_node', p.oid, 'EXECUTE'))
         AS node_cannot_execute,
       count(*) FILTER (WHERE p.proconfig IS NULL
                           OR p.proconfig::text NOT LIKE '%search_path=pg_catalog, pgautofailover, pg_temp%')
         AS missing_search_path
  FROM pg_proc p
 WHERE p.pronamespace = 'pgautofailover'::regnamespace
   AND p.prosecdef
   AND p.proname IN ('get_group_hosts', 'get_group_hosts_hash',
                     'report_wal_received', 'create_rclone_config',
                     'archiver_add_formation');

-- ── serveport: stored per archiver, defaults to 6543, range-checked ─────────

SELECT archiverid AS the_archiverid, serveport AS the_serveport
  FROM pgautofailover.archiver ORDER BY archiverid;

SELECT pgautofailover.register_archiver('archiver3', 'archiver3.local',
                                         in_serveport => 7000)
       AS archiverid3 \gset

SELECT serveport AS the_serveport FROM pgautofailover.archiver
 WHERE archiverid = :archiverid3;

\set VERBOSITY terse
SELECT pgautofailover.register_archiver('archiver4', 'archiver4.local',
                                         in_serveport => 0);
SELECT pgautofailover.register_archiver('archiver4', 'archiver4.local',
                                         in_serveport => 65536);
\set VERBOSITY default

-- get_archiver_node() returns the serving archiver's own serveport
UPDATE pgautofailover.node
   SET reportedstate = 'archiving', goalstate = 'archiving'
 WHERE nodeid = :nodeid2;

SELECT node_name AS archiver_node_name, node_host AS archiver_node_host,
       node_port AS archiver_node_port, serveport AS archiver_serveport
  FROM pgautofailover.get_archiver_node('archiving_test', 0);

UPDATE pgautofailover.archiver SET serveport = 6600 WHERE archiverid = :archiverid2;

SELECT serveport AS archiver_serveport
  FROM pgautofailover.get_archiver_node('archiving_test', 0);

-- ── get_archivers: one row per (archiver, membership) in THAT formation ─────
-- archiver2 is attached to two formations: each formation lists it once,
-- with its own node columns, never a second row with NULL node columns.
-- archiver3 is attached (autoregister-style, no node row yet) to
-- archiving_test only: listed once, with no node.

SELECT count(*) AS attached_count
  FROM pgautofailover.archiver_add_formation(:archiverid2, 'archiving_test_2');

INSERT INTO pgautofailover.archiver_formation (archiverid, formationid)
VALUES (:archiverid3, 'archiving_test');

SELECT archiver_name AS the_archivername, node_id IS NOT NULL AS has_node_row
  FROM pgautofailover.get_archivers('archiving_test')
 ORDER BY archiver_id;

SELECT archiver_name AS the_archivername, node_id IS NOT NULL AS has_node_row
  FROM pgautofailover.get_archivers('archiving_test_2')
 ORDER BY archiver_id;

-- ── basebackup policy resolution order ──────────────────────────────────────
-- group row, then formation default, then the serving archiver's own
-- default, then the seeded 'default' policy

SELECT pgautofailover.create_basebackup_policy('pol_archiver_default', '{}'::jsonb)
       AS pol_archiver \gset
SELECT pgautofailover.create_basebackup_policy('pol_formation_default', '{}'::jsonb)
       AS pol_formation \gset
SELECT pgautofailover.create_basebackup_policy('pol_group_specific', '{}'::jsonb)
       AS pol_group \gset

SELECT pgautofailover.create_formation('archiving_test_3', 'pgsql', 'postgres',
                                        true, 1);
INSERT INTO pgautofailover.node
       (formationid, groupid, nodename, nodehost, nodeport, sysidentifier,
        goalstate, reportedstate)
VALUES ('archiving_test_3', 0, 'node4', 'node4.local', 5432, 333,
        'primary', 'primary');

SELECT pgautofailover.register_archiver('archiver5', 'archiver5.local',
                                         basebackuppolicyid => :pol_archiver)
       AS archiverid5 \gset
SELECT count(*) AS attached_count
  FROM pgautofailover.archiver_add_formation(:archiverid5, 'archiving_test_3');

-- no archiver_policy row at all: the serving archiver's own default wins
SELECT policyname = 'pol_archiver_default' AS archiver_default_wins
  FROM pgautofailover.get_basebackup_policy_for_group('archiving_test_3', 0);

-- formation-wide default beats the archiver's own default
SELECT pgautofailover.set_archiver_policy('archiving_test_3', NULL, 2,
                                          NULL, NULL);
SELECT pgautofailover.set_archiver_policy('archiving_test_3', NULL, NULL,
                                          :pol_formation, NULL);
SELECT policyname = 'pol_formation_default' AS formation_default_wins
  FROM pgautofailover.get_basebackup_policy_for_group('archiving_test_3', 0);

-- set_archiver_policy() with a NULL argument keeps the stored value
SELECT archiverquorum AS quorum_retained
  FROM pgautofailover.get_archiver_policy('archiving_test_3', 5);

-- a group-specific row beats both
SELECT pgautofailover.set_archiver_policy('archiving_test_3', 0, NULL,
                                          :pol_group, NULL);
SELECT policyname = 'pol_group_specific' AS group_specific_wins
  FROM pgautofailover.get_basebackup_policy_for_group('archiving_test_3', 0);

-- a group row that only sets the quorum (NULL policy) falls through
SELECT pgautofailover.set_archiver_policy('archiving_test_3', 1, 3,
                                          NULL, NULL);
SELECT policyname = 'pol_formation_default' AS null_policy_falls_through
  FROM pgautofailover.get_basebackup_policy_for_group('archiving_test_3', 1);

-- the sibling function takes the archiver explicitly; an unknown formation
-- has no policy rows, so the archiver's default applies, and with no known
-- archiver either the seeded 'default' policy applies
SELECT policyname = 'pol_archiver_default' AS sibling_archiver_wins
  FROM pgautofailover.get_basebackup_policy_for_archiver_group(
           :archiverid5, 'no_such_formation', 0);
SELECT policyname = 'default' AS seeded_default_wins
  FROM pgautofailover.get_basebackup_policy_for_archiver_group(
           NULL, 'no_such_formation', 0);

-- ── candidate priority is pinned to zero for archiver rows ──────────────────

\set VERBOSITY terse
SELECT pgautofailover.set_node_candidate_priority(
           'archiving_test', 'archiver-2-0', 50);
\set VERBOSITY default

SELECT candidatepriority AS candidate_priority FROM pgautofailover.node
 WHERE nodeid = :nodeid2;

-- ── formation removal and node removal clean the archiver rows ──────────────

SELECT pgautofailover.create_formation('archiving_test_4', 'pgsql', 'postgres',
                                        true, 1);
SELECT pgautofailover.create_archiver_node(
           :archiverid2, 'warm-standby', '/var/lib/pgaf-archiver/standby4',
           NULL, NULL, 'archiving_test_4', 0, 'continuous')
       AS archivernodeid4 \gset

SELECT pgautofailover.drop_formation('archiving_test_4');

SELECT count(*) AS warm_standby_left FROM pgautofailover.archiver_node
 WHERE formationid = 'archiving_test_4';

-- deleting an ARCHIVING node row cascades to its wal-receiver row
SELECT count(*) AS attached_count
  FROM pgautofailover.archiver_add_formation(:archiverid3, 'archiving_test_2');

DELETE FROM pgautofailover.node
 WHERE formationid = 'archiving_test_2' AND nodename = 'archiver-' || :archiverid3 || '-0';

SELECT count(*) AS wal_receiver_left FROM pgautofailover.archiver_node
 WHERE archiverid = :archiverid3 AND kind = 'wal-receiver';

-- ── hardening of the functions this milestone added or replaced ─────────────

SELECT count(*) AS checked_functions,
       count(*) FILTER (WHERE has_function_privilege('public', p.oid, 'EXECUTE'))
         AS public_can_execute,
       count(*) FILTER (WHERE NOT has_function_privilege('autoctl_node', p.oid, 'EXECUTE'))
         AS node_cannot_execute,
       count(*) FILTER (WHERE p.proconfig IS NULL
                           OR p.proconfig::text NOT LIKE '%search_path=pg_catalog, pgautofailover, pg_temp%')
         AS missing_search_path
  FROM pg_proc p
 WHERE p.pronamespace = 'pgautofailover'::regnamespace
   AND p.prosecdef
   AND p.proname IN ('get_archiver_node', 'get_archivers', 'register_archiver',
                     'get_basebackup_policy_for_group',
                     'get_basebackup_policy_for_archiver_group',
                     'archive_confirmed');
