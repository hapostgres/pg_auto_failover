/*
 * src/bin/pg_autoctl/state.c
 *     Keeper state functions
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "parson.h"

#include "archiver.h"
#include "archiver_state.h"
#include "defaults.h"
#include "file_utils.h"
#include "log.h"


static bool archiver_state_is_readable(int pg_autoctl_state_version);


/*
 * archiver_state_init initializes a new state structure with default values.
 */
void
archiver_state_init(ArchiverStateData *archiverState)
{
	memset(archiverState, 0, sizeof(ArchiverStateData));

	archiverState->pg_autoctl_state_version = PG_AUTOCTL_STATE_VERSION;
	archiverState->archiverId = -1;
}


/*
 * archiver_state_read initializes our current state in-memory from disk.
 */
bool
archiver_state_read(ArchiverStateData *archiverState, const char *filename)
{
	char *content = NULL;
	long fileSize;

	log_debug("Reading current archiver state from \"%s\"", filename);

	if (!read_file(filename, &content, &fileSize))
	{
		log_error("Failed to read archiver state from file \"%s\"", filename);
		return false;
	}

	int pg_autoctl_state_version =
		((ArchiverStateData *) content)->pg_autoctl_state_version;

	if (fileSize >= sizeof(ArchiverStateData) &&
		archiver_state_is_readable(pg_autoctl_state_version))
	{
		*archiverState = *(ArchiverStateData *) content;
		free(content);
		return true;
	}

	free(content);

	/* Looks like it's a mess. */
	log_error("Archiver state file \"%s\" exists but is broken or wrong version",
			  filename);
	return false;
}


/*
 * archiver_state_is_readable returns true if we can read a state file from the
 * given version of pg_autoctl.
 */
static bool
archiver_state_is_readable(int pg_autoctl_state_version)
{
	return true || pg_autoctl_state_version == PG_AUTOCTL_STATE_VERSION;
}


/*
 * The ArchiverStateData data structure contains only direct values (int,
 * long), not a single pointer, so writing to disk is a single fwrite()
 * instruction.
 */
bool
archiver_state_write(ArchiverStateData *archiverState, const char *filename)
{
	char buffer[PG_AUTOCTL_KEEPER_STATE_FILE_SIZE];
	char tempFileName[MAXPGPATH];

	/* we're going to write our contents to archiver.state.new first */
	sformat(tempFileName, MAXPGPATH, "%s.new", filename);

	/*
	 * The archiver process might have been stopped in immediate shutdown mode
	 * (SIGQUIT) and left a stale state.new file around, or maybe another
	 * situation led to a file at tempFileName existing already. Clean-up the
	 * stage before preparing our new state file's content.
	 */
	if (!unlink_file(tempFileName))
	{
		/* errors have already been logged */
		return false;
	}

	log_debug("Writing current state to \"%s\"", tempFileName);

	/*
	 * Comment kept as is from PostgreSQL source code, function
	 * RewriteControlFile() in postgresql/src/bin/pg_resetwal/pg_resetwal.c
	 *
	 * We write out PG_CONTROL_FILE_SIZE bytes into pg_control, zero-padding
	 * the excess over sizeof(ControlFileData).  This reduces the odds of
	 * premature-EOF errors when reading pg_control.  We'll still fail when we
	 * check the contents of the file, but hopefully with a more specific
	 * error than "couldn't read pg_control".
	 */
	memset(buffer, 0, PG_AUTOCTL_KEEPER_STATE_FILE_SIZE);

	/*
	 * Explanation of IGNORE-BANNED:
	 * memcpy is safe to use here.
	 * we have a static assert that sizeof(ArchiverStateData) is always
	 * less than the buffer length PG_AUTOCTL_ARCHIVER_STATE_FILE_SIZE.
	 * also ArchiverStateData is a plain struct that does not contain
	 * any pointers in it. Necessary comment about not using pointers
	 * is added to the struct definition.
	 */
	memcpy(buffer, archiverState, sizeof(ArchiverStateData)); /* IGNORE-BANNED */

	int fd = open(tempFileName, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
	if (fd < 0)
	{
		log_fatal("Failed to create archiver state file \"%s\": %m",
				  tempFileName);
		return false;
	}

	errno = 0;
	if (write(fd, buffer, PG_AUTOCTL_KEEPER_STATE_FILE_SIZE) !=
		PG_AUTOCTL_KEEPER_STATE_FILE_SIZE)
	{
		/* if write didn't set errno, assume problem is no disk space */
		if (errno == 0)
		{
			errno = ENOSPC;
		}
		log_fatal("Failed to write archiver state file \"%s\": %m", tempFileName);
		return false;
	}

	if (fsync(fd) != 0)
	{
		log_fatal("fsync error: %m");
		return false;
	}

	if (close(fd) != 0)
	{
		log_fatal("Failed to close file \"%s\": %m", tempFileName);
		return false;
	}

	log_debug("rename \"%s\" to \"%s\"", tempFileName, filename);

	/* now remove the old state file, and replace it with the new one */
	if (rename(tempFileName, filename) != 0)
	{
		log_fatal("Failed to rename \"%s\" to \"%s\": %m",
				  tempFileName, filename);
		return false;
	}

	return true;
}


/*
 * archiver_state_create_file creates an initial state file from the given
 * archiver.
 */
bool
archiver_state_create_file(const char *filename)
{
	ArchiverStateData archiverState;

	archiver_state_init(&archiverState);

	return archiver_state_write(&archiverState, filename);
}


/*
 * log_archiver_state dumps the current in memory state to the logs.
 */
void
log_archiver_state(ArchiverStateData *archiverState)
{
	log_trace("archiverState.archiverId: %d", archiverState->archiverId);
}


/*
 * print_archiver_state prints the current in-memory state of the keeper to
 * given FILE output (stdout, stderr, etc).
 */
void
print_archiver_state(ArchiverStateData *archiverState, FILE *stream)
{
	fformat(stream, "archiver id: %d\n", archiverState->archiverId);
	fflush(stream);
}


/*
 * archiverStateAsJSON prepares a JSON values containing the archiver state
 * information.
 */
bool
archiverStateAsJSON(ArchiverStateData *archiverState, JSON_Value *js)
{
	JSON_Object *jsobj = json_value_get_object(js);

	json_object_set_number(jsobj,
						   "archiverId", (double) archiverState->archiverId);

	return true;
}


/*
 * archiver_load_state loads the current state of the archiver from the
 * configured state file.
 */
bool
archiver_load_state(Archiver *archiver)
{
	ArchiverStateData *archiverState = &(archiver->state);
	ArchiverConfig *config = &(archiver->config);

	return archiver_state_read(archiverState, config->pathnames.state);
}


/*
 * archiver_store_state stores the current state of the archiver in the
 * configured state file.
 */
bool
archiver_store_state(Archiver *archiver)
{
	ArchiverStateData *archiverState = &(archiver->state);
	ArchiverConfig *config = &(archiver->config);

	return archiver_state_write(archiverState, config->pathnames.state);
}


/*
 * archiver_update_state updates the archiver state and immediately writes
 * it to disk.
 */
bool
archiver_update_state(Archiver *archiver, int archiverId)
{
	ArchiverStateData *archiverState = &(archiver->state);

	archiverState->archiverId = archiverId;

	if (!archiver_store_state(archiver))
	{
		/* archiver_state_write logs errors */
		return false;
	}

	log_archiver_state(archiverState);

	return true;
}


/*
 * archiver_state_print_from_file prints to stdout the on-disk state found at
 * given filename, either in a human formatted way, or in pretty-printed JSON.
 */
bool
archiver_state_print_from_file(const char *filename,
							   bool outputContents,
							   bool outputJSON)
{
	ArchiverStateData archiverState = { 0 };

	if (!archiver_state_read(&archiverState, filename))
	{
		/* errors have already been logged */
		return false;
	}

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();

		if (outputContents)
		{
			archiverStateAsJSON(&archiverState, js);
		}
		else
		{
			JSON_Object *jsObj = json_value_get_object(js);

			json_object_set_string(jsObj, "pathname", filename);
		}

		(void) pprint_json(js);
	}
	else
	{
		if (outputContents)
		{
			print_archiver_state(&archiverState, stdout);
		}
		else
		{
			fformat(stdout, "%s\n", filename);
		}
	}

	return true;
}
