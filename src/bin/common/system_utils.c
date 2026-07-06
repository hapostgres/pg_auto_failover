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
 * pretty_print_bytes pretty prints bytes in a human readable form. Given
 * 17179869184 it places the string "16 GB" in the given buffer.
 */
void
pretty_print_bytes(char *buffer, size_t size, uint64_t bytes)
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

	while (count >= 10240 && sIndex < 7)
	{
		sIndex++;
		count /= 1024;
	}

	/* forget about having more precision, Postgres wants integers here */
	sformat(buffer, size, "%d %s", (int) count, suffixes[sIndex]);
}
