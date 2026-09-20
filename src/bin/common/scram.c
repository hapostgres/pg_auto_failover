/*
 * src/bin/common/scram.c
 *   SCRAM-SHA-256 building blocks, see scram.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <stdlib.h>
#include <string.h>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "postgres_fe.h"

#include "scram.h"

#include "file_utils.h"
#include "log.h"
#include "string_utils.h"


static bool
hmac_sha256(const unsigned char *key, size_t keyLen,
			const unsigned char *data, size_t dataLen,
			unsigned char out[SCRAM_KEY_LEN])
{
	unsigned int outLen = 0;

	return HMAC(EVP_sha256(), key, (int) keyLen, data, dataLen,
				out, &outLen) != NULL && outLen == SCRAM_KEY_LEN;
}


static void
sha256(const unsigned char *data, size_t dataLen,
	   unsigned char out[SCRAM_KEY_LEN])
{
	SHA256(data, dataLen, out);
}


static bool
salted_password(const char *password, const unsigned char *salt, int saltLen,
				int iterations, unsigned char out[SCRAM_KEY_LEN])
{
	return PKCS5_PBKDF2_HMAC(password, (int) strlen(password), salt, saltLen,
							 iterations, EVP_sha256(),
							 SCRAM_KEY_LEN, out) == 1;
}


static bool
base64_encode(const unsigned char *src, size_t srcLen,
			  char *dest, size_t destSize)
{
	size_t needed = 4 * ((srcLen + 2) / 3) + 1;

	if (needed > destSize)
	{
		return false;
	}

	EVP_EncodeBlock((unsigned char *) dest, src, (int) srcLen);

	return true;
}


/* returns the decoded length, or -1 on malformed input / overflow */
static int
base64_decode(const char *src, unsigned char *dest, size_t destSize)
{
	size_t srcLen = strlen(src);

	if (srcLen == 0 || srcLen % 4 != 0 || srcLen / 4 * 3 > destSize + 2)
	{
		return -1;
	}

	unsigned char tmp[SCRAM_MAX_MESSAGE_LEN];

	if (srcLen / 4 * 3 > sizeof(tmp))
	{
		return -1;
	}

	int len = EVP_DecodeBlock(tmp, (const unsigned char *) src, (int) srcLen);

	if (len < 0)
	{
		return -1;
	}

	/* EVP_DecodeBlock counts the padding bytes: trim them */
	if (srcLen >= 1 && src[srcLen - 1] == '=')
	{
		len--;
	}
	if (srcLen >= 2 && src[srcLen - 2] == '=')
	{
		len--;
	}

	if (len < 0 || (size_t) len > destSize)
	{
		return -1;
	}

	memcpy(dest, tmp, len); /* IGNORE-BANNED */

	return len;
}


static bool
random_nonce(char *dest, size_t destSize)
{
	unsigned char raw[SCRAM_NONCE_LEN];

	if (RAND_bytes(raw, sizeof(raw)) != 1)
	{
		return false;
	}

	return base64_encode(raw, sizeof(raw), dest, destSize);
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


bool
scram_build_verifier(const char *password, int iterations,
					 char *dest, size_t destSize)
{
	unsigned char salt[16];
	unsigned char salted[SCRAM_KEY_LEN];
	unsigned char clientKey[SCRAM_KEY_LEN];
	unsigned char storedKey[SCRAM_KEY_LEN];
	unsigned char serverKey[SCRAM_KEY_LEN];

	if (RAND_bytes(salt, sizeof(salt)) != 1 ||
		!salted_password(password, salt, sizeof(salt), iterations, salted) ||
		!hmac_sha256(salted, sizeof(salted),
					 (const unsigned char *) "Client Key", 10, clientKey) ||
		!hmac_sha256(salted, sizeof(salted),
					 (const unsigned char *) "Server Key", 10, serverKey))
	{
		return false;
	}

	sha256(clientKey, sizeof(clientKey), storedKey);

	char saltB64[64], storedB64[64], serverB64[64];

	if (!base64_encode(salt, sizeof(salt), saltB64, sizeof(saltB64)) ||
		!base64_encode(storedKey, sizeof(storedKey), storedB64, sizeof(storedB64)) ||
		!base64_encode(serverKey, sizeof(serverKey), serverB64, sizeof(serverB64)))
	{
		return false;
	}

	int n = sformat(dest, destSize, "SCRAM-SHA-256$%d:%s$%s:%s",
					iterations, saltB64, storedB64, serverB64);

	return n > 0 && (size_t) n < destSize;
}


bool
scram_parse_verifier(const char *secret, ScramVerifier *verifier)
{
	const char *prefix = "SCRAM-SHA-256$";

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

	unsigned char stored[SCRAM_KEY_LEN + 2], server[SCRAM_KEY_LEN + 2];

	int saltLen = base64_decode(colon + 1, verifier->salt, SCRAM_MAX_SALT_LEN);
	int storedLen = base64_decode(dollar + 1, stored, sizeof(stored));
	int serverLen = base64_decode(colon2 + 1, server, sizeof(server));

	if (saltLen <= 0 || storedLen != SCRAM_KEY_LEN || serverLen != SCRAM_KEY_LEN)
	{
		return false;
	}

	verifier->iterations = iterations;
	verifier->saltLen = saltLen;
	memcpy(verifier->storedKey, stored, SCRAM_KEY_LEN); /* IGNORE-BANNED */
	memcpy(verifier->serverKey, server, SCRAM_KEY_LEN); /* IGNORE-BANNED */

	return true;
}


bool
scram_server_first(ScramServerState *state, const ScramVerifier *verifier,
				   const char *clientFirst, char *serverFirst,
				   size_t serverFirstSize)
{
	/* gs2 header: no channel binding ("n,," or "y,,"), no authzid */
	if (strncmp(clientFirst, "n,,", 3) != 0 &&
		strncmp(clientFirst, "y,,", 3) != 0)
	{
		log_warn("SCRAM: unsupported gs2 header in the client-first message");
		return false;
	}

	strlcpy(state->clientFirstBare, clientFirst + 3,
			sizeof(state->clientFirstBare));

	size_t nonceLen = 0;
	const char *clientNonce = find_attribute(state->clientFirstBare, 'r',
											 &nonceLen);

	if (clientNonce == NULL || nonceLen == 0 || nonceLen > 256)
	{
		log_warn("SCRAM: no client nonce in the client-first message");
		return false;
	}

	char serverNonce[64];

	if (!random_nonce(serverNonce, sizeof(serverNonce)))
	{
		return false;
	}

	char clientNonceCopy[260];

	strlcpy(clientNonceCopy, clientNonce, Min(nonceLen + 1, sizeof(clientNonceCopy)));
	sformat(state->nonce, sizeof(state->nonce), "%s%s",
			clientNonceCopy, serverNonce);

	char saltB64[128];

	if (!base64_encode(verifier->salt, verifier->saltLen, saltB64, sizeof(saltB64)))
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
	size_t len = 0;
	const char *nonce = find_attribute(clientFinal, 'r', &len);

	if (nonce == NULL || len != strlen(state->nonce) ||
		CRYPTO_memcmp(nonce, state->nonce, len) != 0)
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

	unsigned char proof[SCRAM_KEY_LEN + 2];

	if (base64_decode(proofMarker + 3, proof, sizeof(proof)) != SCRAM_KEY_LEN)
	{
		return false;
	}

	char authMessage[3 * SCRAM_MAX_MESSAGE_LEN];

	sformat(authMessage, sizeof(authMessage), "%s,%s,%s",
			state->clientFirstBare, state->serverFirst, withoutProof);

	unsigned char clientSignature[SCRAM_KEY_LEN];
	unsigned char clientKey[SCRAM_KEY_LEN];
	unsigned char recomputedStored[SCRAM_KEY_LEN];

	if (!hmac_sha256(verifier->storedKey, SCRAM_KEY_LEN,
					 (const unsigned char *) authMessage, strlen(authMessage),
					 clientSignature))
	{
		return false;
	}

	for (int i = 0; i < SCRAM_KEY_LEN; i++)
	{
		clientKey[i] = proof[i] ^ clientSignature[i];
	}

	sha256(clientKey, sizeof(clientKey), recomputedStored);

	if (CRYPTO_memcmp(recomputedStored, verifier->storedKey, SCRAM_KEY_LEN) != 0)
	{
		return false;
	}

	unsigned char serverSignature[SCRAM_KEY_LEN];
	char sigB64[64];

	if (!hmac_sha256(verifier->serverKey, SCRAM_KEY_LEN,
					 (const unsigned char *) authMessage, strlen(authMessage),
					 serverSignature) ||
		!base64_encode(serverSignature, sizeof(serverSignature),
					   sigB64, sizeof(sigB64)))
	{
		return false;
	}

	int n = sformat(serverFinal, serverFinalSize, "v=%s", sigB64);

	return n > 0 && (size_t) n < serverFinalSize;
}


bool
scram_client_first(ScramClientState *state, char *dest, size_t destSize)
{
	char nonce[64];

	if (!random_nonce(nonce, sizeof(nonce)))
	{
		return false;
	}

	sformat(state->clientFirstBare, sizeof(state->clientFirstBare),
			"n=,r=%s", nonce);

	int n = sformat(dest, destSize, "n,,%s", state->clientFirstBare);

	return n > 0 && (size_t) n < destSize;
}


bool
scram_client_final(ScramClientState *state, const char *password,
				   const char *serverFirst, char *dest, size_t destSize)
{
	strlcpy(state->serverFirst, serverFirst, sizeof(state->serverFirst));

	size_t nonceLen = 0, saltLen = 0, iterLen = 0;
	const char *nonce = find_attribute(serverFirst, 'r', &nonceLen);
	const char *salt = find_attribute(serverFirst, 's', &saltLen);
	const char *iter = find_attribute(serverFirst, 'i', &iterLen);

	if (nonce == NULL || salt == NULL || iter == NULL ||
		nonceLen == 0 || saltLen == 0 || iterLen == 0 ||
		saltLen >= 128 || iterLen >= 16)
	{
		return false;
	}

	/* the server nonce must extend ours */
	size_t ourNonceLen = 0;
	const char *ourNonce = find_attribute(state->clientFirstBare, 'r',
										  &ourNonceLen);

	if (ourNonce == NULL || nonceLen < ourNonceLen ||
		strncmp(nonce, ourNonce, ourNonceLen) != 0)
	{
		return false;
	}

	char saltStr[128], iterStr[16], nonceStr[512];

	strlcpy(saltStr, salt, Min(saltLen + 1, sizeof(saltStr)));
	strlcpy(iterStr, iter, Min(iterLen + 1, sizeof(iterStr)));
	strlcpy(nonceStr, nonce, Min(nonceLen + 1, sizeof(nonceStr)));

	int iterations = 0;
	unsigned char saltRaw[SCRAM_MAX_SALT_LEN];

	if (!stringToInt(iterStr, &iterations) || iterations <= 0)
	{
		return false;
	}

	int saltRawLen = base64_decode(saltStr, saltRaw, sizeof(saltRaw));

	if (saltRawLen <= 0)
	{
		return false;
	}

	unsigned char salted[SCRAM_KEY_LEN], clientKey[SCRAM_KEY_LEN];
	unsigned char storedKey[SCRAM_KEY_LEN], clientSignature[SCRAM_KEY_LEN];

	if (!salted_password(password, saltRaw, saltRawLen, iterations, salted) ||
		!hmac_sha256(salted, sizeof(salted),
					 (const unsigned char *) "Client Key", 10, clientKey) ||
		!hmac_sha256(salted, sizeof(salted),
					 (const unsigned char *) "Server Key", 10,
					 state->serverKeyForVerify))
	{
		return false;
	}

	sha256(clientKey, sizeof(clientKey), storedKey);

	sformat(state->clientFinalNoProof, sizeof(state->clientFinalNoProof),
			"c=biws,r=%s", nonceStr);
	sformat(state->authMessage, sizeof(state->authMessage), "%s,%s,%s",
			state->clientFirstBare, state->serverFirst,
			state->clientFinalNoProof);

	if (!hmac_sha256(storedKey, sizeof(storedKey),
					 (const unsigned char *) state->authMessage,
					 strlen(state->authMessage), clientSignature))
	{
		return false;
	}

	unsigned char proof[SCRAM_KEY_LEN];
	char proofB64[64];

	for (int i = 0; i < SCRAM_KEY_LEN; i++)
	{
		proof[i] = clientKey[i] ^ clientSignature[i];
	}

	if (!base64_encode(proof, sizeof(proof), proofB64, sizeof(proofB64)))
	{
		return false;
	}

	int n = sformat(dest, destSize, "%s,p=%s", state->clientFinalNoProof,
					proofB64);

	return n > 0 && (size_t) n < destSize;
}


bool
scram_client_verify_server_final(ScramClientState *state,
								 const char *serverFinal)
{
	size_t len = 0;
	const char *v = find_attribute(serverFinal, 'v', &len);

	if (v == NULL || len == 0 || len >= 64)
	{
		return false;
	}

	unsigned char expected[SCRAM_KEY_LEN];
	char expectedB64[64];

	if (!hmac_sha256(state->serverKeyForVerify, SCRAM_KEY_LEN,
					 (const unsigned char *) state->authMessage,
					 strlen(state->authMessage), expected) ||
		!base64_encode(expected, sizeof(expected), expectedB64,
					   sizeof(expectedB64)))
	{
		return false;
	}

	return strlen(expectedB64) == len &&
		   CRYPTO_memcmp(expectedB64, v, len) == 0;
}
