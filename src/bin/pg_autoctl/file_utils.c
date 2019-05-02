/*
 * src/bin/pg_autoctl/file_utils.c
 *   Implementations of utility functions for reading and writing files
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <sys/stat.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "file_utils.h"
#include "log.h"


/*
 * file_exists returns true if the given filename is known to exist
 * on the file system or false if it does not exists or in case of
 * error.
 */
bool
file_exists(const char *filename)
{
	bool exists = access(filename, F_OK) != -1;
	if (!exists && errno != 0)
	{
		/*
		 * Only log "interesting" errors here.
		 *
		 * The fact that the file does not exists is not interesting: we're
		 * retuning false and the caller figures it out, maybe then creating
		 * the file.
		 */
		if (errno != ENOENT && errno != ENOTDIR)
		{
			log_error("Failed to check if file \"%s\" exists: %s",
					  filename, strerror(errno));
		}
		return false;
	}

	return exists;
}


/*
 * directory_exists returns whether the given path is the name of a directory that
 * exists on the file system or not.
 */
bool
directory_exists(const char *path)
{
	bool result = false;
	struct stat info;

	if (!file_exists(path))
	{
		return false;
	}

	if (stat(path, &info) != 0)
	{
		log_error("Failed to stat \"%s\": %s\n", path, strerror(errno));
		return false;
	}

	result = (info.st_mode & S_IFMT) == S_IFDIR;
	return result;
}


/*
 * ensure_empty_dir ensures that the given path points to an empty directory with
 * the given mode. If it fails to do so, it returns false.
 */
bool
ensure_empty_dir(const char *dirname, int mode)
{
	/* pg_mkdir_p might modify its input, so create a copy of dirname. */
	char dirname_copy[MAXPGPATH];
	strlcpy(dirname_copy, dirname, MAXPGPATH);

	if (directory_exists(dirname))
	{
		if (!rmtree(dirname, true))
		{
			log_error("Failed to remove directory \"%s\": %s",
					  dirname, strerror(errno));
			return false;
		}
	}
	else
	{
		/*
		 * reset errno, we don't care anymore that it failed because dirname
		 * doesn't exists.
		 */
		errno = 0;
	}

	if (pg_mkdir_p(dirname_copy, mode) == -1)
	{
		log_error("Failed to ensure empty directory \"%s\": %s",
				  dirname, strerror(errno));
		return false;
	}

	return true;
}


/*
 * write_file writes the given data to the file given by filePath using
 * our logging library to report errors. If succesful, the function returns
 * true.
 */
bool
write_file(char *data, long fileSize, const char *filePath)
{
	FILE *fileStream = NULL;

	fileStream = fopen(filePath, "wb");
	if (fileStream == NULL)
	{
		log_error("Failed to open file \"%s\": %s", filePath, strerror(errno));
		return false;
	}

	if (fwrite(data, sizeof(char), fileSize, fileStream) < fileSize)
	{
		log_error("Failed to write file \"%s\": %s", filePath, strerror(errno));
		fclose(fileStream);
		return false;
	}

	if (fclose(fileStream) == EOF)
	{
		log_error("Failed to write file \"%s\"", filePath);
		return false;
	}

	return true;
}


/*
 * append_to_file writes the given data to the end of the file given by
 * filePath using our logging library to report errors. If succesful, the
 * function returns true.
 */
bool
append_to_file(char *data, long fileSize, const char *filePath)
{
	FILE *fileStream = NULL;

	fileStream = fopen(filePath, "ab");
	if (fileStream == NULL)
	{
		log_error("Failed to open file \"%s\": %s", filePath, strerror(errno));
		return false;
	}

	if (fwrite(data, sizeof(char), fileSize, fileStream) < fileSize)
	{
		log_error("Failed to write file \"%s\": %s", filePath, strerror(errno));
		fclose(fileStream);
		return false;
	}

	if (fclose(fileStream) == EOF)
	{
		log_error("Failed to write file \"%s\"", filePath);
		return false;
	}

	return true;
}


/*
 * read_file is a utility function that reads the contents of a file using our
 * logging library to report errors.
 *
 * If successful, the function returns true and fileSize points to the number
 * of bytes that were read and contents points to a buffer containing the entire
 * contents of the file. This buffer should be freed by the caller.
 */
bool
read_file(const char *filePath, char **contents, long *fileSize)
{
	char *data = NULL;
	FILE *fileStream = NULL;

	/* open a file */
	fileStream = fopen(filePath, "rb");
	if (fileStream == NULL)
	{
		log_error("Failed to open file \"%s\": %s", filePath, strerror(errno));
		return false;
	}

	/* get the file size */
	if (fseek(fileStream, 0, SEEK_END) != 0)
	{
		log_error("Failed to read file \"%s\": %s", filePath, strerror(errno));
		fclose(fileStream);
		return false;
	}

	*fileSize = ftell(fileStream);
	if (*fileSize < 0)
	{
		log_error("Failed to read file \"%s\": %s", filePath, strerror(errno));
		fclose(fileStream);
		return false;
	}

	if (fseek(fileStream, 0, SEEK_SET) != 0)
	{
		log_error("Failed to read file \"%s\": %s", filePath, strerror(errno));
		fclose(fileStream);
		return false;
	}

	/* read the contents */
	data = malloc(*fileSize + 1);
	if (data == NULL)
	{
		log_error("Failed to allocate %ld bytes", *fileSize);
		fclose(fileStream);
		return false;
	}

	if (fread(data, sizeof(char), *fileSize, fileStream) < *fileSize)
	{
		log_error("Failed to read file \"%s\": %s", filePath, strerror(errno));
		fclose(fileStream);
		free(data);
		return false;
	}

	if (fclose(fileStream) == EOF)
	{
		log_error("Failed to read file \"%s\"", filePath);
		free(data);
		return false;
	}

	data[*fileSize] = '\0';
	*contents = data;

	return true;
}


/*
 * path_in_same_directory constructs the path for a file with name fileName
 * that is in the same directory as basePath, which should be an absolute
 * path. The result is written to destinationPath, which should be at least
 * MAXPATH in size.
 */
void
path_in_same_directory(const char *basePath, const char *fileName,
					   char *destinationPath)
{
	strlcpy(destinationPath, basePath, MAXPGPATH);
	get_parent_directory(destinationPath);
	join_path_components(destinationPath, destinationPath, fileName);
}


/* From PostgreSQL sources at src/port/path.c */
#ifndef WIN32
#define IS_PATH_VAR_SEP(ch) ((ch) == ':')
#else
#define IS_PATH_VAR_SEP(ch) ((ch) == ';')
#endif


/*
 * Searches all the directories in the ':' separated pathlist for
 * the given filename. Returns number of occurrences, and allocates
 * and returns the path for each of the occurrences in the given result
 * pointer.
 *
 * If the result size is 0, then *result is set to NULL.
 *
 * The caller should free the result by calling search_pathlist_destroy_result().
 */
int
search_pathlist(const char *pathlist, const char *filename, char ***result)
{
	char *stringSpace = NULL;
	char *path = NULL;
	int pathListLength = 0;
	int resultSize = 0;
	int pathIndex = 0;

	/* Create a copy of pathlist, because we modify it here. */
	char *pathlist_copy = strdup(pathlist);

	for (pathIndex = 0; pathlist[pathIndex] != '\0'; pathIndex++)
	{
		if (IS_PATH_VAR_SEP(pathlist[pathIndex]))
		{
			pathListLength++;
		}
	}

	if (pathListLength == 0)
	{
		*result = NULL;
		free(pathlist_copy);
		return 0;
	}

	/* allocate array of pointers */
	*result = malloc(pathListLength * sizeof(char *));

	/* allocate memory to store the strings */
	stringSpace = malloc(pathListLength * MAXPGPATH);

	path = pathlist_copy;

	while (path != NULL)
	{
		char candidate[MAXPGPATH];
		char *sep = first_path_var_separator(path);

		/* split path on current token, null-terminating string at separator */
		if (sep != NULL)
		{
			*sep = '\0';
		}

		join_path_components(candidate, path, filename);
		canonicalize_path(candidate);

		if (file_exists(candidate))
		{
			char *destinationString = stringSpace + resultSize * MAXPGPATH;
			bool duplicated = false;
			strlcpy(destinationString, candidate, MAXPGPATH);

			for (int i=0; i<resultSize; i++)
			{
				if (strcmp((*result)[i], destinationString) == 0)
				{
					duplicated = true;
					break;
				}
			}

			if (!duplicated)
			{
				(*result)[resultSize] = destinationString;
				resultSize++;
			}
		}

		path = (sep == NULL ? NULL : sep + 1);
	}

	if (resultSize == 0)
	{
		/* we won't return a string to the caller, free it now */
		free(stringSpace);

		free(*result);
		*result = NULL;
	}

	free(pathlist_copy);

	return resultSize;
}

/*
 * Frees the space allocated by search_pathlist().
 */
void
search_pathlist_destroy_result(char **result)
{
	if (result != NULL)
	{
		free(result[0]);
	}
	free(result);
}


/*
 * unlink_state_file calls unlink(2) on the state file to make sure we don't
 * leave a lingering state on-disk.
 */
bool
unlink_file(const char *filename)
{
	if (unlink(filename) == -1)
	{
		/* if it didn't exist yet, good news! */
		if (errno != ENOENT && errno != ENOTDIR)
		{
			log_error("Failed to remove stale state file at \"%s\"", filename);
			return false;
		}
	}

	return true;
}
