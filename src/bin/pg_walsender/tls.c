/*
 * src/bin/pg_walsender/tls.c
 *   See tls.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include "postgres_fe.h"

#include "tls.h"

#include "file_utils.h"
#include "log.h"

static SSL_CTX *serverContext = NULL;
static SSL *activeSsl = NULL;


static void
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
 * The same rule PostgreSQL applies to ssl_key_file: a key owned by us must
 * be 0600 (no group/other access at all); one owned by root may also be
 * group readable (0640), for a certificate shared through a group.
 */
static bool
key_permissions_are_safe(const char *keyPath)
{
	struct stat st;

	if (stat(keyPath, &st) != 0)
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
				  "running pg_walsender or by root", keyPath);
		return false;
	}

	return true;
}


bool
ws_tls_server_init(const char *certPath, const char *keyPath)
{
	if (!file_exists(certPath) || !file_exists(keyPath))
	{
		return false;
	}

	if (!key_permissions_are_safe(keyPath))
	{
		return false;
	}

	SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());

	if (ctx == NULL)
	{
		log_openssl_errors("SSL_CTX_new");
		return false;
	}

	SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
	SSL_CTX_set_options(ctx, SSL_OP_NO_COMPRESSION | SSL_OP_NO_RENEGOTIATION);

	if (SSL_CTX_use_certificate_chain_file(ctx, certPath) != 1 ||
		SSL_CTX_use_PrivateKey_file(ctx, keyPath, SSL_FILETYPE_PEM) != 1 ||
		SSL_CTX_check_private_key(ctx) != 1)
	{
		log_openssl_errors("Loading the TLS certificate and key");
		SSL_CTX_free(ctx);
		return false;
	}

	serverContext = ctx;

	return true;
}


bool
ws_tls_server_enabled(void)
{
	return serverContext != NULL;
}


bool
ws_tls_server_accept(int sock)
{
	SSL *ssl = SSL_new(serverContext);

	if (ssl == NULL || SSL_set_fd(ssl, sock) != 1)
	{
		log_openssl_errors("SSL_new");
		return false;
	}

	if (SSL_accept(ssl) != 1)
	{
		log_openssl_errors("TLS handshake with the client");
		SSL_free(ssl);
		return false;
	}

	activeSsl = ssl;

	return true;
}


bool
ws_tls_client_connect(int sock)
{
	SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());

	if (ctx == NULL)
	{
		log_openssl_errors("SSL_CTX_new");
		return false;
	}

	SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

	/*
	 * Like libpq's sslmode=require: encrypted, but the server certificate
	 * is not verified (a pg_autoctl archiver typically uses a self-signed
	 * one).
	 */
	SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

	SSL *ssl = SSL_new(ctx);

	if (ssl == NULL || SSL_set_fd(ssl, sock) != 1 || SSL_connect(ssl) != 1)
	{
		log_openssl_errors("TLS handshake with the server");
		SSL_free(ssl);
		SSL_CTX_free(ctx);
		return false;
	}

	activeSsl = ssl;

	return true;
}


bool
ws_tls_active(void)
{
	return activeSsl != NULL;
}


ssize_t
ws_io_read(int fd, void *buf, size_t len)
{
	if (activeSsl == NULL)
	{
		return read(fd, buf, len);
	}

	for (;;)
	{
		int n = SSL_read(activeSsl, buf, (int) len);

		if (n > 0)
		{
			return n;
		}

		int err = SSL_get_error(activeSsl, n);

		if (err == SSL_ERROR_ZERO_RETURN)
		{
			return 0;
		}

		if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
		{
			continue;
		}

		if (err != SSL_ERROR_SYSCALL)
		{
			errno = ECONNRESET;
		}

		return -1;
	}
}


ssize_t
ws_io_write(int fd, const void *buf, size_t len)
{
	if (activeSsl == NULL)
	{
		return write(fd, buf, len);
	}

	for (;;)
	{
		int n = SSL_write(activeSsl, buf, (int) len);

		if (n > 0)
		{
			return n;
		}

		int err = SSL_get_error(activeSsl, n);

		if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
		{
			continue;
		}

		if (err != SSL_ERROR_SYSCALL)
		{
			errno = ECONNRESET;
		}

		return -1;
	}
}
