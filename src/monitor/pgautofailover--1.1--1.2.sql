--
-- extension update file from 1.1 to 1.2
--
-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "ALTER EXTENSION pgautofailover UPDATE TO 1.2" to load this file. \quit


DROP FUNCTION IF EXISTS pgautofailover.formation_uri(text);

CREATE FUNCTION pgautofailover.formation_uri
 (
    IN formation_id         text DEFAULT 'default',
    IN sslmode              text DEFAULT 'prefer'
 )
RETURNS text LANGUAGE SQL STRICT
AS $$
    select case
           when string_agg(format('%s:%s', nodename, nodeport),',') is not null
           then format('postgres://%s/%s?target_session_attrs=read-write&sslmode=%s',
                       string_agg(format('%s:%s', nodename, nodeport),','),
                       -- as we join formation on node we get the same dbname for all
                       -- entries, pick one.
                       min(dbname),
                       min(sslmode)
                      )
           end as uri
      from pgautofailover.node as node
           join pgautofailover.formation using(formationid)
     where formationid = formation_id
       and groupid = 0;
$$;
