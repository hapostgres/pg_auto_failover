/*
 * src/bin/common/file_crc32c.c
 *   See file_crc32c.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "port/pg_crc32c.h"

#include "file_crc32c.h"

#define FILE_CRC32C_CHUNK_SIZE (128 * 1024)

bool
file_crc32c(const char *path, uint64_t *sizeOut, uint32_t *crcOut)
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

	char buffer[FILE_CRC32C_CHUNK_SIZE];
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
