/*
 * src/bin/pg_walserver/cli_create_cert.h
 *   `pg_walserver create-cert`: a thin CLI wrapper around
 *   pg_create_self_signed_cert() (src/bin/common/pgctl.c) that writes
 *   <pgdata>/server.crt and <pgdata>/server.key by hand, on demand -- the
 *   same self-signed certificate `pg_walserver setup` already creates
 *   automatically the moment a second named route needs one (see
 *   cli_setup.c's own ensure_tls_for_multiple_routes(), which now calls the
 *   same ws_create_cert_run() this sub-command calls directly). Useful when
 *   an operator wants TLS from the very first route (SNI routing isn't the
 *   only reason to want it -- so does a single route served over a
 *   reachable network at all), or wants to replace an existing self-signed
 *   certificate with a freshly generated one (--force) without touching any
 *   route.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_CREATE_CERT_H
#define WS_CLI_CREATE_CERT_H

#include <stdbool.h>

#include "postgres_fe.h"

/*
 * ws_create_cert_run creates a self-signed certificate for pgdata
 * (<pgdata>/server.crt, <pgdata>/server.key), CN=hostname, via
 * pg_create_self_signed_cert(). Refuses -- logging an error, not
 * overwriting anything -- when either file already exists unless force is
 * true, matching this project's own "never silently replace what's already
 * there" principle (cli_fetch_systemid.c's own systemid overwrite check is
 * the same idea applied to a different file). Returns true on success,
 * false with an error already logged otherwise.
 */
bool ws_create_cert_run(const char *pgdata, const char *hostname, bool force);

#endif /* WS_CLI_CREATE_CERT_H */
