# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

PGAFTEST = ./pgaftest

SRC_DIR    := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
PGACTL_DIR := $(SRC_DIR)../pg_autoctl/

include $(SRC_DIR)../common/Makefile.common

# Include both our own dir and pg_autoctl for shared headers
override CFLAGS += -I$(SRC_DIR) -I$(PGACTL_DIR)

# -----------------------------------------------------------------------
# Sources that live in this directory
# -----------------------------------------------------------------------
LOCAL_SRC = main.c cli_root.c cli_demo.c \
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
              nodestate_utils.c pghba.c primary_standby.c \
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
	rm -f $(OBJS) $(PGAFTEST) test_spec_scan.c test_spec_parse.c test_spec_parse.h
	rm -f $(COMMON_LIB) $(COMMON_OBJ)
	rm -rf $(DEPDIR)

install: $(PGAFTEST)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(PGAFTEST) $(DESTDIR)$(BINDIR)

.PHONY: all clean install
