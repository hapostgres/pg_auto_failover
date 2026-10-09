/*
 * src/bin/pg_walserver/cli_root.h
 *   Exports ws_root (cli_root.c), the top-level CommandLine every other
 *   pg_walserver sub-command's own getopt callback needs to print
 *   whole-program usage against on an unrecognized flag
 *   (commandline_print_usage(&ws_root, stderr)) -- the same shape as
 *   pg_autoctl's own cli_root.h exporting "root", adapted to this much
 *   smaller binary (pg_walserver has no analogous cli_common.c usage-on-
 *   error helper of its own to prefer instead).
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_ROOT_H
#define WS_CLI_ROOT_H

#include "commandline.h"

extern CommandLine ws_root;

#endif /* WS_CLI_ROOT_H */
