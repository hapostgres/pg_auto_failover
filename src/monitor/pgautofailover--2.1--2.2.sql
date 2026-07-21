--
-- extension update file from 2.1 to 2.2
--
-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pgautofailover" to load this file. \quit

--
-- Testing-only functions, not granted to autoctl_node: they let
-- regression/isolation tests hold the monitor's own LockFormation()/
-- LockNodeGroup() locks explicitly, and simulate a health-check-worker
-- observation or the passage of time through the same lock discipline
-- production writers use, rather than bypassing it via a raw UPDATE to
-- pgautofailover.node.
--

CREATE FUNCTION pgautofailover.testing_lock_formation
 (
    IN formation_id text
 )
RETURNS void LANGUAGE C STRICT
AS 'MODULE_PATHNAME', $$testing_lock_formation$$;

comment on function pgautofailover.testing_lock_formation(text)
        is 'testing only: hold LockFormation() until the current transaction ends';

CREATE FUNCTION pgautofailover.testing_lock_node_group
 (
    IN formation_id text,
    IN group_id     int
 )
RETURNS void LANGUAGE C STRICT
AS 'MODULE_PATHNAME', $$testing_lock_node_group$$;

comment on function pgautofailover.testing_lock_node_group(text,int)
        is 'testing only: hold LockNodeGroup() until the current transaction ends';

CREATE FUNCTION pgautofailover.testing_set_node_health
 (
    IN node_id                bigint,
    IN health                 int      default NULL,
    IN report_time_ago        interval default NULL,
    IN state_change_time_ago  interval default NULL,
    IN health_check_time_ago  interval default NULL
 )
RETURNS void LANGUAGE C
AS 'MODULE_PATHNAME', $$testing_set_node_health$$;

comment on function pgautofailover.testing_set_node_health(bigint,int,interval,interval,interval)
        is 'testing only: simulate a health-check-worker write and/or backdate timestamps, through the real lock discipline';
