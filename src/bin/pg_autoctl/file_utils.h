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


#include "postgres_fe.h"


bool file_exists(const char *filename);
bool directory_exists(const char *path);
bool ensure_empty_dir(const char *dirname, int mode);
bool write_file(char *data, long fileSize, const char *filePath);
bool append_to_file(char *data, long fileSize, const char *filePath);
bool read_file(const char *filePath, char **contents, long *fileSize);
bool move_file(char* sourcePath, char* destinationPath);

void path_in_same_directory(const char *basePath,
							const char *fileName,
							char *destinationPath);

int search_pathlist(const char *pathlist, const char *filename, char ***result);
void search_pathlist_destroy_result(char **result);
bool unlink_file(const char *filename);
bool set_program_absolute_path(char *program, int size);

#endif /* FILE_UTILS_H */
