/*
 * src/bin/common/scram.h
 *   SCRAM-SHA-256 (RFC 5802 / RFC 7677, as spoken by PostgreSQL) building
 *   blocks shared by pg_walsender (server side of the exchange, and its
 *   fetch-file client) and pg_autoctl. Built directly on OpenSSL's
 *   libcrypto rather than PostgreSQL's libpgcommon, whose scram_* function
 *   signatures differ between PostgreSQL major versions.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef SCRAM_H
#define SCRAM_H

#include <stdbool.h>
#include <stddef.h>

#define SCRAM_KEY_LEN 32
#define SCRAM_MAX_SALT_LEN 64
#define SCRAM_DEFAULT_ITERATIONS 4096
#define SCRAM_NONCE_LEN 18
#define SCRAM_MAX_MESSAGE_LEN 1024

/* what a stored "SCRAM-SHA-256$<iter>:<salt>$<StoredKey>:<ServerKey>" holds */
typedef struct ScramVerifier
{
	int iterations;
	unsigned char salt[SCRAM_MAX_SALT_LEN];
	int saltLen;
	unsigned char storedKey[SCRAM_KEY_LEN];
	unsigned char serverKey[SCRAM_KEY_LEN];
} ScramVerifier;

/* build the stored secret for a password, as PostgreSQL stores it */
bool scram_build_verifier(const char *password, int iterations,
						  char *dest, size_t destSize);
bool scram_parse_verifier(const char *secret, ScramVerifier *verifier);

/*
 * Server side. Two calls: scram_server_first() consumes the client-first
 * message and produces the server-first message; scram_server_final()
 * consumes the client-final message, verifies the proof, and produces the
 * server-final ("v=...") message. The state carries what is needed in
 * between.
 */
typedef struct ScramServerState
{
	char clientFirstBare[SCRAM_MAX_MESSAGE_LEN];
	char serverFirst[SCRAM_MAX_MESSAGE_LEN];
	char nonce[SCRAM_MAX_MESSAGE_LEN];
} ScramServerState;

bool scram_server_first(ScramServerState *state, const ScramVerifier *verifier,
						const char *clientFirst, char *serverFirst,
						size_t serverFirstSize);
bool scram_server_final(ScramServerState *state, const ScramVerifier *verifier,
						const char *clientFinal, char *serverFinal,
						size_t serverFinalSize);

/*
 * Client side (used by pg_walsender's fetch-file client).
 */
typedef struct ScramClientState
{
	char clientFirstBare[SCRAM_MAX_MESSAGE_LEN];
	char clientFinalNoProof[SCRAM_MAX_MESSAGE_LEN];
	char serverFirst[SCRAM_MAX_MESSAGE_LEN];
	unsigned char serverKeyForVerify[SCRAM_KEY_LEN];
	char authMessage[3 * SCRAM_MAX_MESSAGE_LEN];
} ScramClientState;

bool scram_client_first(ScramClientState *state, char *dest, size_t destSize);
bool scram_client_final(ScramClientState *state, const char *password,
						const char *serverFirst, char *dest, size_t destSize);
bool scram_client_verify_server_final(ScramClientState *state,
									  const char *serverFinal);

#endif /* SCRAM_H */
