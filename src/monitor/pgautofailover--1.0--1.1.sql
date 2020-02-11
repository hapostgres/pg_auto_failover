--
-- extension update file from 1.0 to 1.1
--
-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "ALTER EXTENSION pgautofailover UPDATE TO 1.1" to load this file. \quit

ALTER TABLE pgautofailover.node
	RENAME TO node_upgrade_old;

CREATE TABLE pgautofailover.node
 (
    formationid          text not null default 'default',
    nodeid               bigint not null DEFAULT nextval('pgautofailover.node_nodeid_seq'::regclass),
    groupid              int not null,
    nodename             text not null,
    nodeport             integer not null,
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

    UNIQUE (nodename, nodeport),
    PRIMARY KEY (nodeid),
    FOREIGN KEY (formationid) REFERENCES pgautofailover.formation(formationid)
 )
 -- we expect few rows and lots of UPDATE, let's benefit from HOT
 WITH (fillfactor = 25);

ALTER SEQUENCE pgautofailover.node_nodeid_seq OWNED BY pgautofailover.node.nodeid;

INSERT INTO pgautofailover.node (formationid, nodeid, groupid, nodename, nodeport, goalstate, reportedstate,
		reportedpgisrunning, reportedrepstate, reporttime, walreporttime, health, healthchecktime, statechangetime)
	SELECT formationid, nodeid, groupid, nodename, nodeport, goalstate, reportedstate,
		reportedpgisrunning, reportedrepstate, reporttime, walreporttime, health, healthchecktime, statechangetime
	FROM pgautofailover.node_upgrade_old;

ALTER TABLE pgautofailover.event
	RENAME TO event_upgrade_old;

ALTER TABLE pgautofailover.event_upgrade_old	
	ALTER COLUMN nodeid DROP NOT NULL,
    ALTER COLUMN nodeid SET DEFAULT NULL;

DROP SEQUENCE pgautofailover.event_nodeid_seq;

CREATE TABLE pgautofailover.event
 (
    eventid          bigint not null DEFAULT nextval('pgautofailover.event_eventid_seq'::regclass),
    eventtime        timestamptz not null default now(),
    formationid      text not null,
    nodeid           bigint not null,
    groupid          int not null,
    nodename         text not null,
    nodeport         integer not null,
    reportedstate    pgautofailover.replication_state not null,
    goalstate        pgautofailover.replication_state not null,
    reportedrepstate text,
    reportedlsn      pg_lsn not null default '0/0',
    description      text,

    PRIMARY KEY (eventid)
 );

ALTER SEQUENCE pgautofailover.event_eventid_seq OWNED BY pgautofailover.event.eventid;

INSERT INTO pgautofailover.event
		(eventid, eventtime, formationid, nodeid, groupid, nodename, nodeport, reportedstate, goalstate, reportedrepstate, description)
	SELECT eventid, eventtime, formationid, nodeid, groupid, nodename, nodeport, reportedstate, goalstate, reportedrepstate, description
	FROM pgautofailover.event_upgrade_old;


GRANT SELECT ON ALL TABLES IN SCHEMA pgautofailover TO autoctl_node;

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


DROP FUNCTION pgautofailover.last_events(integer);

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
         reportedrepstate, reportedlsn, description
    from pgautofailover.event
order by eventid desc
   limit count
)
select * from last_events order by eventtime, eventid;
$$;

DROP FUNCTION pgautofailover.last_events(text, integer);

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
           reportedrepstate, reportedlsn, description
      from pgautofailover.event
     where formationid = formation_id
  order by eventid desc
     limit count
)
select * from last_events order by eventtime, eventid;
$$;


DROP FUNCTION pgautofailover.last_events(text, integer, integer);

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
           reportedrepstate, reportedlsn, description
      from pgautofailover.event
     where formationid = formation_id
       and groupid = group_id
  order by eventid desc
     limit count
)
select * from last_events order by eventtime, eventid;
$$;

DROP TABLE pgautofailover.node_upgrade_old;
DROP TABLE pgautofailover.event_upgrade_old;
