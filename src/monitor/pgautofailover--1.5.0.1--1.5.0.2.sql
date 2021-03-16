--
-- extension update file from 1.5.0.1 to 1.5.0.2
--
-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pgautofailover" to load this file. \quit

DROP FUNCTION IF EXISTS
     pgautofailover.register_node(text,text,int,name,text,bigint,int,
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
    IN desired_node_id      int default -1,
    IN desired_group_id     int default -1,
    IN initial_group_role   pgautofailover.replication_state default 'init',
    IN node_kind            text default 'standalone',
    IN candidate_priority 	int default 100,
    IN replication_quorum	bool default true,
    IN node_cluster         text default 'default',
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
      pgautofailover.register_node(text,text,int,name,text,bigint,int,int,
                                   pgautofailover.replication_state,text,
                                   int,bool,text)
   to autoctl_node;
