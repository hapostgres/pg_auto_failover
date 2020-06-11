--
-- extension update file from 1.2 to 1.3
--
-- complain if script is sourced in psql, rather than via CREATE EXTENSION
-- \echo Use "ALTER EXTENSION pgautofailover UPDATE TO 1.3" to load this file. \quit

--- The following only works in Postgres 12 onward
-- ALTER TYPE pgautofailover.replication_state ADD VALUE 'join_primary';
-- ALTER TYPE pgautofailover.replication_state ADD VALUE 'apply_settings';


DROP FUNCTION IF EXISTS pgautofailover.register_node(text,text,integer,name,integer,pgautofailover.replication_state,text);

DROP FUNCTION IF EXISTS pgautofailover.node_active(text,text,int,int,int,
                          pgautofailover.replication_state,bool,pg_lsn,text);

DROP FUNCTION IF EXISTS pgautofailover.current_state(text);

DROP FUNCTION IF EXISTS pgautofailover.current_state(text, int);

ALTER TYPE pgautofailover.replication_state RENAME TO old_replication_state;

CREATE TYPE pgautofailover.replication_state
    AS ENUM
 (
    'unknown',
    'init',
    'single',
    'wait_primary',
    'primary',
    'draining',
    'demote_timeout',
    'demoted',
    'catchingup',
    'secondary',
    'prepare_promotion',
    'stop_replication',
    'wait_standby',
    'maintenance',
    'join_primary',
    'apply_settings'
 );

-- Note the double cast here, first to text and only then to the new enums
ALTER TABLE pgautofailover.node
      ALTER COLUMN goalstate DROP NOT NULL,
      ALTER COLUMN goalstate DROP DEFAULT,

      ALTER COLUMN goalstate
              TYPE pgautofailover.replication_state
             USING goalstate::text::pgautofailover.replication_state,

      ALTER COLUMN goalstate SET DEFAULT 'init',
      ALTER COLUMN goalstate SET NOT NULL,

      ALTER COLUMN reportedstate
              TYPE pgautofailover.replication_state
             USING reportedstate::text::pgautofailover.replication_state;

ALTER TABLE pgautofailover.event
      ALTER COLUMN goalstate
              TYPE pgautofailover.replication_state
             USING goalstate::text::pgautofailover.replication_state,

      ALTER COLUMN reportedstate
              TYPE pgautofailover.replication_state
             USING reportedstate::text::pgautofailover.replication_state;

DROP TYPE pgautofailover.old_replication_state;

ALTER TABLE pgautofailover.formation
  ADD COLUMN number_sync_standbys int  NOT NULL DEFAULT 1;

DROP FUNCTION IF EXISTS pgautofailover.create_formation(text, text);

DROP FUNCTION IF EXISTS pgautofailover.create_formation(text,text,name,boolean);
DROP FUNCTION IF EXISTS pgautofailover.get_other_node(text,integer);

CREATE FUNCTION pgautofailover.set_formation_number_sync_standbys
 (
    IN formation_id  		text,
    IN number_sync_standbys int
 )
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$set_formation_number_sync_standbys$$;

grant execute on function
      pgautofailover.set_formation_number_sync_standbys(text, int)
   to autoctl_node;

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

ALTER TABLE pgautofailover.node
	RENAME TO node_upgrade_old;

CREATE TABLE pgautofailover.node
 (
    formationid          text not null default 'default',
    nodeid               bigint not null DEFAULT nextval('pgautofailover.node_nodeid_seq'::regclass),
    groupid              int not null,
    nodename             text not null,
    nodeport             int not null,
    goalstate            pgautofailover.replication_state not null default 'init',
    reportedstate        pgautofailover.replication_state not null,
    reportedpgisrunning  bool default true,
    reportedrepstate     text default 'async',
    reporttime           timestamptz not null default now(),
    reportedlsn          pg_lsn not null default '0/0',
    walreporttime        timestamptz not null default now(),
    health               integer not null default -1,
    healthchecktime      timestamptz not null default now(),
    statechangetime      timestamptz not null default now(),
    candidatepriority	 int not null default 100,
    replicationquorum	 bool not null default true,

    UNIQUE (nodename, nodeport),
    PRIMARY KEY (nodeid),
    FOREIGN KEY (formationid) REFERENCES pgautofailover.formation(formationid)
 )
 -- we expect few rows and lots of UPDATE, let's benefit from HOT
 WITH (fillfactor = 25);

ALTER SEQUENCE pgautofailover.node_nodeid_seq OWNED BY pgautofailover.node.nodeid;

INSERT INTO pgautofailover.node
 (
  formationid, nodeid, groupid, nodename, nodeport,
  goalstate, reportedstate, reportedpgisrunning, reportedrepstate,
  reporttime, reportedlsn, walreporttime,
  health, healthchecktime, statechangetime
 )
 SELECT formationid, nodeid, groupid, nodename, nodeport,
        goalstate, reportedstate, reportedpgisrunning, reportedrepstate,
        reporttime, reportedlsn, walreporttime,
        health, healthchecktime, statechangetime
   FROM pgautofailover.node_upgrade_old;


ALTER TABLE pgautofailover.event
	RENAME TO event_upgrade_old;

CREATE TABLE pgautofailover.event
 (
    eventid           bigint not null DEFAULT nextval('pgautofailover.event_eventid_seq'::regclass),
    eventtime         timestamptz not null default now(),
    formationid       text not null,
    nodeid            bigint not null,
    groupid           int not null,
    nodename          text not null,
    nodeport          integer not null,
    reportedstate     pgautofailover.replication_state not null,
    goalstate         pgautofailover.replication_state not null,
    reportedrepstate  text,
    reportedlsn       pg_lsn not null default '0/0',
    candidatepriority int,
    replicationquorum bool,
    description       text,

    PRIMARY KEY (eventid)
 );

ALTER SEQUENCE pgautofailover.event_eventid_seq
      OWNED BY pgautofailover.event.eventid;

INSERT INTO pgautofailover.event
 (
  eventid, eventtime, formationid, nodeid, groupid,
  nodename, nodeport,
  reportedstate, goalstate, reportedrepstate, description
 )
 SELECT eventid, eventtime, formationid, nodeid, groupid,
        nodename, nodeport,
        reportedstate, goalstate, reportedrepstate, description
   FROM pgautofailover.event_upgrade_old;

GRANT SELECT ON ALL TABLES IN SCHEMA pgautofailover TO autoctl_node;

CREATE FUNCTION pgautofailover.set_node_nodename
 (
    IN node_id   bigint,
    IN node_name text,
   OUT node_id   bigint,
   OUT name      text,
   OUT port      int
 )
RETURNS record LANGUAGE SQL STRICT SECURITY DEFINER
AS $$
      update pgautofailover.node
         set nodename = node_name
       where nodeid = node_id
   returning nodeid, nodename, nodeport;
$$;

grant execute on function pgautofailover.set_node_nodename(bigint,text)
   to autoctl_node;


DROP FUNCTION IF EXISTS pgautofailover.register_node(text, text);

CREATE FUNCTION pgautofailover.register_node
 (
    IN formation_id         text,
    IN node_name            text,
    IN node_port            int,
    IN dbname               name,
    IN desired_group_id     int default -1,
    IN initial_group_role   pgautofailover.replication_state default 'init',
    IN node_kind            text default 'standalone',
    IN candidate_priority 	int default 100,
    IN replication_quorum	bool default true,
   OUT assigned_node_id     int,
   OUT assigned_group_id    int,
   OUT assigned_group_state pgautofailover.replication_state,
   OUT assigned_candidate_priority 	int,
   OUT assigned_replication_quorum  bool
 )
RETURNS record LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$register_node$$;

grant execute on function
      pgautofailover.register_node(text,text,int,name,int,pgautofailover.replication_state,text, int, bool)
   to autoctl_node;


CREATE FUNCTION pgautofailover.node_active
 (
    In formation_id           		text,
    IN node_name              		text,
    IN node_port              		int,
    IN current_node_id        		int default -1,
    IN current_group_id       		int default -1,
    IN current_group_role     		pgautofailover.replication_state default 'init',
    IN current_pg_is_running  		bool default true,
    IN current_lsn			  		pg_lsn default '0/0',
    IN current_rep_state      		text default '',
   OUT assigned_node_id       		int,
   OUT assigned_group_id      		int,
   OUT assigned_group_state   		pgautofailover.replication_state,
   OUT assigned_candidate_priority 	int,
   OUT assigned_replication_quorum  bool
 )
RETURNS record LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$node_active$$;

grant execute on function
      pgautofailover.node_active(text,text,int,int,int,
                          pgautofailover.replication_state,bool,pg_lsn,text)
   to autoctl_node;

DROP FUNCTION IF EXISTS pgautofailover.get_nodes(text, text);

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

DROP FUNCTION IF EXISTS pgautofailover.get_primary(text,int);

CREATE FUNCTION pgautofailover.get_primary
 (
    IN formation_id      text default 'default',
    IN group_id          int default 0,
   OUT primary_node_id   int,
   OUT primary_name      text,
   OUT primary_port      int
 )
RETURNS record LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$get_primary$$;

comment on function pgautofailover.get_primary(text,int)
        is 'get the writable node for a group';

grant execute on function pgautofailover.get_primary(text,int)
   to autoctl_node;

DROP FUNCTION IF EXISTS pgautofailover.get_other_nodes(text. int);

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

DROP FUNCTION IF EXISTS pgautofailover.get_other_nodes
                        (text. int, pgautofailover.replication_state);

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

DROP FUNCTION IF EXISTS pgautofailover.last_events(int);

CREATE FUNCTION pgautofailover.last_events
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
         reportedrepstate, reportedlsn, candidatepriority, replicationquorum, description
    from pgautofailover.event
order by eventid desc
   limit count
)
select * from last_events order by eventtime, eventid;
$$;

comment on function pgautofailover.last_events(int)
        is 'retrieve last COUNT events';

DROP FUNCTION IF EXISTS pgautofailover.last_events(text,int);

CREATE FUNCTION pgautofailover.last_events
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
           reportedrepstate, reportedlsn, candidatepriority, replicationquorum, description
      from pgautofailover.event
     where formationid = formation_id
  order by eventid desc
     limit count
)
select * from last_events order by eventtime, eventid;
$$;

comment on function pgautofailover.last_events(text,int)
        is 'retrieve last COUNT events for given formation';

DROP FUNCTION IF EXISTS pgautofailover.last_events(text,int,int);

CREATE FUNCTION pgautofailover.last_events
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
           reportedrepstate, reportedlsn, candidatepriority, replicationquorum, description
      from pgautofailover.event
     where formationid = formation_id
       and groupid = group_id
  order by eventid desc
     limit count
)
select * from last_events order by eventtime, eventid;
$$;

comment on function pgautofailover.last_events(text,int,int)
        is 'retrieve last COUNT events for given formation and group';


CREATE FUNCTION pgautofailover.current_state
 (
    IN formation_id         text default 'default',
   OUT nodename             text,
   OUT nodeport             int,
   OUT group_id             int,
   OUT node_id              bigint,
   OUT current_group_state  pgautofailover.replication_state,
   OUT assigned_group_state pgautofailover.replication_state,
   OUT candidate_priority	int,
   OUT replication_quorum	bool
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
   OUT candidate_priority	int,
   OUT replication_quorum	bool
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

DROP FUNCTION IF EXISTS pgautofailover.formation_uri(text, text);

CREATE FUNCTION pgautofailover.formation_uri
 (
    IN formation_id         text DEFAULT 'default',
    IN sslmode              text DEFAULT 'prefer',
    IN sslrootcert          text DEFAULT '',
    IN sslcrl               text DEFAULT ''
 )
RETURNS text LANGUAGE SQL STRICT
AS $$
    select case
           when string_agg(format('%s:%s', nodename, nodeport),',') is not null
           then format(
               'postgres://%s/%s?target_session_attrs=read-write&sslmode=%s%s%s',
               string_agg(format('%s:%s', nodename, nodeport),','),
               -- as we join formation on node we get the same dbname for all
               -- entries, pick one.
               min(dbname),
               min(sslmode),
               CASE WHEN min(sslrootcert) = ''
                   THEN ''
                   ELSE '&sslrootcert=' || sslrootcert
               END,
               CASE WHEN min(sslcrl) = ''
                   THEN ''
                   ELSE '&sslcrl=' || sslcrl
               END
           )
           end as uri
      from pgautofailover.node as node
           join pgautofailover.formation using(formationid)
     where formationid = formation_id
       and groupid = 0;
$$;

CREATE FUNCTION pgautofailover.set_node_candidate_priority
 (
    IN nodeid				int,
	IN nodename             text,
	IN nodeport             int,
    IN candidate_priority	int
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
    IN nodeid				int,
	IN nodename             text,
	IN nodeport             int,
    IN replication_quorum	bool
 )
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$set_node_replication_quorum$$;

comment on function pgautofailover.set_node_replication_quorum(int, text, int, bool)
        is 'sets the replication quorum value for a node. true if the node participates in write quorum';

grant execute on function
      pgautofailover.set_node_replication_quorum(int, text, int, bool)
   to autoctl_node;


create function pgautofailover.synchronous_standby_names
 (
    IN formation_id text default 'default',
    IN group_id     int default 0
 )
returns text language C strict
AS 'MODULE_PATHNAME', $$synchronous_standby_names$$;

comment on function pgautofailover.synchronous_standby_names(text, int)
        is 'get the synchronous_standby_names setting for a given group';

grant execute on function
      pgautofailover.synchronous_standby_names(text, int)
   to autoctl_node;


CREATE OR REPLACE FUNCTION pgautofailover.adjust_number_sync_standbys()
  RETURNS trigger
  LANGUAGE 'plpgsql'
AS $$
declare
  standby_count integer := null;
  number_sync_standbys integer := null;
begin
   select count(*) - 1
     into standby_count
     from pgautofailover.node
    where formationid = old.formationid;

   select formation.number_sync_standbys
     into number_sync_standbys
     from pgautofailover.formation
    where formation.formationid = old.formationid;

  if number_sync_standbys > 1
  then
    -- we must have number_sync_standbys + 1 <= standby_count
    if (number_sync_standbys + 1) > standby_count
    then
      update pgautofailover.formation
         set number_sync_standbys = greatest(standby_count - 1, 1)
       where formation.formationid = old.formationid;
    end if;
  end if;

  return old;
end
$$;

comment on function pgautofailover.adjust_number_sync_standbys()
        is 'adjust formation number_sync_standbys when removing a node, if needed';

CREATE TRIGGER adjust_number_sync_standbys
         AFTER DELETE
            ON pgautofailover.node
           FOR EACH ROW
       EXECUTE PROCEDURE pgautofailover.adjust_number_sync_standbys();

DROP TABLE pgautofailover.node_upgrade_old;
DROP TABLE pgautofailover.event_upgrade_old;
