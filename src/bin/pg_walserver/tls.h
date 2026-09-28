/*
 * src/bin/pg_walserver/tls.h
 *   TLS for pg_walserver, the way PostgreSQL does it: a client that wants
 *   TLS sends an SSLRequest, the server answers 'S' and both sides run the
 *   handshake on the same socket before the real StartupMessage. Uses
 *   OpenSSL directly (libssl, the one PostgreSQL itself was built against
 *   on the build host): libpgcommon offers no TLS server, PostgreSQL's own
 *   is backend-only (be-secure-openssl.c).
 *
 *   The server is enabled by <pgdata>/server.crt and <pgdata>/server.key,
 *   the files `pg_autoctl create archiver --ssl-self-signed` (or
 *   --server-cert/--server-key) provides; without them the server answers
 *   'N' to SSLRequest and "hostssl" HBA lines never match. A key file must
 *   not be accessible to group/other (or owned by root and not world
 *   accessible), as PostgreSQL requires. TLS 1.2 or newer.
 *
 *   A passphrase protected key is a clean error, never a prompt (a
 *   password callback that fails, like upstream's dummy_ssl_passwd_cb); the
 *   key's permissions are checked on the very descriptor it is read from.
 *   The handshake and every read/write run under the connection's absolute
 *   authentication deadline (SIGALRM, see accept_loop.h): WANT_READ/
 *   WANT_WRITE and timeouts are errors, never a loop.
 *
 *   ws_io_read()/ws_io_write() are what framing.c reads and writes the
 *   socket through, so everything above it is unaware of TLS.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_TLS_H
#define WS_TLS_H

#include <stdbool.h>
#include <sys/types.h>

/* server side: load the certificate and key, false when TLS is not set up */
bool ws_tls_server_init(const char *certPath, const char *keyPath);
bool ws_tls_server_enabled(void);

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
bool ws_tls_server_load_ca(const char *caPath);

/* was ws_tls_server_load_ca() called and did it succeed? */
bool ws_tls_client_verification_enabled(void);

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
bool ws_tls_get_peer_cert_cn(char *cnBuf, size_t cnBufSize);

/* run the server handshake on sock after the 'S' answer was sent */
bool ws_tls_server_accept(int sock);

/*
 * ws_tls_get_sni_hostname returns the TLS Server Name Indication hostname
 * the client presented during the handshake (SSL_get_servername(),
 * unmodified), or NULL when there is no active TLS connection or the
 * client sent no SNI extension at all. Read-only: this project does not
 * switch certificates based on it (unlike real PostgreSQL's own ssl_sni/
 * hosts_file feature, be-secure-openssl.c) -- see routes_find_by_
 * hostname()'s own comment for what it *is* used for (route selection).
 */
const char * ws_tls_get_sni_hostname(void);

/* tls-server-end-point channel binding data of our certificate (RFC 5929) */
bool ws_tls_certificate_hash(unsigned char *out, int outSize, int *outLen);

/* is the current connection encrypted? */
bool ws_tls_active(void);

ssize_t ws_io_read(int fd, void *buf, size_t len);
ssize_t ws_io_write(int fd, const void *buf, size_t len);

#endif /* WS_TLS_H */
