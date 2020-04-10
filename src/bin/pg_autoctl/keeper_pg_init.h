/*
 * src/bin/pg_autoctl/keeper_pg_init.h
 *     Keeper initialization procedures.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef KEEPER_PG_INIT_H
#define KEEPER_PG_INIT_H

#include <stdbool.h>

#include "keeper.h"
#include "keeper_config.h"

extern bool keeperInitWarnings;

bool keeper_pg_init(Keeper *keeper);
bool keeper_pg_init_continue(Keeper *keeper);
bool keeper_pg_init_and_register(Keeper *keeper);
bool create_database_and_extension(Keeper *keeper);

#endif /* KEEPER_PG_INIT_H */
