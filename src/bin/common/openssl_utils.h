/*
 * src/bin/common/openssl_utils.h
 *   Small generic OpenSSL helpers (error-queue logging, key file
 *   permission checks) with no project-specific type dependency.
 *
 * Licensed under the PostgreSQL License.
 *
 */
#ifndef OPENSSL_UTILS_H
#define OPENSSL_UTILS_H

#include <stdbool.h>

/*
 * log_openssl_errors drains and logs every pending error on OpenSSL's
 * thread-local error queue, each prefixed with what (a short description of
 * the operation that failed).
 */
void log_openssl_errors(const char *what);

/*
 * key_permissions_are_safe checks that the already-opened file descriptor
 * fd (keyPath is used only for log messages) has safe permissions for a
 * TLS private key: a key owned by us must be 0600 (no group/other access
 * at all); one owned by root may also be group readable (0640), for a
 * certificate shared through a group; any other owner is refused.
 *
 * Adapted from PostgreSQL's check_ssl_key_file_permissions()
 * (src/backend/libpq/be-secure-common.c), with ereport() turned into
 * log_error(). The checks run on the already opened descriptor (fstat),
 * which is then the very file the key is read from: no stat()/open() race
 * in between.
 */
bool key_permissions_are_safe(int fd, const char *keyPath);

#endif /* OPENSSL_UTILS_H */
