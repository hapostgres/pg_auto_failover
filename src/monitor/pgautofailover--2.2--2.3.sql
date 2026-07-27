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
