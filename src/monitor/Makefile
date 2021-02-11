# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

EXTENSION = pgautofailover
EXTVERSION = 1.5.0.2

SRC_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

DATA_built = $(EXTENSION)--$(EXTVERSION).sql
DATA = $(EXTENSION)--1.0.sql $(patsubst ${SRC_DIR}%,%,$(wildcard ${SRC_DIR}$(EXTENSION)--*--*.sql))

# compilation configuration
MODULE_big = $(EXTENSION)
OBJS = $(patsubst ${SRC_DIR}%.c,%.o,$(wildcard ${SRC_DIR}*.c))
PG_CPPFLAGS = -std=c99 -Wall -Werror -Wno-unused-parameter -Iinclude -I$(libpq_srcdir) -g
SHLIB_LINK = $(libpq)
REGRESS = create_extension monitor workers dummy_update drop_extension upgrade

PG_CONFIG ?= pg_config
PGXS = $(shell $(PG_CONFIG) --pgxs)
USE_PGXS = 1

.PHONY: cleanup-before-install

DEFAULT_CFLAGS = -std=c99 -D_GNU_SOURCE -g
DEFAULT_CFLAGS += $(shell $(PG_CONFIG) --cflags)
DEFAULT_CFLAGS += -Wformat
DEFAULT_CFLAGS += -Wall
DEFAULT_CFLAGS += -Werror=implicit-int
DEFAULT_CFLAGS += -Werror=implicit-function-declaration
DEFAULT_CFLAGS += -Werror=return-type
DEFAULT_CFLAGS += -Wno-declaration-after-statement

# Needed for OSX
DEFAULT_CFLAGS += -Wno-missing-braces
override CFLAGS := $(DEFAULT_CFLAGS) $(CFLAGS)

include $(PGXS)

cleanup-before-install:
	rm -f $(DESTDIR)$(datadir)/$(datamoduledir)/pgautofailover*

install: cleanup-before-install

$(EXTENSION)--$(EXTVERSION).sql: $(EXTENSION).sql
	cat $^ > $@
