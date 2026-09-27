/*
 * src/bin/common/scram.h
 *   The server side of SCRAM-SHA-256 (RFC 5802 / RFC 7677, as PostgreSQL
 *   speaks it) for pg_walserver, and the stored-secret helpers pg_autoctl
 *   shares with it. The cryptographic primitives are libpgcommon's
 *   (scram_compat.h), password normalization is its SASLprep, and the
 *   exchange follows src/backend/libpq/auth-scram.c: channel binding
 *   (SCRAM-SHA-256-PLUS with tls-server-end-point) when the connection is
 *   encrypted, and "mock" authentication for a user without a usable
 *   secret, so that an unknown role cannot be told from a wrong password.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef SCRAM_H
#define SCRAM_H

#include <stdbool.h>
#include <stddef.h>

#define WS_SCRAM_KEY_LEN 32
#define SCRAM_MAX_SALT_LEN 64
#define WS_SCRAM_ITERATIONS 4096
#define SCRAM_MAX_MESSAGE_LEN 1024
#define SCRAM_MAX_CBIND_LEN 64

#define SCRAM_MECHANISM "SCRAM-SHA-256"
#define SCRAM_MECHANISM_PLUS "SCRAM-SHA-256-PLUS"

/* what a stored "SCRAM-SHA-256$<iter>:<salt>$<StoredKey>:<ServerKey>" holds */
typedef struct ScramVerifier
{
	int iterations;
	unsigned char salt[SCRAM_MAX_SALT_LEN];
	int saltLen;
	unsigned char storedKey[WS_SCRAM_KEY_LEN];
	unsigned char serverKey[WS_SCRAM_KEY_LEN];
} ScramVerifier;

/* build the stored secret of a password (SASLprep'ed first, as PostgreSQL does) */
bool scram_build_verifier(const char *password, int iterations,
						  char *dest, size_t destSize);
bool scram_parse_verifier(const char *secret, ScramVerifier *verifier);

/*
 * A verifier that can never be proven, derived from the user name and a
 * per-process secret: the exchange runs to the end and fails like a wrong
 * password does. Call scram_mock_init() once, before forking.
 */
bool scram_mock_init(void);
bool scram_mock_verifier(const char *user, ScramVerifier *verifier);

typedef struct ScramServerState
{
	char clientFirstBare[SCRAM_MAX_MESSAGE_LEN];
	char serverFirst[SCRAM_MAX_MESSAGE_LEN];
	char nonce[SCRAM_MAX_MESSAGE_LEN];
	char gs2Header[64];         /* "n,," or "p=tls-server-end-point,," ... */
	bool plus;                  /* SCRAM-SHA-256-PLUS was selected */
	unsigned char cbindData[SCRAM_MAX_CBIND_LEN];
	int cbindDataLen;           /* our certificate hash, 0: not offered */
} ScramServerState;

/*
 * scram_server_first consumes the client-first message received for
 * mechanism (SCRAM_MECHANISM or SCRAM_MECHANISM_PLUS); cbindData is the
 * hash of our TLS certificate (RFC 5929 tls-server-end-point), NULL/0 when
 * the connection is not encrypted (channel binding not offered).
 */
bool scram_server_first(ScramServerState *state, const ScramVerifier *verifier,
						const char *mechanism, const char *clientFirst,
						const unsigned char *cbindData, int cbindDataLen,
						char *serverFirst, size_t serverFirstSize);

bool scram_server_final(ScramServerState *state, const ScramVerifier *verifier,
						const char *clientFinal, char *serverFinal,
						size_t serverFinalSize);

#endif /* SCRAM_H */
