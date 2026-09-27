/*
 * src/bin/pg_walserver/cli_basebackup.c
 *   See cli_basebackup.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <string.h>
#include <time.h>

#include "postgres_fe.h"

#include "cli_basebackup.h"
#include "env_utils.h"
#include "file_utils.h"
#include "log.h"
#include "pgctl.h"
#include "string_utils.h"

#define WS_BASEBACKUP_LATEST_FILENAME "basebackups/.latest"


bool
cli_basebackup_run(const WsUpstreamTarget *target,
				   char *labelOut, size_t labelOutSize)
{
	char pgBasebackupPath[MAXPGPATH] = { 0 };

	if (!search_path_first("pg_basebackup", pgBasebackupPath, LOG_ERROR))
	{
		/* errors have already been logged */
		return false;
	}

	/* "basebackup-<UTC timestamp>", the same scheme
	 * service_archiver_basebackup.c uses, so both sit in the same
	 * basebackups/ directory without colliding */
	time_t now = time(NULL);
	struct tm nowUTC = { 0 };

	gmtime_r(&now, &nowUTC);

	char label[NAMEDATALEN] = { 0 };

	strftime(label, sizeof(label), "basebackup-%Y%m%dT%H%M%SZ", &nowUTC);

	char backupDir[MAXPGPATH] = { 0 };

	sformat(backupDir, sizeof(backupDir), "%s/basebackups/%s",
			target->path, label);

	log_info("Taking a base backup of %s:%d into \"%s\"",
			 target->node.host, target->node.port, backupDir);

	ReplicationSource replicationSource = { 0 };

	replicationSource.primaryNode = target->node;
	strlcpy(replicationSource.userName, target->userName,
			sizeof(replicationSource.userName));
	strlcpy(replicationSource.applicationName, "pg_walserver-basebackup",
			sizeof(replicationSource.applicationName));
	strlcpy(replicationSource.backupDir, backupDir,
			sizeof(replicationSource.backupDir));
	strlcpy(replicationSource.walMethod, "stream",
			sizeof(replicationSource.walMethod));
	strlcpy(replicationSource.label, label, sizeof(replicationSource.label));
	replicationSource.sslOptions = target->sslOptions;

	if (env_exists("PGPASSWORD"))
	{
		(void) get_env_copy("PGPASSWORD", replicationSource.password,
							sizeof(replicationSource.password));
	}

	/* pg_basebackup_fetch() only needs pg_ctl's own path to find
	 * pg_basebackup next to it (path_in_same_directory()); pg_walserver has
	 * no pg_ctl of its own to point at, so the resolved pg_basebackup path
	 * itself works exactly as well: same directory, same result. */
	if (!pg_basebackup_fetch(pgBasebackupPath, &replicationSource))
	{
		/* errors have already been logged */
		return false;
	}

	/* pg_basebackup itself already guarantees a well-formed backup_label
	 * on a zero exit; this is a defense against a partial/corrupt result
	 * making it this far, not a re-parse of what pg_basebackup wrote */
	char backupLabelPath[MAXPGPATH] = { 0 };
	char pgVersionPath[MAXPGPATH] = { 0 };

	sformat(backupLabelPath, sizeof(backupLabelPath), "%s/backup_label", backupDir);
	sformat(pgVersionPath, sizeof(pgVersionPath), "%s/PG_VERSION", backupDir);

	if (!file_exists(backupLabelPath) || !file_exists(pgVersionPath))
	{
		log_error("pg_basebackup reported success but \"%s\" is missing "
				  "\"backup_label\" or \"PG_VERSION\" -- not marking it "
				  "as the latest backup", backupDir);
		return false;
	}

	char latestPath[MAXPGPATH] = { 0 };

	sformat(latestPath, sizeof(latestPath), "%s/" WS_BASEBACKUP_LATEST_FILENAME,
			target->path);

	if (!write_file_atomic(label, strlen(label), latestPath))
	{
		log_error("Base backup \"%s\" completed but failed to update \"%s\"",
				  label, latestPath);
		return false;
	}

	log_info("Base backup \"%s\" is now the latest for \"%s\"",
			 label, target->path);

	if (labelOut != NULL)
	{
		strlcpy(labelOut, label, labelOutSize);
	}

	return true;
}
