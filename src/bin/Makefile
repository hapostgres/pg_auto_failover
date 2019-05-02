# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

all: pg_autoctl ;

pg_autoctl:
	$(MAKE) -C pg_autoctl pg_autoctl

clean:
	$(MAKE) -C pg_autoctl clean

install: $(pg_autoctl)
	$(MAKE) -C pg_autoctl install

.PHONY: all pg_autoctl install clean
