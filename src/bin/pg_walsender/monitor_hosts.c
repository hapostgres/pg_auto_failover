/*
 * src/bin/pg_walsender/monitor_hosts.c
 *   See monitor_hosts.h. The connection-side half: a read-only reader of
 *   the node list and a client of the refresher's datagram socket. No
 *   libpq here, no file is ever written.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "monitor_hosts.h"

#include "defaults.h"
#include "file_utils.h"
#include "ipaddr.h"
#include "log.h"
#include "string_utils.h"
#include "ws_util.h"


/* the local copy: its raw contents and modification time */
typedef struct LocalHosts
{
	bool exists;
	char *contents;             /* whole file, malloc'ed */
	struct timespec mtime;
} LocalHosts;


/*
 * monitor_hosts_list_path writes the path of the route's own local nodes
 * list file (PG_AUTOCTL_ARCHIVER_NODES_FILE under routePath) into dest --
 * the single path both the refresher (writer) and this file (reader) agree
 * on.
 */
void
monitor_hosts_list_path(const char *routePath, char *dest, size_t destSize)
{
	sformat(dest, destSize, "%s/" PG_AUTOCTL_ARCHIVER_NODES_FILE, routePath);
}


/*
 * local_hosts_read (re)initializes *local from the nodes list file at path:
 * its whole contents and mtime when it exists and is readable, or
 * local->exists = false (contents left NULL) otherwise.
 */
static void
local_hosts_read(const char *path, LocalHosts *local)
{
	memset(local, 0, sizeof(*local)); /* IGNORE-BANNED */

	struct stat st;
	size_t size = 0;

	if (!ws_read_file_capped(path, WS_MAX_CONFIG_FILE_SIZE, true,
							 &(local->contents), &size, &st))
	{
		return;
	}

	local->exists = true;
	local->mtime = st.st_mtim;
}


/* local_hosts_free releases *local's contents and resets it to "not loaded". */
static void
local_hosts_free(LocalHosts *local)
{
	free(local->contents);
	local->contents = NULL;
	local->exists = false;
}


/*
 * local_hosts_age returns how many seconds old *local's copy is (based on
 * its mtime), or 0 when it doesn't exist -- callers only ever compare this
 * against a threshold guarded by local->exists, so 0 is never mistaken for
 * "fresh".
 */
static int
local_hosts_age(const LocalHosts *local)
{
	time_t now = time(NULL);

	return local->exists ? (int) (now - local->mtime.tv_sec) : 0;
}


/*
 * local_hosts_match returns true when peerIP matches one non-comment line of
 * *local's contents (via ipaddrHostMatchesAddress(), so a line may be a
 * hostname or a literal address), or false when *local doesn't exist or
 * nothing matches.
 */
static bool
local_hosts_match(const LocalHosts *local, const char *peerIP)
{
	if (!local->exists)
	{
		return false;
	}

	/* iterate over a copy: strtok_r writes into what it walks */
	char *copy = strdup(local->contents);

	if (copy == NULL)
	{
		return false;
	}

	bool found = false;
	char *save = NULL;

	for (char *line = strtok_r(copy, "\n", &save);
		 line != NULL && !found;
		 line = strtok_r(NULL, "\n", &save))
	{
		if (line[0] == '#')
		{
			continue;
		}

		found = ipaddrHostMatchesAddress(line, peerIP);
	}

	free(copy);

	return found;
}


/* timespec_equal compares two struct timespec values field by field. */
static bool
timespec_equal(const struct timespec *a, const struct timespec *b)
{
	return a->tv_sec == b->tv_sec && a->tv_nsec == b->tv_nsec;
}


/* mtime of a file, or zeros when it does not exist */
static void
file_mtime(const char *path, struct timespec *mtime)
{
	struct stat st;

	if (stat(path, &st) == 0)
	{
		*mtime = st.st_mtim;
	}
	else
	{
		mtime->tv_sec = 0;
		mtime->tv_nsec = 0;
	}
}


/*
 * send_refresh_request sends the route key to the refresher's datagram
 * socket. Fire and forget: the answer is the list file being renewed.
 */
static bool
send_refresh_request(const char *refreshSockPath, const char *routeKey)
{
	struct sockaddr_un addr;

	memset(&addr, 0, sizeof(addr)); /* IGNORE-BANNED */
	addr.sun_family = AF_UNIX;

	if (refreshSockPath[0] == '\0' ||
		strlen(refreshSockPath) >= sizeof(addr.sun_path))
	{
		return false;
	}

	strlcpy(addr.sun_path, refreshSockPath, sizeof(addr.sun_path));

	int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);

	if (fd < 0)
	{
		return false;
	}

	ssize_t n = sendto(fd, routeKey, strlen(routeKey), MSG_DONTWAIT,
					   (struct sockaddr *) &addr, sizeof(addr));

	close(fd);

	return n > 0;
}


/*
 * request_refresh asks the refresher to validate the list against the
 * monitor, then waits (bounded: WS_HOSTS_WAIT_MAX_MS and the connection's
 * authentication deadline) for the answer -- the list file's mtime moving,
 * or the failure marker appearing -- and re-reads the list.
 */
static void
request_refresh(const char *routeKey, const char *listPath,
				const char *refreshSockPath, LocalHosts *local)
{
	char errPath[MAXPGPATH];

	sformat(errPath, sizeof(errPath), "%s" WS_HOSTS_ERR_SUFFIX, listPath);

	struct timespec listBefore, errBefore;

	file_mtime(listPath, &listBefore);
	file_mtime(errPath, &errBefore);

	/*
	 * The refresher recently failed to reach the monitor and negative-caches
	 * that: do not wait for something that will not happen.
	 */
	if (errBefore.tv_sec != 0 &&
		(time(NULL) - errBefore.tv_sec) < WS_HOSTS_NEGATIVE_SECONDS)
	{
		return;
	}

	if (!send_refresh_request(refreshSockPath, routeKey))
	{
		return;
	}

	int64_t start = ws_monotonic_ms();
	int64_t limit = Min(WS_HOSTS_WAIT_MAX_MS, ws_auth_deadline_remaining_ms());

	for (;;)
	{
		struct timespec listNow, errNow;

		file_mtime(listPath, &listNow);
		file_mtime(errPath, &errNow);

		if (!timespec_equal(&listNow, &listBefore) ||
			!timespec_equal(&errNow, &errBefore))
		{
			break;
		}

		if (ws_monotonic_ms() - start >= limit)
		{
			log_debug("Timed out waiting for the nodes list of \"%s\" to be "
					  "revalidated", listPath);
			break;
		}

		(void) poll(NULL, 0, 20);
	}

	local_hosts_free(local);
	local_hosts_read(listPath, local);
}


bool
monitor_hosts_contain(const char *routeKey, const char *routePath,
					  const char *monitorUriPath, const char *refreshSockPath,
					  const char *peerIP)
{
	static bool warnedTooOld = false;

	char listPath[MAXPGPATH];

	monitor_hosts_list_path(routePath, listPath, sizeof(listPath));

	LocalHosts local;

	local_hosts_read(listPath, &local);

	bool monitored = monitorUriPath != NULL && monitorUriPath[0] != '\0' &&
					 file_exists(monitorUriPath);

	/* validate an old (or missing) copy first, see monitor_hosts.h */
	if (monitored &&
		(!local.exists || local_hosts_age(&local) >= WS_HOSTS_MAX_AGE_SECONDS))
	{
		request_refresh(routeKey, listPath, refreshSockPath, &local);
	}

	/* fail closed: a list the monitor has not confirmed for too long */
	if (monitored && local.exists &&
		local_hosts_age(&local) >= WS_HOSTS_HARD_MAX_AGE_SECONDS)
	{
		if (!warnedTooOld)
		{
			warnedTooOld = true;
			log_error("The nodes list \"%s\" is more than %d seconds old and "
					  "the monitor is unreachable: the \"monitor\" HBA "
					  "address matches nothing", listPath,
					  WS_HOSTS_HARD_MAX_AGE_SECONDS);
		}

		local_hosts_free(&local);
		return false;
	}

	bool found = local_hosts_match(&local, peerIP);

	if (!found && monitored && local.exists &&
		local_hosts_age(&local) >= WS_HOSTS_MISS_MIN_AGE_SECONDS)
	{
		request_refresh(routeKey, listPath, refreshSockPath, &local);

		found = local_hosts_age(&local) < WS_HOSTS_HARD_MAX_AGE_SECONDS &&
				local_hosts_match(&local, peerIP);
	}

	local_hosts_free(&local);

	return found;
}
