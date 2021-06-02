/*
 * src/bin/pg_autoctl/archiver_state.h
 *     Archiver state data structure and function definitions
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef ARCHIVER_STATE_H
#define ARCHIVER_STATE_H

#include <assert.h>
#include "parson.h"

#include "state.h"

/*
 * Handling of an archiver state.
 */
typedef struct ArchiverStateData
{
	int pg_autoctl_state_version;
	int archiverId;
} ArchiverStateData;

_Static_assert(sizeof(ArchiverStateData) < PG_AUTOCTL_KEEPER_STATE_FILE_SIZE,
			   "Size of ArchiverStateData is larger than expected. "
			   "Please review PG_AUTOCTL_KEEPER_STATE_FILE_SIZE");

/* src/bin/pg_autoctl/archiver_state.c */
void archiver_state_init(ArchiverStateData *archiverState);
bool archiver_state_create_file(const char *filename);
bool archiver_state_read(ArchiverStateData *archiverState, const char *filename);
bool archiver_state_write(ArchiverStateData *archiverState, const char *filename);

void log_archiver_state(ArchiverStateData *archiverState);
void print_archiver_state(ArchiverStateData *archiverState, FILE *stream);
bool archiverStateAsJSON(ArchiverStateData *archiverState, JSON_Value *js);

bool archiver_state_print_from_file(const char *filename,
									bool outputContents,
									bool outputJSON);

#endif /* ARCHIVER_STATE_H */
