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
 * log_openssl_errors -- see openssl_utils.h.
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
 * key_permissions_are_safe -- see openssl_utils.h.
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
