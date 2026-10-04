/*
 * src/bin/pg_autoctl/hardware_utils.c
 *   Utility functions for getting CPU and Memory information.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#if defined(__linux__)
#include <sys/sysinfo.h>
#else
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/param.h>
#endif

#include <math.h>
#include <time.h>

#include "log.h"
#include "file_utils.h"
#include "system_utils.h"

#if defined(__linux__)
static bool get_system_info_linux(SystemInfo *sysInfo);
#endif

#if defined(__APPLE__) || defined(BSD)
static bool get_system_info_bsd(SystemInfo *sysInfo);
#endif

/*
 * get_system_info probes for system information and fills the given SystemInfo
 * structure with what we found: number of CPUs and total amount of memory.
 */
bool
get_system_info(SystemInfo *sysInfo)
{
#if defined(__APPLE__) || defined(BSD)
	return get_system_info_bsd(sysInfo);
#elif defined(__linux__)
	return get_system_info_linux(sysInfo);
#else
	log_error("Failed to get system information: "
			  "Operating System not supported");
	return false;
#endif
}


/*
 * On Linux, use sysinfo(2) and getnprocs(3)
 */
#if defined(__linux__)
static bool
get_system_info_linux(SystemInfo *sysInfo)
{
	struct sysinfo linuxSysInfo = { 0 };

	if (sysinfo(&linuxSysInfo) != 0)
	{
		log_error("Failed to call sysinfo(): %m");
		return false;
	}

	sysInfo->ncpu = get_nprocs();
	sysInfo->totalram = linuxSysInfo.totalram;

	return true;
}


#endif


/*
 * FreeBSD, OpenBSD, and darwin use the sysctl(3) API.
 */
#if defined(__APPLE__) || defined(BSD)
static bool
get_system_info_bsd(SystemInfo *sysInfo)
{
	unsigned int ncpu = 0;      /* the API requires an integer here */
	int ncpuMIB[2] = { CTL_HW, HW_NCPU };
	#if defined(HW_MEMSIZE)
	int ramMIB[2] = { CTL_HW, HW_MEMSIZE };   /* MacOS   */
	#elif defined(HW_PHYSMEM64)
	int ramMIB[2] = { CTL_HW, HW_PHYSMEM64 }; /* OpenBSD */
	#else
	int ramMIB[2] = { CTL_HW, HW_PHYSMEM };   /* FreeBSD */
	#endif

	size_t cpuSize = sizeof(ncpu);
	size_t memSize = sizeof(sysInfo->totalram);

	if (sysctl(ncpuMIB, 2, &ncpu, &cpuSize, NULL, 0) == -1)
	{
		log_error("Failed to probe number of CPUs: %m");
		return false;
	}

	sysInfo->ncpu = (unsigned short) ncpu;

	if (sysctl(ramMIB, 2, &(sysInfo->totalram), &memSize, NULL, 0) == -1)
	{
		log_error("Failed to probe Physical Memory: %m");
		return false;
	}

	return true;
}


#endif


/*
 * pretty_print_bytes_scaled is the shared unit-scaling mechanism behind
 * both pretty_print_bytes() just below (human-readable, space-separated,
 * switches to the next unit only once a value reaches 10x it, e.g.
 * "16 GB") and common/wal_segment.c's own wal_segment_size_string()
 * (the wal_segment_size GUC's own SHOW-reply wire format, e.g. "16MB"/
 * "1GB", no space, switching to the next unit at exactly 1024 -- the
 * right rule for a value that is always an exact power of two, unlike an
 * arbitrary byte count). threshold is the cutoff each caller wants
 * ("count >= threshold" advances to the next unit and divides by 1024);
 * withSpace picks "%d %s" vs "%d%s".
 */
void
pretty_print_bytes_scaled(char *buffer, size_t size, uint64_t bytes,
						  uint64_t threshold, bool withSpace)
{
	const char *suffixes[7] = {
		"B",                    /* Bytes */
		"kB",                   /* Kilo */
		"MB",                   /* Mega */
		"GB",                   /* Giga */
		"TB",                   /* Tera */
		"PB",                   /* Peta */
		"EB"                    /* Exa */
	};

	uint sIndex = 0;
	long double count = bytes;

	while (count >= threshold && sIndex < 7)
	{
		sIndex++;
		count /= 1024;
	}

	/* forget about having more precision, Postgres wants integers here */
	sformat(buffer, size, withSpace ? "%d %s" : "%d%s",
			(int) count, suffixes[sIndex]);
}


/*
 * pretty_print_bytes pretty prints bytes in a human readable form. Given
 * 17179869184 it places the string "16 GB" in the given buffer.
 */
void
pretty_print_bytes(char *buffer, size_t size, uint64_t bytes)
{
	pretty_print_bytes_scaled(buffer, size, bytes, 10240, true);
}


/* CLOCK_MONOTONIC in milliseconds */
int64_t
monotonic_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return (int64_t) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
