--
-- extension update file from 1.5 to 1.6
--
-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pgautofailover" to load this file. \quit

DROP FUNCTION
     pgautofailover.register_node(text,text,int,name,text,bigint,int,int,
                                  pgautofailover.replication_state,text,
                                  int,bool,text);

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


DROP FUNCTION
     pgautofailover.node_active(text,int,int,
                                pgautofailover.replication_state,bool,pg_lsn,text);

CREATE FUNCTION pgautofailover.node_active
 (
    IN formation_id           		text,
    IN node_id        		        bigint,
    IN group_id       		        int,
    IN current_group_role     		pgautofailover.replication_state default 'init',
    IN current_pg_is_running  		bool default true,
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
                          pgautofailover.replication_state,bool,pg_lsn,text)
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


DROP FUNCTION pgautofailover.get_other_nodes(int);

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

DROP FUNCTION pgautofailover.get_other_nodes(int,pgautofailover.replication_state);

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
   node_id bigint
 )
RETURNS bool LANGUAGE C STRICT SECURITY DEFINER
AS 'MODULE_PATHNAME', $$remove_node_by_nodeid$$;

comment on function pgautofailover.remove_node(bigint)
        is 'remove a node from the monitor';

grant execute on function pgautofailover.remove_node(bigint)
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
