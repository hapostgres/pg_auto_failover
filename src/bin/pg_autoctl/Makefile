# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

PG_AUTOCTL = ./pg_autoctl

SRC_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

INCLUDES  = $(patsubst ${SRC_DIR}%.h,%.h,$(wildcard ${SRC_DIR}*.h))

SRC   = $(patsubst ${SRC_DIR}%.c,%.c,$(wildcard ${SRC_DIR}*.c))
OBJS  = $(patsubst %.c,%.o,$(SRC))
OBJS += lib-log.o lib-commandline.o lib-parson.o

PG_CONFIG ?= pg_config
BINDIR    ?= $(shell $(PG_CONFIG) --bindir)

LOG_SRC         = $(wildcard ${SRC_DIR}../lib/log/src/log.*)
COMMANDLINE_SRC = $(wildcard ${SRC_DIR}../lib/subcommands.c/commandline.*)
PARSON_SRC      = $(wildcard ${SRC_DIR}../lib/parson/parson.*)

COMMON_LIBS  = -I${SRC_DIR}../lib/log/src/
COMMON_LIBS += -I${SRC_DIR}../lib/subcommands.c/
COMMON_LIBS += -I${SRC_DIR}../lib/libs/
COMMON_LIBS += -I${SRC_DIR}../lib/parson/

CC = $(shell $(PG_CONFIG) --cc)

CFLAGS = -std=c99 -D_GNU_SOURCE -g
CFLAGS += -I $(shell $(PG_CONFIG) --includedir)
CFLAGS += -I $(shell $(PG_CONFIG) --includedir-server)
CFLAGS += -I $(shell $(PG_CONFIG) --pkgincludedir)/internal
CFLAGS += -I $(shell $(PG_CONFIG) --cflags)
CFLAGS += -Wformat
CFLAGS += $(COMMON_LIBS)

LIBS  = -L $(shell $(PG_CONFIG) --pkglibdir)
LIBS += $(shell $(PG_CONFIG) --ldflags)
LIBS += $(shell $(PG_CONFIG) --libs)
LIBS += -lpq

all: $(PG_AUTOCTL) ;

$(PG_AUTOCTL): $(OBJS) $(INCLUDES)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

lib-log.o: $(LOG_SRC)
	$(CC) $(CFLAGS) -c -o $@ ${SRC_DIR}../lib/log/src/log.c

lib-commandline.o: $(COMMANDLINE_SRC)
	$(CC) $(CFLAGS) -c -o $@ ${SRC_DIR}../lib/subcommands.c/commandline.c

lib-parson.o: $(PARSON_SRC)
	$(CC) $(CFLAGS) -c -o $@ ${SRC_DIR}../lib/parson/parson.c

clean:
	rm -f $(OBJS) $(PG_AUTOCTL)

install: $(PG_AUTOCTL)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(PG_AUTOCTL) $(DESTDIR)$(BINDIR)

.PHONY: all monitor clean
