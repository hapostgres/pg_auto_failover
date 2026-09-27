#!/usr/bin/env bash
#
# Check that upgrading the pgautofailover extension 2.2 -> 2.3 in place yields
# the same catalog as a fresh CREATE EXTENSION at 2.3, and that an existing
# node row survives the upgrade with the new columns correctly defaulted.
#
# usage: ci/check-extension-upgrade.sh [IMAGE]     (default pg_auto_failover:pg17)
#
# Runs one throw-away container (no network) that initdb's a scratch cluster
# with shared_preload_libraries=pgautofailover, then does the comparison
# inside it. Exits non-zero on any difference outside the ALLOWLIST below.

set -eu

IMAGE=${1:-${PGAF_IMAGE:-pg_auto_failover:pg17}}
PGMAJOR=${PGVERSION:-17}

# The script that runs inside the container is this same file, re-executed
# with the INSIDE marker set (so it stays a single, self-contained file).
if [ -z "${PGAF_UPGRADE_CHECK_INSIDE:-}" ]; then
    exec docker run --rm --network none --user postgres \
        -e PGAF_UPGRADE_CHECK_INSIDE=1 -e PGVERSION="$PGMAJOR" \
        -v "$(cd "$(dirname "$0")" && pwd)/check-extension-upgrade.sh:/check.sh:ro" \
        --entrypoint bash "$IMAGE" /check.sh
fi

export PATH=/usr/lib/postgresql/$PGMAJOR/bin:$PATH
D=$(mktemp -d)
PORT=5455

initdb -D "$D/data" -A trust >/dev/null
pg_ctl -D "$D/data" -w -l "$D/log" \
    -o "-c shared_preload_libraries=pgautofailover -c port=$PORT -c unix_socket_directories=$D" \
    start >/dev/null
trap 'pg_ctl -D "$D/data" -m immediate stop >/dev/null 2>&1 || true' EXIT

P="psql -h $D -p $PORT -v ON_ERROR_STOP=1 -X -q"

$P -d postgres -c "create database up22" -c "create database fresh23"

# 2.2 install, with one pre-existing node row (raw insert: register_node()
# would need a running keeper), then the in-place upgrade.
$P -d up22 -c "create extension btree_gist" -c "create extension ltree" \
           -c "create extension pgautofailover version '2.2'"
$P -d up22 -c "insert into pgautofailover.node
    (formationid, groupid, nodename, nodehost, nodeport, sysidentifier,
     goalstate, reportedstate, reportedpgisrunning, reportedrepstate,
     reportedtli, reportedlsn, candidatepriority, replicationquorum, nodecluster)
    values ('default', 0, 'n1', 'h1', 5432, 1, 'single', 'single', true, '',
            1, '0/0', 50, true, 'default')"
$P -d up22 -c "alter extension pgautofailover update to '2.3'"

# data-carrying upgrade: the pre-existing node row keeps its data and the
# new columns take their documented defaults
row=$($P -d up22 -Atc "select nodename, haspgdata, region from pgautofailover.node")
if [ "$row" != "n1|t|default" ]; then
    echo "FAIL: node row after upgrade is '$row', expected 'n1|t|default'" >&2
    exit 1
fi
echo "OK: pre-existing node row survived the upgrade ($row)"

$P -d fresh23 -c "create extension btree_gist" -c "create extension ltree" \
              -c "create extension pgautofailover version '2.3'"

cat > "$D/cat.sql" <<'S'
\pset format unaligned
\pset tuples_only on
select '## functions';
select p.proname||'('||pg_get_function_identity_arguments(p.oid)||') secdef='||p.prosecdef||' cfg='||coalesce(p.proconfig::text,'-')||' acl='||coalesce(p.proacl::text,'-')||' vol='||p.provolatile::text||' ret='||pg_get_function_result(p.oid)||' lang='||l.lanname
  from pg_proc p join pg_language l on l.oid=p.prolang where pronamespace='pgautofailover'::regnamespace order by 1;
select '## columns';
select c.relname||'.'||a.attname||' '||format_type(a.atttypid,a.atttypmod)||' notnull='||a.attnotnull||' def='||coalesce(pg_get_expr(d.adbin,d.adrelid),'-')
  from pg_attribute a join pg_class c on c.oid=a.attrelid and c.relnamespace='pgautofailover'::regnamespace and c.relkind in ('r','v')
  left join pg_attrdef d on d.adrelid=a.attrelid and d.adnum=a.attnum where a.attnum>0 and not a.attisdropped order by 1;
select '## enums';
select t.typname||': '||string_agg(e.enumlabel, ',' order by e.enumsortorder) from pg_enum e join pg_type t on t.oid=e.enumtypid where t.typnamespace='pgautofailover'::regnamespace group by t.typname order by 1;
select '## indexes';
select indexdef from pg_indexes where schemaname='pgautofailover' order by 1;
select '## constraints';
select conrelid::regclass||' '||conname||' '||pg_get_constraintdef(oid) from pg_constraint where connamespace='pgautofailover'::regnamespace order by 1;
select '## relacl';
select relname||' '||coalesce(relacl::text,'-') from pg_class where relnamespace='pgautofailover'::regnamespace and relkind in ('r','v','S') order by 1;
select '## views';
select viewname||' '||regexp_replace(definition,'\s+',' ','g') from pg_views where schemaname='pgautofailover' order by 1;
select '## triggers';
select tgrelid::regclass||' '||pg_get_triggerdef(oid) from pg_trigger where not tgisinternal and tgrelid in (select oid from pg_class where relnamespace='pgautofailover'::regnamespace) order by 1;
select '## rows';
select formationid||' '||kind from pgautofailover.formation order by 1;
select 'policy '||policyname from pgautofailover.basebackup_policy order by 1;
S

for db in up22 fresh23; do
    psql -h "$D" -p $PORT -X -v ON_ERROR_STOP=1 -d $db -f "$D/cat.sql" > "$D/$db.cat"
done

# ALLOWLIST: known, benign differences between an upgraded and a fresh
# install. Each pattern is removed from both dumps before comparing.
#
#  1. node_formationid_nodename_key / node_formationid_nodename_key1: the old
#     upgrade scripts (1.x) re-created the unique constraint, leaving the
#     index and constraint named ..._key1 on upgraded catalogs, ..._key on
#     fresh ones. Same definition, different name; renaming would need a
#     new upgrade step for no functional gain.
#  2. system_identifier_is_null_at_init_only: the CHECK constraint is the
#     same condition but pg_get_constraintdef prints a different (equivalent)
#     text form depending on how it was created.
ALLOW='node_formationid_nodename_key1?|system_identifier_is_null_at_init_only'

grep -Ev "$ALLOW" "$D/up22.cat"   > "$D/up22.filtered"
grep -Ev "$ALLOW" "$D/fresh23.cat" > "$D/fresh23.filtered"

if ! diff -u "$D/up22.filtered" "$D/fresh23.filtered" > "$D/cat.diff"; then
    echo "FAIL: upgraded 2.2->2.3 catalog differs from a fresh 2.3 install:" >&2
    cut -c1-300 "$D/cat.diff" >&2
    exit 1
fi

echo "OK: upgraded and fresh 2.3 catalogs are identical ($(wc -l < "$D/fresh23.cat") lines compared, allowlist applied)"
