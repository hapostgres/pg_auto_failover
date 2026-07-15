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
