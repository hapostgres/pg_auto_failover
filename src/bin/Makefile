# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

COMMON_LIB = common/libpgaf_common.a

all: pg_autoctl pgaftest pg_walsender ;

# Build the shared archive once, serially, before the binaries run in
# parallel.  pg_autoctl, pgaftest, and pg_walsender all include
# Makefile.common which defines compile rules for common/*.c; without this
# serialisation a parallel make -j would race to write the same .o files
# simultaneously.
$(COMMON_LIB):
	$(MAKE) -C common

pg_autoctl: $(COMMON_LIB)
	$(MAKE) -C pg_autoctl pg_autoctl

pgaftest: $(COMMON_LIB)
	$(MAKE) -C pgaftest pgaftest

pg_walsender: $(COMMON_LIB)
	$(MAKE) -C pg_walsender pg_walsender

clean:
	$(MAKE) -C common clean
	$(MAKE) -C pg_autoctl clean
	$(MAKE) -C pgaftest clean
	$(MAKE) -C pg_walsender clean

install: pg_autoctl pgaftest pg_walsender
	$(MAKE) -C pg_autoctl install
	$(MAKE) -C pgaftest install
	$(MAKE) -C pg_walsender install

.PHONY: all pg_autoctl pgaftest pg_walsender install clean
