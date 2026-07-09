# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

all: common pg_autoctl ;

common:
	$(MAKE) -C common

pg_autoctl: common
	$(MAKE) -C pg_autoctl pg_autoctl

clean:
	$(MAKE) -C common clean
	$(MAKE) -C pg_autoctl clean

install: pg_autoctl
	$(MAKE) -C pg_autoctl install

.PHONY: all common pg_autoctl install clean
