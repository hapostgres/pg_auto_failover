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
