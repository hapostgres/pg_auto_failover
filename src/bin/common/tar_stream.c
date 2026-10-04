/*
 * src/bin/common/tar_stream.c
 *   See tar_stream.h.
 *
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

#define TAR_READ_CHUNK_SIZE (64 * 1024)

#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))

typedef struct TarWalkState
{
	TarChunkCallback callback;
	void *context;
	bool ok;
	const char **excludeNames;
	int excludeCount;
} TarWalkState;


/*
 * root_name_excluded returns true when name (a root-level directory entry
 * only -- callers check relDir[0] == '\0' before calling this) appears in
 * state's caller-supplied exclude list. tar_stream_directory itself has no
 * opinion on what belongs in that list; see its own header comment.
 */
static bool
root_name_excluded(TarWalkState *state, const char *name)
{
	for (int i = 0; i < state->excludeCount; i++)
	{
		if (streq(state->excludeNames[i], name))
		{
			return true;
		}
	}

	return false;
}


/*
 * emit forwards len bytes of data to state's callback, short-circuiting once
 * state->ok has gone false (a previous emit's callback failed) -- every
 * later emit() in the same walk becomes a cheap no-op instead of a error
 * needing to be checked individually at every call site.
 */
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


/*
 * emit_header builds and emits one tar header block (via tarCreateHeader())
 * for memberName, describing st (and, for a symlink, its linkTarget).
 * Returns false, having logged, on a name/link target too long for the tar
 * format or on the underlying emit() failing.
 */
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


/*
 * emit_file_contents streams a regular file's data block (the file's bytes,
 * in TAR_READ_CHUNK_SIZE pieces, plus the tar format's own zero padding up
 * to the next 512-byte boundary) into state. size is the length recorded in
 * the header emitted just before this call; a short read (the file shrank
 * mid-stream) is treated as a fatal error rather than silently short-writing
 * a tar entry whose header already promised a different length.
 */
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


/*
 * walk_directory recursively emits rootDir/relDir's contents as tar entries:
 * a directory header plus a recursive call for a subdirectory, a bare header
 * (no recursion) for a symlink, a header plus streamed contents for a
 * regular file, backup_manifest at the root skipped (see the comment at its
 * own check below), and anything else (sockets, fifos, devices) silently
 * skipped. Stops and returns false at the first error.
 */
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
		if (streq(entry->d_name, ".") || streq(entry->d_name, ".."))
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

		/*
		 * A caller-supplied root-level exclude list (state->excludeNames):
		 * tar_stream.c itself has no business logic about which files a
		 * particular caller wants left out of the stream -- see
		 * cmd_base_backup.c's own call site for why it passes
		 * "backup_manifest" here (that file sits at basebackupDir's own
		 * root, written there by the real pg_basebackup run that
		 * originally produced this on-disk backup, and describes *that*
		 * pull, not the bytes being retransmitted here; real Postgres
		 * never puts it in the main tar either, serving it as its own
		 * second CopyOut stream instead).
		 */
		if (relDir[0] == '\0' && root_name_excluded(state, entry->d_name))
		{
			continue;
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


/*
 * tar_stream_directory walks rootDir recursively and calls callback with
 * each successive chunk of the resulting tar stream (headers, file data,
 * padding). Returns false as soon as either the walk or callback fails.
 */
bool
tar_stream_directory(const char *rootDir, TarChunkCallback callback, void *context)
{
	TarWalkState state = { callback, context, true };

	return walk_directory(&state, rootDir, "");
}
