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

void log_openssl_errors(const char *what);

bool key_permissions_are_safe(int fd, const char *keyPath);

#endif /* OPENSSL_UTILS_H */
