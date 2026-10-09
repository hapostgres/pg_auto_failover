/*
 * src/bin/common/openssl_utils.c
 *   See openssl_utils.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <sys/stat.h>
#include <unistd.h>

#include <openssl/err.h>

#include "postgres_fe.h"

#include "log.h"
#include "openssl_utils.h"


/*
 * log_openssl_errors drains and logs every pending error on OpenSSL's
 * thread-local error queue, each prefixed with what (a short description of
 * the operation that failed).
 */
void
log_openssl_errors(const char *what)
{
	unsigned long code;
	char buf[256];

	while ((code = ERR_get_error()) != 0)
	{
		ERR_error_string_n(code, buf, sizeof(buf));
		log_error("%s: %s", what, buf);
	}
}


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
bool
key_permissions_are_safe(int fd, const char *keyPath)
{
	struct stat st;

	if (fstat(fd, &st) != 0)
	{
		log_error("Failed to stat the TLS key file \"%s\": %m", keyPath);
		return false;
	}

	if (!S_ISREG(st.st_mode))
	{
		log_error("The TLS key file \"%s\" is not a regular file", keyPath);
		return false;
	}

	if (st.st_uid == geteuid())
	{
		if ((st.st_mode & (S_IRWXG | S_IRWXO)) != 0)
		{
			log_error("The TLS key file \"%s\" has group or world access; "
					  "permissions should be u=rw (0600) or less", keyPath);
			return false;
		}
	}
	else if (st.st_uid == 0)
	{
		if ((st.st_mode & (S_IWGRP | S_IXGRP | S_IRWXO)) != 0)
		{
			log_error("The TLS key file \"%s\" has world access or is "
					  "group-writable; permissions should be u=rw,g=r "
					  "(0640) or less", keyPath);
			return false;
		}
	}
	else
	{
		log_error("The TLS key file \"%s\" must be owned by the user "
				  "running pg_walserver or by root", keyPath);
		return false;
	}

	return true;
}
