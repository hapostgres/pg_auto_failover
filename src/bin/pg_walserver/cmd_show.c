/*
 * src/bin/pg_walserver/cmd_show.c
 *   See cmd_show.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <strings.h>

#include "postgres_fe.h"

#include "cmd_show.h"
#include "framing.h"
#include "wal_dir_scan.h"
#include "wal_segment.h"


/*
 * cmd_show implements the tiny subset of SHOW that real pg_basebackup/
 * pg_receivewal actually query over a replication connection:
 * "wal_segment_size" (the cluster's own configured segment size, in GUC
 * format, e.g. "16MB") and "data_directory_mode" (a fixed "0700"). It also
 * implements "receivewal", this project's own extension with no PostgreSQL
 * equivalent: the connected cluster's own "receivewal" setting, "pull" when
 * WsCluster.receivewalPull is set, "none" otherwise -- how cli_archive.c's
 * "archive-wal" learns, per invocation, straight from the cluster it actually
 * connected to, whether to only ever CHECK_FILE or only ever ARCHIVE_FILE
 * (see that file's own header comment). Any other parameter name is
 * rejected with the same SQLSTATE (42704) real Postgres uses for an unknown
 * GUC.
 */
void
cmd_show(int sock, const WsCluster *cluster, const char *name)
{
	const char *value = NULL;
	char segSizeStr[16];

	if (strcasecmp(name, "wal_segment_size") == 0)
	{
		/* the cluster's own segment size, in the GUC's own format */
		wal_segment_size_string(ws_cluster_wal_segment_size(cluster),
								segSizeStr, sizeof(segSizeStr));
		value = segSizeStr;
	}
	else if (strcasecmp(name, "data_directory_mode") == 0)
	{
		value = "0700";
	}
	else if (strcasecmp(name, "receivewal") == 0)
	{
		value = (cluster != NULL && cluster->receivewalPull) ? "pull" : "none";
	}

	if (value == NULL)
	{
		ws_send_error_response(sock, "42704", "unrecognized configuration parameter");
		return;
	}

	WsColumn columns[] = {
		{ name, WS_TEXTOID, -1 },
	};

	const char *values[] = { value };

	if (!ws_send_row_description(sock, columns, 1) ||
		!ws_send_data_row(sock, values, 1) ||
		!ws_send_command_complete(sock, "SHOW"))
	{
		return;
	}
}
