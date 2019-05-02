-- Copyright (c) Microsoft Corporation. All rights reserved.
-- Licensed under the PostgreSQL License.

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pgautofailover" to load this file. \quit

DO
$body$
BEGIN
   if not exists (select * from pg_catalog.pg_user where usename = 'autoctl_node')
   then
      create role autoctl_node with login;
   end if;
END
$body$;

CREATE SCHEMA pgautofailover;
GRANT USAGE ON SCHEMA pgautofailover TO autoctl_node;

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
    'maintenance'
 );

CREATE TABLE pgautofailover.formation
 (
    formationid   text NOT NULL DEFAULT 'default',
    kind          text NOT NULL DEFAULT 'pgsql',
    dbname        name NOT NULL DEFAULT 'postgres',
    opt_secondary bool NOT NULL DEFAULT true,
    PRIMARY KEY   (formationid)
 );
insert into pgautofailover.formation (formationid) values ('default');

CREATE FUNCTION pgautofailover.create_formation
 (
    IN formation_id  text,
    IN kind          text,
    IN dbname        name,
    IN opt_secondary bool,
   OUT formation_id  text,
   OUT kind          text,
   OUT dbname        name,
   OUT opt_secondary bool
 )
RETURNS record LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$create_formation$$;

grant execute on function
      pgautofailover.create_formation(text,text,name,bool)
   to autoctl_node;

CREATE FUNCTION pgautofailover.drop_formation
 (
    IN formation_id  text
 )
RETURNS void LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$drop_formation$$;

grant execute on function pgautofailover.drop_formation(text) to autoctl_node;


CREATE TABLE pgautofailover.node
 (
    formationid          text not null default 'default',
    nodeid               bigserial,
    groupid              int not null,
    nodename             text not null,
    nodeport             integer not null,
    goalstate            pgautofailover.replication_state not null default 'init',
    reportedstate        pgautofailover.replication_state not null,
    reportedpgisrunning  bool default true,
    reportedrepstate     text default 'async',
    reporttime           timestamptz not null default now(),
    waldelta             bigint not null default -1,
    walreporttime        timestamptz not null default now(),
    health               integer not null default -1,
    healthchecktime      timestamptz not null default now(),
    statechangetime      timestamptz not null default now(),

    UNIQUE (nodename, nodeport),
    PRIMARY KEY (nodeid),
    FOREIGN KEY (formationid) REFERENCES pgautofailover.formation(formationid)
 )
 -- we expect few rows and lots of UPDATE, let's benefit from HOT
 WITH (fillfactor = 25);

CREATE TABLE pgautofailover.event
 (
    eventid          bigserial not null,
    eventtime        timestamptz not null default now(),
    formationid      text not null,
    nodeid           bigserial,
    groupid          int not null,
    nodename         text not null,
    nodeport         integer not null,
    reportedstate    pgautofailover.replication_state not null,
    goalstate        pgautofailover.replication_state not null,
    reportedrepstate text,
    waldelta         bigint not null default -1,
    description      text,

    PRIMARY KEY (eventid)
 );

GRANT SELECT ON ALL TABLES IN SCHEMA pgautofailover TO autoctl_node;

CREATE FUNCTION pgautofailover.register_node
 (
    IN formation_id         text,
    IN node_name            text,
    IN node_port            int,
    IN dbname               name,
    IN desired_group_id     int default -1,
    IN initial_group_role   pgautofailover.replication_state default 'init',
    IN node_kind            text default 'standalone',
   OUT assigned_node_id     int,
   OUT assigned_group_id    int,
   OUT assigned_group_state pgautofailover.replication_state
 )
RETURNS record LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$register_node$$;

grant execute on function
      pgautofailover.register_node(text,text,int,name,int,pgautofailover.replication_state,text)
   to autoctl_node;


CREATE FUNCTION pgautofailover.node_active
 (
    IN formation_id           text,
    IN node_name              text,
    IN node_port              int,
    IN current_node_id        int default -1,
    IN current_group_id       int default -1,
    IN current_group_role     pgautofailover.replication_state default 'init',
    IN current_pg_is_running  bool default true,
    IN current_wal_delta      bigint default -1,
    IN current_rep_state      text default '',
   OUT assigned_node_id       int,
   OUT assigned_group_id      int,
   OUT assigned_group_state   pgautofailover.replication_state
 )
RETURNS record LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$node_active$$;

grant execute on function
      pgautofailover.node_active(text,text,int,int,int,
                          pgautofailover.replication_state,bool,bigint,text)
   to autoctl_node;

CREATE FUNCTION pgautofailover.get_primary
 (
    IN formation_id  text default 'default',
    IN group_id      int default 0,
   OUT primary_name  text,
   OUT primary_port  int
 )
RETURNS record LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$get_primary$$;

comment on function pgautofailover.get_primary(text,int)
        is 'get the writable node for a group';

grant execute on function pgautofailover.get_primary(text,int)
   to autoctl_node;

CREATE FUNCTION pgautofailover.get_other_node
 (
    IN node_name      text,
    IN node_port      int,
   OUT secondary_name text,
   OUT secondary_port int
 )
RETURNS record LANGUAGE C STRICT
AS 'MODULE_PATHNAME', $$get_other_node$$;

comment on function pgautofailover.get_other_node(text,int)
        is 'get the other node in a group';

grant execute on function pgautofailover.get_other_node(text,int)
   to autoctl_node;

CREATE FUNCTION pgautofailover.get_coordinator
 (
    IN formation_id  text default 'default',
   OUT node_name     text,
   OUT node_port     int
 )
RETURNS SETOF record LANGUAGE SQL STRICT
AS $$
  select nodename, nodeport
    from pgautofailover.node
         join pgautofailover.formation using(formationid)
   where formationid = formation_id
     and groupid = 0
     and goalstate in ('single', 'wait_primary', 'primary')
     and reportedstate in ('single', 'wait_primary', 'primary');
$$;

grant execute on function pgautofailover.get_coordinator(text)
   to autoctl_node;

CREATE FUNCTION pgautofailover.remove_node
 (
   node_name text,
   node_port int default 5432
 )
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$remove_node$$;

comment on function pgautofailover.remove_node(text,int)
        is 'remove a node from the monitor';

CREATE FUNCTION pgautofailover.perform_failover
 (
  formation_id text default 'default',
  group_id     int  default 0
 )
RETURNS void LANGUAGE C STRICT
AS 'MODULE_PATHNAME', $$perform_failover$$;

comment on function pgautofailover.perform_failover(text,int)
        is 'manually failover from the primary to the secondary';

CREATE FUNCTION pgautofailover.start_maintenance
 (
   node_name text,
   node_port int default 5432
 )
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$start_maintenance$$;

comment on function pgautofailover.start_maintenance(text,int)
        is 'set a node in maintenance state';

CREATE FUNCTION pgautofailover.stop_maintenance
 (
   node_name text,
   node_port int default 5432
 )
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$stop_maintenance$$;

comment on function pgautofailover.stop_maintenance(text,int)
        is 'set a node out of maintenance state';


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
         reportedrepstate, waldelta, description
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
           nodeid, groupid, nodename, nodeport,
           reportedstate, goalstate,
           reportedrepstate, waldelta, description
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
           nodeid, groupid, nodename, nodeport,
           reportedstate, goalstate,
           reportedrepstate, waldelta, description
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
   OUT assigned_group_state pgautofailover.replication_state
 )
RETURNS SETOF record LANGUAGE SQL STRICT
AS $$
   select nodename, nodeport, groupid, nodeid, reportedstate, goalstate
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
   OUT assigned_group_state pgautofailover.replication_state
 )
RETURNS SETOF record LANGUAGE SQL STRICT
AS $$
   select nodename, nodeport, groupid, nodeid, reportedstate, goalstate
     from pgautofailover.node
    where formationid = formation_id
      and groupid = group_id
 order by groupid, nodeid;
$$;

comment on function pgautofailover.current_state(text, int)
        is 'get the current state of both nodes of a group in a formation';

CREATE FUNCTION pgautofailover.formation_uri
 (
    IN formation_id         text DEFAULT 'default'
 )
RETURNS text LANGUAGE SQL STRICT
AS $$
    select case
           when string_agg(format('%s:%s', nodename, nodeport),',') is not null
           then format('postgres://%s/%s?target_session_attrs=read-write',
                       string_agg(format('%s:%s', nodename, nodeport),','),
                       -- as we join formation on node we get the same dbname for all
                       -- entries, pick one.
                       min(dbname)
                      )
           end as uri
      from pgautofailover.node as node
           join pgautofailover.formation using(formationid)
     where formationid = formation_id
       and groupid = 0;
$$;

CREATE FUNCTION pgautofailover.enable_secondary
 (
   formation_id text
 )
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$enable_secondary$$;

comment on function pgautofailover.enable_secondary(text)
        is 'changes the state of a formation to assign secondaries for nodes when added';

CREATE FUNCTION pgautofailover.disable_secondary
 (
   formation_id text
 )
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$disable_secondary$$;

comment on function pgautofailover.disable_secondary(text)
        is 'changes the state of a formation to disable the assignment of secondaries for nodes when added';


CREATE OR REPLACE FUNCTION pgautofailover.update_secondary_check()
  RETURNS trigger
  LANGUAGE 'plpgsql'
AS $$
declare
  nodeid        integer := null;
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
		   and node.reportedstate <> 'single';

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
