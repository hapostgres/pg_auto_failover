# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

all: monitor bin ;
clean: clean-monitor clean-bin ;
install: install-monitor install-bin ;

monitor:
	$(MAKE) -C monitor all

install-monitor:
	$(MAKE) -C monitor install

clean-monitor:
	$(MAKE) -C monitor clean

bin:
	$(MAKE) -C bin all

install-bin:
	$(MAKE) -C bin install

clean-bin:
	$(MAKE) -C bin clean


.PHONY: all clean bin test monitor
.PHONY: clean-monitor clean-bin install-monitor install-bin
