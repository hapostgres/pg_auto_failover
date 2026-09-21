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
#include <openssl/x509.h>
#include <openssl/ssl.h>

#include "postgres_fe.h"

#include "tls.h"

#include "file_utils.h"
#include "log.h"

/* PostgreSQL's ssl_ciphers and ssl_groups defaults */
#define WS_TLS_CIPHER_LIST "HIGH:!aNULL"
#define WS_TLS_GROUPS "X25519:prime256v1"

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
 * Adapted from PostgreSQL's check_ssl_key_file_permissions()
 * (src/backend/libpq/be-secure-common.c), with ereport() turned into
 * log_error(): a key owned by us must be 0600 (no group/other access at
 * all); one owned by root may also be group readable (0640), for a
 * certificate shared through a group; any other owner is refused.
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

	/*
	 * The context settings of be_tls_init() (src/backend/libpq/
	 * be-secure-openssl.c): TLS 1.2 or newer, no session tickets and no
	 * session cache (a connection here is short and authenticated by
	 * password, resumption buys nothing and costs state), no compression,
	 * no renegotiation, moving write buffers, the server's cipher order,
	 * and PostgreSQL's default cipher list and curves.
	 */
	SSL_CTX_set_mode(ctx, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
	SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
	SSL_CTX_set_num_tickets(ctx, 0);
	SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);
	SSL_CTX_set_options(ctx, SSL_OP_NO_TICKET | SSL_OP_NO_COMPRESSION |
						SSL_OP_NO_RENEGOTIATION | SSL_OP_CIPHER_SERVER_PREFERENCE);

	/* PostgreSQL guards this one too: it is not in every OpenSSL */
#ifdef SSL_OP_NO_CLIENT_RENEGOTIATION
	SSL_CTX_set_options(ctx, SSL_OP_NO_CLIENT_RENEGOTIATION);
#endif

	if (SSL_CTX_set_cipher_list(ctx, WS_TLS_CIPHER_LIST) != 1)
	{
		log_openssl_errors("Setting the TLS cipher list");
		SSL_CTX_free(ctx);
		return false;
	}

	SSL_CTX_set_dh_auto(ctx, 1);

	if (SSL_CTX_set1_groups_list(ctx, WS_TLS_GROUPS) != 1)
	{
		log_openssl_errors("Setting the TLS groups");
		SSL_CTX_free(ctx);
		return false;
	}

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


/*
 * ws_tls_certificate_hash computes the tls-server-end-point channel binding
 * data (RFC 5929) of our own certificate: its hash, with the digest of its
 * signature algorithm, or SHA-256 when that is MD5 or SHA-1. The same
 * computation as be_tls_get_certificate_hash() in PostgreSQL's
 * be-secure-openssl.c, which is what makes libpq's own value agree.
 */
bool
ws_tls_certificate_hash(unsigned char *out, int outSize, int *outLen)
{
	*outLen = 0;

	X509 *cert = activeSsl != NULL ? SSL_get_certificate(activeSsl) : NULL;
	int algoNid = 0;

	if (cert == NULL)
	{
		return false;
	}

	if (!X509_get_signature_info(cert, &algoNid, NULL, NULL, NULL))
	{
		log_error("Failed to determine the certificate's signature algorithm");
		return false;
	}

	const EVP_MD *digest = NULL;

	switch (algoNid)
	{
		case NID_md5:
		case NID_sha1:
		{
			digest = EVP_sha256();
			break;
		}

		default:
		{
			digest = EVP_get_digestbynid(algoNid);
			break;
		}
	}

	unsigned char hash[EVP_MAX_MD_SIZE];
	unsigned int hashSize = 0;

	if (digest == NULL || !X509_digest(cert, digest, hash, &hashSize) ||
		hashSize > (unsigned int) outSize)
	{
		log_error("Failed to compute the certificate hash for channel binding");
		return false;
	}

	memcpy(out, hash, hashSize); /* IGNORE-BANNED */
	*outLen = (int) hashSize;

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
