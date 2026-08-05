-- Copyright (c) Microsoft Corporation. All rights reserved.
-- Licensed under the PostgreSQL License.

-- complain if script is sourced in psql, rather than via ALTER EXTENSION
\echo Use "ALTER EXTENSION pgautofailover UPDATE TO '2.3'" to load this file. \quit

--
-- Add region and replication_stall_since columns to pgautofailover.node.
--
-- region: operator-assigned label for the data-centre or availability zone
--   the node lives in.  Used in watch output and future topology-aware routing.
--
-- replication_stall_since: set by node_active() the first time a PRIMARY node
--   reports an empty pg_stat_replication (no standby connected).  Cleared when
--   a standby reconnects.  The FSM uses this to transition to wait_primary after
--   pgautofailover.replication_stall_timeout elapses, unblocking writes in
--   3-DC topologies where the primary↔standby link breaks while both nodes
--   remain reachable from the monitor (issue #997).
--

ALTER TABLE pgautofailover.node
    ADD COLUMN IF NOT EXISTS region text not null default 'default',
    ADD COLUMN IF NOT EXISTS replication_stall_since timestamptz;


--
-- Replace current_state() with a version that also returns the region column.
--

DROP FUNCTION IF EXISTS pgautofailover.current_state(text);
DROP FUNCTION IF EXISTS pgautofailover.current_state(text, int);

CREATE FUNCTION pgautofailover.current_state
 (
    IN formation_id         text default 'default',
   OUT formation_kind       text,
   OUT nodename             text,
   OUT nodehost             text,
   OUT nodeport             int,
   OUT group_id             int,
   OUT node_id              bigint,
   OUT current_group_state  pgautofailover.replication_state,
   OUT assigned_group_state pgautofailover.replication_state,
   OUT candidate_priority	int,
   OUT replication_quorum	bool,
   OUT reported_tli         int,
   OUT reported_lsn         pg_lsn,
   OUT health               integer,
   OUT nodecluster          text,
   OUT noderegion           text
 )
RETURNS SETOF record LANGUAGE SQL STRICT
AS $$
   select kind, nodename, nodehost, nodeport, groupid, nodeid,
          reportedstate, goalstate,
   		  candidatepriority, replicationquorum,
          reportedtli, reportedlsn, health, nodecluster, region
     from pgautofailover.node
     join pgautofailover.formation using(formationid)
    where formationid = formation_id
 order by groupid, nodeid;
$$;

comment on function pgautofailover.current_state(text)
        is 'get the current state of both nodes of a formation';

grant execute on function pgautofailover.current_state(text)
   to autoctl_node;

CREATE FUNCTION pgautofailover.current_state
 (
    IN formation_id         text,
    IN group_id             int,
   OUT formation_kind       text,
   OUT nodename             text,
   OUT nodehost             text,
   OUT nodeport             int,
   OUT group_id             int,
   OUT node_id              bigint,
   OUT current_group_state  pgautofailover.replication_state,
   OUT assigned_group_state pgautofailover.replication_state,
   OUT candidate_priority	int,
   OUT replication_quorum	bool,
   OUT reported_tli         int,
   OUT reported_lsn         pg_lsn,
   OUT health               integer,
   OUT nodecluster          text,
   OUT noderegion           text
 )
RETURNS SETOF record LANGUAGE SQL STRICT
AS $$
   select kind, nodename, nodehost, nodeport, groupid, nodeid,
          reportedstate, goalstate,
   		  candidatepriority, replicationquorum,
          reportedtli, reportedlsn, health, nodecluster, region
     from pgautofailover.node
     join pgautofailover.formation using(formationid)
    where formationid = formation_id
      and groupid = group_id
 order by groupid, nodeid;
$$;

comment on function pgautofailover.current_state(text, int)
        is 'get the current state of both nodes of a group in a formation';

grant execute on function pgautofailover.current_state(text, int)
   to autoctl_node;


--
-- Replace register_node() with a version that accepts the new node_region
-- parameter.  The old 13-argument form is dropped first so the new 14-argument
-- form can be created cleanly.
--

DROP FUNCTION IF EXISTS pgautofailover.register_node(
    text, text, int, name, text, bigint, bigint, int,
    pgautofailover.replication_state, text, int, bool, text);

CREATE FUNCTION pgautofailover.register_node
 (
    IN formation_id         text,
    IN node_host            text,
    IN node_port            int,
    IN dbname               name,
    IN node_name            text default '',
    IN sysidentifier        bigint default 0,
    IN desired_node_id      bigint default -1,
    IN desired_group_id     int default -1,
    IN initial_group_role   pgautofailover.replication_state default 'init',
    IN node_kind            text default 'standalone',
    IN candidate_priority   int default 100,
    IN replication_quorum   bool default true,
    IN node_cluster         text default 'default',
    IN node_region          text default 'default',
   OUT assigned_node_id     bigint,
   OUT assigned_group_id    int,
   OUT assigned_group_state pgautofailover.replication_state,
   OUT assigned_candidate_priority 	int,
   OUT assigned_replication_quorum  bool,
   OUT assigned_node_name   text
 )
RETURNS record LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$register_node$$;

grant execute on function
      pgautofailover.register_node(text,text,int,name,text,bigint,bigint,int,
                                   pgautofailover.replication_state,text,
                                   int,bool,text,text)
   to autoctl_node;


--
-- Add set_node_region(), making the region column (added above) mutable
-- after registration (#1156).
--

CREATE FUNCTION pgautofailover.set_node_region
 (
    IN formation_id text,
    IN node_name    text,
    IN region       text
 )
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$set_node_region$$;

comment on function pgautofailover.set_node_region(text, text, text)
        is 'sets the region label for a node, identifying its data-centre or availability zone';

grant execute on function
      pgautofailover.set_node_region(text, text, text)
   to autoctl_node;


--
-- Add Postgres/Citus version tracking columns and report_postgres_version()
-- (#1157).
--

ALTER TABLE pgautofailover.node
    ADD COLUMN IF NOT EXISTS pg_versionnum    int,
    ADD COLUMN IF NOT EXISTS pg_version       text,
    ADD COLUMN IF NOT EXISTS pg_versionstring text,
    ADD COLUMN IF NOT EXISTS citus_version    text;

-- Deliberately NOT STRICT: version_num/version/versionstring/citus_version
-- are all allowed to be NULL. citus_version legitimately is, whenever
-- Citus isn't installed on that node. This is a plain self-report, called
-- once per Postgres restart by the keeper -- not part of the FSM, and not
-- routed through node_active() since none of this can change without a
-- restart.
CREATE FUNCTION pgautofailover.report_postgres_version
 (
    IN node_id          bigint,
    IN pg_versionnum    int  default null,
    IN pg_version       text default null,
    IN pg_versionstring text default null,
    IN citus_version    text default null
 )
RETURNS void LANGUAGE C SECURITY DEFINER
AS 'MODULE_PATHNAME', $$report_postgres_version$$;

comment on function pgautofailover.report_postgres_version(bigint,int,text,text,text)
        is 'reports a node''s Postgres server version and, when installed, Citus extension version';

grant execute on function
      pgautofailover.report_postgres_version(bigint,int,text,text,text)
   to autoctl_node;


--
-- Add timeline-fork detection: node_timeline_history / accepted_timeline
-- tables, and the report_timeline_history() / accept_timeline() /
-- resolve_accepted_timeline() / node_timeline_status() functions.
--

-- Every Postgres timeline has exactly one parent and one fork LSN, so the
-- union of every node's known history forms a single tree. Append-only: a
-- (tli, parenttli, switchpoint_lsn) triple never changes once recorded, and
-- is an objective fact about that timeline, not about which node reported
-- it -- nodeid is provenance (who told us), not part of what the row means.
CREATE TABLE pgautofailover.node_timeline_history
 (
    nodeid          bigint not null
                    references pgautofailover.node(nodeid) on delete cascade,
    tli             int not null,
    parenttli       int not null,
    switchpoint_lsn pg_lsn not null,

    PRIMARY KEY (nodeid, tli)
 );

-- An operator's explicit pin of which timeline is ground truth after a
-- detected fork. Normally empty: absent any unresolved row, the election
-- runs its ordinary auto-detection unchanged.
CREATE TABLE pgautofailover.accepted_timeline
 (
    formationid   text not null,
    groupid       int not null,
    accepted_tli  int not null,
    decided_by    text,
    decided_at    timestamptz not null default now(),
    resolved_at   timestamptz,

    PRIMARY KEY (formationid, groupid, decided_at)
 );

-- matches the blanket grant used by a fresh install (pgautofailover.sql)
GRANT SELECT ON pgautofailover.node_timeline_history TO autoctl_node;
GRANT SELECT ON pgautofailover.accepted_timeline TO autoctl_node;

-- history is a JSON array of {"tli": int, "parenttli": int,
-- "switchpoint": text} objects, oldest first, as produced by the keeper's
-- timeline_history_to_json(). Called both periodically (whenever the local
-- timeline has advanced) and synchronously right before a report_lsn
-- restart, so this needs to be cheap and idempotent: an unconditional
-- ON CONFLICT DO NOTHING insert of already-known facts is a no-op.
CREATE FUNCTION pgautofailover.report_timeline_history
 (
    IN node_id bigint,
    IN history jsonb
 )
RETURNS void LANGUAGE plpgsql SECURITY DEFINER
AS $$
BEGIN
    INSERT INTO pgautofailover.node_timeline_history
           (nodeid, tli, parenttli, switchpoint_lsn)
    SELECT node_id,
           (entry->>'tli')::int,
           (entry->>'parenttli')::int,
           (entry->>'switchpoint')::pg_lsn
      FROM jsonb_array_elements(history) AS entry
     ON CONFLICT (nodeid, tli) DO NOTHING;
END;
$$;

comment on function pgautofailover.report_timeline_history(bigint,jsonb)
        is 'reports a node''s own known timeline history (tli, parent tli, switchpoint LSN)';

grant execute on function
      pgautofailover.report_timeline_history(bigint,jsonb)
   to autoctl_node;

-- Explicit operator resolution of a detected timeline fork: pins which
-- lineage is ground truth for a (formation, group), so the election's
-- ancestry filter uses it instead of auto-detecting (see
-- FilterNodesByTimelineAncestry() in timeline_history.c). Refuses to pin a
-- timeline nobody in the group has ever reported.
CREATE FUNCTION pgautofailover.accept_timeline
 (
    IN formation_id text,
    IN group_id     int,
    IN target_tli   int,
    IN decided_by   text default null
 )
RETURNS bool LANGUAGE plpgsql SECURITY DEFINER
AS $$
DECLARE
    known bool;
BEGIN
    SELECT EXISTS (
        SELECT 1
          FROM pgautofailover.node_timeline_history h
          JOIN pgautofailover.node n ON n.nodeid = h.nodeid
         WHERE n.formationid = formation_id
           AND n.groupid = group_id
           AND h.tli = target_tli
    ) INTO known;

    IF NOT known THEN
        RAISE EXCEPTION
            'timeline % has never been reported by any node in formation "%" group %',
            target_tli, formation_id, group_id;
    END IF;

    INSERT INTO pgautofailover.accepted_timeline
           (formationid, groupid, accepted_tli, decided_by)
    VALUES (formation_id, group_id, target_tli, decided_by);

    RETURN true;
END;
$$;

comment on function pgautofailover.accept_timeline(text,int,int,text)
        is 'pins the accepted timeline for a (formation, group) after an operator resolves a detected fork';

grant execute on function
      pgautofailover.accept_timeline(text,int,int,text)
   to autoctl_node;

-- Marks the most recent unresolved accepted_timeline pin for a
-- (formation, group) as resolved, once a primary has been promoted on the
-- accepted lineage. Kept as a permanent audit record rather than deleted.
CREATE FUNCTION pgautofailover.resolve_accepted_timeline
 (
    IN formation_id text,
    IN group_id     int
 )
RETURNS bool LANGUAGE SQL SECURITY DEFINER
AS $$
    UPDATE pgautofailover.accepted_timeline
       SET resolved_at = now()
     WHERE formationid = formation_id
       AND groupid = group_id
       AND resolved_at IS NULL;

    SELECT true;
$$;

comment on function pgautofailover.resolve_accepted_timeline(text,int)
        is 'marks the current accepted timeline pin resolved, once a primary has been promoted on it';

grant execute on function
      pgautofailover.resolve_accepted_timeline(text,int)
   to autoctl_node;

-- Per-node status used by `pg_autoctl show timeline`: whether each node's
-- reported timeline is on the group's reference lineage (an operator's pin
-- in accepted_timeline if any, otherwise the branch containing the highest
-- reported tli in the group), the same rule
-- FilterNodesByTimelineAncestry() applies during an election, expressed
-- here as SQL for read-only reporting.
CREATE FUNCTION pgautofailover.node_timeline_status
 (
    IN formation_id           text,
    IN group_id               int,
   OUT node_id                bigint,
   OUT node_name              text,
   OUT tli                    int,
   OUT lsn                    pg_lsn,
   OUT reference_tli          int,
   OUT on_accepted_lineage    bool
 )
RETURNS SETOF record LANGUAGE SQL STRICT
AS $$
    WITH reference AS (
        SELECT COALESCE(
            (SELECT accepted_tli
               FROM pgautofailover.accepted_timeline
              WHERE formationid = formation_id AND groupid = group_id
                AND resolved_at IS NULL
              ORDER BY decided_at DESC LIMIT 1),
            (SELECT max(reportedtli)
               FROM pgautofailover.node
              WHERE formationid = formation_id AND groupid = group_id)
        ) AS tli
    ),
    ancestry AS (
        WITH RECURSIVE chain AS (
            SELECT h.tli, h.parenttli
              FROM pgautofailover.node_timeline_history h
              JOIN pgautofailover.node n ON n.nodeid = h.nodeid
             WHERE n.formationid = formation_id AND n.groupid = group_id
               AND h.tli = (SELECT tli FROM reference)
          UNION ALL
            SELECT h.tli, h.parenttli
              FROM pgautofailover.node_timeline_history h
              JOIN pgautofailover.node n ON n.nodeid = h.nodeid
              JOIN chain c ON h.tli = c.parenttli
             WHERE n.formationid = formation_id AND n.groupid = group_id
        )
        SELECT tli FROM chain
        UNION
        SELECT tli FROM reference
    )
    SELECT n.nodeid,
           n.nodename,
           n.reportedtli,
           n.reportedlsn,
           (SELECT tli FROM reference),
           (n.reportedtli IN (SELECT tli FROM ancestry))
      FROM pgautofailover.node n
     WHERE n.formationid = formation_id AND n.groupid = group_id
     ORDER BY n.nodeid;
$$;

comment on function pgautofailover.node_timeline_status(text,int)
        is 'per-node timeline ancestry status against the group''s reference lineage, for pg_autoctl show timeline';

grant execute on function
      pgautofailover.node_timeline_status(text,int)
   to autoctl_node;


--
-- Fix fast_forward self-reference infinite loop and add a
-- guard_data_loss-aware health filter to get_most_advanced_standby()
-- (#1143, fixes #1060).
--

DROP FUNCTION IF EXISTS pgautofailover.get_most_advanced_standby(text, int);

CREATE FUNCTION pgautofailover.get_most_advanced_standby
 (
   IN formationid       text default 'default',
   IN groupid           int default 0,
   IN caller_node_id    bigint default 0,
   OUT node_id          bigint,
   OUT node_name        text,
   OUT node_host        text,
   OUT node_port        int,
   OUT node_lsn         pg_lsn,
   OUT node_is_primary  bool
 )
RETURNS SETOF record LANGUAGE SQL STRICT
AS $$
   select nodeid, nodename, nodehost, nodeport, reportedlsn, false
     from pgautofailover.node
    where formationid = $1
      and groupid = $2
      and nodeid != $3
      and reportedstate = 'report_lsn'
      and (current_setting('pgautofailover.guard_data_loss')::bool or health > 0)
 order by reportedlsn desc, health desc
    limit 1;
$$;

grant execute on function pgautofailover.get_most_advanced_standby(text,int,bigint)
   to autoctl_node;

--
-- Expose the monitor's declarative dispatch table (MonitorFSM[] in
-- group_state_machine.c) to SQL: pgautofailover.dump_fsm()/dump_fsm_edges()/
-- pgautofailover.fsm/check_fsm_reachability(), plus rule_pos/rule_section
-- attribution on pgautofailover.event. See pgautofailover.sql's own
-- comments on each object below for the full rationale -- unchanged here,
-- this is the same DDL, just applied incrementally to an existing 2.2
-- install instead of as part of a fresh CREATE EXTENSION.
--

-- pgautofailover.fsm's section_path column (below) is cast to ltree (a
-- plain SQL cast through Postgres's own type-casting machinery, not a
-- C-level dependency -- see that view's own comment). Note there is
-- deliberately no "CREATE EXTENSION IF NOT EXISTS ltree" in this script:
-- ALTER EXTENSION ... UPDATE checks every "requires" entry in the *target*
-- version's control file is already installed before it ever runs the
-- upgrade script body for any step on the path, so a CREATE EXTENSION
-- placed here would never get a chance to run -- the ALTER EXTENSION
-- statement itself already fails first ("required extension \"ltree\" is
-- not installed"). The actual fix is client-side, in
-- monitor_extension_update() (monitor.c), which now creates "ltree" before
-- issuing the ALTER EXTENSION statement -- mirroring the exact same
-- pre-existing pattern that function already uses for btree_gist.

-- Mirrors group_state_machine.h's MonitorFSMSection: which of the three
-- real control-flow regions of the monitor's declarative dispatch table
-- (MonitorFSM[] in group_state_machine.c) a rule belongs to. See dump_fsm()
-- and pgautofailover.fsm below, and the rule_section column on
-- pgautofailover.event.
CREATE TYPE pgautofailover.fsm_section
    AS ENUM
 (
    'api_triggered',
    'early_checks',
    'reporting_node',
    'primary_node'
 );

-- Which MonitorFSM[] row this event is attributed to (see
-- CurrentMonitorFSMRulePos in notifications.h for the mechanism) -- see
-- pgautofailover.sql's own comment on the pgautofailover.event table
-- definition for the full rationale of what is and isn't attributed.
ALTER TABLE pgautofailover.event
    ADD COLUMN IF NOT EXISTS rule_pos     int,
    ADD COLUMN IF NOT EXISTS rule_section pgautofailover.fsm_section;

-- Exposes the monitor's declarative dispatch table (MonitorFSM[] in
-- group_state_machine.c) to SQL, one row per rule, in first-match-wins
-- order -- see dump_fsm()'s own C-side comment for exactly what this does
-- and doesn't cover. pgautofailover.event.rule_pos/rule_section (added
-- above) join back to this view's pos column to show which rule produced
-- a given event. section is plain text here (not pgautofailover.fsm_section):
-- an api_triggered row's section names its specific operator-triggered
-- entry point too, e.g. "api_triggered: remove_node".
CREATE FUNCTION pgautofailover.dump_fsm()
RETURNS TABLE
 (
    pos                          int,
    section                      text,
    comment                      text,
    active_node_current_state    text,
    other_node_current_state     text,
    candidate_node_current_state text,
    active_node_conditions       text,
    other_node_conditions        text,
    candidate_node_conditions    text,
    group_conditions             text,
    active_node_assigned_state   pgautofailover.replication_state,
    other_node_assigned_state    pgautofailover.replication_state,
    has_extra_action             bool,
    section_path                 text
 )
LANGUAGE C SECURITY DEFINER
AS 'MODULE_PATHNAME', $$dump_fsm$$;

grant execute on function pgautofailover.dump_fsm() to autoctl_node;

-- section_path is cast to ltree here (a plain SQL cast, going through
-- Postgres's own ordinary type-casting machinery) rather than in the C
-- function itself: dump_fsm() only ever builds the dotted text, this view
-- is the sole place pgautofailover takes a dependency on ltree.
CREATE VIEW pgautofailover.fsm AS
    SELECT pos,
           section,
           comment,
           active_node_current_state,
           other_node_current_state,
           candidate_node_current_state,
           active_node_conditions,
           other_node_conditions,
           candidate_node_conditions,
           group_conditions,
           active_node_assigned_state,
           other_node_assigned_state,
           has_extra_action,
           section_path::ltree AS section_path
      FROM pgautofailover.dump_fsm()
     ORDER BY pos;

-- Flat, fully-resolved (pos, current_state, assigned_state) edges derived
-- from MonitorFSM[] -- see dump_fsm_edges()'s own C-side comment. Never
-- queried directly by an operator; check_fsm_reachability() below is built
-- on top of it.
CREATE FUNCTION pgautofailover.dump_fsm_edges()
RETURNS TABLE
 (
    pos            int,
    current_state  pgautofailover.replication_state,
    assigned_state pgautofailover.replication_state
 )
LANGUAGE C SECURITY DEFINER
AS 'MODULE_PATHNAME', $$dump_fsm_edges$$;

grant execute on function pgautofailover.dump_fsm_edges() to autoctl_node;

-- Compares the monitor's own declarative dispatch table against a keeper's
-- KeeperFSM[] edges (serialized to JSON by KeeperFSMToJSON(),
-- src/bin/pg_autoctl/fsm.c, and sent here by "pg_autoctl inspect fsm check")
-- and returns every monitor edge with no matching keeper entry -- an empty
-- result means every transition the monitor can ever assign has somewhere
-- for the keeper to go. keeper_edges is expected to be a jsonb array of
-- {"current": ..., "assigned": ...} objects, one per KeeperFSMTransition
-- row. "current" can be the literal string "any" (KeeperFSMToJSON()'s own
-- sentinel for a row whose real .current is ANY_STATE): matched against
-- every e.current_state without ever casting it to
-- pgautofailover.replication_state, via a CASE (not "k.current = 'any' OR
-- k.current::...= e.current_state", which does not reliably guarantee the
-- cast is skipped once the left side matches -- CASE WHEN/THEN is the only
-- construct Postgres guarantees short-circuits). Any other current value,
-- and "assigned" always, still go through the enum cast unconditionally --
-- a keeper reporting a state name this enum doesn't recognize still fails
-- loudly, with a real cast error, rather than silently never matching.
CREATE FUNCTION pgautofailover.check_fsm_reachability(keeper_edges jsonb)
RETURNS TABLE
 (
    pos            int,
    current_state  pgautofailover.replication_state,
    assigned_state pgautofailover.replication_state,
    comment        text
 )
LANGUAGE sql
AS $$
    SELECT e.pos, e.current_state, e.assigned_state, f.comment
      FROM pgautofailover.dump_fsm_edges() e
      JOIN pgautofailover.fsm f ON f.pos = e.pos
     WHERE NOT EXISTS (
       SELECT 1
         FROM jsonb_to_recordset(keeper_edges) AS k(current text, assigned text)
        WHERE k.assigned::pgautofailover.replication_state = e.assigned_state
          AND CASE WHEN k.current = 'any' THEN true
                   ELSE k.current::pgautofailover.replication_state = e.current_state
              END)
     ORDER BY e.pos;
$$;

grant execute on function pgautofailover.check_fsm_reachability(jsonb) to autoctl_node;

-- Re-create last_events()'s three overloads to also select the new
-- rule_pos/rule_section columns added to pgautofailover.event above.
-- Return type is SETOF pgautofailover.event (the whole row type), which
-- already reflects the table's new columns automatically once ALTER TABLE
-- above has run -- only the function bodies' own column lists need
-- updating, via CREATE OR REPLACE (same signature, so no DROP needed).
CREATE OR REPLACE FUNCTION pgautofailover.last_events
 (
  count int default 10
 )
RETURNS SETOF pgautofailover.event LANGUAGE SQL STRICT
AS $$
with last_events as
(
  select eventid, eventtime, formationid,
         nodeid, groupid, nodename, nodehost, nodeport,
         reportedstate, goalstate,
         reportedrepstate, reportedtli, reportedlsn,
         candidatepriority, replicationquorum, description,
         rule_pos, rule_section
    from pgautofailover.event
order by eventid desc
   limit count
)
select * from last_events order by eventtime, eventid;
$$;

CREATE OR REPLACE FUNCTION pgautofailover.last_events
 (
  formation_id text default 'default',
  count        int  default 10
 )
RETURNS SETOF pgautofailover.event LANGUAGE SQL STRICT
AS $$
with last_events as
(
    select eventid, eventtime, formationid,
           nodeid, groupid, nodename, nodehost, nodeport,
           reportedstate, goalstate,
           reportedrepstate, reportedtli, reportedlsn,
           candidatepriority, replicationquorum, description,
           rule_pos, rule_section
      from pgautofailover.event
     where formationid = formation_id
  order by eventid desc
     limit count
)
select * from last_events order by eventtime, eventid;
$$;

CREATE OR REPLACE FUNCTION pgautofailover.last_events
 (
  formation_id text,
  group_id     int,
  count        int default 10
 )
RETURNS SETOF pgautofailover.event LANGUAGE SQL STRICT
AS $$
with last_events as
(
    select eventid, eventtime, formationid,
           nodeid, groupid, nodename, nodehost, nodeport,
           reportedstate, goalstate,
           reportedrepstate, reportedtli, reportedlsn,
           candidatepriority, replicationquorum, description,
           rule_pos, rule_section
      from pgautofailover.event
     where formationid = formation_id
       and groupid = group_id
  order by eventid desc
     limit count
)
select * from last_events order by eventtime, eventid;
$$;


--
-- Archiving & Disaster Recovery, milestone 1: schema + monitor API only
-- (#TODO -- update with the actual PR number once opened). See
-- ~/dev/temp/archiving-disaster-recovery.md for the full design, and
-- pgautofailover.sql's own comments on each object below (this mirrors
-- that file's DDL, applied incrementally to an existing 2.2 install
-- instead of as part of a fresh CREATE EXTENSION).
--

-- New terminal state for a pgautofailover.node row representing an
-- ARCHIVING membership (see haspgdata below) rather than an ordinary
-- Postgres instance. Safe to add live: nothing in this script uses the
-- new value in the same transaction it's added in.
ALTER TYPE pgautofailover.replication_state ADD VALUE 'archiving';

-- true for every ordinary Postgres node (its own PGDATA, promotable);
-- false only for an ARCHIVING membership row (a pg_receivewal client,
-- no PGDATA, no postmaster to manage). See archiving-disaster-recovery
-- design: this single boolean is what candidate_priority enforcement,
-- keeper_ensure_current_state's liveness check, and the FAST_FORWARD
-- source-selection branch all key off, instead of a third node-kind
-- value -- a cascading follower is still haspgdata = true, and a
-- future proxy never becomes a pgautofailover.node row at all.
ALTER TABLE pgautofailover.node
    ADD COLUMN IF NOT EXISTS haspgdata bool NOT NULL DEFAULT true;

-- The old "any nodehost:port can only be a unique node in the system"
-- constraint (unconditional UNIQUE (nodehost, nodeport), added in
-- 1.5--1.6) can't hold for ARCHIVING rows: one archiver's (hostname, 0)
-- pair is deliberately shared across every group it serves. Replace it
-- with the same partial unique index pgautofailover.sql's fresh-install
-- table definition uses, scoped to haspgdata rows only.
--
-- The live constraint name is node_nodehost_nodeport_key1, not the
-- "expected" node_nodehost_nodeport_key: both 1.3--1.4 and 1.5--1.6
-- separately recreate pgautofailover.node with their own unnamed
-- UNIQUE (nodehost, nodeport), so by the time a real install reaches
-- 2.2 (via the only upgrade path that exists -- there is no standalone
-- "--2.2.sql", so even a "fresh" VERSION '2.2' install runs this same
-- incremental chain from 1.0), Postgres has already disambiguated the
-- second one with a "1" suffix. Verified empirically against a real
-- 1.0 -> ... -> 2.2 -> 2.3 upgrade, not guessed from naming convention.
ALTER TABLE pgautofailover.node
    DROP CONSTRAINT IF EXISTS node_nodehost_nodeport_key1;

CREATE UNIQUE INDEX IF NOT EXISTS node_nodehost_nodeport_haspgdata_idx
    ON pgautofailover.node (nodehost, nodeport)
 WHERE haspgdata;

--
--
-- Archiving & Disaster Recovery: schema for the Archiver process identity,
-- ARCHIVING node memberships, base-backup policy/history, and PITR.
-- See ~/dev/temp/archiving-disaster-recovery.md for the full design.
--
-- Milestone 1 (schema + monitor API only): every function here is plain
-- plpgsql/SQL, callable directly with no service_archiver process running
-- -- the pgaftest coverage for this milestone exercises these functions
-- via direct SQL calls against a plain cluster.
--

CREATE TYPE pgautofailover.storage_method
    AS ENUM ('local', 'rclone');

CREATE TYPE pgautofailover.basebackup_source
    AS ENUM ('live', 'replay');

CREATE TYPE pgautofailover.basebackup_replay_mode
    AS ENUM ('volatile', 'persistent');

CREATE TYPE pgautofailover.basebackup_cache
    AS ENUM ('local', 'none');

CREATE TYPE pgautofailover.basebackup_status
    AS ENUM ('in_progress', 'complete', 'failed', 'deleted');
    -- 'deleted' is what makes basebackup a full history rather
    -- than just a live catalog

-- shared or per-archiver base-backup production/retention policy
CREATE TABLE pgautofailover.basebackup_policy
 (
    basebackuppolicyid   bigserial PRIMARY KEY,
    policyname           text UNIQUE,

    source     pgautofailover.basebackup_source
                         NOT NULL DEFAULT 'replay',
    replaymode pgautofailover.basebackup_replay_mode
                         DEFAULT 'volatile',
    cache      pgautofailover.basebackup_cache
                         NOT NULL DEFAULT 'local',

    -- strong, ready-to-use-as-is defaults -- nightly, 3 days retention
    frequency            interval NOT NULL DEFAULT '24 hours',
    maxcount             int NOT NULL DEFAULT 3,
    maxage               interval NOT NULL DEFAULT '3 days',
    onpromotion          bool NOT NULL DEFAULT true,

    -- backpressure: cap on simultaneous base-backup production jobs,
    -- per archiver, per referencing policy
    concurrency          int NOT NULL DEFAULT 1,

    CHECK (source <> 'replay' OR replaymode IS NOT NULL),
    CHECK (concurrency >= 1)
 );

INSERT INTO pgautofailover.basebackup_policy (policyname) VALUES ('default');

-- the physical Archiver entity: one row per archiver host/process
CREATE TABLE pgautofailover.archiver
 (
    archiverid       bigserial PRIMARY KEY,
    archivername     text NOT NULL,
    hostname         text NOT NULL,
    createdat        timestamptz NOT NULL DEFAULT now(),

    -- same convention as pgautofailover.node.region: a free-form label for
    -- the data-centre or availability zone this archiver runs in, purely
    -- informational (get_archivers()'s own consumers, e.g. pg_autoctl
    -- watch, may display it) -- set at registration time via
    -- register_archiver()'s own region parameter, never inferred. Multiple
    -- archivers can attach to the very same formation (archiver_add_
    -- formation() names each ARCHIVING node row after its own archiverid,
    -- so two different archivers never collide there) -- distinct regions
    -- is the expected shape for geographically-redundant DR coverage of
    -- one formation, and archiver_policy's own archiverquorum column
    -- already anticipates requiring more than one archiver's confirmation.
    region           text not null default 'default',

    basebackuppolicyid bigint NOT NULL
                       REFERENCES pgautofailover.basebackup_policy (basebackuppolicyid),

    autoregister     bool NOT NULL DEFAULT true,

    -- cap on resident 'warm-standby' archiver_node rows (either cadence)
    -- this host is allowed to keep running at once
    maxresidentreplay int NOT NULL DEFAULT 1,

    -- storage stats for the archiver's own PGDATA (walcache + basebackups,
    -- same root -- see service_archiver_serve.c's own header comment on
    -- why an archiver has no other pgdata to speak of), reported
    -- periodically by service_archiver_loop(); NULL until the first report.
    -- usedbytes is this archiver's own footprint (directory_size() over its
    -- whole pgdata); freebytes is the containing filesystem's available
    -- space (statvfs's f_bavail, "available to a non-privileged process" --
    -- the number that actually predicts whether the next base backup or
    -- WAL segment fits, not f_bfree's superuser-reserved total).
    usedbytes        bigint,
    freebytes        bigint,

    lastreporttime   timestamptz,

    UNIQUE (archivername),
    CHECK (maxresidentreplay >= 0),
    CHECK (usedbytes IS NULL OR usedbytes >= 0),
    CHECK (freebytes IS NULL OR freebytes >= 0)
 );

-- a named, shareable rclone remote configuration -- the literal contents
-- of an rclone config file (real INI format, exactly as rclone itself
-- reads it: https://rclone.org/docs/#config-file). `config` should hold
-- only the non-secret, architectural half of an rclone remote (type,
-- provider, endpoint, region, acl, and a `type = alias` remote baking in
-- the bucket/prefix) -- credentials belong in the archiver process's own
-- environment (RCLONE_CONFIG_<REMOTE>_<KEY>), never in this column, which
-- is backed up and readable by anyone with SQL access to the monitor.
CREATE TABLE pgautofailover.rclone_config
 (
    rcloneconfigid   bigserial PRIMARY KEY,
    name             text UNIQUE NOT NULL,
    config           text NOT NULL,
    createdat        timestamptz NOT NULL DEFAULT now()
 );

-- 1-N: an archiver's storage targets. Exactly one 'local' row always
-- exists (the mandatory default); adding cloud storage means adding one
-- or more 'rclone' rows, each an independent push target, each
-- referencing a (possibly shared) rclone_config row
CREATE TABLE pgautofailover.archiver_storage
 (
    archiverstorageid  bigserial PRIMARY KEY,
    archiverid         bigint NOT NULL REFERENCES pgautofailover.archiver (archiverid)
                              ON DELETE CASCADE,
    storagemethod      pgautofailover.storage_method NOT NULL,

    storagepath        text,  -- 'local' only: override the default topdir path
    rcloneconfigid     bigint REFERENCES pgautofailover.rclone_config (rcloneconfigid),
                       -- 'rclone' only: which named config this target uses

    createdat          timestamptz NOT NULL DEFAULT now(),

    CHECK (storagemethod <> 'local' OR rcloneconfigid IS NULL),
    CHECK (storagemethod <> 'rclone' OR rcloneconfigid IS NOT NULL)
 );

CREATE UNIQUE INDEX archiver_storage_one_local
    ON pgautofailover.archiver_storage (archiverid)
    WHERE storagemethod = 'local';

-- formation-granularity attachment. Only holds explicit rows for the
-- restricted case -- when autoregister is true this table isn't consulted
CREATE TABLE pgautofailover.archiver_formation
 (
    archiverid    bigint NOT NULL REFERENCES pgautofailover.archiver (archiverid)
                         ON DELETE CASCADE,
    formationid   text NOT NULL REFERENCES pgautofailover.formation (formationid)
                         ON DELETE CASCADE,
    attachedat    timestamptz NOT NULL DEFAULT now(),

    PRIMARY KEY (archiverid, formationid)
 );

-- policy override, resolved formation-default then group-specific;
-- groupid IS NULL means "the formation-wide default for this archiver"
CREATE TABLE pgautofailover.archiver_policy
 (
    formationid        text NOT NULL REFERENCES pgautofailover.formation (formationid)
                              ON DELETE CASCADE,
    groupid            int,
    archiverquorum     int NOT NULL DEFAULT 1,
    basebackuppolicyid bigint
                       REFERENCES pgautofailover.basebackup_policy (basebackuppolicyid),
    replicationquorumeligible bool NOT NULL DEFAULT false
 );

-- A plain UNIQUE (formationid, groupid) constraint would not actually
-- enforce "at most one formation-wide default row": Postgres treats every
-- NULL groupid as distinct from every other NULL for uniqueness purposes,
-- so two formation-wide rows for the same formation would never conflict.
-- coalesce(groupid, -1) normalizes NULL to a real, comparable value
-- instead -- -1 is safe as a stand-in since groupid is otherwise always
-- >= 0. set_archiver_policy's own ON CONFLICT targets this index.
CREATE UNIQUE INDEX archiver_policy_formation_group_idx
    ON pgautofailover.archiver_policy (formationid, coalesce(groupid, -1));

-- one row per base backup taken by any archiver -- full history, not just
-- a live catalog: rows are never deleted by retention, only marked
-- status = 'deleted'; get_latest_basebackup filters on status = 'complete'
CREATE TABLE pgautofailover.basebackup
 (
    basebackupid     bigserial PRIMARY KEY,
    archiverid       bigint NOT NULL REFERENCES pgautofailover.archiver (archiverid)
                            ON DELETE CASCADE,
    formationid      text NOT NULL,
    groupid          int NOT NULL,
    label            text NOT NULL,
    timeline         int NOT NULL,
    startlsn         pg_lsn NOT NULL,
    endlsn           pg_lsn,

    period           tstzrange NOT NULL DEFAULT tstzrange(now(), NULL),

    -- snapshot of how this specific backup was produced, independent of
    -- whatever basebackup_policy says *now*
    source     pgautofailover.basebackup_source NOT NULL,
    replaymode pgautofailover.basebackup_replay_mode,

    sizebytes        bigint,
    storagelocation  text NOT NULL,  -- local path, or object-storage URI
    status           pgautofailover.basebackup_status
                     NOT NULL DEFAULT 'in_progress',
    deletedat        timestamptz
 );

CREATE INDEX basebackup_group_idx
    ON pgautofailover.basebackup (formationid, groupid, lower(period) DESC);

-- remote-side sync/prune tracking, one row per (basebackup, remote
-- storage target) -- a single backup can sync to several remotes
CREATE TABLE pgautofailover.basebackup_storage
 (
    basebackupid      bigint NOT NULL REFERENCES pgautofailover.basebackup (basebackupid)
                             ON DELETE CASCADE,
    archiverstorageid bigint NOT NULL REFERENCES pgautofailover.archiver_storage (archiverstorageid)
                             ON DELETE CASCADE,

    syncedat          timestamptz,
    remotelocation    text,
    deletedat         timestamptz,

    PRIMARY KEY (basebackupid, archiverstorageid)
 );

-- one row per (archiver, WAL segment) durably captured -- the real
-- backing store wal_archived() queries.
--
-- PRIMARY KEY is (formationid, groupid, walfilename, archiverid) -- the
-- hot path is wal_archived()'s lookup across every archiver holding %f
-- for this group, so this ordering makes it a direct index range scan.
--
-- FILLFACTOR 20: traffic is INSERT + DELETE, never UPDATE, but is
-- continuous and high-throughput -- a low fillfactor spreads rows across
-- more pages, reducing buffer-lock contention between concurrently
-- inserting archivers and easing autovacuum on a table that's never
-- write-quiet.
CREATE TABLE pgautofailover.archiver_wal
 (
    formationid   text NOT NULL,
    groupid       int NOT NULL,
    walfilename   text NOT NULL,
    archiverid    bigint NOT NULL REFERENCES pgautofailover.archiver (archiverid)
                         ON DELETE CASCADE,

    lsn           pg_lsn NOT NULL,
    receivedat    timestamptz NOT NULL DEFAULT now(),

    PRIMARY KEY (formationid, groupid, walfilename, archiverid)
 ) WITH (fillfactor = 20);

CREATE TYPE pgautofailover.archiver_node_kind
    AS ENUM ('wal-receiver', 'warm-standby', 'pitr');
    -- 'staging' anticipated for a later, not-yet-designed feature
    -- (periodic dev/test environments refreshed from the archiver)

CREATE TYPE pgautofailover.archiver_node_cadence
    AS ENUM ('continuous', 'scheduled');
    -- 'manual' considered (operator-driven "advance only when I say so"),
    -- not added yet -- same one-value-enum-addition cost as 'staging'

CREATE TYPE pgautofailover.pitr_status
    AS ENUM ('restoring', 'paused', 'registered', 'discarded');

-- every concrete Postgres instance an archiver hosts, derives, or is
-- otherwise associated with, beyond the archiver process itself
CREATE TABLE pgautofailover.archiver_node
 (
    archivernodeid   bigserial PRIMARY KEY,
    archiverid       bigint NOT NULL REFERENCES pgautofailover.archiver (archiverid)
                            ON DELETE CASCADE,
    kind             pgautofailover.archiver_node_kind NOT NULL,

    -- placement, uniform across every kind: NULL = colocated (local file
    -- reads, zero network); non-NULL = a separate node (remote fetch)
    hostname         text,
    pgdata           text NOT NULL,

    -- 'wal-receiver' only: which ARCHIVING row this instance backs.
    -- ON DELETE CASCADE: the ARCHIVING node row can be removed through
    -- more than one path (this schema's own archiver_remove_formation,
    -- or the ordinary pgautofailover.remove_node() every other node type
    -- already goes through) -- cascading here means every path safely
    -- cleans up this row too, instead of only the one this schema
    -- controls directly.
    nodeid           bigint REFERENCES pgautofailover.node (nodeid)
                            ON DELETE CASCADE,

    -- 'warm-standby' only: which group's WAL cache this instance replays
    formationid      text REFERENCES pgautofailover.formation (formationid),
    groupid          int,

    -- 'warm-standby' only: continuous (chases the primary continuously,
    -- eligible for nodecluster read exposure) or scheduled (advances only
    -- at basebackup_policy.frequency's cadence, paused via
    -- recovery_target_action = pause in between)
    cadence          pgautofailover.archiver_node_cadence,

    -- 'warm-standby' + cadence = 'continuous' only: opt-in read-only
    -- exposure. Enforced by CHECK, not just CLI convention -- a
    -- 'scheduled' instance is stale by up to a full frequency between
    -- cycles and must never be reachable as an ordinary read-replica
    -- connection string without that caveat
    nodecluster      text,

    -- 'pitr' only: lifecycle (restoring -> paused -> registered/discarded)
    pitrstatus       pgautofailover.pitr_status,

    createdat        timestamptz NOT NULL DEFAULT now(),

    CHECK (kind <> 'wal-receiver' OR nodeid IS NOT NULL),
    CHECK (kind = 'wal-receiver' OR nodeid IS NULL),
    CHECK (kind <> 'warm-standby'
           OR (formationid IS NOT NULL AND groupid IS NOT NULL AND cadence IS NOT NULL)),
    CHECK (kind = 'warm-standby'
           OR (formationid IS NULL AND groupid IS NULL AND cadence IS NULL)),
    CHECK (nodecluster IS NULL OR (kind = 'warm-standby' AND cadence = 'continuous')),
    CHECK (kind = 'pitr' OR pitrstatus IS NULL)
 );

CREATE TYPE pgautofailover.pitr_operation
    AS ENUM ('create', 'status', 'retarget', 'resume', 'promote',
             'register', 'discard');

-- every PITR operation, recorded -- not just current status
CREATE TABLE pgautofailover.pitr_history
 (
    pitrhistoryid     bigserial PRIMARY KEY,
    archivernodeid    bigint NOT NULL
                      REFERENCES pgautofailover.archiver_node (archivernodeid)
                      ON DELETE CASCADE,
    operation         pgautofailover.pitr_operation NOT NULL,
    occurredat        timestamptz NOT NULL DEFAULT now(),

    requestedspec     jsonb,   -- what was asked for
    observedlsn       pg_lsn,  -- what Postgres actually reported afterward
    observedtimestamp timestamptz,
    observedpausestate text,   -- verbatim: 'not paused'/'pause requested'/'paused'

    note              text
 );

CREATE INDEX pitr_history_node_idx
    ON pgautofailover.pitr_history (archivernodeid, occurredat);

CREATE VIEW pgautofailover.pitr_node_status AS
    SELECT n.archivernodeid, n.archiverid, n.hostname, n.pgdata,
           n.pitrstatus, h.operation AS lastoperation,
           h.observedlsn, h.observedtimestamp, h.observedpausestate,
           h.occurredat AS lastupdatedat
      FROM pgautofailover.archiver_node n
      LEFT JOIN LATERAL (
             SELECT * FROM pgautofailover.pitr_history
              WHERE archivernodeid = n.archivernodeid
              ORDER BY occurredat DESC LIMIT 1
           ) h ON true
     WHERE n.kind = 'pitr';

-- opt-in monitor-mediated PITR command queue, for the headless,
-- no-interactive-access deployment shape only (pg_autoctl node run
-- against a node.ini declaring kind = pitr)
CREATE TYPE pgautofailover.pitr_command
    AS ENUM ('none', 'retarget', 'pause', 'resume', 'promote',
             'register', 'discard');

CREATE TABLE pgautofailover.pitr_pending_command
 (
    archivernodeid  bigint PRIMARY KEY
                    REFERENCES pgautofailover.archiver_node (archivernodeid)
                    ON DELETE CASCADE,
    command         pgautofailover.pitr_command NOT NULL DEFAULT 'none',
    commandspec     jsonb,
    queuedat        timestamptz NOT NULL DEFAULT now()
 );


--
-- Functions
--

CREATE FUNCTION pgautofailover.create_basebackup_policy
 (
    IN policyname text,
    IN policyspec jsonb
 )
RETURNS bigint LANGUAGE plpgsql SECURITY DEFINER
AS $$
DECLARE
    new_id bigint;
BEGIN
    INSERT INTO pgautofailover.basebackup_policy
           (policyname, source, replaymode, cache,
            frequency, maxcount, maxage, onpromotion, concurrency)
    SELECT policyname,
           coalesce((policyspec->>'source')::pgautofailover.basebackup_source,
                    'replay'),
           coalesce((policyspec->>'replaymode')::pgautofailover.basebackup_replay_mode,
                    'volatile'),
           coalesce((policyspec->>'cache')::pgautofailover.basebackup_cache,
                    'local'),
           coalesce((policyspec->>'frequency')::interval, '24 hours'),
           coalesce((policyspec->>'maxcount')::int, 3),
           coalesce((policyspec->>'maxage')::interval, '3 days'),
           coalesce((policyspec->>'onpromotion')::bool, true),
           coalesce((policyspec->>'concurrency')::int, 1)
      RETURNING basebackuppolicyid INTO new_id;

    RETURN new_id;
END;
$$;

comment on function pgautofailover.create_basebackup_policy(text,jsonb)
        is 'create a named, shareable base-backup production/retention policy';

grant execute on function
      pgautofailover.create_basebackup_policy(text,jsonb)
   to autoctl_node;

CREATE FUNCTION pgautofailover.set_basebackup_policy
 (
    IN policyname text,
    IN policyspec jsonb
 )
RETURNS void LANGUAGE plpgsql SECURITY DEFINER
AS $$
BEGIN
    UPDATE pgautofailover.basebackup_policy
       SET source     = coalesce((policyspec->>'source')::pgautofailover.basebackup_source, source),
           replaymode = coalesce((policyspec->>'replaymode')::pgautofailover.basebackup_replay_mode, replaymode),
           cache      = coalesce((policyspec->>'cache')::pgautofailover.basebackup_cache, cache),
           frequency  = coalesce((policyspec->>'frequency')::interval, frequency),
           maxcount   = coalesce((policyspec->>'maxcount')::int, maxcount),
           maxage     = coalesce((policyspec->>'maxage')::interval, maxage),
           onpromotion = coalesce((policyspec->>'onpromotion')::bool, onpromotion),
           concurrency = coalesce((policyspec->>'concurrency')::int, concurrency)
     WHERE basebackup_policy.policyname = set_basebackup_policy.policyname;

    IF NOT FOUND THEN
        RAISE EXCEPTION 'basebackup_policy "%" does not exist', policyname;
    END IF;
END;
$$;

comment on function pgautofailover.set_basebackup_policy(text,jsonb)
        is 'update an existing named base-backup production/retention policy';

grant execute on function
      pgautofailover.set_basebackup_policy(text,jsonb)
   to autoctl_node;

CREATE FUNCTION pgautofailover.get_basebackup_policy(policyname text)
 RETURNS pgautofailover.basebackup_policy LANGUAGE sql STRICT SECURITY DEFINER
AS $$
    SELECT * FROM pgautofailover.basebackup_policy
     WHERE basebackup_policy.policyname = get_basebackup_policy.policyname;
$$;

comment on function pgautofailover.get_basebackup_policy(text)
        is 'fetch a named base-backup production/retention policy';

grant execute on function pgautofailover.get_basebackup_policy(text)
   to autoctl_node;

-- creates the physical Archiver entity plus its mandatory 'local'
-- archiver_storage row. basebackuppolicyid NULL resolves to 'default'.
-- rcloneconfigname, when given, also attaches an additional 'rclone' row
-- referencing that existing, already-created rclone_config -- the
-- one-command way to "start a new archiver with the same shared rclone
-- setup" another archiver already uses; omit it to start local-only and
-- attach storage later via archiver_add_storage
CREATE FUNCTION pgautofailover.register_archiver
 (
    archivername text, hostname text,
    storagepath text DEFAULT NULL,
    basebackuppolicyid bigint DEFAULT NULL,
    autoregister bool DEFAULT true,
    maxresidentreplay int DEFAULT 1,
    rcloneconfigname text DEFAULT NULL,
    region text DEFAULT 'default'
 )
 RETURNS bigint LANGUAGE plpgsql SECURITY DEFINER
AS $$
DECLARE
    new_archiverid bigint;
    resolved_policyid bigint;
BEGIN
    resolved_policyid := coalesce(
        basebackuppolicyid,
        (SELECT p.basebackuppolicyid
           FROM pgautofailover.basebackup_policy p
          WHERE p.policyname = 'default'));

    INSERT INTO pgautofailover.archiver
           (archivername, hostname, basebackuppolicyid,
            autoregister, maxresidentreplay, region)
    VALUES (archivername, hostname, resolved_policyid,
            autoregister, maxresidentreplay,
            coalesce(register_archiver.region, 'default'))
      RETURNING archiverid INTO new_archiverid;

    INSERT INTO pgautofailover.archiver_storage
           (archiverid, storagemethod, storagepath)
    VALUES (new_archiverid, 'local', storagepath);

    IF rcloneconfigname IS NOT NULL THEN
        PERFORM pgautofailover.archiver_add_storage(new_archiverid, rcloneconfigname);
    END IF;

    RETURN new_archiverid;
END;
$$;

comment on function pgautofailover.register_archiver(text,text,text,bigint,bool,int,text,text)
        is 'register a new Archiver process identity, with its mandatory local storage target';

grant execute on function
      pgautofailover.register_archiver(text,text,text,bigint,bool,int,text,text)
   to autoctl_node;

-- periodic storage heartbeat: usedbytes/freebytes/lastreporttime all move
-- together, from the same service_archiver_loop() tick (service_archiver.c)
-- that already reports this archiver's captured-WAL LSN.
CREATE FUNCTION pgautofailover.report_archiver_storage
    (archiverid bigint, usedbytes bigint, freebytes bigint)
 RETURNS void LANGUAGE sql SECURITY DEFINER
AS $$
    UPDATE pgautofailover.archiver
       SET usedbytes = report_archiver_storage.usedbytes,
           freebytes = report_archiver_storage.freebytes,
           lastreporttime = now()
     WHERE archiver.archiverid = report_archiver_storage.archiverid;
$$;

comment on function pgautofailover.report_archiver_storage(bigint,bigint,bigint)
        is 'record an archiver''s own reported disk usage and free space';

grant execute on function
      pgautofailover.report_archiver_storage(bigint,bigint,bigint)
   to autoctl_node;

-- one row per archiver attached to formationid, with its FSM state (the
-- 'wal-receiver' archiver_node row created by archiver_add_formation, one
-- per group -- a multi-group formation returns one row per (archiver,
-- group)). Used by `pg_autoctl watch`'s own archivers section.
CREATE FUNCTION pgautofailover.get_archivers
 (
   IN formationid       text default 'default',
   OUT archiver_id       bigint,
   OUT archiver_name     text,
   OUT hostname          text,
   OUT region            text,
   OUT used_bytes        bigint,
   OUT free_bytes        bigint,
   OUT last_report_time  timestamptz,
   OUT node_id           bigint,
   OUT reported_state    pgautofailover.replication_state,
   OUT goal_state        pgautofailover.replication_state
 )
RETURNS SETOF record LANGUAGE SQL STRICT SECURITY DEFINER
AS $$
   SELECT a.archiverid, a.archivername, a.hostname, a.region,
          a.usedbytes, a.freebytes, a.lastreporttime,
          n.nodeid, n.reportedstate, n.goalstate
     FROM pgautofailover.archiver a
     JOIN pgautofailover.archiver_formation af
       ON af.archiverid = a.archiverid
      AND af.formationid = get_archivers.formationid
     LEFT JOIN pgautofailover.archiver_node an
            ON an.archiverid = a.archiverid AND an.kind = 'wal-receiver'
     LEFT JOIN pgautofailover.node n
            ON n.nodeid = an.nodeid AND n.formationid = get_archivers.formationid
 ORDER BY a.archiverid;
$$;

comment on function pgautofailover.get_archivers(text)
        is 'list the archivers attached to a formation, with storage stats and FSM state';

grant execute on function pgautofailover.get_archivers(text)
   to autoctl_node;

-- named, shareable rclone config objects -- see rclone_config above for
-- what belongs in `config` (architecture only, never credentials)
CREATE FUNCTION pgautofailover.create_rclone_config(name text, config text)
 RETURNS bigint  -- rcloneconfigid
 LANGUAGE plpgsql SECURITY DEFINER
AS $$
DECLARE
    new_id bigint;
BEGIN
    INSERT INTO pgautofailover.rclone_config (name, config)
    VALUES (name, config)
      RETURNING rcloneconfigid INTO new_id;

    RETURN new_id;
END;
$$;

comment on function pgautofailover.create_rclone_config(text,text)
        is 'register a named, shareable rclone remote configuration';

grant execute on function pgautofailover.create_rclone_config(text,text)
   to autoctl_node;

CREATE FUNCTION pgautofailover.set_rclone_config(name text, config text)
 RETURNS void LANGUAGE plpgsql SECURITY DEFINER
AS $$
BEGIN
    UPDATE pgautofailover.rclone_config AS rc
       SET config = set_rclone_config.config
     WHERE rc.name = set_rclone_config.name;

    IF NOT FOUND THEN
        RAISE EXCEPTION 'rclone_config "%" does not exist', name;
    END IF;
END;
$$;

comment on function pgautofailover.set_rclone_config(text,text)
        is 'update the content of an existing named rclone configuration -- every archiver referencing it picks up the change';

grant execute on function pgautofailover.set_rclone_config(text,text)
   to autoctl_node;

CREATE FUNCTION pgautofailover.get_rclone_config(name text)
 RETURNS pgautofailover.rclone_config LANGUAGE sql STRICT
AS $$
    SELECT * FROM pgautofailover.rclone_config AS rc
     WHERE rc.name = get_rclone_config.name;
$$;

comment on function pgautofailover.get_rclone_config(text)
        is 'fetch a named rclone configuration''s raw content';

grant execute on function pgautofailover.get_rclone_config(text)
   to autoctl_node;

-- attaches an archiver to an existing, already-named rclone_config row
-- (the sharing path -- several archivers' archiver_storage rows can
-- reference the same rcloneconfigid at once, edit the config once via
-- set_rclone_config and every referencing archiver picks it up)
CREATE FUNCTION pgautofailover.archiver_add_storage
    (archiverid bigint, rcloneconfigname text)
 RETURNS bigint  -- archiverstorageid
 LANGUAGE plpgsql SECURITY DEFINER
AS $$
DECLARE
    resolved_rcloneconfigid bigint;
    new_id bigint;
BEGIN
    SELECT rc.rcloneconfigid INTO resolved_rcloneconfigid
      FROM pgautofailover.rclone_config rc
     WHERE rc.name = rcloneconfigname;

    IF resolved_rcloneconfigid IS NULL THEN
        RAISE EXCEPTION 'rclone_config "%" does not exist', rcloneconfigname;
    END IF;

    INSERT INTO pgautofailover.archiver_storage
           (archiverid, storagemethod, rcloneconfigid)
    VALUES (archiverid, 'rclone', resolved_rcloneconfigid)
      RETURNING archiverstorageid INTO new_id;

    RETURN new_id;
END;
$$;

comment on function pgautofailover.archiver_add_storage(bigint,text)
        is 'attach an additional rclone storage target to an archiver, referencing an existing named rclone_config';

grant execute on function pgautofailover.archiver_add_storage(bigint,text)
   to autoctl_node;

-- detaches only; the referenced rclone_config row is untouched and
-- keeps serving any other archiver still referencing it
CREATE FUNCTION pgautofailover.archiver_remove_storage(archiverstorageid bigint)
 RETURNS void LANGUAGE plpgsql SECURITY DEFINER
AS $$
BEGIN
    DELETE FROM pgautofailover.archiver_storage AS a_s
     WHERE a_s.archiverstorageid = archiver_remove_storage.archiverstorageid
       AND a_s.storagemethod <> 'local';

    IF NOT FOUND THEN
        RAISE EXCEPTION 'archiver_storage % does not exist, or is the mandatory local target',
              archiverstorageid;
    END IF;
END;
$$;

comment on function pgautofailover.archiver_remove_storage(bigint)
        is 'detach a non-local storage target from an archiver (the local target cannot be removed)';

grant execute on function pgautofailover.archiver_remove_storage(bigint)
   to autoctl_node;

-- fans out to one CREATE of a pgautofailover.node row (haspgdata =
-- false) per group currently in formationid
-- Parameters are prefixed in_* here (unlike this file's usual
-- function-qualified-reference convention): ON CONFLICT's own target
-- column list can't be schema/function-qualified at all (that syntax
-- only accepts bare column names or ON CONSTRAINT), so a same-named
-- parameter would still be genuinely ambiguous there even when every
-- other clause in this function could disambiguate it.
CREATE FUNCTION pgautofailover.archiver_add_formation
    (in_archiverid bigint, in_formationid text)
 RETURNS SETOF bigint LANGUAGE plpgsql SECURITY DEFINER
AS $$
DECLARE
    grp record;
    new_nodeid bigint;
BEGIN
    INSERT INTO pgautofailover.archiver_formation (archiverid, formationid)
    VALUES (in_archiverid, in_formationid)
       ON CONFLICT (archiverid, formationid) DO NOTHING;

    FOR grp IN
        SELECT DISTINCT n.groupid
          FROM pgautofailover.node n
         WHERE n.formationid = in_formationid
    LOOP
        new_nodeid := NULL;

        -- nodeport = 0 is a permanent sentinel, not an M1 stopgap: an
        -- ARCHIVING row has no postmaster of its own to be reachable on,
        -- so nodehost:nodeport isn't a connectable address here the way
        -- it is for every haspgdata row -- see node_nodehost_nodeport_
        -- haspgdata_idx's own comment, which is exactly why that unique
        -- index is scoped to haspgdata rows only. reportedstate starts at
        -- 'wait_standby', same as any freshly-registered node -- it only
        -- reaches 'archiving' once a real keeper's pg_receivewal is
        -- actually running.
        --
        -- ON CONFLICT DO NOTHING on (formationid, nodename): this
        -- function must be safe to call again for a formation some of
        -- whose groups are already attached -- an operator re-running it
        -- on purpose, or the archiver's own reconciler picking up a
        -- newly-added Citus worker group -- without failing on every
        -- group that was already covered by an earlier call. A skipped
        -- insert leaves new_nodeid NULL (no row returned), handled below.
        INSERT INTO pgautofailover.node
               (formationid, groupid, nodename, nodehost, nodeport,
                goalstate, reportedstate, haspgdata, candidatepriority,
                replicationquorum)
        VALUES (in_formationid, grp.groupid,
                'archiver-' || in_archiverid || '-' || grp.groupid,
                (SELECT a.hostname FROM pgautofailover.archiver a
                  WHERE a.archiverid = in_archiverid),
                0,
                'wait_standby', 'wait_standby', false, 0, false)
        ON CONFLICT (formationid, nodename) DO NOTHING
          RETURNING nodeid INTO new_nodeid;

        IF new_nodeid IS NULL THEN
            -- this group was already attached by an earlier call --
            -- nothing new to report for it, and archiver_node already
            -- has its row from that earlier call too.
            CONTINUE;
        END IF;

        INSERT INTO pgautofailover.archiver_node
               (archiverid, kind, pgdata, nodeid)
        VALUES (in_archiverid, 'wal-receiver',
                '', new_nodeid);

        RETURN NEXT new_nodeid;
    END LOOP;

    RETURN;
END;
$$;

comment on function pgautofailover.archiver_add_formation(bigint,text)
        is 'attach an archiver to every group of a formation, creating one lightweight ARCHIVING node row per group not already attached';

grant execute on function pgautofailover.archiver_add_formation(bigint,text)
   to autoctl_node;

-- one row per (formation, group) an archiver currently holds a
-- 'wal-receiver' membership in, across every formation it is attached
-- to -- what an archiver process itself calls, at startup and
-- periodically thereafter, to discover the full set of WAL streams and
-- base-backup schedules it is responsible for running (see
-- service_archiver_reconciler.c). Unlike get_archivers() above (scoped
-- to one formation, for `pg_autoctl watch`'s own display), this is
-- scoped to one archiver, across all its formations.
CREATE FUNCTION pgautofailover.list_archiver_memberships
 (
   IN archiverid         bigint,
   OUT formation_id       text,
   OUT group_id           int,
   OUT node_id            bigint,
   OUT reported_state     pgautofailover.replication_state,
   OUT goal_state         pgautofailover.replication_state
 )
RETURNS SETOF record LANGUAGE SQL STRICT SECURITY DEFINER
AS $$
    SELECT n.formationid, n.groupid, n.nodeid, n.reportedstate, n.goalstate
      FROM pgautofailover.archiver_node an
      JOIN pgautofailover.node n ON n.nodeid = an.nodeid
     WHERE an.archiverid = list_archiver_memberships.archiverid
       AND an.kind = 'wal-receiver'
  ORDER BY n.formationid, n.groupid;
$$;

comment on function pgautofailover.list_archiver_memberships(bigint)
        is 'list every (formation, group) membership an archiver currently belongs to, across every formation it is attached to';

grant execute on function pgautofailover.list_archiver_memberships(bigint)
   to autoctl_node;

-- Deleting the node row is enough: archiver_node.nodeid's own
-- ON DELETE CASCADE removes the matching wal-receiver archiver_node row
-- automatically (see that column's own comment).
CREATE FUNCTION pgautofailover.archiver_remove_formation
    (archiverid bigint, formationid text)
 RETURNS void LANGUAGE plpgsql SECURITY DEFINER
AS $$
BEGIN
    DELETE FROM pgautofailover.node n
     WHERE n.formationid = archiver_remove_formation.formationid
       AND n.nodeid IN (SELECT an.nodeid
                           FROM pgautofailover.archiver_node an
                          WHERE an.archiverid = archiver_remove_formation.archiverid
                            AND an.kind = 'wal-receiver');

    DELETE FROM pgautofailover.archiver_formation af
     WHERE af.archiverid = archiver_remove_formation.archiverid
       AND af.formationid = archiver_remove_formation.formationid;
END;
$$;

comment on function pgautofailover.archiver_remove_formation(bigint,text)
        is 'detach an archiver from a formation, removing its ARCHIVING node row in every group';

grant execute on function pgautofailover.archiver_remove_formation(bigint,text)
   to autoctl_node;

-- in_* parameters: see archiver_add_formation's own comment on why an ON
-- CONFLICT target list (which can't be qualified, even inside an
-- expression like coalesce(groupid, -1)) forces this naming here.
CREATE FUNCTION pgautofailover.set_archiver_policy
 (
    in_formationid text, in_groupid int DEFAULT NULL,
    in_archiverquorum int DEFAULT NULL,
    in_basebackuppolicyid bigint DEFAULT NULL,
    in_replicationquorumeligible bool DEFAULT NULL
 )
 RETURNS void LANGUAGE plpgsql SECURITY DEFINER
AS $$
BEGIN
    INSERT INTO pgautofailover.archiver_policy
           (formationid, groupid, archiverquorum,
            basebackuppolicyid, replicationquorumeligible)
    VALUES (in_formationid, in_groupid,
            coalesce(in_archiverquorum, 1),
            in_basebackuppolicyid,
            coalesce(in_replicationquorumeligible, false))
       ON CONFLICT (formationid, (coalesce(groupid, -1))) DO UPDATE
       SET archiverquorum = coalesce(EXCLUDED.archiverquorum,
                                     pgautofailover.archiver_policy.archiverquorum),
           basebackuppolicyid = coalesce(EXCLUDED.basebackuppolicyid,
                                         pgautofailover.archiver_policy.basebackuppolicyid),
           replicationquorumeligible = coalesce(EXCLUDED.replicationquorumeligible,
                                               pgautofailover.archiver_policy.replicationquorumeligible);
END;
$$;

comment on function pgautofailover.set_archiver_policy(text,int,int,bigint,bool)
        is 'set (or override) archiver_quorum/basebackup policy/replication-quorum eligibility for a formation, or one of its groups';

grant execute on function
      pgautofailover.set_archiver_policy(text,int,int,bigint,bool)
   to autoctl_node;

-- resolves group-specific override first, then the formation-wide
-- (groupid IS NULL) default, then this schema's own hardcoded defaults.
-- Deliberately plpgsql, not a single SQL query: an earlier draft tried to
-- express the three-way fallback as one UNION ALL ... LIMIT 1 query, but
-- UNION ALL has no ordering guarantee across its branches, so LIMIT 1
-- could just as easily return the formation-wide or hardcoded default
-- even when a group-specific override exists. Sequential SELECT INTO ...
-- IF FOUND is unambiguous.
CREATE FUNCTION pgautofailover.get_archiver_policy(formationid text, groupid int)
 RETURNS TABLE (archiverquorum int, basebackuppolicyid bigint,
                replicationquorumeligible bool)
 LANGUAGE plpgsql STABLE
AS $$
BEGIN
    RETURN QUERY
    SELECT ap.archiverquorum, ap.basebackuppolicyid, ap.replicationquorumeligible
      FROM pgautofailover.archiver_policy ap
     WHERE ap.formationid = get_archiver_policy.formationid
       AND ap.groupid = get_archiver_policy.groupid;

    IF FOUND THEN
        RETURN;
    END IF;

    RETURN QUERY
    SELECT ap.archiverquorum, ap.basebackuppolicyid, ap.replicationquorumeligible
      FROM pgautofailover.archiver_policy ap
     WHERE ap.formationid = get_archiver_policy.formationid
       AND ap.groupid IS NULL;

    IF FOUND THEN
        RETURN;
    END IF;

    RETURN QUERY
    SELECT 1, p.basebackuppolicyid, false
      FROM pgautofailover.basebackup_policy p
     WHERE p.policyname = 'default';
END;
$$;

comment on function pgautofailover.get_archiver_policy(text,int)
        is 'resolve archiver policy for (formation, group): group override, else formation default, else this schema''s own defaults';

grant execute on function pgautofailover.get_archiver_policy(text,int)
   to autoctl_node;

-- one round trip from the archiver-basebackup side: resolves the
-- basebackup_policy row that applies to (formation, group) via get_
-- archiver_policy() above, then flattens its interval columns to plain
-- integer seconds -- easy time_t arithmetic on the C side, no interval-
-- text parsing needed. SECURITY DEFINER: reads archiver_policy/
-- basebackup_policy directly, both created (like every table in this
-- milestone's own schema) after the blanket "GRANT SELECT ON ALL TABLES"
-- near the top of this file, so autoctl_node has no direct grant on
-- either -- same class of gap already hit (and fixed) twice for wal_
-- archived()/get_latest_basebackup().
CREATE FUNCTION pgautofailover.get_basebackup_policy_for_group
 (
    formationid           text,
    groupid               int,
    OUT policyname         text,
    OUT source             pgautofailover.basebackup_source,
    OUT replaymode         pgautofailover.basebackup_replay_mode,
    OUT cache              pgautofailover.basebackup_cache,
    OUT frequency_seconds  int,
    OUT maxcount           int,
    OUT maxage_seconds     int,
    OUT onpromotion        bool,
    OUT concurrency        int
 )
 RETURNS record LANGUAGE plpgsql STABLE SECURITY DEFINER
AS $$
DECLARE
    ap record;
BEGIN
    SELECT * INTO ap
      FROM pgautofailover.get_archiver_policy(
               get_basebackup_policy_for_group.formationid,
               get_basebackup_policy_for_group.groupid);

    SELECT p.policyname, p.source, p.replaymode, p.cache,
           extract(epoch FROM p.frequency)::int,
           p.maxcount,
           extract(epoch FROM p.maxage)::int,
           p.onpromotion, p.concurrency
      INTO policyname, source, replaymode, cache, frequency_seconds,
           maxcount, maxage_seconds, onpromotion, concurrency
      FROM pgautofailover.basebackup_policy p
     WHERE p.basebackuppolicyid = ap.basebackuppolicyid;
END;
$$;

comment on function pgautofailover.get_basebackup_policy_for_group(text,int)
        is 'resolve the full base-backup production/retention policy for (formation, group), intervals flattened to seconds';

grant execute on function pgautofailover.get_basebackup_policy_for_group(text,int)
   to autoctl_node;

-- the archive_command confirmation check: true iff at least
-- archiver_quorum distinct archivers have durably reported %f
CREATE FUNCTION pgautofailover.wal_archived
    (formationid text, groupid int, walfilename text)
 RETURNS bool
 LANGUAGE sql STABLE SECURITY DEFINER
AS $$
    SELECT count(DISTINCT aw.archiverid) >=
           (SELECT archiverquorum
              FROM pgautofailover.get_archiver_policy(wal_archived.formationid,
                                                       wal_archived.groupid))
      FROM pgautofailover.archiver_wal aw
     WHERE aw.formationid = wal_archived.formationid
       AND aw.groupid = wal_archived.groupid
       AND aw.walfilename = wal_archived.walfilename;
$$;

comment on function pgautofailover.wal_archived(text,int,text)
        is 'archive_command confirmation check: has segment %f already landed durably on archiver_quorum archiver(s)?';

grant execute on function pgautofailover.wal_archived(text,int,text)
   to autoctl_node;

-- inserts into archiver_wal (idempotent on conflict)
-- in_* parameters: see archiver_add_formation's own comment on why an ON
-- CONFLICT target list (which can't be qualified) forces this naming here.
CREATE FUNCTION pgautofailover.report_wal_received
    (in_nodeid bigint, in_walfilename text, in_lsn pg_lsn)
 RETURNS void LANGUAGE plpgsql SECURITY DEFINER
AS $$
DECLARE
    target record;
BEGIN
    SELECT n.formationid, n.groupid, an.archiverid
      INTO target
      FROM pgautofailover.archiver_node an
      JOIN pgautofailover.node n ON n.nodeid = an.nodeid
     WHERE an.nodeid = in_nodeid
       AND an.kind = 'wal-receiver';

    IF NOT FOUND THEN
        RAISE EXCEPTION 'node % is not an ARCHIVING wal-receiver node', in_nodeid;
    END IF;

    INSERT INTO pgautofailover.archiver_wal
           (formationid, groupid, walfilename, archiverid, lsn)
    VALUES (target.formationid, target.groupid, in_walfilename, target.archiverid, in_lsn)
       ON CONFLICT (formationid, groupid, walfilename, archiverid) DO NOTHING;
END;
$$;

comment on function pgautofailover.report_wal_received(bigint,text,pg_lsn)
        is 'reports a WAL segment durably captured by an ARCHIVING node';

grant execute on function pgautofailover.report_wal_received(bigint,text,pg_lsn)
   to autoctl_node;

CREATE FUNCTION pgautofailover.report_basebackup_started
 (
    archiverid bigint, formationid text, groupid int,
    label text, timeline int, startlsn pg_lsn,
    source pgautofailover.basebackup_source,
    replaymode pgautofailover.basebackup_replay_mode DEFAULT NULL
 )
 RETURNS bigint LANGUAGE plpgsql SECURITY DEFINER
AS $$
DECLARE
    new_id bigint;
BEGIN
    INSERT INTO pgautofailover.basebackup
           (archiverid, formationid, groupid, label, timeline, startlsn,
            source, replaymode, storagelocation, status)
    VALUES (archiverid, formationid, groupid, label, timeline, startlsn,
            source, replaymode, '', 'in_progress')
      RETURNING basebackupid INTO new_id;

    RETURN new_id;
END;
$$;

comment on function pgautofailover.report_basebackup_started
     (bigint,text,int,text,int,pg_lsn,pgautofailover.basebackup_source,pgautofailover.basebackup_replay_mode)
        is 'records the start of a new base-backup production job';

grant execute on function
      pgautofailover.report_basebackup_started
        (bigint,text,int,text,int,pg_lsn,pgautofailover.basebackup_source,pgautofailover.basebackup_replay_mode)
   to autoctl_node;

CREATE FUNCTION pgautofailover.report_basebackup_completed
    (basebackupid bigint, endlsn pg_lsn, sizebytes bigint, storagelocation text)
 RETURNS void LANGUAGE plpgsql SECURITY DEFINER
AS $$
BEGIN
    UPDATE pgautofailover.basebackup AS bb
       SET endlsn = report_basebackup_completed.endlsn,
           sizebytes = report_basebackup_completed.sizebytes,
           storagelocation = report_basebackup_completed.storagelocation,
           status = 'complete',
           period = tstzrange(lower(bb.period), now())
     WHERE bb.basebackupid = report_basebackup_completed.basebackupid;

    IF NOT FOUND THEN
        RAISE EXCEPTION 'basebackup % does not exist', basebackupid;
    END IF;
END;
$$;

comment on function pgautofailover.report_basebackup_completed(bigint,pg_lsn,bigint,text)
        is 'records the successful completion of a base-backup production job';

grant execute on function
      pgautofailover.report_basebackup_completed(bigint,pg_lsn,bigint,text)
   to autoctl_node;

-- marks the basebackup row deleted (never a real DELETE), then prunes
-- any archiver_wal rows this group no longer needs to retain
CREATE FUNCTION pgautofailover.report_basebackup_deleted(basebackupid bigint)
 RETURNS void LANGUAGE plpgsql SECURITY DEFINER
AS $$
DECLARE
    bb record;
BEGIN
    UPDATE pgautofailover.basebackup AS b
       SET status = 'deleted', deletedat = now()
     WHERE b.basebackupid = report_basebackup_deleted.basebackupid
      RETURNING b.formationid, b.groupid INTO bb;

    IF NOT FOUND THEN
        RAISE EXCEPTION 'basebackup % does not exist', basebackupid;
    END IF;

    PERFORM pgautofailover.prune_archiver_wal(bb.formationid, bb.groupid);
END;
$$;

comment on function pgautofailover.report_basebackup_deleted(bigint)
        is 'marks a base backup deleted (retains history) and prunes any archiver_wal rows no group backup needs anymore';

grant execute on function pgautofailover.report_basebackup_deleted(bigint)
   to autoctl_node;

-- deletes every archiver_wal row for (formationid, groupid) older than
-- the earliest still-'complete' basebackup's startlsn, across every
-- archiver holding a copy. When no 'complete' backup remains for this
-- group, nothing is pruned -- there is no anchor point to replay forward
-- from, so every captured segment is still needed.
CREATE FUNCTION pgautofailover.prune_archiver_wal(formationid text, groupid int)
 RETURNS bigint LANGUAGE plpgsql SECURITY DEFINER
AS $$
DECLARE
    oldest_startlsn pg_lsn;
    deleted_count bigint;
BEGIN
    SELECT min(b.startlsn) INTO oldest_startlsn
      FROM pgautofailover.basebackup b
     WHERE b.formationid = prune_archiver_wal.formationid
       AND b.groupid = prune_archiver_wal.groupid
       AND b.status = 'complete';

    IF oldest_startlsn IS NULL THEN
        RETURN 0;
    END IF;

    WITH deleted AS (
        DELETE FROM pgautofailover.archiver_wal aw
         WHERE aw.formationid = prune_archiver_wal.formationid
           AND aw.groupid = prune_archiver_wal.groupid
           AND aw.lsn < oldest_startlsn
        RETURNING 1
    )
    SELECT count(*) INTO deleted_count FROM deleted;

    RETURN deleted_count;
END;
$$;

comment on function pgautofailover.prune_archiver_wal(text,int)
        is 'deletes archiver_wal rows for (formation, group) older than the oldest still-complete base backup''s startlsn';

grant execute on function pgautofailover.prune_archiver_wal(text,int)
   to autoctl_node;

-- in_* parameters: see archiver_add_formation's own comment on why an ON
-- CONFLICT target list (which can't be qualified) forces this naming here.
CREATE FUNCTION pgautofailover.report_basebackup_synced
    (in_basebackupid bigint, in_archiverstorageid bigint, in_remotelocation text)
 RETURNS void LANGUAGE plpgsql SECURITY DEFINER
AS $$
BEGIN
    INSERT INTO pgautofailover.basebackup_storage
           (basebackupid, archiverstorageid, syncedat, remotelocation)
    VALUES (in_basebackupid, in_archiverstorageid, now(), in_remotelocation)
       ON CONFLICT (basebackupid, archiverstorageid) DO UPDATE
       SET syncedat = now(),
           remotelocation = EXCLUDED.remotelocation;
END;
$$;

comment on function pgautofailover.report_basebackup_synced(bigint,bigint,text)
        is 'records a successful cold-storage sync of a base backup to one storage target';

grant execute on function
      pgautofailover.report_basebackup_synced(bigint,bigint,text)
   to autoctl_node;

CREATE FUNCTION pgautofailover.report_basebackup_remote_deleted
    (basebackupid bigint, archiverstorageid bigint)
 RETURNS void LANGUAGE plpgsql SECURITY DEFINER
AS $$
BEGIN
    UPDATE pgautofailover.basebackup_storage AS bs
       SET deletedat = now()
     WHERE bs.basebackupid = report_basebackup_remote_deleted.basebackupid
       AND bs.archiverstorageid = report_basebackup_remote_deleted.archiverstorageid;
END;
$$;

comment on function pgautofailover.report_basebackup_remote_deleted(bigint,bigint)
        is 'records that a base backup''s remote copy on one storage target has been pruned';

grant execute on function
      pgautofailover.report_basebackup_remote_deleted(bigint,bigint)
   to autoctl_node;

-- filters status = 'complete' only. SECURITY DEFINER matches every other
-- autoctl_node-callable helper reading a table that role has no direct
-- SELECT grant on (e.g. archiver_add_formation) -- autoctl_node is only
-- ever granted EXECUTE on the function, never SELECT on pgautofailover.
-- basebackup itself.
--
-- preferred_source (default NULL, meaning "any") exists for service_
-- archiver_serve.c's own routes-file refresh: a 'replay' backup promotes a
-- throwaway extracted copy, which genuinely puts it on a *later* timeline
-- than whatever the archiver's own walcache has actually captured (which
-- only ever advances on the real primary's timeline) -- serving that pair
-- together breaks a real pg_basebackup's own timeline consistency check
-- (receivelog.c). Since a 'live' backup is taken directly from the
-- actively-followed primary, it always shares the walcache's timeline by
-- construction; passing preferred_source = 'live' is how the routes
-- refresh asks for one specifically, rather than "whatever is newest
-- regardless of type".
CREATE FUNCTION pgautofailover.get_latest_basebackup
 (
    formationid text,
    groupid int,
    preferred_source pgautofailover.basebackup_source default NULL
 )
 RETURNS pgautofailover.basebackup LANGUAGE sql STABLE SECURITY DEFINER
AS $$
    SELECT * FROM pgautofailover.basebackup b
     WHERE b.formationid = get_latest_basebackup.formationid
       AND b.groupid = get_latest_basebackup.groupid
       AND b.status = 'complete'
       AND (get_latest_basebackup.preferred_source IS NULL
            OR b.source = get_latest_basebackup.preferred_source)
  ORDER BY lower(b.period) DESC
     LIMIT 1;
$$;

comment on function pgautofailover.get_latest_basebackup
                    (text,int,pgautofailover.basebackup_source)
        is 'fetch the most recent complete base backup for (formation, group), optionally filtered to one source';

grant execute on function pgautofailover.get_latest_basebackup
                          (text,int,pgautofailover.basebackup_source)
   to autoctl_node;

-- every 'complete' base backup for (formation, group), newest first --
-- what service_archiver_basebackup.c's own retention pass (maxcount/
-- maxage) walks to decide what to keep vs. prune, and what a future `pg_
-- autoctl show basebackup` would list. basebackupid/storagelocation are
-- what report_basebackup_deleted()/an actual directory removal need;
-- startedat_epoch (extract(epoch from lower(period))) is plain integer
-- seconds for the same reason get_basebackup_policy_for_group() flattens
-- its own interval columns -- easy time_t arithmetic, no timestamptz-text
-- parsing on the C side.
CREATE FUNCTION pgautofailover.list_basebackups
 (
    formationid          text,
    groupid              int,
    OUT basebackupid      bigint,
    OUT label             text,
    OUT storagelocation   text,
    OUT startedat_epoch   bigint
 )
 RETURNS SETOF record LANGUAGE sql STABLE SECURITY DEFINER
AS $$
    SELECT b.basebackupid, b.label, b.storagelocation,
           extract(epoch FROM lower(b.period))::bigint
      FROM pgautofailover.basebackup b
     WHERE b.formationid = list_basebackups.formationid
       AND b.groupid = list_basebackups.groupid
       AND b.status = 'complete'
  ORDER BY lower(b.period) DESC;
$$;

comment on function pgautofailover.list_basebackups(text,int)
        is 'list complete base backups for (formation, group), newest first -- retention/inventory';

grant execute on function pgautofailover.list_basebackups(text,int)
   to autoctl_node;

-- an archiving node has no sysidentifier of its own (haspgdata = false,
-- see that column's own comment): it never runs a real Postgres instance
-- to report one. Every other node in the group shares the same physical
-- cluster's identifier, so any one of them answers for the whole group --
-- needed by pg_walsender's own IDENTIFY_SYSTEM response (cmd_identify_
-- system.c) so a real standby streaming from the archiver doesn't reject
-- it with "database system identifier differs between the primary and
-- standby".
CREATE FUNCTION pgautofailover.get_group_system_identifier
    (formationid text, groupid int)
 RETURNS bigint LANGUAGE sql STABLE SECURITY DEFINER
AS $$
    SELECT sysidentifier
      FROM pgautofailover.node
     WHERE node.formationid = get_group_system_identifier.formationid
       AND node.groupid = get_group_system_identifier.groupid
       AND sysidentifier IS NOT NULL
       AND sysidentifier != 0
     LIMIT 1;
$$;

comment on function pgautofailover.get_group_system_identifier(text,int)
        is 'the Postgres system identifier shared by every node in a group, for an archiving node (which has none of its own) to serve via IDENTIFY_SYSTEM';

grant execute on function pgautofailover.get_group_system_identifier(text,int)
   to autoctl_node;

-- `create postgres --from-archiver` needs the ARCHIVING row itself, not
-- get_most_advanced_standby()'s election-only pool: that function filters
-- on reportedstate = 'report_lsn', a transient state a group's ARCHIVING
-- node only visits during a FAST_FORWARD election, never during its normal
-- steady-state operation (reportedstate = 'archiving'). node_port is the
-- port == 0 sentinel documented on get_most_advanced_standby's own C
-- caller (keeper_get_most_advanced_standby, keeper.c) -- resolving it to
-- the archiver's real pg_walsender serve port is this milestone's C
-- caller's job too, same pattern.
CREATE FUNCTION pgautofailover.get_archiver_node
 (
   IN formationid       text default 'default',
   IN groupid           int default 0,
   OUT node_id          bigint,
   OUT node_name        text,
   OUT node_host        text,
   OUT node_port        int,
   OUT node_lsn         pg_lsn,
   OUT node_is_primary  bool
 )
RETURNS SETOF record LANGUAGE SQL STRICT
AS $$
   select nodeid, nodename, nodehost, nodeport, reportedlsn, false
     from pgautofailover.node
    where formationid = $1
      and groupid = $2
      and reportedstate = 'archiving'
 order by nodeid
    limit 1;
$$;

comment on function pgautofailover.get_archiver_node(text,int)
        is 'fetch the ARCHIVING node for (formation, group), for create postgres --from-archiver to bootstrap from';

grant execute on function pgautofailover.get_archiver_node(text,int)
   to autoctl_node;

-- for kind = 'warm-standby': raises if the owning archiver is already at
-- its maxresidentreplay cap
CREATE FUNCTION pgautofailover.create_archiver_node
 (
    archiverid bigint,
    kind pgautofailover.archiver_node_kind,
    pgdata text,
    hostname text DEFAULT NULL,
    nodeid bigint DEFAULT NULL,          -- required iff kind = 'wal-receiver'
    formationid text DEFAULT NULL,       -- required iff kind = 'warm-standby'
    groupid int DEFAULT NULL,            -- required iff kind = 'warm-standby'
    cadence pgautofailover.archiver_node_cadence DEFAULT NULL,
    nodecluster text DEFAULT NULL,       -- only for 'warm-standby' + cadence = 'continuous'
    pitrstatus pgautofailover.pitr_status DEFAULT NULL
 )
 RETURNS bigint LANGUAGE plpgsql SECURITY DEFINER
AS $$
DECLARE
    residentcount int;
    maxresident int;
    new_id bigint;
BEGIN
    IF kind = 'warm-standby' THEN
        SELECT a.maxresidentreplay INTO maxresident
          FROM pgautofailover.archiver a
         WHERE a.archiverid = create_archiver_node.archiverid;

        SELECT count(*) INTO residentcount
          FROM pgautofailover.archiver_node an
         WHERE an.archiverid = create_archiver_node.archiverid
           AND an.kind = 'warm-standby';

        IF residentcount >= maxresident THEN
            RAISE EXCEPTION
                  'archiver % is already at its maxresidentreplay cap (%)',
                  archiverid, maxresident;
        END IF;
    END IF;

    INSERT INTO pgautofailover.archiver_node
           (archiverid, kind, pgdata, hostname, nodeid,
            formationid, groupid, cadence, nodecluster, pitrstatus)
    VALUES (archiverid, kind, pgdata, hostname, nodeid,
            formationid, groupid, cadence, nodecluster, pitrstatus)
      RETURNING archivernodeid INTO new_id;

    RETURN new_id;
END;
$$;

comment on function pgautofailover.create_archiver_node
     (bigint,pgautofailover.archiver_node_kind,text,text,bigint,text,int,
      pgautofailover.archiver_node_cadence,text,pgautofailover.pitr_status)
        is 'registers a concrete Postgres instance an archiver hosts, derives, or is otherwise associated with';

grant execute on function
      pgautofailover.create_archiver_node
        (bigint,pgautofailover.archiver_node_kind,text,text,bigint,text,int,
         pgautofailover.archiver_node_cadence,text,pgautofailover.pitr_status)
   to autoctl_node;

CREATE FUNCTION pgautofailover.remove_archiver_node(archivernodeid bigint)
 RETURNS void LANGUAGE plpgsql SECURITY DEFINER
AS $$
BEGIN
    DELETE FROM pgautofailover.archiver_node an
     WHERE an.archivernodeid = remove_archiver_node.archivernodeid;

    IF NOT FOUND THEN
        RAISE EXCEPTION 'archiver_node % does not exist', archivernodeid;
    END IF;
END;
$$;

comment on function pgautofailover.remove_archiver_node(bigint)
        is 'removes an archiver_node row';

grant execute on function pgautofailover.remove_archiver_node(bigint)
   to autoctl_node;

CREATE FUNCTION pgautofailover.set_archiver_node_pitr_status
    (archivernodeid bigint, pitrstatus pgautofailover.pitr_status)
 RETURNS void LANGUAGE plpgsql SECURITY DEFINER
AS $$
BEGIN
    UPDATE pgautofailover.archiver_node AS an
       SET pitrstatus = set_archiver_node_pitr_status.pitrstatus
     WHERE an.archivernodeid = set_archiver_node_pitr_status.archivernodeid
       AND an.kind = 'pitr';

    IF NOT FOUND THEN
        RAISE EXCEPTION 'archiver_node % does not exist, or is not kind = pitr',
              archivernodeid;
    END IF;
END;
$$;

comment on function pgautofailover.set_archiver_node_pitr_status(bigint,pgautofailover.pitr_status)
        is 'updates a PITR archiver_node''s lifecycle status';

grant execute on function
      pgautofailover.set_archiver_node_pitr_status(bigint,pgautofailover.pitr_status)
   to autoctl_node;

-- pushed by the local pg_autoctl pitr CLI immediately after acting
-- locally -- never blocks or gates the local action on this succeeding
CREATE FUNCTION pgautofailover.report_pitr_status
 (
    archivernodeid bigint, operation pgautofailover.pitr_operation,
    requestedspec jsonb,
    observedlsn pg_lsn, observedtimestamp timestamptz,
    observedpausestate text, note text DEFAULT NULL
 )
 RETURNS void LANGUAGE plpgsql SECURITY DEFINER
AS $$
BEGIN
    INSERT INTO pgautofailover.pitr_history
           (archivernodeid, operation, requestedspec,
            observedlsn, observedtimestamp, observedpausestate, note)
    VALUES (archivernodeid, operation, requestedspec,
            observedlsn, observedtimestamp, observedpausestate, note);
END;
$$;

comment on function pgautofailover.report_pitr_status
     (bigint,pgautofailover.pitr_operation,jsonb,pg_lsn,timestamptz,text,text)
        is 'records one PITR operation''s outcome -- a best-effort report, never gating the local action it follows';

grant execute on function
      pgautofailover.report_pitr_status
        (bigint,pgautofailover.pitr_operation,jsonb,pg_lsn,timestamptz,text,text)
   to autoctl_node;

-- in_* parameters: see archiver_add_formation's own comment on why an ON
-- CONFLICT target list (which can't be qualified) forces this naming here.
CREATE FUNCTION pgautofailover.pitr_queue_command
    (in_archivernodeid bigint, in_command pgautofailover.pitr_command,
     in_commandspec jsonb DEFAULT NULL)
 RETURNS void LANGUAGE plpgsql SECURITY DEFINER
AS $$
BEGIN
    INSERT INTO pgautofailover.pitr_pending_command
           (archivernodeid, command, commandspec)
    VALUES (in_archivernodeid, in_command, in_commandspec)
       ON CONFLICT (archivernodeid) DO UPDATE
       SET command = EXCLUDED.command,
           commandspec = EXCLUDED.commandspec,
           queuedat = now();
END;
$$;

comment on function pgautofailover.pitr_queue_command(bigint,pgautofailover.pitr_command,jsonb)
        is 'queues a PITR command for a monitor-mediated (kind = pitr, pg_autoctl node run) agent to pick up';

grant execute on function
      pgautofailover.pitr_queue_command(bigint,pgautofailover.pitr_command,jsonb)
   to autoctl_node;

-- returns the pending command and resets the queue slot to 'none' in the
-- same call -- an agent polling this never processes the same command twice
-- Reads the pending command, then clears it, as two separate statements:
-- UPDATE ... RETURNING always reflects the row *after* the update is
-- applied, so folding the reset into the same RETURNING clause that reads
-- the command would always report back the very 'none' this function just
-- set, never the command that was actually queued. FOR UPDATE locks the
-- row across both statements, so a concurrent caller for the same
-- archivernodeid still can't observe or consume the same command twice.
CREATE FUNCTION pgautofailover.pitr_next_command(in_archivernodeid bigint)
 RETURNS pgautofailover.pitr_command LANGUAGE plpgsql SECURITY DEFINER
AS $$
DECLARE
    next_command pgautofailover.pitr_command;
BEGIN
    SELECT pc.command INTO next_command
      FROM pgautofailover.pitr_pending_command pc
     WHERE pc.archivernodeid = in_archivernodeid
       FOR UPDATE;

    IF next_command IS NULL OR next_command = 'none' THEN
        RETURN 'none';
    END IF;

    UPDATE pgautofailover.pitr_pending_command AS pc
       SET command = 'none', commandspec = NULL
     WHERE pc.archivernodeid = in_archivernodeid;

    RETURN next_command;
END;
$$;

comment on function pgautofailover.pitr_next_command(bigint)
        is 'pops and clears the next queued PITR command for an agent to act on';

grant execute on function pgautofailover.pitr_next_command(bigint)
   to autoctl_node;


--
-- Archiving & Disaster Recovery, milestone 2: monitor-side FSM support for
-- the ARCHIVING state (group_state_machine.c). Loosen
-- system_identifier_is_null_at_init_only to also allow a NULL sysidentifier
-- in 'archiving' and 'report_lsn': an ARCHIVING row (haspgdata = false) has
-- no PGDATA of its own, ever, so it never acquires a real sysidentifier --
-- see pgautofailover.sql's own comment on this constraint for why
-- 'report_lsn' is safe to loosen too.
--
-- reportedstate::text IN (...) here, not reportedstate IN (...): this
-- script's own earlier "ALTER TYPE ... ADD VALUE 'archiving'" added that
-- label in this same transaction (ALTER EXTENSION ... UPDATE runs the whole
-- upgrade script as one transaction), and Postgres refuses to cast a string
-- literal to a not-yet-committed enum value ("unsafe use of new value...
-- must be committed before they can be used") -- casting the *column*
-- to text instead of the literals to the enum sidesteps that restriction
-- entirely, since reportedstate's own stored value is already a valid,
-- fully-committed enum datum by the time this constraint ever evaluates it.
-- pgautofailover.sql's fresh-install CHECK constraint doesn't need this: a
-- freshly CREATE TYPE'd enum has 'archiving' as a member from the start,
-- never added mid-transaction, so the ordinary enum-typed comparison there
-- is unaffected.
--

ALTER TABLE pgautofailover.node
    DROP CONSTRAINT system_identifier_is_null_at_init_only;

ALTER TABLE pgautofailover.node
    ADD CONSTRAINT system_identifier_is_null_at_init_only
         CHECK (
                  (
                       sysidentifier IS NULL
                   AND reportedstate::text
                       IN (
                           'init',
                           'wait_standby',
                           'catchingup',
                           'dropped',
                           'archiving',
                           'report_lsn'
                          )
                   )
                OR sysidentifier IS NOT NULL
               );
