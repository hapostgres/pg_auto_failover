/*
 * src/bin/pg_walsender/tar_stream.c
 *   See tar_stream.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "pgtar.h"

#include "tar_stream.h"
#include "file_utils.h"
#include "log.h"

/* matches basebackup.c's own TAR_NUM_TERMINATION_BLOCKS */
#define TAR_NUM_TERMINATION_BLOCKS 2

#define TAR_READ_CHUNK_SIZE (64 * 1024)

typedef struct TarWalkState
{
	TarChunkCallback callback;
	void *context;
	bool ok;
} TarWalkState;


static bool
emit(TarWalkState *state, const char *data, size_t len)
{
	if (!state->ok)
	{
		return false;
	}

	if (!state->callback(state->context, data, len))
	{
		state->ok = false;
	}

	return state->ok;
}


static bool
emit_header(TarWalkState *state, const char *memberName,
			const char *linkTarget, struct stat *st)
{
	char header[TAR_BLOCK_SIZE];

	enum tarError rc = tarCreateHeader(header, memberName, linkTarget,
									   st->st_size, st->st_mode,
									   st->st_uid, st->st_gid, st->st_mtime);

	if (rc != TAR_OK)
	{
		log_error("Failed to build a tar header for \"%s\": %s", memberName,
				  rc == TAR_NAME_TOO_LONG
				  ? "file name too long for tar format"
				  : "symbolic link target too long for tar format");
		return false;
	}

	return emit(state, header, TAR_BLOCK_SIZE);
}


static bool
emit_file_contents(TarWalkState *state, const char *path, off_t size)
{
	FILE *file = fopen(path, "rb"); /* IGNORE-BANNED */

	if (file == NULL)
	{
		log_error("Failed to open \"%s\": %m", path);
		return false;
	}

	char buffer[TAR_READ_CHUNK_SIZE];
	off_t remaining = size;

	while (remaining > 0)
	{
		size_t want = (size_t) Min(remaining, (off_t) sizeof(buffer));
		size_t got = fread(buffer, 1, want, file);

		if (got == 0)
		{
			log_error("Short read on \"%s\" while building a base backup tar "
					  "stream (file changed size mid-read?)", path);
			fclose(file);
			return false;
		}

		if (!emit(state, buffer, got))
		{
			fclose(file);
			return false;
		}

		remaining -= (off_t) got;
	}

	fclose(file);

	size_t pad = tarPaddingBytesRequired((size_t) size);

	if (pad > 0)
	{
		char zeros[TAR_BLOCK_SIZE] = { 0 };

		if (!emit(state, zeros, pad))
		{
			return false;
		}
	}

	return true;
}


static bool
walk_directory(TarWalkState *state, const char *rootDir, const char *relDir)
{
	char fullDir[MAXPGPATH];

	if (relDir[0] == '\0')
	{
		strlcpy(fullDir, rootDir, sizeof(fullDir));
	}
	else
	{
		sformat(fullDir, sizeof(fullDir), "%s/%s", rootDir, relDir);
	}

	DIR *dir = opendir(fullDir);

	if (dir == NULL)
	{
		log_error("Failed to open directory \"%s\": %m", fullDir);
		return false;
	}

	struct dirent *entry;

	while (state->ok && (entry = readdir(dir)) != NULL)
	{
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
		{
			continue;
		}

		char fullPath[MAXPGPATH];
		char relPath[MAXPGPATH];

		sformat(fullPath, sizeof(fullPath), "%s/%s", fullDir, entry->d_name);

		if (relDir[0] == '\0')
		{
			strlcpy(relPath, entry->d_name, sizeof(relPath));
		}
		else
		{
			sformat(relPath, sizeof(relPath), "%s/%s", relDir, entry->d_name);
		}

		struct stat st;

		if (lstat(fullPath, &st) != 0)
		{
			log_error("Failed to stat \"%s\": %m", fullPath);
			state->ok = false;
			break;
		}

		if (S_ISLNK(st.st_mode))
		{
			char linkTarget[MAXPGPATH];
			ssize_t len = readlink(fullPath, linkTarget, sizeof(linkTarget) - 1);

			if (len < 0)
			{
				log_error("Failed to read symbolic link \"%s\": %m", fullPath);
				state->ok = false;
				break;
			}

			linkTarget[len] = '\0';

			/*
			 * A symlink to a directory (Postgres uses this for tablespace
			 * links under pg_tblspc/) is written as a directory entry with
			 * a link target, matching tarCreateHeader()'s own convention
			 * (see its S_ISDIR/linktarget handling) -- but we don't
			 * recurse through it: multi-tablespace archives are a later
			 * milestone (see this file's own header comment), a symlink
			 * here is emitted as a bare tar entry, not expanded.
			 */
			if (!emit_header(state, relPath, linkTarget, &st))
			{
				state->ok = false;
				break;
			}

			continue;
		}

		if (S_ISDIR(st.st_mode))
		{
			if (!emit_header(state, relPath, NULL, &st))
			{
				state->ok = false;
				break;
			}

			if (!walk_directory(state, rootDir, relPath))
			{
				state->ok = false;
				break;
			}

			continue;
		}

		if (!S_ISREG(st.st_mode))
		{
			/* skip anything else (sockets, fifos, device files) */
			continue;
		}

		if (!emit_header(state, relPath, NULL, &st))
		{
			state->ok = false;
			break;
		}

		if (!emit_file_contents(state, fullPath, st.st_size))
		{
			state->ok = false;
			break;
		}
	}

	closedir(dir);

	return state->ok;
}


bool
tar_stream_directory(const char *rootDir, TarChunkCallback callback, void *context)
{
	TarWalkState state = { callback, context, true };

	if (!walk_directory(&state, rootDir, ""))
	{
		return false;
	}

	char zeros[TAR_BLOCK_SIZE * TAR_NUM_TERMINATION_BLOCKS] = { 0 };

	return emit(&state, zeros, sizeof(zeros));
}
