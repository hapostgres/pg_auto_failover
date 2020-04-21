/*
 * src/bin/pg_autoctl/file_utils.c
 *   Implementations of utility functions for reading and writing files
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "postgres_fe.h"

#include "snprintf.h"

#include "cli_root.h"
#include "defaults.h"
#include "env_utils.h"
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
			log_error("Failed to check if file \"%s\" exists: %m", filename);
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
		log_error("Failed to stat \"%s\": %m\n", path);
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
			log_error("Failed to remove directory \"%s\": %m", dirname);
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
		log_error("Failed to ensure empty directory \"%s\": %m", dirname);
		return false;
	}

	return true;
}


/*
 * fopen_with_umask is a version of fopen that gives more control. The main
 * advantage of it is that it allows specifying a umask of the file. This makes
 * sure files are not accidentally created with umask 777 if the user has it
 * configured in a weird way.
 *
 * This function returns NULL when opening the file fails. So this should be
 * handled. It will log an error in this case though, so that's not necessary
 * at the callsite.
 */
FILE *
fopen_with_umask(const char *filePath, const char *modes, int flags, mode_t umask)
{
	int fileDescriptor = open(filePath, flags, umask);
	FILE *fileStream = NULL;
	if (fileDescriptor == -1)
	{
		log_error("Failed to open file \"%s\": %m", filePath);
		return NULL;
	}

	fileStream = fdopen(fileDescriptor, modes);
	if (fileStream == NULL)
	{
		log_error("Failed to open file \"%s\": %m", filePath);
		close(fileDescriptor);
	}
	return fileStream;
}


/*
 * fopen_read_only opens the file as a read only stream.
 */
FILE *
fopen_read_only(const char *filePath)
{
	/*
	 * Explanation of IGNORE-BANNED
	 * fopen is safe here because we open the file in read only mode. So no
	 * exclusive access is needed.
	 */
	return fopen(filePath, "rb"); /* IGNORE-BANNED */
}


/*
 * write_file writes the given data to the file given by filePath using
 * our logging library to report errors. If succesful, the function returns
 * true.
 */
bool
write_file(char *data, long fileSize, const char *filePath)
{
	FILE *fileStream = fopen_with_umask(filePath, "wb", FOPEN_FLAGS_W, 0644);

	if (fileStream == NULL)
	{
		/* errors have already been logged */
		return false;
	}

	if (fwrite(data, sizeof(char), fileSize, fileStream) < fileSize)
	{
		log_error("Failed to write file \"%s\": %m", filePath);
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
	FILE *fileStream = fopen_with_umask(filePath, "ab", FOPEN_FLAGS_A, 0644);

	if (fileStream == NULL)
	{
		/* errors have already been logged */
		return false;
	}

	if (fwrite(data, sizeof(char), fileSize, fileStream) < fileSize)
	{
		log_error("Failed to write file \"%s\": %m", filePath);
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
	fileStream = fopen_read_only(filePath);
	if (fileStream == NULL)
	{
		log_error("Failed to open file \"%s\": %m", filePath);
		return false;
	}

	/* get the file size */
	if (fseek(fileStream, 0, SEEK_END) != 0)
	{
		log_error("Failed to read file \"%s\": %m", filePath);
		fclose(fileStream);
		return false;
	}

	*fileSize = ftell(fileStream);
	if (*fileSize < 0)
	{
		log_error("Failed to read file \"%s\": %m", filePath);
		fclose(fileStream);
		return false;
	}

	if (fseek(fileStream, 0, SEEK_SET) != 0)
	{
		log_error("Failed to read file \"%s\": %m", filePath);
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
		log_error("Failed to read file \"%s\": %m", filePath);
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
 * destinationPath. It behaves like mv system command. First attempts to move
 * a file using rename. if it fails with EXDEV error, the function duplicates
 * the source file with owner and permission information and removes it.
 */
bool
move_file(char *sourcePath, char *destinationPath)
{
	if (strncmp(sourcePath, destinationPath, MAXPGPATH) == 0)
	{
		/* nothing to do */
		log_warn("Source and destination are the same \"%s\", nothing to move.",
				 sourcePath);
		return true;
	}

	if (!file_exists(sourcePath))
	{
		log_error("Failed to move file, source file \"%s\" does not exist.",
				  sourcePath);
		return false;
	}

	if (file_exists(destinationPath))
	{
		log_error("Failed to move file, destination file \"%s\" already exists.",
				  destinationPath);
		return false;
	}

	/* first try atomic move operation */
	if (rename(sourcePath, destinationPath) == 0)
	{
		return true;
	}

	/*
	 * rename fails with errno = EXDEV when moving file to a different file
	 * system.
	 */
	if (errno != EXDEV)
	{
		log_error("Failed to move file \"%s\" to \"%s\": %m",
				  sourcePath, destinationPath);
		return false;
	}

	if (!duplicate_file(sourcePath, destinationPath))
	{
		/* specific error is already logged */
		log_error("Canceling file move due to errors.");
		return false;
	}

	/* everything is successful we can remove the file */
	unlink_file(sourcePath);

	return true;
}


/*
 * duplicate_file is a utility function to duplicate a file from sourcePath to
 * destinationPath. It reads the contents of the source file and writes to the
 * destination file. It expects non-existing destination file and does not
 * copy over if it exists. The function returns true on successful execution.
 *
 * Note: the function reads the whole file into memory before copying out.
 */
bool
duplicate_file(char *sourcePath, char *destinationPath)
{
	char *fileContents;
	long fileSize;
	struct stat sourceFileStat;
	bool foundError = false;

	if (!read_file(sourcePath, &fileContents, &fileSize))
	{
		/* errors are logged */
		return false;
	}

	if (file_exists(destinationPath))
	{
		log_error("Failed to duplicate, destination file already exists : %s",
				  destinationPath);
		return false;
	}

	foundError = !write_file(fileContents, fileSize, destinationPath);

	free(fileContents);

	if (foundError)
	{
		/* errors are logged in write_file */
		return false;
	}

	/* set uid gid and mode */
	if (stat(sourcePath, &sourceFileStat) != 0)
	{
		log_error("Failed to get ownership and file permissions on \"%s\"",
				  sourcePath);
		foundError = true;
	}
	else
	{
		if (chown(destinationPath, sourceFileStat.st_uid, sourceFileStat.st_gid) != 0)
		{
			log_error("Failed to set user and group id on \"%s\"",
					  destinationPath);
			foundError = true;
		}
		if (chmod(destinationPath, sourceFileStat.st_mode) != 0)
		{
			log_error("Failed to set file permissions on \"%s\"",
					  destinationPath);
			foundError = true;
		}
	}

	if (foundError)
	{
		/* errors are already logged */
		unlink_file(destinationPath);
		return false;
	}

	return true;
}


/*
 * create_symbolic_link creates a symbolic link to source path.
 */
bool
create_symbolic_link(char *sourcePath, char *targetPath)
{
	if (symlink(sourcePath, targetPath) != 0)
	{
		log_error("Failed to create symbolic link to \"%s\": %m", targetPath);
		return false;
	}
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
 * search_path_first copies the first entry found in PATH to result. result
 * should be a buffer of (at least) MAXPGPATH size.
 * The function returns false and logs an error when it cannot find the command
 * in PATH.
 */
bool
search_path_first(const char *filename, char *result)
{
	char **paths = NULL;
	int n = search_path(filename, &paths);
	if (n < 1)
	{
		log_error("Failed to find %s command in your PATH", filename);
		return false;
	}
	strlcpy(result, paths[0], MAXPGPATH);
	search_path_destroy_result(paths);
	return true;
}


/*
 * Searches all the directories in the PATH environment variable for
 * the given filename. Returns number of occurrences, and allocates
 * and returns the path for each of the occurrences in the given result
 * pointer.
 *
 * If the result size is 0, then *result is set to NULL.
 *
 * The caller should free the result by calling search_path_destroy_result().
 */
int
search_path(const char *filename, char ***result)
{
	char *stringSpace = NULL;
	char *path = NULL;
	int pathListLength = 0;
	int resultSize = 0;
	int pathIndex = 0;

	/* Create a copy of pathlist, because we modify it here. */
	char pathlist[MAXPATHSIZE];
	if (!get_env_copy("PATH", pathlist, MAXPATHSIZE))
	{
		return 0;
	}


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
		return 0;
	}

	/* allocate array of pointers */
	*result = malloc(pathListLength * sizeof(char *));

	/* allocate memory to store the strings */
	stringSpace = malloc(pathListLength * MAXPGPATH);

	path = pathlist;

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

			for (int i = 0; i < resultSize; i++)
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


	return resultSize;
}


/*
 * Frees the space allocated by search_path().
 */
void
search_path_destroy_result(char **result)
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
			log_error("Failed to remove file \"%s\": %m", filename);
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
		"/proc/self/exe",       /* Linux */
		"/proc/curproc/file",   /* FreeBSD */
		"/proc/self/path/a.out" /* Solaris */
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
						  "pg_autoctl program: %m");
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

		n = search_path(pg_autoctl_argv0, &pathEntries);

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
			search_path_destroy_result(pathEntries);

			return true;
		}
	}
#endif

	return true;
}


/*
 * normalize_filename returns the real path of a given filename that belongs to
 * an existing file on-disk, resolving symlinks and pruning double-slashes and
 * other weird constructs. filename and dst are allowed to point to the same
 * adress.
 */
bool
normalize_filename(const char *filename, char *dst, int size)
{
	/* normalize the path to the configuration file, if it exists */
	if (file_exists(filename))
	{
		char realPath[PATH_MAX] = { 0 };

		if (realpath(filename, realPath) == NULL)
		{
			log_fatal("Failed to normalize file name \"%s\": %m", filename);
			return false;
		}

		if (strlcpy(dst, realPath, size) >= size)
		{
			log_fatal("Real path \"%s\" is %d bytes long, and pg_autoctl "
					  "is limited to handling paths of %d bytes long, maximum",
					  realPath, (int) strlen(realPath), size);
			return false;
		}
	}
	else
	{
		char realPath[PATH_MAX] = { 0 };

		/* protect against undefined behavior if dst overlaps with filename */
		strlcpy(realPath, filename, MAXPGPATH);
		strlcpy(dst, realPath, MAXPGPATH);
	}

	return true;
}


/*
 * fformat is a secured down version of pg_fprintf:
 *
 * Additional security checks are:
 *  - make sure stream is not null
 *  - make sure fmt is not null
 *  - rely on pg_fprintf Assert() that %s arguments are not null
 */
int
fformat(FILE *stream, const char *fmt, ...)
{
	int len;
	va_list args;

	if (stream == NULL || fmt == NULL)
	{
		log_error("BUG: fformat is called with a NULL target or format string");
		return -1;
	}

	va_start(args, fmt);
	len = pg_vfprintf(stream, fmt, args);
	va_end(args);
	return len;
}


/*
 * sformat is a secured down version of pg_snprintf
 */
int
sformat(char *str, size_t count, const char *fmt, ...)
{
	int len;
	va_list args;

	if (str == NULL || fmt == NULL)
	{
		log_error("BUG: sformat is called with a NULL target or format string");
		return -1;
	}

	va_start(args, fmt);
	len = pg_vsnprintf(str, count, fmt, args);
	va_end(args);

	if (len >= count)
	{
		log_error("BUG: sformat needs %d bytes to expend format string \"%s\", "
				  "and a target string of %lu bytes only has been given.",
				  len, fmt, count);
	}

	return len;
}
