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


/* taken from sysinfo(2) on Linux */
typedef struct SystemInfo
{
	uint64_t totalram;          /* Total usable main memory size */
	unsigned short ncpu;        /* Number of current processes */
} SystemInfo;

bool get_system_info(SystemInfo *sysInfo);
void pretty_print_bytes(char *buffer, size_t size, uint64_t bytes);


#endif /* SYSTEM_UTILS_H */
