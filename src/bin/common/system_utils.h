/*
 * src/bin/pg_autoctl/system_utils.h
 *   Utility functions for getting CPU and Memory information.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef SYSTEM_UTILS_H
#define SYSTEM_UTILS_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>


/* taken from sysinfo(2) on Linux */
typedef struct SystemInfo
{
	uint64_t totalram;          /* Total usable main memory size */
	unsigned short ncpu;        /* Number of current processes */
} SystemInfo;

bool get_system_info(SystemInfo *sysInfo);
void pretty_print_bytes(char *buffer, size_t size, uint64_t bytes);

/*
 * pretty_print_bytes_scaled is the shared unit-scaling mechanism behind
 * pretty_print_bytes() and wal_segment.c's own wal_segment_size_string() --
 * see pretty_print_bytes_scaled's own comment in system_utils.c.
 */
void pretty_print_bytes_scaled(char *buffer, size_t size, uint64_t bytes,
							   uint64_t threshold, bool withSpace);

int64_t monotonic_ms(void);

/*
 * format_elapsed_time is the shared "-"-sentinel wrapper around
 * IntervalToString() (common/string_utils.c) that both pg_walserver's
 * "ps" and "status" sub-commands use -- see its own comment in
 * system_utils.c.
 */
void format_elapsed_time(time_t startedAt, char *dest, size_t destSize);


#endif /* SYSTEM_UTILS_H */
