/*
 * src/bin/common/scram.c
 *   The server side of SCRAM-SHA-256, see scram.h. The exchange follows
 *   PostgreSQL's src/backend/libpq/auth-scram.c; the primitives are
 *   libpgcommon's through scram_compat.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <stdlib.h>
#include <string.h>

#include "postgres_fe.h"

#include "scram.h"
#include "scram_compat.h"

#include "file_utils.h"
#include "log.h"
#include "string_utils.h"

/* the tls-server-end-point channel binding of RFC 5929, as PostgreSQL names it */
#define CBIND_TYPE "tls-server-end-point"

static uint8 mockNonce[32];
static bool mockNonceReady = false;


/* comparison whose time does not depend on where the inputs differ */
static bool
constant_time_equal(const uint8 *a, const uint8 *b, size_t len)
{
	uint8 diff = 0;

	for (size_t i = 0; i < len; i++)
	{
		diff |= a[i] ^ b[i];
	}

	return diff == 0;
}


/*
 * find_attribute returns a pointer to the value of the "k=" attribute in a
 * comma-separated SCRAM message (NULL if absent) and its length.
 */
static const char *
find_attribute(const char *message, char key, size_t *valueLen)
{
	const char *p = message;

	while (*p != '\0')
	{
		if (p[0] == key && p[1] == '=')
		{
			const char *value = p + 2;
			const char *end = strchr(value, ',');

			*valueLen = end != NULL ? (size_t) (end - value) : strlen(value);

			return value;
		}

		p = strchr(p, ',');

		if (p == NULL)
		{
			break;
		}

		p++;
	}

	return NULL;
}


/* build the stored secret of a password (SASLprep'ed first, as PostgreSQL does) */
bool
scram_build_verifier(const char *password, int iterations,
					 char *dest, size_t destSize)
{
	uint8 salt[PGAF_SCRAM_SALT_LEN];
	char *prepared = NULL;
	const char *effective = password;

	/*
	 * Like PostgreSQL (pg_be_scram_build_secret), normalize the password
	 * with SASLprep; a password SASLprep rejects is used as it is.
	 */
	pg_saslprep_rc rc = pg_saslprep(password, &prepared);

	if (rc == SASLPREP_SUCCESS)
	{
		effective = prepared;
	}
	else if (rc == SASLPREP_OOM)
	{
		return false;
	}

	if (!pg_strong_random(salt, sizeof(salt)))
	{
		free(prepared);
		return false;
	}

	char *secret = pgaf_scram_build_secret(salt, sizeof(salt), iterations,
										   effective);

	free(prepared);

	if (secret == NULL)
	{
		return false;
	}

	bool ok = strlen(secret) < destSize;

	if (ok)
	{
		strlcpy(dest, secret, destSize);
	}

	free(secret);

	return ok;
}


/*
 * scram_parse_verifier parses one pg_walserver_passwd secret field, the format
 * scram_build_verifier() produces: "SCRAM-SHA-256$<iterations>:<salt>$
 * <StoredKey>:<ServerKey>" with salt/StoredKey/ServerKey base64-encoded.
 * Fills *verifier and returns true on success; returns false, *verifier
 * untouched, on a wrong mechanism prefix, a malformed field structure, or a
 * key that doesn't decode to exactly WS_SCRAM_KEY_LEN bytes.
 */
bool
scram_parse_verifier(const char *secret, ScramVerifier *verifier)
{
	const char *prefix = SCRAM_MECHANISM "$";

	if (strncmp(secret, prefix, strlen(prefix)) != 0)
	{
		return false;
	}

	char copy[512];

	strlcpy(copy, secret + strlen(prefix), sizeof(copy));

	/* <iter>:<salt>$<stored>:<server> */
	char *colon = strchr(copy, ':');
	char *dollar = colon != NULL ? strchr(colon, '$') : NULL;
	char *colon2 = dollar != NULL ? strchr(dollar, ':') : NULL;

	if (colon == NULL || dollar == NULL || colon2 == NULL)
	{
		return false;
	}

	*colon = '\0';
	*dollar = '\0';
	*colon2 = '\0';

	int iterations = 0;

	if (!stringToInt(copy, &iterations) || iterations <= 0)
	{
		return false;
	}

	uint8 stored[WS_SCRAM_KEY_LEN + 2], server[WS_SCRAM_KEY_LEN + 2];

	int saltLen = pgaf_b64_decode(colon + 1, verifier->salt, SCRAM_MAX_SALT_LEN);
	int storedLen = pgaf_b64_decode(dollar + 1, stored, sizeof(stored));
	int serverLen = pgaf_b64_decode(colon2 + 1, server, sizeof(server));

	if (saltLen <= 0 || storedLen != WS_SCRAM_KEY_LEN || serverLen != WS_SCRAM_KEY_LEN)
	{
		return false;
	}

	verifier->iterations = iterations;
	verifier->saltLen = saltLen;
	memcpy(verifier->storedKey, stored, WS_SCRAM_KEY_LEN); /* IGNORE-BANNED */
	memcpy(verifier->serverKey, server, WS_SCRAM_KEY_LEN); /* IGNORE-BANNED */

	return true;
}


/*
 * scram_mock_init generates the one process-wide random nonce
 * scram_mock_verifier() uses to fabricate a plausible-looking verifier for a
 * user that has none, so a login attempt against an unknown user goes
 * through the same SCRAM exchange (and fails the same way) as one against a
 * real user with a wrong password -- an observer cannot tell the two apart.
 * Must be called once before any fork (accept_loop.c does this before
 * forking any connection child), so every connection sees the same mock
 * nonce for a given user rather than a fresh, distinguishable one per
 * connection. Returns false if the system's random source failed.
 */
bool
scram_mock_init(void)
{
	mockNonceReady = pg_strong_random(mockNonce, sizeof(mockNonce));

	return mockNonceReady;
}


/*
 * A deterministic salt per user name (an attacker asking twice must get the
 * same one, as for a real role) and keys that no client proof can match.
 */
bool
scram_mock_verifier(const char *user, ScramVerifier *verifier)
{
	uint8 digest[WS_SCRAM_KEY_LEN];

	if (!mockNonceReady && !scram_mock_init())
	{
		return false;
	}

	if (!pgaf_hmac_sha256(mockNonce, sizeof(mockNonce),
						  (const uint8 *) user, strlen(user), digest))
	{
		return false;
	}

	memset(verifier, 0, sizeof(*verifier)); /* IGNORE-BANNED */
	verifier->iterations = WS_SCRAM_ITERATIONS;
	verifier->saltLen = PGAF_SCRAM_SALT_LEN;
	memcpy(verifier->salt, digest, PGAF_SCRAM_SALT_LEN); /* IGNORE-BANNED */

	return true;
}


/*
 * scram_server_first consumes the client-first message received for
 * mechanism (SCRAM_MECHANISM or SCRAM_MECHANISM_PLUS); cbindData is the
 * hash of our TLS certificate (RFC 5929 tls-server-end-point), NULL/0 when
 * the connection is not encrypted (channel binding not offered).
 */
bool
scram_server_first(ScramServerState *state, const ScramVerifier *verifier,
				   const char *mechanism, const char *clientFirst,
				   const unsigned char *cbindData, int cbindDataLen,
				   char *serverFirst, size_t serverFirstSize)
{
	state->plus = strcmp(mechanism, SCRAM_MECHANISM_PLUS) == 0;
	state->cbindDataLen = 0;

	if (cbindData != NULL && cbindDataLen > 0 && cbindDataLen <= SCRAM_MAX_CBIND_LEN)
	{
		memcpy(state->cbindData, cbindData, cbindDataLen); /* IGNORE-BANNED */
		state->cbindDataLen = cbindDataLen;
	}

	/*
	 * gs2-header = gs2-cbind-flag "," [ authzid ] ",": n = the client does
	 * not support channel binding, y = it does but thinks we do not, p= =
	 * it uses the named one. Same rules as auth-scram.c.
	 */
	const char *rest = NULL;

	switch (clientFirst[0])
	{
		case 'n':
		{
			if (state->plus)
			{
				log_warn("SCRAM: PLUS mechanism without channel binding");
				return false;
			}
			rest = clientFirst + 1;
			break;
		}

		case 'y':
		{
			/* we offered channel binding: the client is being downgraded */
			if (state->plus || state->cbindDataLen > 0)
			{
				log_warn("SCRAM: channel binding negotiation error");
				return false;
			}
			rest = clientFirst + 1;
			break;
		}

		case 'p':
		{
			if (!state->plus || state->cbindDataLen == 0 ||
				strncmp(clientFirst, "p=" CBIND_TYPE, strlen("p=" CBIND_TYPE)) != 0)
			{
				log_warn("SCRAM: unsupported channel binding");
				return false;
			}
			rest = clientFirst + strlen("p=" CBIND_TYPE);
			break;
		}

		default:
		{
			log_warn("SCRAM: malformed gs2 header");
			return false;
		}
	}

	/* no authzid: the header ends with ",," */
	if (rest[0] != ',' || rest[1] != ',')
	{
		log_warn("SCRAM: unsupported authorization identity");
		return false;
	}

	size_t headerLen = (size_t) (rest + 2 - clientFirst);

	if (headerLen >= sizeof(state->gs2Header))
	{
		return false;
	}

	memcpy(state->gs2Header, clientFirst, headerLen); /* IGNORE-BANNED */
	state->gs2Header[headerLen] = '\0';

	strlcpy(state->clientFirstBare, clientFirst + headerLen,
			sizeof(state->clientFirstBare));

	size_t nonceLen = 0;
	const char *clientNonce = find_attribute(state->clientFirstBare, 'r',
											 &nonceLen);

	if (clientNonce == NULL || nonceLen == 0 || nonceLen > 256)
	{
		log_warn("SCRAM: no client nonce in the client-first message");
		return false;
	}

	uint8 rawNonce[18];
	char serverNonce[64];

	if (!pg_strong_random(rawNonce, sizeof(rawNonce)) ||
		pgaf_b64_encode(rawNonce, sizeof(rawNonce), serverNonce,
						sizeof(serverNonce)) < 0)
	{
		return false;
	}

	char clientNonceCopy[260];

	strlcpy(clientNonceCopy, clientNonce, Min(nonceLen + 1, sizeof(clientNonceCopy)));
	sformat(state->nonce, sizeof(state->nonce), "%s%s",
			clientNonceCopy, serverNonce);

	char saltB64[128];

	if (pgaf_b64_encode(verifier->salt, verifier->saltLen, saltB64,
						sizeof(saltB64)) < 0)
	{
		return false;
	}

	int n = sformat(state->serverFirst, sizeof(state->serverFirst),
					"r=%s,s=%s,i=%d", state->nonce, saltB64,
					verifier->iterations);

	if (n <= 0 || (size_t) n >= serverFirstSize)
	{
		return false;
	}

	strlcpy(serverFirst, state->serverFirst, serverFirstSize);

	return true;
}


bool
scram_server_final(ScramServerState *state, const ScramVerifier *verifier,
				   const char *clientFinal, char *serverFinal,
				   size_t serverFinalSize)
{
	/* c= must be base64(gs2 header [ || certificate hash ]) */
	size_t cbindLen = 0;
	const char *cbind = find_attribute(clientFinal, 'c', &cbindLen);
	uint8 expected[128];
	char expectedB64[256];
	size_t headerLen = strlen(state->gs2Header);

	if (cbind == NULL || headerLen + (size_t) state->cbindDataLen > sizeof(expected))
	{
		return false;
	}

	memcpy(expected, state->gs2Header, headerLen); /* IGNORE-BANNED */

	size_t expectedLen = headerLen;

	if (state->plus)
	{
		memcpy(expected + headerLen, state->cbindData, /* IGNORE-BANNED */
			   state->cbindDataLen);
		expectedLen += state->cbindDataLen;
	}

	if (pgaf_b64_encode(expected, (int) expectedLen, expectedB64,
						sizeof(expectedB64)) < 0 ||
		strlen(expectedB64) != cbindLen ||
		strncmp(expectedB64, cbind, cbindLen) != 0)
	{
		log_warn("SCRAM: unexpected channel binding in the client-final message");
		return false;
	}

	size_t len = 0;
	const char *nonce = find_attribute(clientFinal, 'r', &len);

	if (nonce == NULL || len != strlen(state->nonce) ||
		!constant_time_equal((const uint8 *) nonce, (const uint8 *) state->nonce,
							 len))
	{
		log_warn("SCRAM: nonce mismatch in the client-final message");
		return false;
	}

	const char *proofMarker = strstr(clientFinal, ",p=");

	if (proofMarker == NULL)
	{
		return false;
	}

	char withoutProof[SCRAM_MAX_MESSAGE_LEN];
	size_t noProofLen = (size_t) (proofMarker - clientFinal);

	if (noProofLen >= sizeof(withoutProof))
	{
		return false;
	}

	memcpy(withoutProof, clientFinal, noProofLen); /* IGNORE-BANNED */
	withoutProof[noProofLen] = '\0';

	uint8 proof[WS_SCRAM_KEY_LEN + 2];

	if (pgaf_b64_decode(proofMarker + 3, proof, sizeof(proof)) != WS_SCRAM_KEY_LEN)
	{
		return false;
	}

	char authMessage[3 * SCRAM_MAX_MESSAGE_LEN];

	sformat(authMessage, sizeof(authMessage), "%s,%s,%s",
			state->clientFirstBare, state->serverFirst, withoutProof);

	uint8 clientSignature[WS_SCRAM_KEY_LEN];
	uint8 clientKey[WS_SCRAM_KEY_LEN];
	uint8 recomputedStored[WS_SCRAM_KEY_LEN];

	if (!pgaf_hmac_sha256(verifier->storedKey, WS_SCRAM_KEY_LEN,
						  (const uint8 *) authMessage, strlen(authMessage),
						  clientSignature))
	{
		return false;
	}

	for (int i = 0; i < WS_SCRAM_KEY_LEN; i++)
	{
		clientKey[i] = proof[i] ^ clientSignature[i];
	}

	if (!pgaf_scram_h(clientKey, recomputedStored) ||
		!constant_time_equal(recomputedStored, verifier->storedKey,
							 WS_SCRAM_KEY_LEN))
	{
		return false;
	}

	uint8 serverSignature[WS_SCRAM_KEY_LEN];
	char sigB64[64];

	if (!pgaf_hmac_sha256(verifier->serverKey, WS_SCRAM_KEY_LEN,
						  (const uint8 *) authMessage, strlen(authMessage),
						  serverSignature) ||
		pgaf_b64_encode(serverSignature, sizeof(serverSignature), sigB64,
						sizeof(sigB64)) < 0)
	{
		return false;
	}

	int n = sformat(serverFinal, serverFinalSize, "v=%s", sigB64);

	return n > 0 && (size_t) n < serverFinalSize;
}
