# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

all: pg_autoctl pgaftest ;

pg_autoctl:
	$(MAKE) -C pg_autoctl pg_autoctl

pgaftest:
	$(MAKE) -C pgaftest pgaftest

clean:
	$(MAKE) -C pg_autoctl clean
	$(MAKE) -C pgaftest clean

install: pg_autoctl pgaftest
	$(MAKE) -C pg_autoctl install
	$(MAKE) -C pgaftest install

.PHONY: all pg_autoctl pgaftest install clean
