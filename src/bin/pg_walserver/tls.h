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

/* run the server handshake on sock after the 'S' answer was sent */
bool ws_tls_server_accept(int sock);

/* tls-server-end-point channel binding data of our certificate (RFC 5929) */
bool ws_tls_certificate_hash(unsigned char *out, int outSize, int *outLen);

/* is the current connection encrypted? */
bool ws_tls_active(void);

ssize_t ws_io_read(int fd, void *buf, size_t len);
ssize_t ws_io_write(int fd, const void *buf, size_t len);

#endif /* WS_TLS_H */
