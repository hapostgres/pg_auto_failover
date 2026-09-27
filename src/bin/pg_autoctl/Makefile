# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

PG_AUTOCTL = ./pg_autoctl

SRC_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Must be set before include so that targets in Makefile.common don't
# become the default goal when included before the all: rule below.
.DEFAULT_GOAL := all

include $(SRC_DIR)../common/Makefile.common

# pg_autoctl includes its own source directory too
override CFLAGS += -I$(SRC_DIR)

INCLUDES = $(patsubst $(SRC_DIR)%.h,%.h,$(wildcard $(SRC_DIR)*.h))

SRC  = $(patsubst $(SRC_DIR)%.c,%.c,$(wildcard $(SRC_DIR)*.c))
OBJS = $(patsubst %.c,%.o,$(SRC))
OBJS += lib-log.o lib-commandline.o lib-parson.o lib-snprintf.o lib-strerror.o
OBJS += $(COMMON_LIB)

# vendor/pg_receivewal/ (src/bin/common/vendor/pg_receivewal/, see that
# directory's own pg_receivewal.c header comment) -- an in-process
# pg_receivewal, used by the pgaf-integrated archiver's own WAL-capture
# controller (service_archiver_pgreceivewal_ctl.c). Its build rule
# (PG_RECEIVEWAL_VENDOR_OBJS) is shared with pg_walserver's own embedded
# pull capturer (capture.c, later in this same PR) via Makefile.common, so
# both binaries build the identical vendored sources rather than
# duplicating them.
OBJS += $(PG_RECEIVEWAL_VENDOR_OBJS)

# ncurses needed for pg_autoctl watch
LIBS += -lncurses

# vendor/pg_receivewal/ (see its own header comment) needs the same static
# libs any real frontend client of the replication protocol links against
# -- libpgfeutils for option_parse_int()/option_utils.h, libpgcommon/
# libpgport for everything receivelog.c/streamutil.c/walmethods.c build on
# top of. pg_walserver's own Makefile adds the same libs for the same
# reason (capture.c); pgaftest links Makefile.common too but never links
# the vendored pg_receivewal itself, so it doesn't need them.
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
