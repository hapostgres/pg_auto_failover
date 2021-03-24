--
-- extension update file from 1.4.2 to 1.5.1
--
-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pgautofailover" to load this file. \quit

ALTER TABLE pgautofailover.node
  ADD COLUMN nodecluster text not null default 'default';

DROP FUNCTION IF EXISTS pgautofailover.formation_uri(text, text, text, text);

CREATE FUNCTION pgautofailover.formation_uri
 (
    IN formation_id         text DEFAULT 'default',
    IN cluster_name         text DEFAULT 'default',
    IN sslmode              text DEFAULT 'prefer',
    IN sslrootcert          text DEFAULT '',
    IN sslcrl               text DEFAULT ''
 )
RETURNS text LANGUAGE SQL STRICT
AS $$
    select case
           when string_agg(format('%s:%s', nodehost, nodeport),',') is not null
           then format(
               'postgres://%s/%s?%ssslmode=%s%s%s',
               string_agg(format('%s:%s', nodehost, nodeport),','),
               -- as we join formation on node we get the same dbname for all
               -- entries, pick one.
               min(dbname),
               case when cluster_name = 'default'
                    then 'target_session_attrs=read-write&'
                    else ''
               end,
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
       and groupid = 0
       and nodecluster = cluster_name;
$$;

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
