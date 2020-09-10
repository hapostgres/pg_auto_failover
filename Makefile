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
	perl -pi -e 's/(^.. figure:: .*)\.pdf/\1.svg/' docs/*.rst
	ls -l $@

$(FSM): bin
	PG_AUTOCTL_DEBUG=1 ./src/bin/pg_autoctl/pg_autoctl do fsm gv | dot -Tpng > $@

cluster: install
	killall --exact pg_autoctl || true
	rm -rf monitor node1 node2 node3 ~/.config/pg_autoctl/ ~/.local/share/pg_autoctl/ /tmp/pg_autoctl/
	tmux \
		set-option -g default-shell /bin/bash \; \
		new -s pgautofailover \; \
		send-keys 'export PGDATA=monitor' Enter \; \
		send-keys 'pg_autoctl create monitor --ssl-self-signed --auth trust --pgport 5500 --run' Enter \; \
		split-window -h \; \
		send-keys 'export PGDATA=node1' Enter \; \
		send-keys 'sleep 1' Enter \; \
		send-keys 'pg_autoctl create postgres --monitor $$(pg_autoctl show uri --pgdata monitor --monitor) --pgport 5501 --ssl-self-signed --auth trust' Enter \; \
		send-keys 'pg_autoctl run' Enter \; \
		split-window -v \; \
		send-keys 'export PGDATA=node2' Enter \; \
		send-keys 'sleep 3' Enter \; \
		send-keys 'pg_autoctl create postgres --monitor $$(pg_autoctl show uri --pgdata monitor --monitor) --pgport 5502 --ssl-self-signed --auth trust' Enter \; \
		send-keys 'pg_autoctl run' Enter \; \
		select-pane -L \; \
		split-window -v \; \
		send-keys 'watch -n 0.2 "pg_autoctl show state --pgdata monitor"' Enter
	killall --exact pg_autoctl || true

cluster3: install
	killall --exact pg_autoctl || true
	rm -rf monitor node1 node2 node3 ~/.config/pg_autoctl/ ~/.local/share/pg_autoctl/ /tmp/pg_autoctl/
	tmux \
		set-option -g default-shell /bin/bash \; \
		new -s pgautofailover \; \
		send-keys 'export PGDATA=monitor' Enter \; \
		send-keys 'pg_autoctl create monitor --ssl-self-signed --auth trust --pgport 5500 --run' Enter \; \
		split-window -h \; \
		send-keys 'export PGDATA=node1' Enter \; \
		send-keys 'sleep 1' Enter \; \
		send-keys 'pg_autoctl create postgres --monitor $$(pg_autoctl show uri --pgdata monitor --monitor) --pgport 5501 --ssl-self-signed --auth trust' Enter \; \
		send-keys 'pg_autoctl run' Enter \; \
		split-window -v \; \
		send-keys 'export PGDATA=node2' Enter \; \
		send-keys 'sleep 3' Enter \; \
		send-keys 'pg_autoctl create postgres --monitor $$(pg_autoctl show uri --pgdata monitor --monitor) --pgport 5502 --ssl-self-signed --auth trust' Enter \; \
		send-keys 'pg_autoctl run' Enter \; \
		split-window -v \; \
		send-keys 'export PGDATA=node3' Enter \; \
		send-keys 'sleep 6' Enter \; \
		send-keys 'pg_autoctl create postgres --monitor $$(pg_autoctl show uri --pgdata monitor --monitor) --pgport 5503 --ssl-self-signed --auth trust' Enter \; \
		send-keys 'pg_autoctl run' Enter \; \
		select-pane -L \; \
		split-window -v \; \
		send-keys 'watch -n 0.2 "pg_autoctl show state --pgdata monitor"' Enter
	killall --exact pg_autoctl || true

.PHONY: all clean check install docs
.PHONY: monitor clean-monitor check-monitor install-monitor
.PHONY: bin clean-bin install-bin
.PHONY: build-test run-test
