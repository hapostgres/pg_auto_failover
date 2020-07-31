# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

FSM = docs/fsm.png
TEST_CONTAINER_NAME = pg_auto_failover-test
DOCKER_RUN_OPTS = --cap-add=SYS_ADMIN --cap-add=NET_ADMIN -ti --rm
PDF = ./docs/_build/latex/pg_auto_failover.pdf

# Tests for multiple standbys
MULTI_SB_TESTS = $(basename $(notdir $(wildcard tests/test*_multi*)))

# TEST indicates the testfile to run
TEST ?=
ifeq ($(TEST),)
	TEST_ARGUMENT = --where=tests
else ifeq ($(TEST),multi)
	TEST_ARGUMENT = --where=tests --tests=$(MULTI_SB_TESTS)
else ifeq ($(TEST),single)
	TEST_ARGUMENT = --where=tests --exclude='_multi_'
else
	TEST_ARGUMENT = $(TEST:%=tests/%.py)
endif


all: monitor bin ;

install: install-monitor install-bin ;
clean: clean-monitor clean-bin ;
check: check-monitor ;

monitor:
	$(MAKE) -C src/monitor/ all

clean-monitor:
	$(MAKE) -C src/monitor/ clean

install-monitor: monitor
	$(MAKE) -C src/monitor/ install

check-monitor: install-monitor
	$(MAKE) -C src/monitor/ installcheck

bin:
	$(MAKE) -C src/bin/ all

clean-bin:
	$(MAKE) -C src/bin/ clean

install-bin: bin
	$(MAKE) -C src/bin/ install

test:
	sudo -E env "PATH=${PATH}" USER=$(shell whoami) \
		`which nosetests`			\
		--verbose				\
		--nocapture				\
		--stop					\
		${TEST_ARGUMENT}

indent:
	citus_indent

docs: $(FSM)
	$(MAKE) -C docs html

build-test:
	docker build				\
		$(DOCKER_BUILD_OPTS)		\
		-t $(TEST_CONTAINER_NAME)	\
		.


run-test: build-test
	docker run					\
		--name $(TEST_CONTAINER_NAME)		\
		$(DOCKER_RUN_OPTS)			\
		$(TEST_CONTAINER_NAME)			\
		make -C /usr/src/pg_auto_failover test	\
		TEST='${TEST}'

man:
	$(MAKE) -C docs man

pdf: $(PDF)

$(PDF):
	$(MAKE) -s -C docs/tikz pdf
	perl -pi -e 's/(^.. figure:: .*)\.svg/\1.pdf/' docs/*.rst
	$(MAKE) -s -C docs latexpdf
	perl -pi -e 's/(^.. figure:: .*)\.pdf/\1./' docs/*.rst
	ls -l $@

$(FSM): bin
	PG_AUTOCTL_DEBUG=1 ./src/bin/pg_autoctl/pg_autoctl do fsm gv | dot -Tpng > $@

.PHONY: all clean check install docs
.PHONY: monitor clean-monitor check-monitor install-monitor
.PHONY: bin clean-bin install-bin
.PHONY: build-test run-test
