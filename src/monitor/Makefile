# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

EXTENSION = pgautofailover
EXTVERSION = 2.2

SRC_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

DATA_built = $(EXTENSION)--$(EXTVERSION).sql
DATA = $(EXTENSION)--1.0.sql $(patsubst ${SRC_DIR}%,%,$(wildcard ${SRC_DIR}$(EXTENSION)--*--*.sql))

# compilation configuration
MODULE_big = $(EXTENSION)
OBJS = $(patsubst ${SRC_DIR}%.c,%.o,$(wildcard ${SRC_DIR}*.c))
PG_CPPFLAGS = -Wall -Werror -Wno-unused-parameter -Iinclude -I$(libpq_srcdir) -g
SHLIB_LINK = $(libpq)
REGRESS = create_extension monitor workers node_active_protocol guard_data_loss fast_forward drop_node stale_primary_report lock_and_fetch_migration dummy_update drop_extension upgrade
ISOLATION = concurrent_remove_node concurrent_remove_standby_and_primary_report concurrent_remove_standby_and_standby_report concurrent_second_primary_death_report concurrent_health_check_and_report concurrent_candidate_priority_and_quorum

PG_CONFIG ?= pg_config

# PG19 changed pg_lsn display format (e.g. 0/0 -> 0/00000000).
# Use version-specific expected output files when available.
PG_MAJOR_VERSION := $(shell $(PG_CONFIG) --version | sed 's/PostgreSQL \([0-9]*\).*/\1/')
# pg_regress appends /expected/<name>.out to the expecteddir path,
# so point at the parent directory that contains the expected/ subdirectory.
PG19_EXPECTED_PARENT := $(SRC_DIR)expected/pg19
ifneq ($(wildcard $(PG19_EXPECTED_PARENT)/expected),)
ifeq ($(shell test "$(PG_MAJOR_VERSION)" -ge 19 2>/dev/null && echo yes),yes)
REGRESS_OPTS += --expecteddir=$(PG19_EXPECTED_PARENT)
endif
endif
PGXS = $(shell $(PG_CONFIG) --pgxs)
USE_PGXS = 1

.PHONY: cleanup-before-install

DEFAULT_CFLAGS = -D_GNU_SOURCE -g
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

# See regress_schedule for why a custom schedule is needed instead of
# PGXS's default behavior (the whole $(REGRESS) list as one parallel group).
REGRESS_OPTS += --schedule=$(SRC_DIR)regress_schedule

# Override PGXS's installcheck: with --schedule in REGRESS_OPTS, pg_regress
# runs the schedule's groups first and then re-runs every bare $(REGRESS)
# name again, one at a time, as extra tests appended after the schedule
# (pg_regress.c's extra_tests/run_single_test()). So $(REGRESS) must not
# also be passed on the command line here -- the schedule file is the only
# source of truth for which tests run and how they're grouped.
installcheck: submake $(REGRESS_PREP)
	$(pg_regress_installcheck) $(REGRESS_OPTS)
	$(pg_isolation_regress_installcheck) $(ISOLATION_OPTS) $(ISOLATION)

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
