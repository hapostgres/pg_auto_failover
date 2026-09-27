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


void
ws_auth_deadline_clear(void)
{
	authDeadlineMs = 0;
}


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
