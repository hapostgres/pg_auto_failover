/*
 * src/bin/pg_walserver/ws_util.c
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

#include "port/pg_crc32c.h"

#include "file_utils.h"
#include "log.h"
#include "string_utils.h"
#include "ws_util.h"

/* a plain sequential read chunk size, matching cmd_fetch_file.c's own */
#define WS_CRC32C_CHUNK_SIZE (128 * 1024)


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

/*
 * ws_auth_deadline_set arms the current connection's absolute
 * authentication deadline, seconds from now (CLOCK_MONOTONIC-based, see
 * ws_monotonic_ms(), unaffected by wall-clock adjustments), read back by
 * accept_loop.c's own SIGALRM arming -- see README.md's "Process model"
 * section for the full startup/TLS handshake/HBA/SCRAM window it covers.
 */
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
 * ws_file_crc32c reads path sequentially, in chunks, and computes its size
 * and CRC32C in one pass -- see ws_util.h for the full contract (why
 * CRC32C specifically, and how cmd_check_file.c/cmd_archive_file.c/
 * cli_archive.c each build on it).
 */
bool
ws_file_crc32c(const char *path, uint64_t *sizeOut, uint32_t *crcOut)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);

	if (fd < 0)
	{
		/* errno left exactly as open() set it (ENOENT for a missing file) */
		return false;
	}

	struct stat st;

	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
	{
		close(fd);
		errno = EINVAL;
		return false;
	}

	pg_crc32c crc;

	INIT_CRC32C(crc);

	char buffer[WS_CRC32C_CHUNK_SIZE];
	uint64_t total = 0;
	bool ok = true;

	for (;;)
	{
		ssize_t got = read(fd, buffer, sizeof(buffer));

		if (got < 0 && errno == EINTR)
		{
			continue;
		}

		if (got < 0)
		{
			ok = false;
			break;
		}

		if (got == 0)
		{
			break;
		}

		COMP_CRC32C(crc, buffer, (size_t) got);
		total += (uint64_t) got;
	}

	int savedErrno = errno;

	close(fd);

	if (!ok)
	{
		errno = savedErrno;
		return false;
	}

	FIN_CRC32C(crc);

	*sizeOut = total;
	*crcOut = crc;

	return true;
}
