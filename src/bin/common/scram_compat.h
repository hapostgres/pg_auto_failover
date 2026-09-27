/*
 * src/bin/common/scram_compat.h
 *   One stable spelling of the libpgcommon primitives SCRAM is built from,
 *   over the PostgreSQL major versions we build against. libpgcommon is
 *   already linked (pg_config --libs), but its scram_* / pg_b64_* function
 *   signatures moved between releases:
 *
 *     14  scram_SaltedPassword(pw, salt, saltlen, iter, out)
 *     15  ... + const char **errstr
 *     16  scram_SaltedPassword(pw, hash_type, key_length, salt, ...)
 *     18  the salt is a const uint8 * (was const char *); pg_b64_encode/
 *         decode take uint8 * (were char *)
 *
 *   Everything below is a thin static inline over the installed headers,
 *   selected by PG_VERSION_NUM, so a package build against any of them
 *   picks the right one and nothing else in this tree knows.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef SCRAM_COMPAT_H
#define SCRAM_COMPAT_H

#include "postgres_fe.h"

#include "common/base64.h"
#include "common/cryptohash.h"
#include "common/hmac.h"
#include "common/saslprep.h"
#include "common/scram-common.h"

#define PGAF_SCRAM_KEY_LEN 32       /* SHA-256 */
#define PGAF_SCRAM_SALT_LEN 16
#define PGAF_SCRAM_DEFAULT_ITERATIONS 4096

static inline bool
pgaf_scram_salted_password(const char *password, const uint8 *salt, int saltLen,
						   int iterations, uint8 *out)
{
#if PG_VERSION_NUM >= 180000
	const char *errstr = NULL;

	return scram_SaltedPassword(password, PG_SHA256, PGAF_SCRAM_KEY_LEN,
								salt, saltLen, iterations, out, &errstr) == 0;
#elif PG_VERSION_NUM >= 160000
	const char *errstr = NULL;

	return scram_SaltedPassword(password, PG_SHA256, PGAF_SCRAM_KEY_LEN,
								(const char *) salt, saltLen, iterations,
								out, &errstr) == 0;
#elif PG_VERSION_NUM >= 150000
	const char *errstr = NULL;

	return scram_SaltedPassword(password, (const char *) salt, saltLen,
								iterations, out, &errstr) == 0;
#else
	return scram_SaltedPassword(password, (const char *) salt, saltLen,
								iterations, out) == 0;
#endif
}


/* H(key): SHA-256 of a SCRAM key */
static inline bool
pgaf_scram_h(const uint8 *key, uint8 *out)
{
#if PG_VERSION_NUM >= 160000
	const char *errstr = NULL;

	return scram_H(key, PG_SHA256, PGAF_SCRAM_KEY_LEN, out, &errstr) == 0;
#elif PG_VERSION_NUM >= 150000
	const char *errstr = NULL;

	return scram_H(key, PGAF_SCRAM_KEY_LEN, out, &errstr) == 0;
#else
	return scram_H(key, PGAF_SCRAM_KEY_LEN, out) == 0;
#endif
}


static inline bool
pgaf_scram_client_key(const uint8 *saltedPassword, uint8 *out)
{
#if PG_VERSION_NUM >= 160000
	const char *errstr = NULL;

	return scram_ClientKey(saltedPassword, PG_SHA256, PGAF_SCRAM_KEY_LEN,
						   out, &errstr) == 0;
#elif PG_VERSION_NUM >= 150000
	const char *errstr = NULL;

	return scram_ClientKey(saltedPassword, out, &errstr) == 0;
#else
	return scram_ClientKey(saltedPassword, out) == 0;
#endif
}


static inline bool
pgaf_scram_server_key(const uint8 *saltedPassword, uint8 *out)
{
#if PG_VERSION_NUM >= 160000
	const char *errstr = NULL;

	return scram_ServerKey(saltedPassword, PG_SHA256, PGAF_SCRAM_KEY_LEN,
						   out, &errstr) == 0;
#elif PG_VERSION_NUM >= 150000
	const char *errstr = NULL;

	return scram_ServerKey(saltedPassword, out, &errstr) == 0;
#else
	return scram_ServerKey(saltedPassword, out) == 0;
#endif
}


/* the stored secret for an already SASLprep'ed password; malloc'ed or NULL */
static inline char *
pgaf_scram_build_secret(const uint8 *salt, int saltLen, int iterations,
						const char *password)
{
#if PG_VERSION_NUM >= 180000
	const char *errstr = NULL;

	return scram_build_secret(PG_SHA256, PGAF_SCRAM_KEY_LEN, salt, saltLen,
							  iterations, password, &errstr);
#elif PG_VERSION_NUM >= 160000
	const char *errstr = NULL;

	return scram_build_secret(PG_SHA256, PGAF_SCRAM_KEY_LEN,
							  (const char *) salt, saltLen, iterations,
							  password, &errstr);
#elif PG_VERSION_NUM >= 150000
	const char *errstr = NULL;

	return scram_build_secret((const char *) salt, saltLen, iterations,
							  password, &errstr);
#else
	return scram_build_secret((const char *) salt, saltLen, iterations,
							  password);
#endif
}


/* pg_b64_* take char * up to 17 and uint8 * from 18: void * fits both */
static inline int
pgaf_b64_encode(const uint8 *src, int len, char *dst, int dstLen)
{
	int n = pg_b64_encode((const void *) src, len, (void *) dst, dstLen);

	if (n >= 0 && n < dstLen)
	{
		dst[n] = '\0';
	}

	return n;
}


static inline int
pgaf_b64_decode(const char *src, uint8 *dst, int dstLen)
{
	return pg_b64_decode(src, (int) strlen(src), (void *) dst, dstLen);
}


/* HMAC-SHA-256 through libpgcommon's pg_hmac_*, stable since 14 */
static inline bool
pgaf_hmac_sha256(const uint8 *key, size_t keyLen, const uint8 *data,
				 size_t dataLen, uint8 *out)
{
	pg_hmac_ctx *ctx = pg_hmac_create(PG_SHA256);

	if (ctx == NULL)
	{
		return false;
	}

	bool ok = pg_hmac_init(ctx, key, keyLen) == 0 &&
			  pg_hmac_update(ctx, data, dataLen) == 0 &&
			  pg_hmac_final(ctx, out, PGAF_SCRAM_KEY_LEN) == 0;

	pg_hmac_free(ctx);

	return ok;
}


#endif /* SCRAM_COMPAT_H */
