--
-- extension update file from 1.3 to 1.4
--
-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pgautofailover" to load this file. \quit

DROP FUNCTION IF EXISTS pgautofailover.register_node(text,text,int,name,int,
                          pgautofailover.replication_state,text, int, bool);

DROP FUNCTION IF EXISTS pgautofailover.node_active(text,text,int,int,int,
                          pgautofailover.replication_state,bool,pg_lsn,text);

DROP FUNCTION IF EXISTS pgautofailover.get_other_nodes
                          (text,integer,pgautofailover.replication_state);

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
    'apply_settings',
    'prepare_maintenance',
    'wait_maintenance',
    'report_lsn',
    'fast_forward',
    'join_secondary'
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
      ALTER COLUMN number_sync_standbys
       SET DEFAULT 0;

--
-- The default used to be 1, now it's zero. Change it for people who left
-- the default (everybody, most certainly, because this used to have no
-- impact).
--
UPDATE pgautofailover.formation
   SET number_sync_standbys = 0
 WHERE number_sync_standbys = 1;

ALTER TABLE pgautofailover.formation
        ADD CHECK (kind IN ('pgsql', 'citus'));

ALTER TABLE pgautofailover.node
	RENAME TO node_upgrade_old;

CREATE TABLE pgautofailover.node
 (
    formationid          text not null default 'default',
    nodeid               bigint not null DEFAULT nextval('pgautofailover.node_nodeid_seq'::regclass),
    groupid              int not null,
    nodename             text not null,
    nodehost             text not null,
    nodeport             int not null,
    sysidentifier        bigint,
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

    -- node names must be unique in a given formation
    UNIQUE (formationid, nodename),
    -- any nodehost:port can only be a unique node in the system
    UNIQUE (nodehost, nodeport),
    --
    -- The EXCLUDE constraint only allows the same sysidentifier for all the
    -- nodes in the same group. The system_identifier is a property that is
    -- kept when implementing streaming replication and should be unique per
    -- Postgres instance in all other cases.
    --
    -- We allow the sysidentifier column to be NULL when registering a new
    -- primary server from scratch, because we have not done pg_ctl initdb
    -- at the time we call the register_node() function.
    --
    CONSTRAINT system_identifier_is_null_at_init_only
         CHECK (  (    sysidentifier IS NULL
                   AND reportedstate in ('init', 'wait_standby', 'catchingup') )
                OR sysidentifier IS NOT NULL),

    CONSTRAINT same_system_identifier_within_group
       EXCLUDE USING gist(formationid with =,
                          groupid with =,
                          sysidentifier with <>)
    DEFERRABLE INITIALLY DEFERRED,

    PRIMARY KEY (nodeid),
    FOREIGN KEY (formationid) REFERENCES pgautofailover.formation(formationid)
 )
 -- we expect few rows and lots of UPDATE, let's benefit from HOT
 WITH (fillfactor = 25);

ALTER SEQUENCE pgautofailover.node_nodeid_seq OWNED BY pgautofailover.node.nodeid;

INSERT INTO pgautofailover.node
 (
  formationid, nodeid, groupid, nodename, nodehost, nodeport, sysidentifier,
  goalstate, reportedstate, reportedpgisrunning, reportedrepstate,
  reporttime, reportedlsn, walreporttime,
  health, healthchecktime, statechangetime,
  candidatepriority, replicationquorum
 )
 SELECT formationid, nodeid, groupid,
        format('node_%s', nodeid) as nodename,
        nodename as nodehost, nodeport, 0 as sysidentifier,
        goalstate, reportedstate, reportedpgisrunning, reportedrepstate,
        reporttime, reportedlsn, walreporttime,
        health, healthchecktime, statechangetime,
        candidatepriority, replicationquorum
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
    nodehost          text not null,
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
  nodename, nodehost, nodeport,
  reportedstate, goalstate, reportedrepstate, description
 )
 SELECT eventid, eventtime, event.formationid, event.nodeid, event.groupid,
        node.nodename, node.nodehost, event.nodeport,
        event.reportedstate, event.goalstate, event.reportedrepstate,
        event.description
   FROM pgautofailover.event_upgrade_old as event
   JOIN pgautofailover.node USING(nodeid);

GRANT SELECT ON ALL TABLES IN SCHEMA pgautofailover TO autoctl_node;

CREATE FUNCTION pgautofailover.set_node_system_identifier
 (
    IN node_id             bigint,
    IN node_sysidentifier  bigint,
   OUT node_id          bigint,
   OUT node_name        text,
   OUT node_host        text,
   OUT node_port        int
 )
RETURNS record LANGUAGE SQL STRICT SECURITY DEFINER
AS $$
      update pgautofailover.node
         set sysidentifier = node_sysidentifier
       where nodeid = set_node_system_identifier.node_id
   returning nodeid, nodename, nodehost, nodeport;
$$;

grant execute on function pgautofailover.set_node_system_identifier(bigint,bigint)
   to autoctl_node;

CREATE FUNCTION pgautofailover.set_group_system_identifier
 (
    IN group_id            bigint,
    IN node_sysidentifier  bigint,
   OUT node_id          bigint,
   OUT node_name        text,
   OUT node_host        text,
   OUT node_port        int
 )
RETURNS setof record LANGUAGE SQL STRICT SECURITY DEFINER
AS $$
      update pgautofailover.node
         set sysidentifier = node_sysidentifier
       where groupid = set_group_system_identifier.group_id
         and sysidentifier = 0
   returning nodeid, nodename, nodehost, nodeport;
$$;

grant execute on function pgautofailover.set_group_system_identifier(bigint,bigint)
   to autoctl_node;


DROP FUNCTION pgautofailover.set_node_nodename(bigint,text);

CREATE FUNCTION pgautofailover.update_node_metadata
  (
     IN node_id   bigint,
     IN node_name text,
     IN node_host text,
     IN node_port int
  )
 RETURNS boolean LANGUAGE C SECURITY DEFINER
 AS 'MODULE_PATHNAME', $$update_node_metadata$$;

grant execute on function pgautofailover.update_node_metadata(bigint,text,text,int)
   to autoctl_node;

CREATE FUNCTION pgautofailover.register_node
 (
    IN formation_id         text,
    IN node_host            text,
    IN node_port            int,
    IN dbname               name,
    IN node_name            text default '',
    IN sysidentifier        bigint default 0,
    IN desired_group_id     int default -1,
    IN initial_group_role   pgautofailover.replication_state default 'init',
    IN node_kind            text default 'standalone',
    IN candidate_priority 	int default 100,
    IN replication_quorum	bool default true,
   OUT assigned_node_id     int,
   OUT assigned_group_id    int,
   OUT assigned_group_state pgautofailover.replication_state,
   OUT assigned_candidate_priority 	int,
   OUT assigned_replication_quorum  bool,
   OUT assigned_node_name   text
 )
RETURNS record LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$register_node$$;

grant execute on function
      pgautofailover.register_node(text,text,int,name,text,bigint,int,pgautofailover.replication_state,text, int, bool)
   to autoctl_node;

CREATE FUNCTION pgautofailover.node_active
 (
    IN formation_id           		text,
    IN node_id        		        int,
    IN group_id       		        int,
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
      pgautofailover.node_active(text,int,int,
                          pgautofailover.replication_state,bool,pg_lsn,text)
   to autoctl_node;


DROP FUNCTION pgautofailover.get_nodes(text, int);

CREATE FUNCTION pgautofailover.get_nodes
 (
    IN formation_id     text default 'default',
    IN group_id         int default NULL,
   OUT node_id          int,
   OUT node_name        text,
   OUT node_host        text,
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

DROP FUNCTION pgautofailover.get_primary(text, int);

CREATE FUNCTION pgautofailover.get_primary
 (
    IN formation_id      text default 'default',
    IN group_id          int default 0,
   OUT primary_node_id   int,
   OUT primary_name      text,
   OUT primary_host      text,
   OUT primary_port      int
 )
RETURNS record LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$get_primary$$;

comment on function pgautofailover.get_primary(text,int)
        is 'get the writable node for a group';

grant execute on function pgautofailover.get_primary(text,int)
   to autoctl_node;

DROP FUNCTION IF EXISTS pgautofailover.get_other_nodes (text,integer);

CREATE FUNCTION pgautofailover.get_other_nodes
 (
    IN nodeid           int,
   OUT node_id          int,
   OUT node_name        text,
   OUT node_host        text,
   OUT node_port        int,
   OUT node_lsn         pg_lsn,
   OUT node_is_primary  bool
 )
RETURNS SETOF record LANGUAGE C STRICT
AS 'MODULE_PATHNAME', $$get_other_nodes$$;

comment on function pgautofailover.get_other_nodes(int)
        is 'get the other nodes in a group';

grant execute on function pgautofailover.get_other_nodes(int)
   to autoctl_node;

CREATE FUNCTION pgautofailover.get_other_nodes
 (
    IN nodeid           int,
    IN current_state    pgautofailover.replication_state,
   OUT node_id          int,
   OUT node_name        text,
   OUT node_host        text,
   OUT node_port        int,
   OUT node_lsn         pg_lsn,
   OUT node_is_primary  bool
 )
RETURNS SETOF record LANGUAGE C STRICT
AS 'MODULE_PATHNAME', $$get_other_nodes$$;

comment on function pgautofailover.get_other_nodes
                    (int,pgautofailover.replication_state)
        is 'get the other nodes in a group, filtering on current_state';

grant execute on function pgautofailover.get_other_nodes
                          (int,pgautofailover.replication_state)
   to autoctl_node;


DROP FUNCTION pgautofailover.get_coordinator(text);

CREATE FUNCTION pgautofailover.get_coordinator
 (
    IN formation_id  text default 'default',
   OUT node_host     text,
   OUT node_port     int
 )
RETURNS SETOF record LANGUAGE SQL STRICT
AS $$
  select nodehost, nodeport
    from pgautofailover.node
         join pgautofailover.formation using(formationid)
   where formationid = formation_id
     and groupid = 0
     and goalstate in ('single', 'wait_primary', 'primary')
     and reportedstate in ('single', 'wait_primary', 'primary');
$$;

grant execute on function pgautofailover.get_coordinator(text)
   to autoctl_node;


CREATE FUNCTION pgautofailover.get_most_advanced_standby
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
      and reportedstate = 'report_lsn'
 order by reportedlsn desc, health desc
    limit 1;
$$;

grant execute on function pgautofailover.get_most_advanced_standby(text,int)
   to autoctl_node;

DROP FUNCTION IF EXISTS pgautofailover.remove_node(text, int);

CREATE FUNCTION pgautofailover.remove_node
 (
   node_id int
 )
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$remove_node_by_nodeid$$;

comment on function pgautofailover.remove_node(int)
        is 'remove a node from the monitor';

grant execute on function pgautofailover.remove_node(int)
   to autoctl_node;

CREATE FUNCTION pgautofailover.remove_node
 (
   node_host text,
   node_port int default 5432
 )
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$remove_node_by_host$$;

comment on function pgautofailover.remove_node(text,int)
        is 'remove a node from the monitor';

grant execute on function pgautofailover.remove_node(text,int)
   to autoctl_node;

CREATE FUNCTION pgautofailover.perform_promotion
 (
  formation_id text,
  node_name    text
 )
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$perform_promotion$$;

comment on function pgautofailover.perform_promotion(text,text)
        is 'manually failover from the primary to the given node';

grant execute on function pgautofailover.perform_promotion(text,text)
   to autoctl_node;

DROP FUNCTION pgautofailover.start_maintenance(text, int);
DROP FUNCTION pgautofailover.stop_maintenance(text, int);

CREATE FUNCTION pgautofailover.start_maintenance(node_id int)
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$start_maintenance$$;

comment on function pgautofailover.start_maintenance(int)
        is 'set a node in maintenance state';

grant execute on function pgautofailover.start_maintenance(int)
   to autoctl_node;

CREATE FUNCTION pgautofailover.stop_maintenance(node_id int)
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$stop_maintenance$$;

comment on function pgautofailover.stop_maintenance(int)
        is 'set a node out of maintenance state';

grant execute on function pgautofailover.stop_maintenance(int)
   to autoctl_node;

DROP FUNCTION pgautofailover.last_events(int);
DROP FUNCTION pgautofailover.last_events(text,int);
DROP FUNCTION pgautofailover.last_events(text,int,int);

CREATE FUNCTION pgautofailover.last_events
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
         reportedrepstate, reportedlsn,
         candidatepriority, replicationquorum, description
    from pgautofailover.event
order by eventid desc
   limit count
)
select * from last_events order by eventtime, eventid;
$$;

comment on function pgautofailover.last_events(int)
        is 'retrieve last COUNT events';

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
           nodeid, groupid, nodename, nodehost, nodeport,
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

comment on function pgautofailover.last_events(text,int)
        is 'retrieve last COUNT events for given formation';

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
           nodeid, groupid, nodename, nodehost, nodeport,
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

comment on function pgautofailover.last_events(text,int,int)
        is 'retrieve last COUNT events for given formation and group';


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
   OUT reported_lsn         pg_lsn,
   OUT health               integer
 )
RETURNS SETOF record LANGUAGE SQL STRICT
AS $$
   select kind, nodename, nodehost, nodeport, groupid, nodeid,
          reportedstate, goalstate,
   		  candidatepriority, replicationquorum,
          reportedlsn, health
     from pgautofailover.node
     join pgautofailover.formation using(formationid)
    where formationid = formation_id
 order by groupid, nodeid;
$$;

comment on function pgautofailover.current_state(text)
        is 'get the current state of both nodes of a formation';

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
   OUT reported_lsn         pg_lsn,
   OUT health               integer
 )
RETURNS SETOF record LANGUAGE SQL STRICT
AS $$
   select kind, nodename, nodehost, nodeport, groupid, nodeid,
          reportedstate, goalstate,
   		  candidatepriority, replicationquorum,
          reportedlsn, health
     from pgautofailover.node
     join pgautofailover.formation using(formationid)
    where formationid = formation_id
      and groupid = group_id
 order by groupid, nodeid;
$$;

comment on function pgautofailover.current_state(text, int)
        is 'get the current state of both nodes of a group in a formation';


CREATE OR REPLACE FUNCTION pgautofailover.formation_uri
 (
    IN formation_id         text DEFAULT 'default',
    IN sslmode              text DEFAULT 'prefer',
    IN sslrootcert          text DEFAULT '',
    IN sslcrl               text DEFAULT ''
 )
RETURNS text LANGUAGE SQL STRICT
AS $$
    select case
           when string_agg(format('%s:%s', nodehost, nodeport),',') is not null
           then format(
               'postgres://%s/%s?target_session_attrs=read-write&sslmode=%s%s%s',
               string_agg(format('%s:%s', nodehost, nodeport),','),
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

DROP FUNCTION pgautofailover.set_node_candidate_priority(int,text,int,int);

CREATE FUNCTION pgautofailover.set_node_candidate_priority
 (
    IN formation_id         text,
    IN node_name            text,
    IN candidate_priority	int
 )
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$set_node_candidate_priority$$;

comment on function pgautofailover.set_node_candidate_priority(text, text, int)
        is 'sets the candidate priority value for a node. Expects a priority value between 0 and 100. 0 if the node is not a candidate to be promoted to be primary.';

grant execute on function
      pgautofailover.set_node_candidate_priority(text, text, int)
   to autoctl_node;

DROP FUNCTION pgautofailover.set_node_replication_quorum(int,text,int,bool);

CREATE FUNCTION pgautofailover.set_node_replication_quorum
 (
    IN formation_id       text,
    IN node_name          text,
    IN replication_quorum bool
 )
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$set_node_replication_quorum$$;

comment on function pgautofailover.set_node_replication_quorum(text, text, bool)
        is 'sets the replication quorum value for a node. true if the node participates in write quorum';

grant execute on function
      pgautofailover.set_node_replication_quorum(text, text, bool)
   to autoctl_node;

CREATE FUNCTION pgautofailover.formation_settings
 (
    IN formation_id         text default 'default',
   OUT context              text,
   OUT group_id             int,
   OUT node_id              bigint,
   OUT nodename             text,
   OUT setting              text,
   OUT value                text
 )
RETURNS SETOF record LANGUAGE SQL STRICT
AS $$
  with groups(formationid, groupid) as
  (
     select formationid, groupid
       from pgautofailover.node
      where formationid = formation_id
   group by formationid, groupid
  )

  -- context: formation, number_sync_standbys
  select 'formation' as context,
         NULL as group_id, NULL as node_id, formationid as nodename,
         'number_sync_standbys' as setting,
         cast(number_sync_standbys as text) as value
    from pgautofailover.formation
   where formationid = formation_id

union all

  -- context: primary, one entry per group in the formation
  select 'primary', groups.groupid, nodes.node_id, nodes.node_name,
         'synchronous_standby_names',
         format('''%s''',
         pgautofailover.synchronous_standby_names(formationid, groupid))
    from groups, pgautofailover.get_nodes(formationid, groupid) as nodes
   where node_is_primary

union all

(
  -- context: node, one entry per node in the formation
  select 'node', node.groupid, node.nodeid, node.nodename,
         'replication quorum', cast(node.replicationquorum as text)
    from pgautofailover.node as node
   where node.formationid = formation_id
order by nodeid
)

union all

(
  select 'node', node.groupid, node.nodeid, node.nodename,
         'candidate priority', cast(node.candidatepriority as text)
    from pgautofailover.node as node
   where node.formationid = formation_id
order by nodeid
)
$$;

comment on function pgautofailover.formation_settings(text)
        is 'get the current replication settings a formation';

drop function pgautofailover.adjust_number_sync_standbys() cascade;

DROP TABLE pgautofailover.node_upgrade_old;
DROP TABLE pgautofailover.event_upgrade_old;
