# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

EXTENSION = pgautofailover
EXTVERSION = 1.6

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

ifdef USE_SECURITY_FLAGS
# Flags taken from: https://liquid.microsoft.com/Web/Object/Read/ms.security/Requirements/Microsoft.Security.SystemsADM.10203#guide
SECURITY_CFLAGS=-fstack-protector-strong -D_FORTIFY_SOURCE=2 -O2 -z noexecstack -fpic -shared -Wl,-z,relro -Wl,-z,now -Wformat -Wformat-security -Werror=format-security
DEFAULT_CFLAGS += $(SECURITY_CFLAGS)
endif

override CFLAGS := $(DEFAULT_CFLAGS) $(CFLAGS)

include $(PGXS)

ifdef USE_SECURITY_FLAGS
# Flags taken from: https://liquid.microsoft.com/Web/Object/Read/ms.security/Requirements/Microsoft.Security.SystemsADM.10203#guide
SECURITY_BITCODE_CFLAGS=-fsanitize=safe-stack -fstack-protector-strong -flto -fPIC -Wformat -Wformat-security -Werror=format-security
override BITCODE_CFLAGS := $(BITCODE_CFLAGS) $(SECURITY_BITCODE_CFLAGS)
endif

cleanup-before-install:
	rm -f $(DESTDIR)$(datadir)/$(datamoduledir)/pgautofailover*

install: cleanup-before-install

$(EXTENSION)--$(EXTVERSION).sql: $(EXTENSION).sql
	cat $^ > $@
