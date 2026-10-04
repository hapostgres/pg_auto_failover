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

bool file_crc32c(const char *path, uint64_t *sizeOut, uint32_t *crcOut);

#endif /* FILE_CRC32C_H */
