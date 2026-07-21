# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

COMMON_LIB = common/libpgaf_common.a

all: pg_autoctl pgaftest ;

# Build the shared archive once, serially, before the two binaries run in
# parallel.  Both pg_autoctl and pgaftest include Makefile.common which
# defines compile rules for common/*.c; without this serialisation a
# parallel make -j would race to write the same .o files simultaneously.
$(COMMON_LIB):
	$(MAKE) -C common

pg_autoctl: $(COMMON_LIB)
	$(MAKE) -C pg_autoctl pg_autoctl

pgaftest: $(COMMON_LIB)
	$(MAKE) -C pgaftest pgaftest

clean:
	$(MAKE) -C common clean
	$(MAKE) -C pg_autoctl clean
	$(MAKE) -C pgaftest clean

install: pg_autoctl pgaftest
	$(MAKE) -C pg_autoctl install
	$(MAKE) -C pgaftest install

.PHONY: all pg_autoctl pgaftest install clean
