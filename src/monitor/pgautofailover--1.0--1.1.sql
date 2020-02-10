--
-- extension update file from 1.0 to 1.1
--
-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "ALTER EXTENSION pgautofailover UPDATE TO 1.1" to load this file. \quit

ALTER TABLE pgautofailover.node
       DROP COLUMN waldelta,
        ADD COLUMN reportedlsn pg_lsn NOT NULL DEFAULT '0/0',
        ADD COLUMN candidatepriority INT NOT NULL DEFAULT 100,
        ADD COLUMN replicationquorum BOOL NOT NULL DEFAULT true;

ALTER TABLE pgautofailover.event
       DROP COLUMN waldelta,
        ADD COLUMN reportedlsn pg_lsn NOT NULL DEFAULT '0/0';

DROP FUNCTION pgautofailover.node_active(text,text,int,int,int, pgautofailover.replication_state,bool,bigint,text);

CREATE FUNCTION pgautofailover.node_active
 (
    IN formation_id                 text,
    IN node_name                    text,
    IN node_port                    int,
    IN current_node_id              int default -1,
    IN current_group_id             int default -1,
    IN current_group_role           pgautofailover.replication_state default 'init',
    IN current_pg_is_running        bool default true,
    IN current_lsn                  pg_lsn default '0/0',
    IN current_rep_state            text default '',
   OUT assigned_node_id             int,
   OUT assigned_group_id            int,
   OUT assigned_group_state         pgautofailover.replication_state
 )
RETURNS record LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$node_active$$;

grant execute on function
      pgautofailover.node_active(text,text,int,int,int,
                          pgautofailover.replication_state,bool,pg_lsn,text)
   to autoctl_node;


grant execute on function pgautofailover.remove_node(text,int)
   to autoctl_node;


ALTER FUNCTION pgautofailover.perform_failover(text,int)
      SECURITY DEFINER;
 
grant execute on function pgautofailover.perform_failover(text,int)
   to autoctl_node;

grant execute on function pgautofailover.start_maintenance(text,int)
   to autoctl_node;

grant execute on function pgautofailover.stop_maintenance(text,int)
   to autoctl_node;


CREATE OR REPLACE FUNCTION pgautofailover.last_events
 (
  count int default 10
 )
RETURNS SETOF pgautofailover.event LANGUAGE SQL STRICT
AS $$
with last_events as
(
  select eventid, eventtime, formationid,
         nodeid, groupid, nodename, nodeport,
         reportedstate, goalstate,
         reportedrepstate, reportedlsn,
         description
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
           nodeid, groupid, nodename, nodeport,
           reportedstate, goalstate,
           reportedrepstate, reportedlsn,
           description
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
           nodeid, groupid, nodename, nodeport,
           reportedstate, goalstate,
           reportedrepstate, reportedlsn,
           description
      from pgautofailover.event
     where formationid = formation_id
       and groupid = group_id
  order by eventid desc
     limit count
)
select * from last_events order by eventtime, eventid;
$$;

