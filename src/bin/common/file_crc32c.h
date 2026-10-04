/*
 * src/bin/common/file_crc32c.h
 *   See file_crc32c.c.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef FILE_CRC32C_H
#define FILE_CRC32C_H

#include <stdbool.h>
#include <stdint.h>

/*
 * file_crc32c reads the whole regular file at path sequentially, in
 * chunks (never loading the whole file into memory, so this is safe to
 * use on a multi-megabyte WAL segment as readily as on a small config
 * file), and computes its size and CRC32C in one pass, using the same
 * INIT_CRC32C/COMP_CRC32C/FIN_CRC32C facility (port/pg_crc32c.h) real
 * Postgres uses for its own backup manifests. Returns false, with errno
 * left exactly as open()/read() set it (ENOENT for "does not exist" is a
 * case callers commonly branch on), on any failure to open or read the
 * file; sizeOut/crcOut are left untouched in that case.
 */
bool file_crc32c(const char *path, uint64_t *sizeOut, uint32_t *crcOut);

#endif /* FILE_CRC32C_H */
