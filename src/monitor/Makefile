# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

EXTENSION = pgautofailover
EXTVERSION = 1.1

SRC_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

DATA_built = $(EXTENSION)--$(EXTVERSION).sql
DATA = $(EXTENSION)--1.0.sql $(wildcard $(EXTENSION)--*--*.sql)

# compilation configuration
MODULE_big = $(EXTENSION)
OBJS = $(patsubst ${SRC_DIR}%.c,%.o,$(wildcard ${SRC_DIR}*.c))
PG_CPPFLAGS = -std=c99 -Wall -Werror -Wno-unused-parameter -Iinclude -I$(libpq_srcdir) -g
SHLIB_LINK = $(libpq)
REGRESS = create_extension monitor dummy_update drop_extension upgrade

PG_CONFIG ?= pg_config
PGXS = $(shell $(PG_CONFIG) --pgxs)
USE_PGXS = 1

include $(PGXS)

$(EXTENSION)--1.1.sql: $(EXTENSION).sql
	cat $^ > $@
