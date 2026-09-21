/*
 * src/bin/pg_autoctl/archiver_confirm.h
 *   archive_command confirmation: `pg_autoctl archive command`. It never
 *   moves data, it only asks the monitor whether the archiver's
 *   pg_receivewal already holds the WAL segment.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#ifndef ARCHIVER_CONFIRM_H
#define ARCHIVER_CONFIRM_H

#include <stdbool.h>

/* true for a 24 hexadecimal digits WAL segment file name */
bool archiver_confirm_is_wal_segment(const char *name);

/*
 * Returns the process exit code: 0 when the segment is confirmed (or does not
 * need confirming), 1 otherwise so that Postgres retries.
 */
int archiver_confirm_run(const char *pgdata, const char *walFileName);

#endif /* ARCHIVER_CONFIRM_H */
