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
#include <time.h>


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
 * timegm(), see this file's own retentionAgeCutoff()) -- NOT a fixed
 * 30-day approximation, since month lengths vary.
 */
typedef struct RetentionAge
{
	long value;
	char unit;   /* 'h', 'd', 'w', or 'm' */
} RetentionAge;

bool stringToRetentionAge(const char *str, RetentionAge *age);

time_t retentionAgeCutoff(const RetentionAge *age, time_t now);

void sanitizeForLog(const char *in, char *out, size_t outSize);

#endif /* STRING_UTILS_h */
