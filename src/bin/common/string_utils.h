/*
 * src/bin/pg_autoctl/string_utils.h
 *   Utility functions for string handling
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <stdbool.h>


/* maximum decimal int64 length with minus and NUL */
#define INTSTRING_MAX_DIGITS 21
typedef struct IntString
{
	int64_t intValue;
	char strValue[INTSTRING_MAX_DIGITS];
} IntString;

IntString intToString(int64_t number);

bool stringToInt(const char *str, int *number);
bool stringToUInt(const char *str, unsigned int *number);

bool stringToInt64(const char *str, int64_t *number);
bool stringToUInt64(const char *str, uint64_t *number);

bool stringToShort(const char *str, short *number);
bool stringToUShort(const char *str, unsigned short *number);

bool stringToInt32(const char *str, int32_t *number);
bool stringToUInt32(const char *str, uint32_t *number);

bool stringToDouble(const char *str, double *number);
bool IntervalToString(double seconds, char *buffer, size_t size);

int countLines(char *buffer);
int splitLines(char *errorMessage, char **linesArray, int size);
void processBufferCallback(const char *buffer, bool error);

/*
 * A parsed --keep-age value (pg_walserver's archive-cleanup and basebackup
 * sub-commands): a bare count and one of the four required suffixes.
 * 'h'/'d'/'w' are fixed-length durations (3600/86400/604800 seconds
 * respectively); 'm' is real calendar-month arithmetic (struct tm plus
 * timegm(), see cli_archive_cleanup.c's own ws_retention_age_cutoff()) --
 * NOT a fixed 30-day approximation, since month lengths vary.
 */
typedef struct RetentionAge
{
	long value;
	char unit;   /* 'h', 'd', 'w', or 'm' */
} RetentionAge;

/*
 * stringToRetentionAge parses a --keep-age argument such as "72h",
 * "14d", "4w", "3m" into *age. An explicit suffix is required -- there is
 * no bare-number default, ambiguity here is worse than a clear error.
 * Returns false with an error already logged (naming the accepted
 * suffixes) on anything else.
 */
bool stringToRetentionAge(const char *str, RetentionAge *age);

/*
 * sanitizeForLog copies a possibly-untrusted string (e.g. straight from
 * an unauthenticated client: a user name, a route key) into out (bounded
 * by outSize), replacing any control character (< 0x20 or 0x7f) with '?'
 * so it can be logged or shown in a process title without letting it
 * inject terminal escape sequences or fake log lines. Truncates with a
 * trailing "..." when in doesn't fit.
 */
void sanitizeForLog(const char *in, char *out, size_t outSize);

#endif /* STRING_UTILS_h */
