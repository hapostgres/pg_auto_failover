# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

COMMON_LIB = common/libpgaf_common.a

all: pg_autoctl pgaftest pg_walserver ;

# Build the shared archive once, serially, before the binaries run in
# parallel.  pg_autoctl, pgaftest, and pg_walserver all include
# Makefile.common which defines compile rules for common/*.c; without this
# serialisation a parallel make -j would race to write the same .o files
# simultaneously.
$(COMMON_LIB):
	$(MAKE) -C common

pg_autoctl: $(COMMON_LIB)
	$(MAKE) -C pg_autoctl pg_autoctl

pgaftest: $(COMMON_LIB)
	$(MAKE) -C pgaftest pgaftest

pg_walserver: $(COMMON_LIB)
	$(MAKE) -C pg_walserver pg_walserver

clean:
	$(MAKE) -C common clean
	$(MAKE) -C pg_autoctl clean
	$(MAKE) -C pgaftest clean
	$(MAKE) -C pg_walserver clean

install: pg_autoctl pgaftest pg_walserver
	$(MAKE) -C pg_autoctl install
	$(MAKE) -C pgaftest install
	$(MAKE) -C pg_walserver install

.PHONY: all pg_autoctl pgaftest pg_walserver install clean
