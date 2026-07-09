/*
 * src/bin/pg_autoctl/pgtuning.c
 *     Adjust some very basic Postgres tuning to the system properties.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include "postgres_fe.h"
#include "pqexpbuffer.h"

#include "config.h"
#include "env_utils.h"
#include "file_utils.h"
#include "log.h"
#include "pgctl.h"
#include "pgtuning.h"
#include "system_utils.h"

/*
 * In most cases we are going to initdb a Postgres instance for our users, we
 * might as well introduce some naive Postgres tuning. In the static array are
 * selected Postgres default values and static values we always set.
 *
 * Dynamic code is then used on the target systems to compute better values
 * dynamically for some parameters: work_mem, maintenance_work_mem,
 * effective_cache_size, autovacuum_max_workers.
 */
GUC postgres_tuning[] = {
	{ "track_functions", "pl" },
	{ "shared_buffers", "'128 MB'" },
	{ "work_mem", "'4 MB'" },
	{ "maintenance_work_mem", "'64MB'" },
	{ "effective_cache_size", "'4 GB'" },
	{ "autovacuum_max_workers", "3" },
	{ "autovacuum_vacuum_scale_factor", "0.08" },
	{ "autovacuum_analyze_scale_factor", "0.02" },
	{ NULL, NULL }
};


typedef struct DynamicTuning
{
	int autovacuum_max_workers;
	uint64_t shared_buffers;
	uint64_t work_mem;
	uint64_t maintenance_work_mem;
	uint64_t effective_cache_size;
} DynamicTuning;


static bool pgtuning_compute_mem_settings(SystemInfo *sysInfo,
										  DynamicTuning *tuning);

void pgtuning_log_settings(DynamicTuning *tuning, int logLevel);

static int pgtuning_compute_max_workers(SystemInfo *sysInfo);

static bool pgtuning_edit_guc_settings(GUC *settings, DynamicTuning *tuning,
									   char *config, size_t size);


/*
 * pgtuning_prepare_guc_settings probes the system information (nCPU and total
 * RAM) and computes some better defaults for Postgres.
 */
bool
pgtuning_prepare_guc_settings(GUC *settings, char *config, size_t size)
{
	SystemInfo sysInfo = { 0 };
	DynamicTuning tuning = { 0 };
	char totalram[BUFSIZE] = { 0 };

	if (!get_system_info(&sysInfo))
	{
		/* errors have already been logged */
		return false;
	}

	(void) pretty_print_bytes(totalram, sizeof(totalram), sysInfo.totalram);

	log_debug("Detected %d CPUs and %s total RAM on this server",
			  sysInfo.ncpu,
			  totalram);

	/*
	 * Disable Postgres tuning when running the unit test suite: we install our
	 * default set of values rather than computing better values for the
	 * current environment.
	 */
	if (!(env_exists(PG_AUTOCTL_DEBUG) && env_exists("PG_REGRESS_SOCK_DIR")))
	{
		tuning.autovacuum_max_workers = pgtuning_compute_max_workers(&sysInfo);

		if (!pgtuning_compute_mem_settings(&sysInfo, &tuning))
		{
			log_error("Failed to compute memory settings, using defaults");
			return false;
		}

		(void) pgtuning_log_settings(&tuning, LOG_DEBUG);
	}

	return pgtuning_edit_guc_settings(settings, &tuning, config, size);
}


/*
 * pgtuning_compute_max_workers returns how many autovacuum max workers we can
 * setup on the local system, depending on its number of CPUs.
 *
 * We could certainly cook a simple enough maths expression to compute the
 * numbers assigned in this range based "grid" here, but that would be much
 * harder to maintain and change our mind about, and not as easy to grasp on a
 * quick reading.
 */
static int
pgtuning_compute_max_workers(SystemInfo *sysInfo)
{
	/* use the default up to 16 cores (HT included) */
	if (sysInfo->ncpu < 16)
	{
		return 3;
	}
	else if (sysInfo->ncpu < 24)
	{
		return 4;
	}
	else if (sysInfo->ncpu < 32)
	{
		return 6;
	}
	else if (sysInfo->ncpu < 48)
	{
		return 8;
	}
	else if (sysInfo->ncpu < 64)
	{
		return 12;
	}
	else
	{
		return 16;
	}
}


/*
 * pgtuning_compute_work_mem computes how much work mem to use on this system.
 *
 * Inspiration has been taken from http://pgconfigurator.cybertec.at
 *
 * Rather than trying to devise a good maths expression to compute values, we
 * implement our decision making with a range based approach. Some values are
 * still computed with an expression (shared_buffers is set to 25% of the total
 * RAM up to 256 GB of RAM, for instance).
 */
static bool
pgtuning_compute_mem_settings(SystemInfo *sysInfo, DynamicTuning *tuning)
{
	uint64_t oneGB = ((uint64_t) 1) << 30;

	/*
	 * <= 8 GB of RAM
	 */
	if (sysInfo->totalram <= (8 * oneGB))
	{
		tuning->shared_buffers = sysInfo->totalram / 4;
		tuning->work_mem = 16 * 1 << 20;              /*  16 MB */
		tuning->maintenance_work_mem = 256 * 1 << 20; /* 256 MB */
	}

	/*
	 * > 8 GB up to 64 GB of RAM
	 */
	else if (sysInfo->totalram <= (64 * oneGB))
	{
		tuning->shared_buffers = sysInfo->totalram / 4;
		tuning->work_mem = 24 * 1 << 20;              /*  24 MB */
		tuning->maintenance_work_mem = 512 * 1 << 20; /* 512 MB */
	}

	/*
	 * > 64 GB up to 256 GB of RAM
	 */
	else if (sysInfo->totalram <= (256 * oneGB))
	{
		tuning->shared_buffers = 16 * oneGB;        /* 16 GB */
		tuning->work_mem = 32 * 1 << 20;              /* 32 MB */
		tuning->maintenance_work_mem = oneGB;       /*  1 GB */
	}

	/*
	 * > 256 GB of RAM
	 */
	else
	{
		tuning->shared_buffers = 32 * oneGB;        /* 32 GB */
		tuning->work_mem = 64 * 1 << 20;              /* 64 MB */
		tuning->maintenance_work_mem = 2 * oneGB;   /*  2 GB */
	}

	/*
	 * What's not in shared buffers is expected to be mostly file system cache,
	 * and then again effective_cache_size is a hint and does not need to be
	 * the exact value as shown by the free(1) command.
	 */
	tuning->effective_cache_size = sysInfo->totalram - tuning->shared_buffers;

	return true;
}


/*
 * pgtuning_log_mem_settings logs the memory settings we computed.
 */
void
pgtuning_log_settings(DynamicTuning *tuning, int logLevel)
{
	char buf[BUFSIZE] = { 0 };

	log_level(logLevel,
			  "Setting autovacuum_max_workers to %d",
			  tuning->autovacuum_max_workers);

	(void) pretty_print_bytes(buf, sizeof(buf), tuning->shared_buffers);
	log_level(logLevel, "Setting shared_buffers to %s", buf);

	(void) pretty_print_bytes(buf, sizeof(buf), tuning->work_mem);
	log_level(logLevel, "Setting work_mem to %s", buf);

	(void) pretty_print_bytes(buf, sizeof(buf),
							  tuning->maintenance_work_mem);
	log_level(logLevel, "Setting maintenance_work_mem to %s", buf);

	(void) pretty_print_bytes(buf, sizeof(buf),
							  tuning->effective_cache_size);
	log_level(logLevel, "Setting effective_cache_size to %s", buf);
}


/*
 * pgtuning_edit_guc_settings prepares a Postgres configuration file snippet
 * from the given GUC settings and the dynamic tuning adjusted to the system
 * and place the resulting snippet in the pre-allocated string buffer config of
 * given size.
 */
#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))

static bool
pgtuning_edit_guc_settings(GUC *settings, DynamicTuning *tuning,
						   char *config, size_t size)
{
	PQExpBuffer contents = createPQExpBuffer();
	int settingIndex = 0;

	if (contents == NULL)
	{
		log_error("Failed to allocate memory");
		return false;
	}

	appendPQExpBuffer(contents,
					  "# basic tuning computed by pg_auto_failover\n");

	/* replace placeholder values with dynamic tuned values */
	for (settingIndex = 0; settings[settingIndex].name != NULL; settingIndex++)
	{
		GUC *setting = &settings[settingIndex];

		if (streq(setting->name, "autovacuum_max_workers"))
		{
			if (tuning->autovacuum_max_workers > 0)
			{
				appendPQExpBuffer(contents, "%s = %d\n",
								  setting->name,
								  tuning->autovacuum_max_workers);
			}
			else
			{
				appendPQExpBuffer(contents, "%s = %s\n",
								  setting->name,
								  setting->value);
			}
		}
		else if (streq(setting->name, "shared_buffers"))
		{
			if (tuning->shared_buffers > 0)
			{
				char pretty[BUFSIZE] = { 0 };

				(void) pretty_print_bytes(pretty, sizeof(pretty),
										  tuning->shared_buffers);

				appendPQExpBuffer(contents, "%s = '%s'\n",
								  setting->name, pretty);
			}
			else
			{
				appendPQExpBuffer(contents, "%s = %s\n",
								  setting->name, setting->value);
			}
		}
		else if (streq(setting->name, "work_mem"))
		{
			if (tuning->work_mem > 0)
			{
				char pretty[BUFSIZE] = { 0 };

				(void) pretty_print_bytes(pretty, sizeof(pretty),
										  tuning->work_mem);

				appendPQExpBuffer(contents, "%s = '%s'\n",
								  setting->name, pretty);
			}
			else
			{
				appendPQExpBuffer(contents, "%s = %s\n",
								  setting->name, setting->value);
			}
		}
		else if (streq(setting->name, "maintenance_work_mem"))
		{
			if (tuning->maintenance_work_mem > 0)
			{
				char pretty[BUFSIZE] = { 0 };

				(void) pretty_print_bytes(pretty, sizeof(pretty),
										  tuning->maintenance_work_mem);

				appendPQExpBuffer(contents, "%s = '%s'\n",
								  setting->name, pretty);
			}
			else
			{
				appendPQExpBuffer(contents, "%s = %s\n",
								  setting->name, setting->value);
			}
		}
		else if (streq(setting->name, "effective_cache_size"))
		{
			if (tuning->effective_cache_size > 0)
			{
				char pretty[BUFSIZE] = { 0 };

				(void) pretty_print_bytes(pretty, sizeof(pretty),
										  tuning->effective_cache_size);

				appendPQExpBuffer(contents, "%s = '%s'\n",
								  setting->name, pretty);
			}
			else
			{
				appendPQExpBuffer(contents, "%s = %s\n",
								  setting->name, setting->value);
			}
		}
		else
		{
			appendPQExpBuffer(contents, "%s = %s\n",
							  setting->name, setting->value);
		}
	}

	/* memory allocation could have failed while building string */
	if (PQExpBufferBroken(contents))
	{
		log_error("Failed to allocate memory");
		destroyPQExpBuffer(contents);
		return false;
	}

	if (size < contents->len)
	{
		log_error("Failed to prepare Postgres tuning for the local system, "
				  "the setup needs %lu bytes and pg_autoctl only support "
				  "up to %lu bytes",
				  (unsigned long) contents->len,
				  (unsigned long) size);
		destroyPQExpBuffer(contents);
		return false;
	}

	strlcpy(config, contents->data, size);

	destroyPQExpBuffer(contents);

	return true;
}
