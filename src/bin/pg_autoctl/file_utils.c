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

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "postgres_fe.h"

#include "cli_root.h"
#include "defaults.h"
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
 * move_file is a utility function to move a file from sourcePath to
 * destinationPath. It behaves like mv system command. First attempts
 * to move a file using rename. if it fails with EXDEV error copies
 * the content of the file and removes the source file after setting
 * owner and permission information on the new file.
 */
bool
move_file(char* sourcePath, char* destinationPath)
{
	char *fileContents;
	long fileSize;
	struct stat sourceFileStat;
	bool foundError = false;

	if (strncmp(sourcePath, destinationPath, MAXPGPATH) == 0)
	{
		/* nothing to do */
		return true;
	}

	if (!file_exists(sourcePath))
	{
		log_error("Can not move, source file '%s' does not exist.", sourcePath);
		return false;
	}

	if (file_exists(destinationPath))
	{
		log_error("Can not move. Destination file '%s' already exists.", destinationPath);
		return false;
	}

	/* first try atomic move operation */
	if (rename(sourcePath, destinationPath) == 0)
	{
		return true;
	}

	/* rename fails with errno = EXDEV when moving file to a different file system */
	if (errno != EXDEV)
	{
		int errorCode = errno;

		log_error("File move failed with error %d", errorCode);
		return false;
	}


	if (!read_file(sourcePath, &fileContents, &fileSize))
	{
		return false;
	}

	foundError = !write_file(fileContents, fileSize, destinationPath);

	free(fileContents);

	if (foundError)
	{
		return false;
	}

	/* set uid gid and mode */
	if (stat(sourcePath, &sourceFileStat) != 0)
	{
		log_error("Unable to set ownership and file permissions");
		foundError = true;
	}
	else
	{
		if (chown(destinationPath, sourceFileStat.st_uid, sourceFileStat.st_gid) != 0)
		{
			log_error("Unable to set user and group id for file '%s'", destinationPath);
			foundError = true;
		}
		if (chmod(destinationPath, sourceFileStat.st_mode) != 0)
		{
			log_error("Unable to set file permissions for '%s'", destinationPath);
			foundError = true;
		}
	}

	if (foundError)
	{
		log_error("Canceling file move due to errors");
		unlink_file(destinationPath);
		return false;
	}

	unlink_file(sourcePath);

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


/*
 * get_program_absolute_path returns the absolute path of the current program
 * being executed. Note: the shell is responsible to set that in interactive
 * environments, and when the pg_autoctl binary is in the PATH of the user,
 * then argv[0] (here pg_autoctl_argv0) is just "pg_autoctl".
 */
bool
set_program_absolute_path(char *program, int size)
{
#if defined(__APPLE__)
	int actualSize = _NSGetExecutablePath(program, (uint32_t *) &size);

	if (actualSize != 0)
	{
		log_error("Failed to get absolute path for the pg_autoctl program, "
				  "absolute path requires %d bytes and we support paths up "
				  "to %d bytes only", actualSize, size);
		return false;
	}

	log_debug("Found absolute program: \"%s\"", program);

#else
	/*
	 * On Linux and FreeBSD and Solaris, we can find a symbolic link to our
	 * program and get the information with readlink. Of course the /proc entry
	 * to read is not the same on both systems, so we try several things here.
	 */
	bool found = false;
	char *procEntryCandidates[] = {
		"/proc/self/exe",		/* Linux */
		"/proc/curproc/file",	/* FreeBSD */
		"/proc/self/path/a.out"	/* Solaris */
	};
	int procEntrySize = sizeof(procEntryCandidates) / sizeof(char *);
	int procEntryIndex = 0;

	for (procEntryIndex = 0; procEntryIndex < procEntrySize; procEntryIndex++)
	{
		if (readlink(procEntryCandidates[procEntryIndex], program, size) != -1)
		{
			found = true;
			log_debug("Found absolute program \"%s\" in \"%s\"",
					  program,
					  procEntryCandidates[procEntryIndex]);
		}
		else
		{
			/* when the file does not exists, we try our next guess */
			if (errno != ENOENT && errno != ENOTDIR)
			{
				log_error("Failed to get absolute path for the "
						  "pg_autoctl program: %s", strerror(errno));
				return false;
			}
		}
	}

	if (found)
	{
		return true;
	}
	else
	{
		/*
		 * Now either return pg_autoctl_argv0 when that's an absolute filename,
		 * or search for it in the PATH otherwise.
		 */
		char **pathEntries = NULL;
		int n;

		if (pg_autoctl_argv0[0] == '/')
		{
			strlcpy(program, pg_autoctl_argv0, size);
			return true;
		}

		n = search_pathlist(getenv("PATH"), pg_autoctl_argv0, &pathEntries);

		if (n < 1)
		{
			log_error("Failed to find \"%s\" in PATH environment",
					  pg_autoctl_argv0);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
		else
		{
			log_debug("Found \"%s\" in PATH at \"%s\"",
					  pg_autoctl_argv0, pathEntries[0]);
			strlcpy(program, pathEntries[0], size);
			search_pathlist_destroy_result(pathEntries);

			return true;
		}
	}
#endif

	return true;
}



