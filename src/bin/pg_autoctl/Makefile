# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

PG_AUTOCTL = ./pg_autoctl

SRC_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

include $(SRC_DIR)../common/Makefile.common

# pg_autoctl includes its own source directory too
override CFLAGS += -I$(SRC_DIR)

INCLUDES = $(patsubst $(SRC_DIR)%.h,%.h,$(wildcard $(SRC_DIR)*.h))

SRC  = $(patsubst $(SRC_DIR)%.c,%.c,$(wildcard $(SRC_DIR)*.c))
OBJS = $(patsubst %.c,%.o,$(SRC))
OBJS += lib-log.o lib-commandline.o lib-parson.o lib-snprintf.o lib-strerror.o

# ncurses needed for pg_autoctl watch
LIBS += -lncurses

all: $(PG_AUTOCTL) ;

$(PG_AUTOCTL): $(OBJS) $(INCLUDES)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -f $(OBJS) $(PG_AUTOCTL)
	rm -rf $(DEPDIR)

install: $(PG_AUTOCTL)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(PG_AUTOCTL) $(DESTDIR)$(BINDIR)

.PHONY: all clean install
