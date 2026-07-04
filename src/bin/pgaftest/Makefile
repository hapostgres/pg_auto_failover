# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

PGAFTEST = ./pgaftest

SRC_DIR    := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
PGACTL_DIR := $(SRC_DIR)../pg_autoctl/

# Must be set before include so that targets in Makefile.common don't
# become the default goal when included before the all: rule below.
.DEFAULT_GOAL := all

include $(SRC_DIR)../common/Makefile.common

# Include both our own dir and pg_autoctl for shared headers
override CFLAGS += -I$(SRC_DIR) -I$(PGACTL_DIR)

# -----------------------------------------------------------------------
# Sources that live in this directory
# -----------------------------------------------------------------------
LOCAL_SRC = main.c cli_root.c cli_demo.c cli_indent.c \
            compose_gen.c test_runner.c \
            test_spec_scan.c test_spec_parse.c

LOCAL_OBJS = $(patsubst %.c,%.o,$(LOCAL_SRC))

# -----------------------------------------------------------------------
# pg_autoctl-specific shared sources (business logic; not in common/).
# Compiled as shared-%.o to distinguish from local objects.
# -----------------------------------------------------------------------
SHARED_SRCS = cli_common.c config.c coordinator.c fsm.c fsm_transition.c \
              fsm_transition_citus.c keeper.c keeper_config.c keeper_pg_init.c \
              monitor.c monitor_config.c monitor_pg_init.c \
              nodespec.c nodestate_utils.c pghba.c primary_standby.c \
              service_keeper.c service_keeper_init.c service_monitor.c \
              service_monitor_init.c service_postgres.c service_postgres_ctl.c \
              state.c supervisor.c systemd_config.c

SHARED_OBJS = $(patsubst %.c,shared-%.o,$(SHARED_SRCS))

OBJS  = $(LOCAL_OBJS) $(SHARED_OBJS)
OBJS += lib-log.o lib-commandline.o lib-parson.o lib-snprintf.o lib-strerror.o
OBJS += $(COMMON_LIB)

INCLUDES = $(wildcard $(SRC_DIR)*.h)

# -----------------------------------------------------------------------
# Flex / Bison
# -----------------------------------------------------------------------
FLEX  = flex
BISON = bison

test_spec_scan.c: test_spec_scan.l test_spec_parse.h
	$(FLEX) -o $@ $<

test_spec_parse.c test_spec_parse.h: test_spec_parse.y
	$(BISON) -d -o test_spec_parse.c $<

# cli_indent.c includes test_spec_parse.h (bison-generated); ensure it's
# built first so a parallel clean build doesn't race on the generated header.
cli_indent.o: test_spec_parse.h

# Bison/flex generated files have unavoidable unused tables and helper
# functions that vary by generator version.  Suppress those warnings so
# that pg_config --cflags -Werror does not break the build.
GENERATED_CFLAGS = $(CFLAGS) \
    -Wno-unused-variable \
    -Wno-unused-function \
    -Wno-unused-const-variable \
    -Wno-return-type \
    -Wno-error

test_spec_parse.o: test_spec_parse.c
	@if test ! -d $(DEPDIR); then mkdir -p $(DEPDIR); fi
	$(CC) $(GENERATED_CFLAGS) -c -MMD -MP -MF$(DEPDIR)/test_spec_parse.Po -o $@ $<

test_spec_scan.o: test_spec_scan.c
	@if test ! -d $(DEPDIR); then mkdir -p $(DEPDIR); fi
	$(CC) $(GENERATED_CFLAGS) -c -MMD -MP -MF$(DEPDIR)/test_spec_scan.Po -o $@ $<

# -----------------------------------------------------------------------
# Rules for shared objects (compiled from pg_autoctl/ sources)
# -----------------------------------------------------------------------
shared-%.o: $(PGACTL_DIR)%.c
	@if test ! -d $(DEPDIR); then mkdir -p $(DEPDIR); fi
	$(CC) $(CFLAGS) -c -MMD -MP -MF$(DEPDIR)/shared-$(*F).Po -o $@ $<

Po_files := $(wildcard $(DEPDIR)/*.Po)
ifneq (,$(Po_files))
include $(Po_files)
endif

# -----------------------------------------------------------------------
all: $(COMMON_LIB) $(PGAFTEST) ;

$(PGAFTEST): $(OBJS) $(INCLUDES)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -f $(OBJS) $(PGAFTEST)
	rm -f $(COMMON_LIB) $(COMMON_OBJ)
	rm -rf $(DEPDIR)

# Remove generated parser/scanner sources only when explicitly requested.
# The committed .c files allow building without bison/flex installed and
# without risking version-skew in Docker.
distclean: clean
	rm -f test_spec_scan.c test_spec_parse.c test_spec_parse.h

install: $(PGAFTEST)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(PGAFTEST) $(DESTDIR)$(BINDIR)

.PHONY: all clean install
