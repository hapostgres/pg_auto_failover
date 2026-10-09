/*
 * src/bin/pg_walserver/cli_wal_target.c
 *   See cli_wal_target.h's own header comment.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <getopt.h>
#include <string.h>

#include "postgres_fe.h"

#include "commandline.h"
#include "log.h"
#include "string_utils.h"

#include "cli_root.h"
#include "cli_wal_target.h"
#include "defaults.h"

static struct option walTargetLongOptions[] = {
	{ "cluster", required_argument, NULL, 'c' },
	{ "host", required_argument, NULL, 'h' },
	{ "port", required_argument, NULL, 'p' },
	{ "user", required_argument, NULL, 'U' },
	{ "sslmode", required_argument, NULL, 's' },
	{ NULL, 0, NULL, 0 }
};

int
cli_wal_target_getopt(int argc, char **argv, WsWalServerTarget *target)
{
	optind = 0;
	*target = (WsWalServerTarget) {
		0
	};
	target->port = WS_DEFAULT_PORT;
	strlcpy(target->user, PG_AUTOCTL_REPLICA_USERNAME, sizeof(target->user));

	int c;

	while ((c = getopt_long(argc, argv, "c:h:p:U:s:",
							walTargetLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'c':
			{
				strlcpy(target->cluster, optarg, sizeof(target->cluster));
				break;
			}

			case 'h':
			{
				strlcpy(target->host, optarg, sizeof(target->host));
				break;
			}

			case 'p':
			{
				if (!stringToInt(optarg, &(target->port)))
				{
					log_fatal("Invalid --port value \"%s\"", optarg);
					exit(1);
				}
				break;
			}

			case 'U':
			{
				strlcpy(target->user, optarg, sizeof(target->user));
				break;
			}

			case 's':
			{
				strlcpy(target->sslmode, optarg, sizeof(target->sslmode));
				break;
			}

			default:
			{
				commandline_print_usage(&ws_root, stderr);
				exit(1);
			}
		}
	}

	return optind;
}
