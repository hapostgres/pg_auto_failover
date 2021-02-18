/*
 * src/bin/pg_autoctl/archiver.h
 *    Main data structures for the pg_autoctl archiver.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef ARCHIVER_H
#define ARCHIVER_H

#include "commandline.h"
#include "archiver_config.h"
#include "log.h"
#include "monitor.h"
#include "primary_standby.h"
#include "state.h"


#define MAX_ARCHIVER_GROUP_COUNT 12
#define MAX_ARCHIVER_FORMATION_COUNT 12

typedef struct GroupArray
{
	int count;
	int array[MAX_ARCHIVER_GROUP_COUNT];
} GroupArray;


typedef struct ArchiverFormation
{
	char formation[NAMEDATALEN];
	GroupArray groups;
} ArchiverFormation;


typedef struct FormationArray
{
	int count;
	ArchiverFormation array[MAX_ARCHIVER_FORMATION_COUNT];
} FormationArray;


typedef struct Archiver
{
	ArchiverConfig config;
	ArchiverState state;

	/* formations registration */
	FormationArray formations;
} Archiver;


typedef struct CreateArchiverNodeOpts
{
	char name[_POSIX_HOST_NAME_MAX];
	char formation[NAMEDATALEN];
	int groupId;
} CreateArchiverNodeOpts;


typedef struct AddArchiverNodeOpts
{
	char name[_POSIX_HOST_NAME_MAX];
	char formation[NAMEDATALEN];
	int groupId;
} AddArchiverNodeOpts;

bool archiver_init(Archiver *keeper, ArchiverConfig *config);

#endif /* ARCHIVER_H */
