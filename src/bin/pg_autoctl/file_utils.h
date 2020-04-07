/*
 * src/bin/pg_autoctl/file_utils.h
 *   Utility functions for reading and writing files
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include <stdarg.h>

#include "postgres_fe.h"

#include <fcntl.h>


bool file_exists(const char *filename);
bool directory_exists(const char *path);
bool ensure_empty_dir(const char *dirname, int mode);
FILE * fopen_with_umask(const char *filePath, const char *modes, int flags, mode_t umask);
FILE * fopen_read_only(const char *filePath);
bool write_file(char *data, long fileSize, const char *filePath);
bool append_to_file(char *data, long fileSize, const char *filePath);
bool read_file(const char *filePath, char **contents, long *fileSize);
bool move_file(char *sourcePath, char *destinationPath);
bool duplicate_file(char *sourcePath, char *destinationPath);
bool create_symbolic_link(char *sourcePath, char *targetPath);

void path_in_same_directory(const char *basePath,
							const char *fileName,
							char *destinationPath);

bool search_path_first(const char *filename, char *result);
int search_path(const char *filename, char ***result);
void search_path_destroy_result(char **result);
bool unlink_file(const char *filename);
bool set_program_absolute_path(char *program, int size);
bool normalize_filename(const char *filename, char *dst, int size);

int fformat(FILE *stream, const char *fmt, ...)
__attribute__((format(printf, 2, 3)));

bool sformat(char *str, size_t count, const char *result_name, const char *fmt, ...)
__attribute__((format(printf, 4, 5)));

#define sformat_fail(str, count, result_name, fmt, ...) \
	if (!sformat(str, count, result_name, fmt, __VA_ARGS__)) { \
		log_debug("lineinfo for string formatting failure"); \
		return false; \
	}

#define sformat_exit(str, count, result_name, fmt, ...) \
	if (!sformat(str, count, result_name, fmt, __VA_ARGS__)) { \
		log_debug("lineinfo for string formatting failure"); \
		exit(EXIT_CODE_BAD_CONFIG); \
	}

#endif /* FILE_UTILS_H */
