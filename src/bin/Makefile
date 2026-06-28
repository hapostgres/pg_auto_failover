# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

all: common pg_autoctl pgaftest ;

common:
	$(MAKE) -C common

pg_autoctl: common
	$(MAKE) -C pg_autoctl pg_autoctl

pgaftest: common
	$(MAKE) -C pgaftest pgaftest

clean:
	$(MAKE) -C common clean
	$(MAKE) -C pg_autoctl clean
	$(MAKE) -C pgaftest clean

install: pg_autoctl pgaftest
	$(MAKE) -C pg_autoctl install
	$(MAKE) -C pgaftest install

.PHONY: all common pg_autoctl pgaftest install clean
