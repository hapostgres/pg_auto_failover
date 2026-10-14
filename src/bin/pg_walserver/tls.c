/*
 * src/bin/pg_walserver/tls.c
 *   See tls.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/ssl.h>

#include "postgres_fe.h"

#include "tls.h"

#include "file_utils.h"
#include "log.h"
#include "openssl_utils.h"

/* PostgreSQL's ssl_ciphers and ssl_groups defaults */
#define WS_TLS_CIPHER_LIST "HIGH:!aNULL"
#define WS_TLS_GROUPS "X25519:prime256v1"

/*
 * SSL_CTX_set_cipher_list() above only configures suites for TLS 1.2 and
 * below -- TLS 1.3 has its own, separate suite list and its own setter
 * (SSL_CTX_set_ciphersuites(), OpenSSL 1.1.1+). Without this, a TLS 1.3
 * handshake falls back to whatever OpenSSL was compiled with by default
 * rather than an explicit, reviewed policy. These three are OpenSSL's own
 * documented TLS 1.3 defaults (all AEAD, all considered secure), named
 * explicitly here so the policy doesn't silently depend on the local
 * OpenSSL build's own defaults.
 */
#define WS_TLS_CIPHERSUITES \
	"TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256"

static SSL_CTX *serverContext = NULL;
static SSL *activeSsl = NULL;
static bool clientVerificationEnabled = false;


/*
 * Like upstream's dummy_ssl_passwd_cb(): a passphrase protected key is an
 * error, never a prompt on whatever terminal the server happens to have.
 */
static int
dummy_passwd_cb(char *buf, int size, int rwflag, void *userdata)
{
	return 0;
}


/*
 * load_private_key opens the key once, checks its permissions on that
 * descriptor and parses the PEM from the same descriptor.
 */
static bool
load_private_key(SSL_CTX *ctx, const char *keyPath)
{
	int fd = open(keyPath, O_RDONLY | O_CLOEXEC);

	if (fd < 0)
	{
		log_error("Failed to open the TLS key file \"%s\": %m", keyPath);
		return false;
	}

	if (!key_permissions_are_safe(fd, keyPath))
	{
		close(fd);
		return false;
	}

	BIO *bio = BIO_new_fd(fd, BIO_NOCLOSE);

	if (bio == NULL)
	{
		close(fd);
		log_openssl_errors("BIO_new_fd");
		return false;
	}

	EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bio, NULL, dummy_passwd_cb, NULL);

	BIO_free(bio);
	close(fd);

	if (pkey == NULL)
	{
		log_openssl_errors("Loading the TLS private key (a passphrase "
						   "protected key is not supported)");
		return false;
	}

	bool ok = SSL_CTX_use_PrivateKey(ctx, pkey) == 1;

	EVP_PKEY_free(pkey);

	if (!ok)
	{
		log_openssl_errors("Using the TLS private key");
	}

	return ok;
}


/*
 * ws_tls_server_init builds this process's server-side SSL_CTX from certPath
 * and keyPath, applying PostgreSQL's own be_tls_init() settings (minimum
 * TLS 1.2, no session tickets/cache/compression/renegotiation, its default
 * cipher list and curves), and stores it as the single serverContext used by
 * every later connection. Returns false (serverContext left NULL, i.e. TLS
 * off) when either file is missing or anything about loading/checking the
 * certificate and key fails; ws_tls_server_enabled() reports the result.
 */
bool
ws_tls_server_init(const char *certPath, const char *keyPath)
{
	if (!file_exists(certPath) || !file_exists(keyPath))
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
	SSL_CTX_set_default_passwd_cb(ctx, dummy_passwd_cb);
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

	if (SSL_CTX_set_ciphersuites(ctx, WS_TLS_CIPHERSUITES) != 1)
	{
		log_openssl_errors("Setting the TLS 1.3 ciphersuites");
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

	if (SSL_CTX_use_certificate_chain_file(ctx, certPath) != 1)
	{
		log_openssl_errors("Loading the TLS certificate");
		SSL_CTX_free(ctx);
		return false;
	}

	if (!load_private_key(ctx, keyPath) || SSL_CTX_check_private_key(ctx) != 1)
	{
		log_openssl_errors("Checking the TLS certificate and key");
		SSL_CTX_free(ctx);
		return false;
	}

	serverContext = ctx;

	return true;
}


/* ws_tls_server_enabled reports whether ws_tls_server_init() succeeded. */
bool
ws_tls_server_enabled(void)
{
	return serverContext != NULL;
}


/*
 * verify_cb is OpenSSL's per-certificate verification callback: preverifyOk
 * is OpenSSL's own chain-validation result (signature, trust anchor,
 * validity dates, ...) for the certificate currently being checked. This
 * project adds no extra check of its own here (unlike real PostgreSQL's
 * verify_cb in be-secure-openssl.c, which also enforces a CRL and a
 * certificate name length cap) -- it exists only so the intent (accept
 * OpenSSL's own verdict, do not override it) is explicit rather than
 * relying on OpenSSL's built-in default callback.
 */
static int
verify_cb(int preverifyOk, X509_STORE_CTX *ctx)
{
	(void) ctx;

	return preverifyOk;
}


/*
 * ws_tls_server_load_ca loads caPath (a PEM bundle of one or more trusted
 * CA certificates, mirroring real PostgreSQL's own ssl_ca_file) into the
 * already-initialized serverContext (ws_tls_server_init() must have
 * succeeded first) and switches every future handshake to *request* a
 * client certificate (SSL_VERIFY_PEER, not SSL_VERIFY_FAIL_IF_NO_PEER_CERT:
 * a client presenting none is still allowed to complete the handshake, the
 * same way real PostgreSQL's be_tls_open_server() only requests one at the
 * TLS layer -- whether one was actually required is an HBA-time decision,
 * "clientcert=verify-full", see hba.h/auth.c). A client that DOES present a
 * certificate not signed by a CA in caPath fails the handshake outright,
 * exactly like real PostgreSQL. Returns false, TLS left exactly as
 * ws_tls_server_init() set it up, on any failure to read or load caPath.
 */
bool
ws_tls_server_load_ca(const char *caPath)
{
	if (serverContext == NULL)
	{
		log_error("ws_tls_server_load_ca() called before ws_tls_server_init()");
		return false;
	}

	if (!file_exists(caPath))
	{
		log_error("The TLS CA file \"%s\" does not exist", caPath);
		return false;
	}

	if (SSL_CTX_load_verify_locations(serverContext, caPath, NULL) != 1)
	{
		log_openssl_errors("Loading the TLS CA file");
		return false;
	}

	/*
	 * SSL_VERIFY_PEER alone requests a client certificate without requiring
	 * one: a client that sends none still completes the handshake (whether
	 * one was actually required is an HBA-time decision, see hba.h's own
	 * "clientcert=verify-full"); a client that sends one that does not
	 * chain to a CA in caPath fails the handshake, exactly like real
	 * PostgreSQL's own be_tls_open_server() the moment ssl_ca_file is set.
	 */
	SSL_CTX_set_verify(serverContext, SSL_VERIFY_PEER, verify_cb);

	/* also advertise caPath's own subjects in the CertificateRequest,
	 * the same server_ca_names real PostgreSQL sends */
	STACK_OF(X509_NAME) * caNames = SSL_load_client_CA_file(caPath);

	if (caNames != NULL)
	{
		SSL_CTX_set_client_CA_list(serverContext, caNames);
	}

	clientVerificationEnabled = true;

	return true;
}


/* ws_tls_client_verification_enabled reports whether a CA is loaded. */
bool
ws_tls_client_verification_enabled(void)
{
	return clientVerificationEnabled;
}


/*
 * ws_tls_server_accept creates a per-connection SSL object bound to sock and
 * runs the server-side TLS handshake (SSL_accept()) to completion. On
 * success it becomes the process's activeSsl (this project forks one child
 * per connection, so a single global is safe); on failure it is freed and
 * false is returned, having logged the OpenSSL error.
 */
bool
ws_tls_server_accept(int sock)
{
	SSL *ssl = SSL_new(serverContext);

	if (ssl == NULL || SSL_set_fd(ssl, sock) != 1)
	{
		log_openssl_errors("SSL_new");

		if (ssl != NULL)
		{
			SSL_free(ssl);
		}

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
 * ws_tls_get_sni_hostname returns the client's SNI hostname via
 * SSL_get_servername(), valid to call any time after ws_tls_server_accept()
 * succeeds -- unlike registering a servername callback (real PostgreSQL's
 * own sni_clienthello_cb(), be-secure-openssl.c, uses the lower-level
 * SSL_client_hello_get0_ext() instead, on OpenSSL's own advice, because it
 * needs to pick a certificate *during* the handshake), a plain post-
 * handshake read has no such ordering concern: nothing here depends on the
 * result to decide anything about the handshake itself, only about
 * addressing a request afterward.
 */
const char *
ws_tls_get_sni_hostname(void)
{
	if (activeSsl == NULL)
	{
		return NULL;
	}

	return SSL_get_servername(activeSsl, TLSEXT_NAMETYPE_host_name);
}


/*
 * ws_tls_get_peer_cert_cn writes the active connection's peer certificate
 * Subject CN into cnBuf (truncated to cnBufSize), the same
 * SSL_get_peer_certificate() + X509_NAME_get_text_by_NID(subject,
 * NID_commonName, ...) pattern ws_tls_get_sni_hostname() already documents
 * as the established way this codebase reads TLS-handshake metadata.
 * Returns false, cnBuf left empty, when there is no active TLS connection,
 * the client presented no certificate at all, or the certificate has no
 * CN -- never a "no certificate" error string, since the caller (auth.c)
 * generates its own error message in that case, matching this project's
 * response-sanitizing convention.
 */
bool
ws_tls_get_peer_cert_cn(char *cnBuf, size_t cnBufSize)
{
	cnBuf[0] = '\0';

	if (activeSsl == NULL)
	{
		return false;
	}

	X509 *peerCert = SSL_get_peer_certificate(activeSsl);

	if (peerCert == NULL)
	{
		/* the client sent no certificate at all: not an error here, the
		 * caller (auth.c) decides whether one was required */
		return false;
	}

	X509_NAME *subject = X509_get_subject_name(peerCert);
	bool found = false;

	if (subject != NULL)
	{
		int len = X509_NAME_get_text_by_NID(subject, NID_commonName,
											cnBuf, (int) cnBufSize);

		found = len > 0;
	}

	X509_free(peerCert);

	if (!found)
	{
		cnBuf[0] = '\0';
	}

	return found;
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


/* ws_tls_active reports whether this connection completed a TLS handshake. */
bool
ws_tls_active(void)
{
	return activeSsl != NULL;
}


/*
 * ws_io_read is framing.c's own read() substitute: a plain read(fd, ...)
 * when no TLS handshake is active, or SSL_read() otherwise. Since the
 * socket is always blocking and the connection's authentication deadline is
 * enforced by SIGALRM, an SSL_ERROR_WANT_READ/WRITE is treated as a hard
 * error (ETIMEDOUT) rather than retried, and any other TLS-layer error as
 * ECONNRESET; only EINTR on the underlying syscall is retried.
 */
ssize_t
ws_io_read(int fd, void *buf, size_t len)
{
	if (activeSsl == NULL)
	{
		return read(fd, buf, len);
	}

	/*
	 * The socket is blocking, and the authentication deadline is enforced
	 * by SIGALRM (accept_loop.c), so SSL_read() only reports WANT_READ/
	 * WANT_WRITE when there is nothing more to wait for here: treat it as
	 * an error, never as a reason to spin.
	 */
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
			errno = ETIMEDOUT;
			return -1;
		}

		if (err == SSL_ERROR_SYSCALL && errno == EINTR)
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


/*
 * ws_io_write is framing.c's own write() substitute, the mirror of
 * ws_io_read(): plain write() with no active TLS handshake, SSL_write()
 * otherwise, with the same WANT_READ/WRITE-is-fatal and EINTR-is-retried
 * handling.
 */
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
			errno = ETIMEDOUT;
			return -1;
		}

		if (err == SSL_ERROR_SYSCALL && errno == EINTR)
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
