# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.
#
# pg_walsender -- the archiver's own replication-protocol server. Standalone
# binary: does NOT link any pg_autoctl/*.c, only src/bin/common/ and
# src/bin/lib/log/, so it can be exec'd and tested independently of
# pg_autoctl (see ~/dev/temp/archiving-disaster-recovery.md and
# src/bin/pg_autoctl/service_archiver.c's own "colocated fast path" note).

PG_WALSENDER = ./pg_walsender

SRC_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Must be set before include so that targets in Makefile.common don't
# become the default goal when included before the all: rule below.
.DEFAULT_GOAL := all

include $(SRC_DIR)../common/Makefile.common

override CFLAGS += -I$(SRC_DIR) -I$(SRC_DIR)vendor

# -----------------------------------------------------------------------
# Sources that live in this directory
# -----------------------------------------------------------------------
LOCAL_SRC = main.c accept_loop.c startup.c auth.c framing.c repl_command.c \
            routes.c cmd_identify_system.c cmd_show.c cmd_base_backup.c \
            tar_stream.c cmd_fetch_file.c fetch_client.c \
            cmd_timeline_history.c cmd_replication_slot.c \
            cmd_start_replication.c wal_dir_scan.c

LOCAL_OBJS = $(patsubst %.c,%.o,$(LOCAL_SRC))

# -----------------------------------------------------------------------
# Vendored PostgreSQL source (see vendor/tar.c's own header comment) --
# compiled as vendor-%.o to keep it visually distinct from this project's
# own code.
# -----------------------------------------------------------------------
VENDOR_SRC = tar.c
VENDOR_OBJS = $(patsubst %.c,vendor-%.o,$(VENDOR_SRC))

vendor-%.o: $(SRC_DIR)vendor/%.c
	@if test ! -d $(DEPDIR); then mkdir -p $(DEPDIR); fi
	$(CC) $(CFLAGS) -c -MMD -MP -MF$(DEPDIR)/vendor-$(*F).Po -o $@ $<

OBJS  = $(LOCAL_OBJS) $(VENDOR_OBJS)
OBJS += lib-log.o lib-snprintf.o lib-strerror.o
OBJS += $(COMMON_LIB)

INCLUDES = $(wildcard $(SRC_DIR)*.h)

all: $(COMMON_LIB) $(PG_WALSENDER) ;

$(PG_WALSENDER): $(OBJS) $(INCLUDES)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -f $(OBJS) $(PG_WALSENDER)
	rm -rf $(DEPDIR)

install: $(PG_WALSENDER)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(PG_WALSENDER) $(DESTDIR)$(BINDIR)

.PHONY: all clean install
