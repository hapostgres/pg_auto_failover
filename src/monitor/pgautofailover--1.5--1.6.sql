--
-- extension update file from 1.5 to 1.6
--
-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pgautofailover" to load this file. \quit

-- remove a possible leftover from pg_auto_failover 1.4 that was not correctly
-- removed in a migration to 1.5
DROP FUNCTION IF EXISTS
     pgautofailover.register_node(text,text,int,name,text,bigint,int,
                                  pgautofailover.replication_state,text,
                                  int,bool);

DROP FUNCTION
     pgautofailover.register_node(text,text,int,name,text,bigint,int,int,
                                  pgautofailover.replication_state,text,
                                  int,bool,text);

DROP FUNCTION
     pgautofailover.node_active(text,int,int,
                                pgautofailover.replication_state,bool,pg_lsn,text);

DROP FUNCTION pgautofailover.get_other_nodes(int);

DROP FUNCTION pgautofailover.get_other_nodes
              (integer,pgautofailover.replication_state);

DROP FUNCTION pgautofailover.last_events(int);
DROP FUNCTION pgautofailover.last_events(text,int);
DROP FUNCTION pgautofailover.last_events(text,int,int);

DROP FUNCTION pgautofailover.current_state(text);
DROP FUNCTION pgautofailover.current_state(text,int);

DROP TRIGGER disable_secondary_check ON pgautofailover.formation;
DROP FUNCTION pgautofailover.update_secondary_check();

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
    'join_secondary',
    'dropped'
 );

-- Note the double cast here, first to text and only then to the new enums
ALTER TABLE pgautofailover.event
      ALTER COLUMN goalstate
              TYPE pgautofailover.replication_state
             USING goalstate::text::pgautofailover.replication_state,

      ALTER COLUMN reportedstate
              TYPE pgautofailover.replication_state
             USING reportedstate::text::pgautofailover.replication_state;

ALTER TABLE pgautofailover.node RENAME TO node_upgrade_old;

ALTER TABLE pgautofailover.node_upgrade_old
      RENAME CONSTRAINT system_identifier_is_null_at_init_only
                     TO system_identifier_is_null_at_init_only_old;

ALTER TABLE pgautofailover.node_upgrade_old
      RENAME CONSTRAINT same_system_identifier_within_group
                     TO same_system_identifier_within_group_old;

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
    reportedtli          int not null default 1 check (reportedtli > 0),
    reportedlsn          pg_lsn not null default '0/0',
    walreporttime        timestamptz not null default now(),
    health               integer not null default -1,
    healthchecktime      timestamptz not null default now(),
    statechangetime      timestamptz not null default now(),
    candidatepriority	 int not null default 100,
    replicationquorum	 bool not null default true,
    nodecluster          text not null default 'default',

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
         CHECK (
                  (
                       sysidentifier IS NULL
                   AND reportedstate
                       IN (
                           'init',
                           'wait_standby',
                           'catchingup',
                           'dropped'
                          )
                   )
                OR sysidentifier IS NOT NULL
               ),

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
  reporttime, reportedtli, reportedlsn, walreporttime,
  health, healthchecktime, statechangetime,
  candidatepriority, replicationquorum, nodecluster
 )
 SELECT formationid, nodeid, groupid,
        nodename, nodehost, nodeport, sysidentifier,
        goalstate::text::pgautofailover.replication_state,
        reportedstate::text::pgautofailover.replication_state,
        reportedpgisrunning, reportedrepstate, reporttime,
        1 as reportedtli,
        reportedlsn, walreporttime, health, healthchecktime, statechangetime,
        candidatepriority, replicationquorum, nodecluster
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
    reportedtli       int not null default 1 check (reportedtli > 0),
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
  reportedstate, goalstate, reportedrepstate,
  reportedtli, reportedlsn, candidatepriority, replicationquorum,
  description
 )
 SELECT eventid, eventtime, formationid, nodeid, groupid,
        nodename, nodehost, nodeport,
        reportedstate, goalstate, reportedrepstate,
        1 as reportedtli, reportedlsn, candidatepriority, replicationquorum,
        description
   FROM pgautofailover.event_upgrade_old as event;

DROP TABLE pgautofailover.event_upgrade_old;
DROP TABLE pgautofailover.node_upgrade_old;
DROP TYPE pgautofailover.old_replication_state;

GRANT SELECT ON ALL TABLES IN SCHEMA pgautofailover TO autoctl_node;



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
    IN candidate_priority 	int default 100,
    IN replication_quorum	bool default true,
    IN node_cluster         text default 'default',
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
                                   int,bool,text)
   to autoctl_node;


CREATE FUNCTION pgautofailover.node_active
 (
    IN formation_id           		text,
    IN node_id        		        bigint,
    IN group_id       		        int,
    IN current_group_role     		pgautofailover.replication_state default 'init',
    IN current_pg_is_running  		bool default true,
    IN current_tli			  		integer default 1,
    IN current_lsn			  		pg_lsn default '0/0',
    IN current_rep_state      		text default '',
   OUT assigned_node_id       		bigint,
   OUT assigned_group_id      		int,
   OUT assigned_group_state   		pgautofailover.replication_state,
   OUT assigned_candidate_priority 	int,
   OUT assigned_replication_quorum  bool
 )
RETURNS record LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$node_active$$;

grant execute on function
      pgautofailover.node_active(text,bigint,int,
                          pgautofailover.replication_state,bool,int,pg_lsn,text)
   to autoctl_node;


DROP FUNCTION pgautofailover.get_nodes(text,int);

CREATE FUNCTION pgautofailover.get_nodes
 (
    IN formation_id     text default 'default',
    IN group_id         int default NULL,
   OUT node_id          bigint,
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


DROP FUNCTION pgautofailover.get_primary(text,int);

CREATE FUNCTION pgautofailover.get_primary
 (
    IN formation_id      text default 'default',
    IN group_id          int default 0,
   OUT primary_node_id   bigint,
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


CREATE FUNCTION pgautofailover.get_other_nodes
 (
    IN nodeid           bigint,
   OUT node_id          bigint,
   OUT node_name        text,
   OUT node_host        text,
   OUT node_port        int,
   OUT node_lsn         pg_lsn,
   OUT node_is_primary  bool
 )
RETURNS SETOF record LANGUAGE C STRICT
AS 'MODULE_PATHNAME', $$get_other_nodes$$;

comment on function pgautofailover.get_other_nodes(bigint)
        is 'get the other nodes in a group';

grant execute on function pgautofailover.get_other_nodes(bigint)
   to autoctl_node;

CREATE FUNCTION pgautofailover.get_other_nodes
 (
    IN nodeid           bigint,
    IN current_state    pgautofailover.replication_state,
   OUT node_id          bigint,
   OUT node_name        text,
   OUT node_host        text,
   OUT node_port        int,
   OUT node_lsn         pg_lsn,
   OUT node_is_primary  bool
 )
RETURNS SETOF record LANGUAGE C STRICT
AS 'MODULE_PATHNAME', $$get_other_nodes$$;

comment on function pgautofailover.get_other_nodes
                    (bigint,pgautofailover.replication_state)
        is 'get the other nodes in a group, filtering on current_state';

grant execute on function pgautofailover.get_other_nodes
                          (bigint,pgautofailover.replication_state)
   to autoctl_node;


DROP FUNCTION pgautofailover.remove_node(int);

CREATE FUNCTION pgautofailover.remove_node
 (
   node_id bigint,
   force   bool default 'false'
 )
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$remove_node_by_nodeid$$;

comment on function pgautofailover.remove_node(bigint,bool)
        is 'remove a node from the monitor';

grant execute on function pgautofailover.remove_node(bigint,bool)
   to autoctl_node;

DROP FUNCTION pgautofailover.remove_node(text,int);

CREATE FUNCTION pgautofailover.remove_node
 (
   node_host text,
   node_port int default 5432,
   force     bool default 'false'
 )
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$remove_node_by_host$$;

comment on function pgautofailover.remove_node(text,int,bool)
        is 'remove a node from the monitor';

grant execute on function pgautofailover.remove_node(text,int,bool)
   to autoctl_node;

DROP FUNCTION pgautofailover.start_maintenance(node_id int);

CREATE FUNCTION pgautofailover.start_maintenance(node_id bigint)
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$start_maintenance$$;

comment on function pgautofailover.start_maintenance(bigint)
        is 'set a node in maintenance state';

grant execute on function pgautofailover.start_maintenance(bigint)
   to autoctl_node;

DROP FUNCTION pgautofailover.stop_maintenance(node_id int);

CREATE FUNCTION pgautofailover.stop_maintenance(node_id bigint)
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$stop_maintenance$$;

comment on function pgautofailover.stop_maintenance(bigint)
        is 'set a node out of maintenance state';

grant execute on function pgautofailover.stop_maintenance(bigint)
   to autoctl_node;


CREATE OR REPLACE FUNCTION pgautofailover.update_secondary_check()
  RETURNS trigger
  LANGUAGE 'plpgsql'
AS $$
declare
  nodeid        bigint := null;
  reportedstate pgautofailover.replication_state := null;
begin
	-- when secondary changes from true to false, check all nodes remaining are primary
	if     new.opt_secondary is false
	   and new.opt_secondary is distinct from old.opt_secondary
	then
		select node.nodeid, node.reportedstate
		  into nodeid, reportedstate
		  from pgautofailover.node
		 where node.formationid = new.formationid
		   and node.reportedstate <> 'single'
           and node.goalstate <> 'dropped';

		if nodeid is not null
		then
		    raise exception object_not_in_prerequisite_state
		      using
		        message = 'formation has nodes that are not in SINGLE state',
		         detail = 'nodeid ' || nodeid || ' is in state ' || reportedstate,
		           hint = 'drop secondary nodes before disabling secondaries on formation';
		end if;
	end if;

    return new;
end
$$;

comment on function pgautofailover.update_secondary_check()
        is 'performs a check when changes to hassecondary on pgautofailover.formation are made, verifying cluster state allows the change';

CREATE TRIGGER disable_secondary_check
	BEFORE UPDATE
	ON pgautofailover.formation
	FOR EACH ROW
	EXECUTE PROCEDURE pgautofailover.update_secondary_check();


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
         reportedrepstate, reportedtli, reportedlsn,
         candidatepriority, replicationquorum, description
    from pgautofailover.event
order by eventid desc
   limit count
)
select * from last_events order by eventtime, eventid;
$$;

comment on function pgautofailover.last_events(int)
        is 'retrieve last COUNT events';

grant execute on function pgautofailover.last_events(int)
   to autoctl_node;

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
           reportedrepstate, reportedtli, reportedlsn,
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

grant execute on function pgautofailover.last_events(text,int)
   to autoctl_node;

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
           reportedrepstate, reportedtli, reportedlsn,
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

grant execute on function pgautofailover.last_events(text,int,int)
   to autoctl_node;


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
   OUT nodecluster          text
 )
RETURNS SETOF record LANGUAGE SQL STRICT
AS $$
   select kind, nodename, nodehost, nodeport, groupid, nodeid,
          reportedstate, goalstate,
   		  candidatepriority, replicationquorum,
          reportedtli, reportedlsn, health, nodecluster
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
   OUT nodecluster          text
 )
RETURNS SETOF record LANGUAGE SQL STRICT
AS $$
   select kind, nodename, nodehost, nodeport, groupid, nodeid,
          reportedstate, goalstate,
   		  candidatepriority, replicationquorum,
          reportedtli, reportedlsn, health, nodecluster
     from pgautofailover.node
     join pgautofailover.formation using(formationid)
    where formationid = formation_id
      and groupid = group_id
 order by groupid, nodeid;
$$;

grant execute on function pgautofailover.current_state(text, int)
   to autoctl_node;

comment on function pgautofailover.current_state(text, int)
        is 'get the current state of both nodes of a group in a formation';
