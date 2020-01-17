--
-- extension update file from 1.0 to 1.1
--
-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "ALTER EXTENSION pgautofailover UPDATE TO 1.1" to load this file. \quit

ALTER TYPE pgautofailover.replication_state
       ADD VALUE IF NOT EXISTS 'join_primary'

ALTER TABLE pgautofailover.formation
        ADD COLUMN  number_sync_standbys INT NOT NULL DEFAULT 1;

DROP FUNCTION pgautofailover.create_formation(text,text,name,bool);

CREATE FUNCTION pgautofailover.create_formation
 (
    IN formation_id         text,
    IN kind                 text,
    IN dbname               name,
    IN opt_secondary        bool,
    IN number_sync_standbys int,
   OUT formation_id         text,
   OUT kind                 text,
   OUT dbname               name,
   OUT opt_secondary        bool,
   OUT number_sync_standbys int
 )
RETURNS record LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$create_formation$$;

grant execute on function
      pgautofailover.create_formation(text,text,name,bool,int)
   to autoctl_node;


CREATE FUNCTION pgautofailover.set_formation_number_sync_standbys
 (
    IN formation_id             text,
    IN number_sync_standbys int
 )
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$set_formation_number_sync_standbys$$;

grant execute on function
      pgautofailover.set_formation_number_sync_standbys(text, int)
   to autoctl_node;


ALTER TABLE pgautofailover.node
       DROP COLUMN waldelta,
        ADD COLUMN reportedlsn pg_lsn NOT NULL DEFAULT '0/0',
        ADD COLUMN candidatepriority INT NOT NULL DEFAULT 100,
        ADD COLUMN replicationquorum BOOL NOT NULL DEFAULT true;

ALTER TABLE pgautofailover.event
       DROP COLUMN waldelta;

DROP FUNCTION pgautofailover.register_node(text,text,int,name,int,pgautofailover.replication_state,text);

CREATE FUNCTION pgautofailover.register_node
 (
    IN formation_id                 text,
    IN node_name                    text,
    IN node_port                    int,
    IN dbname                       name,
    IN desired_group_id             int default -1,
    IN initial_group_role           pgautofailover.replication_state default 'init',
    IN node_kind                    text default 'standalone',
    IN candidate_priority           int default 100,
    IN replication_quorum           bool default true,
   OUT assigned_node_id             int,
   OUT assigned_group_id            int,
   OUT assigned_group_state         pgautofailover.replication_state,
   OUT assigned_candidate_priority  int,
   OUT assigned_replication_quorum  bool
 )
RETURNS record LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$register_node$$;

grant execute on function
      pgautofailover.register_node(text,text,int,name,int,pgautofailover.replication_state,text, int, bool)
   to autoctl_node;

DROP FUNCTION pgautofailover.node_active(text,text,int,int,int, pgautofailover.replication_state,bool,bigint,text)

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
   OUT assigned_group_state         pgautofailover.replication_state,
   OUT assigned_candidate_priority  int,
   OUT assigned_replication_quorum  bool
 )
RETURNS record LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$node_active$$;

grant execute on function
      pgautofailover.node_active(text,text,int,int,int,
                          pgautofailover.replication_state,bool,pg_lsn,text)
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
         candidatepriority, replicationquorum, description
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
           candidatepriority, replicationquorum, description
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
           candidatepriority, replicationquorum, description
      from pgautofailover.event
     where formationid = formation_id
       and groupid = group_id
  order by eventid desc
     limit count
)
select * from last_events order by eventtime, eventid;
$$;

DROP FUNCTION function pgautofailover.current_state(text);

CREATE FUNCTION pgautofailover.current_state
 (
    IN formation_id         text default 'default',
   OUT nodename             text,
   OUT nodeport             int,
   OUT group_id             int,
   OUT node_id              bigint,
   OUT current_group_state  pgautofailover.replication_state,
   OUT assigned_group_state pgautofailover.replication_state,
   OUT candidate_priority   int,
   OUT replication_quorum   bool
 )
RETURNS SETOF record LANGUAGE SQL STRICT
AS $$
   select nodename, nodeport, groupid, nodeid, reportedstate, goalstate,
          candidatepriority, replicationquorum
     from pgautofailover.node
    where formationid = formation_id
 order by groupid, nodeid;
$$;

comment on function pgautofailover.current_state(text)
        is 'get the current state of both nodes of a formation';


DROP FUNCTION pgautofailover.current_state(text, int);

CREATE FUNCTION pgautofailover.current_state
 (
    IN formation_id         text,
    IN group_id             int,
   OUT nodename             text,
   OUT nodeport             int,
   OUT group_id             int,
   OUT node_id              bigint,
   OUT current_group_state  pgautofailover.replication_state,
   OUT assigned_group_state pgautofailover.replication_state,
   OUT candidate_priority   int,
   OUT replication_quorum   bool
 )
RETURNS SETOF record LANGUAGE SQL STRICT
AS $$
   select nodename, nodeport, groupid, nodeid, reportedstate, goalstate,
          candidatepriority, replicationquorum
     from pgautofailover.node
    where formationid = formation_id
      and groupid = group_id
 order by groupid, nodeid;
$$;

comment on function pgautofailover.current_state(text, int)
        is 'get the current state of both nodes of a group in a formation';

CREATE FUNCTION pgautofailover.set_node_candidate_priority
 (
    IN nodeid             int,
    IN nodename           text,
    IN nodeport           int,
    IN candidate_priority int
 )
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$set_node_candidate_priority$$;

comment on function pgautofailover.set_node_candidate_priority(int, text, int, int)
        is 'sets the candidate priority value for a node. Expects a priority value between 0 and 100. 0 if the node is not a candidate to be promoted to be primary.';

grant execute on function
      pgautofailover.set_node_candidate_priority(int, text, int, int)
   to autoctl_node;
 
CREATE FUNCTION pgautofailover.set_node_replication_quorum
 (
    IN nodeid             int,
    IN nodename           text,
    IN nodeport           int,
    IN replication_quorum bool
 )
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$set_node_replication_quorum$$;

comment on function pgautofailover.set_node_replication_quorum(int, text, int, bool)
        is 'sets the replication quorum value for a node. true if the node participates in write quorum';

grant execute on function
      pgautofailover.set_node_replication_quorum(int, text, int, bool)
   to autoctl_node;

CREATE FUNCTION pgautofailover.get_nodes
 (
    IN formation_id     text default 'default',
    IN group_id         int default NULL,
   OUT node_id          int,
   OUT node_name        text,
   OUT node_port        int,
   OUT node_lsn         pg_lsn,
   OUT node_is_primary  bool
 )
RETURNS SETOF record LANGUAGE C
AS 'MODULE_PATHNAME', $$get_nodes$$;

comment on function pgautofailover.get_nodes(text,int)
        is 'get all the nodes in a group';

grant execute on function pgautofailover.get_nodes(text,int)
   to autoctl_node;


DROP FUNCTION pgautofailover.get_primary(text,int);

CREATE FUNCTION pgautofailover.get_primary
 (
    IN formation_id      text default 'default',
    IN group_id          int default 0,
   OUT secondary_node_id int,
   OUT primary_name      text,
   OUT primary_port      int
 )
RETURNS record LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$get_primary$$;

comment on function pgautofailover.get_primary(text,int)
        is 'get the writable node for a group';

grant execute on function pgautofailover.get_primary(text,int)
   to autoctl_node;


DROP FUNCTION pgautofailover.get_other_node(text,int);

CREATE FUNCTION pgautofailover.get_other_nodes
 (
    IN node_name        text,
    IN node_port        int,
   OUT node_id          int,
   OUT node_name        text,
   OUT node_port        int,
   OUT node_lsn         pg_lsn,
   OUT node_is_primary  bool
 )
RETURNS SETOF record LANGUAGE C STRICT
AS 'MODULE_PATHNAME', $$get_other_nodes$$;

comment on function pgautofailover.get_other_nodes(text,int)
        is 'get the other nodes in a group';

grant execute on function pgautofailover.get_other_nodes(text,int)
   to autoctl_node;

CREATE FUNCTION pgautofailover.get_other_nodes
 (
    IN node_name        text,
    IN node_port        int,
    IN current_state    pgautofailover.replication_state,
   OUT node_id          int,
   OUT node_name        text,
   OUT node_port        int,
   OUT node_lsn         pg_lsn,
   OUT node_is_primary  bool
 )
RETURNS SETOF record LANGUAGE C STRICT
AS 'MODULE_PATHNAME', $$get_other_nodes$$;

comment on function pgautofailover.get_other_nodes
                    (text,int,pgautofailover.replication_state)
        is 'get the other nodes in a group, filtering on current_state';

grant execute on function pgautofailover.get_other_nodes
                          (text,int,pgautofailover.replication_state)
   to autoctl_node;



grant execute on function pgautofailover.remove_node(text,int)
   to autoctl_node;

grant execute on function pgautofailover.start_maintenance(text,int)
   to autoctl_node;

grant execute on function pgautofailover.stop_maintenance(text,int)
   to autoctl_node;


ALTER FUNCTION pgautofailover.perform_failover(text,int)
      SECURITY DEFINER;
