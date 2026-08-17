# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

PG_AUTOCTL = ./pg_autoctl

SRC_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Must be set before include so that targets in Makefile.common don't
# become the default goal when included before the all: rule below.
.DEFAULT_GOAL := all

include $(SRC_DIR)../common/Makefile.common

# pg_autoctl includes its own source directory too
override CFLAGS += -I$(SRC_DIR) -I$(SRC_DIR)vendor/pg_receivewal

INCLUDES = $(patsubst $(SRC_DIR)%.h,%.h,$(wildcard $(SRC_DIR)*.h))

SRC  = $(patsubst $(SRC_DIR)%.c,%.c,$(wildcard $(SRC_DIR)*.c))
OBJS = $(patsubst %.c,%.o,$(SRC))
OBJS += lib-log.o lib-commandline.o lib-parson.o lib-snprintf.o lib-strerror.o
OBJS += $(COMMON_LIB)

# -----------------------------------------------------------------------
# Vendored PostgreSQL source: an in-process pg_receivewal (see vendor/
# pg_receivewal/pg_receivewal.c's own header comment) -- compiled as
# vendor-%.o to keep it visually distinct from this project's own code,
# matching pg_walsender's own vendor/tar.c precedent.
# -----------------------------------------------------------------------
VENDOR_SRC = pg_receivewal.c receivelog.c streamutil.c walmethods.c
VENDOR_OBJS = $(patsubst %.c,vendor-%.o,$(VENDOR_SRC))

vendor-%.o: $(SRC_DIR)vendor/pg_receivewal/%.c
	@if test ! -d $(DEPDIR); then mkdir -p $(DEPDIR); fi
	$(CC) $(CFLAGS) -c -MMD -MP -MF$(DEPDIR)/vendor-$(*F).Po -o $@ $<

OBJS += $(VENDOR_OBJS)

# ncurses needed for pg_autoctl watch
LIBS += -lncurses

# vendor/pg_receivewal/ (see its own header comment) needs the same static
# libs any real frontend client of the replication protocol links against
# -- libpgfeutils for option_parse_int()/option_utils.h, libpgcommon/
# libpgport for everything receivelog.c/streamutil.c/walmethods.c build on
# top of. Scoped to pg_autoctl's own Makefile, not Makefile.common: pg_
# walsender and pgaftest don't need any of this.
LIBS += -lpgfeutils -lpgcommon -lpgport

all: $(COMMON_LIB) $(PG_AUTOCTL) ;

$(PG_AUTOCTL): $(OBJS) $(INCLUDES)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -f $(OBJS) $(PG_AUTOCTL)
	rm -f $(COMMON_LIB) $(COMMON_OBJ)
	rm -rf $(DEPDIR)

install: $(PG_AUTOCTL)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(PG_AUTOCTL) $(DESTDIR)$(BINDIR)

.PHONY: all clean install
