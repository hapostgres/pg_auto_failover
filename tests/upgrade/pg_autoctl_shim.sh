#!/bin/bash
# Static shim at /usr/local/bin/pg_autoctl — never replaced during the upgrade.
#
# Delegates to /usr/local/bin/pgaf/current/pg_autoctl, which is a symlink:
#   initially → pgaf/2.1   (v2.1 binary)
#   after upgrade → pgaf/2.2  (v2.2 binary)
#
# exec -a sets argv[0] to /usr/local/bin/pg_autoctl so the real binary saves
# that path as pg_autoctl_argv0.  The keeper uses that path for the on-disk
# version check (pg_autoctl_argv0 version --json).  After the symlink flip
# that invocation hits this shim again → delegates to v2.2 → returns "2.2".
#
# PG_AUTOCTL_DEBUG_BIN_PATH is set in ENV (Dockerfile.current) so the v2.1
# supervisor's pg_autoctl_program is also /usr/local/bin/pg_autoctl; child
# processes after the symlink flip therefore pick up v2.2.
#
# Startup translation:
#   pgaftest generates:  pg_autoctl node run /etc/pgaf/node.ini  (v2.2 syntax)
#   v2.1 does not know "node run"; we translate per the [node] kind in the ini.

SHIM=/usr/local/bin/pg_autoctl
CURRENT=/usr/local/bin/pgaf/current/pg_autoctl

ini_get() {
    awk -F'[[:space:]]*=[[:space:]]*' \
        -v sec="$1" -v key="$2" \
        '/^\[/{in_sec=($0 == "["sec"]")} in_sec && $1==key{print $2; exit}' "$3"
}

if [ "$1" = "node" ] && [ "$2" = "run" ] && [ -n "$3" ]; then
    ini="$3"

    kind=$(ini_get     node       kind     "$ini")
    hostname=$(ini_get node       hostname "$ini")
    port=$(ini_get     node       port     "$ini")
    pgdata=$(ini_get   postgresql pgdata   "$ini")
    monitor=$(ini_get  monitor    pguri    "$ini")
    name=$(ini_get     node       name     "$ini")
    formation=$(ini_get formation name     "$ini")
    ssl=$(ini_get      options    ssl      "$ini")
    auth=$(ini_get     options    auth     "$ini")

    [ -z "$pgdata" ] && { echo "pg_autoctl_shim: missing [postgresql] pgdata in $ini" >&2; exit 1; }

    case "$ssl" in
        off|"") ssl_flag="--no-ssl" ;;
        *)      ssl_flag="--ssl-self-signed" ;;
    esac

    set -- \
        ${hostname:+--hostname "$hostname"} \
        ${port:+--pgport "$port"} \
        ${auth:+--auth "$auth"} \
        "$ssl_flag"

    cfg_dir=$(printf '%s' "$pgdata" | sed 's|^/||')
    cfg="/var/lib/postgres/.config/pg_autoctl/${cfg_dir}/pg_autoctl.cfg"

    if [ "$kind" = "monitor" ]; then
        if [ ! -f "$cfg" ]; then
            # v2.1 create monitor supports --run (stays in the foreground).
            exec -a "$SHIM" "$CURRENT" create monitor --pgdata "$pgdata" "$@" --run
        else
            exec -a "$SHIM" "$CURRENT" run --pgdata "$pgdata"
        fi
    else
        set -- "$@" \
            ${monitor:+--monitor "$monitor"} \
            ${name:+--name "$name"} \
            ${formation:+--formation "$formation"}

        if [ ! -f "$cfg" ]; then
            # v2.1 create postgres does NOT have --run; exec the run loop after.
            "$CURRENT" create postgres --pgdata "$pgdata" "$@"
            rc=$?; [ $rc -ne 0 ] && exit $rc
        fi

        exec -a "$SHIM" "$CURRENT" run --pgdata "$pgdata"
    fi
fi

# v2.1 supervisor spawns its children as "do service {node-active,postgres,listener}".
# v2.2 renamed the "do" subcommand tree to "internal".  After the symlink flip
# ($CURRENT resolves to pgaf/2.2) the v2.1 supervisor's child-spawn calls still
# use "do service X" — translate to "internal service X" so v2.2 accepts them.
# Before the flip $CURRENT → pgaf/2.1 and the v2.1 binary already understands "do".
if [ "$1" = "do" ] && [ "$2" = "service" ]; then
    resolved=$(readlink /usr/local/bin/pgaf/current 2>/dev/null)
    if [ "$resolved" = "/usr/local/bin/pgaf/2.2" ]; then
        exec -a "$SHIM" "$CURRENT" "internal" "${@:2}"
    fi
fi

# All other commands (version --json, run --pgdata respawn, …)
exec -a "$SHIM" "$CURRENT" "$@"
