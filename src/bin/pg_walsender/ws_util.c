/*
 * src/bin/pg_walsender/ws_util.c
 *   See ws_util.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "file_utils.h"
#include "log.h"
#include "string_utils.h"
#include "ws_util.h"


bool
ws_read_file_capped(const char *path, size_t maxSize, bool missingOk,
					char **contents, size_t *size, struct stat *stOut)
{
	return ws_read_file_flags(path, O_RDONLY | O_CLOEXEC, maxSize, missingOk,
							  contents, size, stOut);
}


/*
 * ws_open_served_file opens path read-only for FETCH_FILE, refusing to
 * follow a symlink (O_NOFOLLOW) and refusing anything that isn't a regular
 * file once opened (fstat()). Returns -1 (errno set) on any failure.
 */
int
ws_open_served_file(const char *path)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);

	if (fd < 0)
	{
		return -1;
	}

	struct stat st;

	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
	{
		close(fd);
		errno = EINVAL;
		return -1;
	}

	return fd;
}


bool
ws_read_file_flags(const char *path, int openFlags, size_t maxSize,
				   bool missingOk, char **contents, size_t *size,
				   struct stat *stOut)
{
	*contents = NULL;
	*size = 0;

	int fd = open(path, openFlags);

	if (fd < 0)
	{
		if (!(missingOk && errno == ENOENT))
		{
			log_error("Failed to open \"%s\": %m", path);
		}

		return false;
	}

	struct stat st;

	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
	{
		log_error("\"%s\" is not a readable regular file", path);
		close(fd);
		return false;
	}

	if ((uint64_t) st.st_size > maxSize)
	{
		log_error("\"%s\" is too large (%lld bytes, the limit is %zu)", path,
				  (long long) st.st_size, maxSize);
		close(fd);
		return false;
	}

	char *buf = (char *) malloc((size_t) st.st_size + 1);

	if (buf == NULL)
	{
		close(fd);
		return false;
	}

	size_t total = 0;
	size_t capacity = (size_t) st.st_size;

	while (total < capacity)
	{
		ssize_t n = read(fd, buf + total, capacity - total);

		if (n < 0 && errno == EINTR)
		{
			continue;
		}

		if (n <= 0)
		{
			break;
		}

		total += (size_t) n;
	}

	close(fd);

	buf[total] = '\0';

	*contents = buf;
	*size = total;

	if (stOut != NULL)
	{
		*stOut = st;
	}

	return true;
}


/*
 * ws_sanitize_for_log copies in into out (bounded by outSize), replacing any
 * control character (< 0x20 or 0x7f) with '?' -- so a value that came
 * straight from an unauthenticated client (a user name, a route key) can be
 * logged without letting it inject terminal escape sequences or fake log
 * lines. Truncates with a trailing "..." when in doesn't fit.
 */
void
ws_sanitize_for_log(const char *in, char *out, size_t outSize)
{
	size_t o = 0;

	if (outSize == 0)
	{
		return;
	}

	for (const char *p = in; *p != '\0'; p++)
	{
		if (o + 1 >= outSize)
		{
			if (outSize > 4)
			{
				strlcpy(out + outSize - 4, "...", 4);
				return;
			}
			break;
		}

		unsigned char c = (unsigned char) *p;

		out[o++] = (c < 0x20 || c == 0x7f) ? '?' : (char) c;
	}

	out[o] = '\0';
}


/*
 * ws_monotonic_ms returns a CLOCK_MONOTONIC timestamp in milliseconds, for
 * measuring elapsed time (deadlines, backoff) unaffected by wall-clock
 * adjustments.
 */
int64_t
ws_monotonic_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return (int64_t) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}


static int64_t authDeadlineMs = 0;   /* 0: none armed */

void
ws_auth_deadline_set(int seconds)
{
	authDeadlineMs = ws_monotonic_ms() + (int64_t) seconds * 1000;
}


/*
 * ws_auth_deadline_clear disarms the connection's authentication deadline
 * (the countdown started by ws_auth_deadline_set()), called once
 * authentication has succeeded.
 */
void
ws_auth_deadline_clear(void)
{
	authDeadlineMs = 0;
}


/*
 * ws_auth_deadline_remaining_ms returns the milliseconds left until the
 * connection's own authentication deadline (0 if it has already passed), or
 * a generous 1-hour placeholder when no deadline is currently armed (e.g.
 * --insecure mode, or authentication already completed) -- callers use this
 * to cap how long they may block waiting on something (like
 * monitor_hosts.c's request_refresh()) without ever exceeding the deadline.
 */
int
ws_auth_deadline_remaining_ms(void)
{
	if (authDeadlineMs == 0)
	{
		return 3600 * 1000;
	}

	int64_t remaining = authDeadlineMs - ws_monotonic_ms();

	return remaining > 0 ? (int) remaining : 0;
}
