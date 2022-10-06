# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

TOP := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

CONTAINER_NAME = pg_auto_failover
BUILD_CONTAINER_NAME = pg_auto_failover_build
TEST_CONTAINER_NAME = pg_auto_failover_test
DOCKER_RUN_OPTS = --privileged --rm

PGVERSION ?= 14

NOSETESTS = $(shell which nosetests3 || which nosetests)

# Tests for the monitor
TESTS_MONITOR  = test_extension_update
TESTS_MONITOR += test_installcheck
TESTS_MONITOR += test_monitor_disabled
TESTS_MONITOR += test_replace_monitor

# This could be in TESTS_MULTI, but adding it here optimizes Travis run time
TESTS_MONITOR += test_multi_alternate_primary_failures

# Tests for single standby
TESTS_SINGLE  = test_auth
TESTS_SINGLE += test_basic_operation
TESTS_SINGLE += test_basic_operation_listen_flag
TESTS_SINGLE += test_create_run
TESTS_SINGLE += test_create_standby_with_pgdata
TESTS_SINGLE += test_ensure
TESTS_SINGLE += test_skip_pg_hba
TESTS_SINGLE += test_config_get_set

# Tests for SSL
TESTS_SSL  = test_enable_ssl
TESTS_SSL += test_ssl_cert
TESTS_SSL += test_ssl_self_signed

# This could be in TESTS_SINGLE, but adding it here optimizes Travis run time
TESTS_SSL += test_debian_clusters

# Tests for multiple standbys
TESTS_MULTI  = test_multi_async
TESTS_MULTI += test_multi_ifdown
TESTS_MULTI += test_multi_maintenance
TESTS_MULTI += test_multi_standbys

# Tests for Citus
TESTS_CITUS  = test_basic_citus_operation
TESTS_CITUS += test_citus_cluster_name
TESTS_CITUS += test_citus_force_failover
TESTS_CITUS += test_citus_multi_standbys
TESTS_CITUS += test_nonha_citus_operation
TESTS_CITUS += test_citus_skip_pg_hba

# TEST indicates the testfile to run
TEST ?=
ifeq ($(TEST),)
	TEST_ARGUMENT = --where=tests
else ifeq ($(TEST),citus)
	TEST_ARGUMENT = --where=tests --tests=$(TESTS_CITUS)
else ifeq ($(TEST),multi)
	TEST_ARGUMENT = --where=tests --tests=$(TESTS_MULTI)
else ifeq ($(TEST),single)
	TEST_ARGUMENT = --where=tests --tests=$(TESTS_SINGLE)
else ifeq ($(TEST),monitor)
	TEST_ARGUMENT = --where=tests --tests=$(TESTS_MONITOR)
else ifeq ($(TEST),ssl)
	TEST_ARGUMENT = --where=tests --tests=$(TESTS_SSL)
else
	TEST_ARGUMENT = $(TEST:%=tests/%.py)
endif

# Documentation and images
FSM = docs/fsm.png
PDF = ./docs/_build/latex/pg_auto_failover.pdf

# Command line with DEBUG facilities
VALGRIND ?=
ifeq ($(VALGRIND),)
	BINPATH = ./src/bin/pg_autoctl/pg_autoctl
	PG_AUTOCTL = PG_AUTOCTL_DEBUG=1 ./src/bin/pg_autoctl/pg_autoctl
else
	BINPATH = $(abspath $(TOP))/src/tools/pg_autoctl.valgrind
	PG_AUTOCTL = PG_AUTOCTL_DEBUG=1 PG_AUTOCTL_DEBUG_BIN_PATH="$(BINPATH)" ./src/tools/pg_autoctl.valgrind
endif


NODES ?= 2						# total count of Postgres nodes
NODES_ASYNC ?= 0				# count of replication-quorum false nodes
NODES_PRIOS ?= 50				# either "50", or "50,50", or "50,50,0" etc
NODES_SYNC_SB ?= -1

CITUS = 0
WORKERS = 2
NODES_SECONDARY = 0
CLUSTER_OPTS = ""			# could be "--skip-pg-hba"

TMUX_EXTRA_COMMANDS ?= ""
TMUX_LAYOUT ?= even-vertical	# could be "tiled"
TMUX_TOP_DIR = ./tmux
TMUX_SCRIPT = ./tmux/script-$(FIRST_PGPORT).tmux

ifeq ($(CITUS),1)
	FIRST_PGPORT ?= 5600
	CLUSTER_OPTS += --citus
	TMUX_TOP_DIR = ./tmux/citus
else
	FIRST_PGPORT ?= 5500
	TMUX_TOP_DIR = ./tmux/pgsql
endif

# make azcluster arguments
AZURE_PREFIX ?= ha-demo-$(shell whoami)
AZURE_REGION ?= paris
AZURE_LOCATION ?= francecentral

# Pick a version of Postgres and pg_auto_failover packages to install
# in our target Azure VMs when provisionning
#
#  sudo apt-get install -q -y postgresql-13-auto-failover-1.5=1.5.2
#  postgresql-${AZ_PG_VERSION}-auto-failover-${AZ_PGAF_DEB_VERSION}=${AZ_PGAF_VERSION}
AZ_PG_VERSION ?= 13
AZ_PGAF_DEB_VERSION ?= 1.6
AZ_PGAF_DEB_REVISION ?= 1.6.4-1

export AZ_PG_VERSION
export AZ_PGAF_DEB_VERSION
export AZ_PGAF_DEB_REVISION

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

#
# make ci-test; is run on the GitHub Action workflow
#
# In that environment we have a local git checkout of the code, and docker
# is available too. We run our tests in docker, except for the code linting
# parts which requires full access to the git repository, so linter tooling
# is installed directly on the CI vm.
#
ci-test:
ifeq ($(TEST),tablespaces)
	$(MAKE) -C tests/tablespaces run-test
else ifeq ($(TEST),linting)
	$(MAKE) spellcheck
else
	$(MAKE) run-test
endif

#
# make test; is run from inside the testing Docker image.
#
test:
ifeq ($(TEST),tablespaces)
	$(MAKE) -C tests/tablespaces run-test
else ifeq ($(TEST),linting)
	$(MAKE) spellcheck
else
	sudo -E env "PATH=${PATH}" USER=$(shell whoami) \
		$(NOSETESTS)			\
		--verbose				\
		--nologcapture			\
		--nocapture				\
		--stop					\
		${TEST_ARGUMENT}
endif

#
# make indent; edits the code when necessary
#
indent:
	citus_indent
	black .

#
# make lint; is an alias for make spellcheck
# make linting; is an alias for make spellcheck
#
lint: spellcheck ;
linting: spellcheck ;

#
# make spellcheck; runs our linting tools without editing the code, only
# reports compliance with the rules.
#
spellcheck:
	citus_indent --check
	black --check .
	ci/banned.h.sh

docs: $(FSM) tikz
	$(MAKE) -C docs html

tikz:
	$(MAKE) -C docs/tikz all

interactive-test:
	docker run --name $(CONTAINER_NAME) --rm -ti $(CONTAINER_NAME)

build-image:
	docker build --target build -t $(BUILD_CONTAINER_NAME) .

# Citus 9.0 seems to be the most recent version of Citus with Postgres 10
# support, but fails to compile nowadays...
# build-test-pg10:
# 	docker build --build-arg PGVERSION=10 --build-arg CITUSTAG=v9.0.2 --target test -t $(TEST_CONTAINER_NAME):pg10 .

build-test-pg11:
	docker build --build-arg PGVERSION=11 --build-arg CITUSTAG=v9.5.10 --target test -t $(TEST_CONTAINER_NAME):pg11 .

build-test-pg12:
	docker build --build-arg PGVERSION=12 --build-arg CITUSTAG=v10.2.3 --target test -t $(TEST_CONTAINER_NAME):pg12 .

build-test-pg13:
	docker build --build-arg PGVERSION=13 --build-arg CITUSTAG=v10.2.3 --target test -t $(TEST_CONTAINER_NAME):pg13 .

build-test-pg14:
	docker build --build-arg PGVERSION=14 --build-arg CITUSTAG=v11.1.2 --target test -t $(TEST_CONTAINER_NAME):pg14 .

build-test-pg15:
	docker build --build-arg PGVERSION=15 --build-arg CITUSTAG=v11.1.2 --target test -t $(TEST_CONTAINER_NAME):pg15 .


build-test-image: build-test-pg$(PGVERSION) ;


#
# make run-test; is the main testing entry point used to run tests inside
# our testing Docker container. The docker container depends on PGVERSION.
#
run-test: build-test-pg$(PGVERSION)
	docker run					                \
		--name $(TEST_CONTAINER_NAME)		    \
		$(DOCKER_RUN_OPTS)			            \
		$(TEST_CONTAINER_NAME):pg$(PGVERSION)   \
		make -C /usr/src/pg_auto_failover test	\
		PGVERSION=$(PGVERSION) TEST='${TEST}'

# build-pg10: build-test-pg10
# 	docker build --build-arg PGVERSION=10 -t $(CONTAINER_NAME):pg10 .

build-pg11: build-test-pg11
	docker build --build-arg PGVERSION=11 -t $(CONTAINER_NAME):pg11 .

build-pg12: build-test-pg12
	docker build --build-arg PGVERSION=12 -t $(CONTAINER_NAME):pg12 .

build-pg13: build-test-pg13
	docker build --build-arg PGVERSION=13 -t $(CONTAINER_NAME):pg13 .

build-pg14: build-test-pg14
	docker build --build-arg PGVERSION=14 -t $(CONTAINER_NAME):pg14 .

build-pg15: build-test-pg15
	docker build --build-arg PGVERSION=15 -t $(CONTAINER_NAME):pg15 .

build: build-pg11 build-pg12 build-pg13 build-pg14 build-pg15 ;

build-check:
	for v in 11 12 13 14 15; do \
		docker run --rm -t pg_auto_failover_test:pg$$v pg_autoctl version --json | jq ".pg_version" | xargs echo $$v: ; \
	done

build-i386:
	docker build -t i386:latest -f Dockerfile.i386 .

build-demo:
	docker build -t citusdata/pg_auto_failover:demo .

# expected to be run from within the i386 docker container
installcheck-i386:
	pg_autoctl run &
	pg_autoctl do pgsetup wait
	$(MAKE) -C src/monitor installcheck

run-installcheck-i386: build-i386
	docker run --platform linux/386 --rm -it --privileged i386 make installcheck-i386

man:
	$(MAKE) -C docs man

pdf: $(PDF) ;

$(PDF):
	$(MAKE) -s -C docs/tikz pdf
	perl -pi -e 's/(^.. figure:: .*)\.svg/\1.pdf/' docs/*.rst
	perl -pi -e 's/▒/~/g' docs/ref/pg_autoctl_do_demo.rst
	$(MAKE) -s -C docs latexpdf
	perl -pi -e 's/(^.. figure:: .*)\.pdf/\1.svg/' docs/*.rst
	perl -pi -e 's/~/▒/g' docs/ref/pg_autoctl_do_demo.rst
	ls -l $@

$(FSM): bin
	$(PG_AUTOCTL) do fsm gv | dot -Tpng > $@

$(TMUX_SCRIPT): bin
	mkdir -p $(TMUX_TOP_DIR)
	$(PG_AUTOCTL) do tmux script          \
         --root $(TMUX_TOP_DIR)           \
         --first-pgport $(FIRST_PGPORT)   \
         --nodes $(NODES)                 \
         --async-nodes $(NODES_ASYNC)     \
         --node-priorities $(NODES_PRIOS) \
         --sync-standbys $(NODES_SYNC_SB) \
         --citus-workers $(WORKERS) \
         --citus-secondaries $(NODES_SECONDARY) \
         $(CLUSTER_OPTS)                  \
         --binpath $(BINPATH)             \
		 --layout $(TMUX_LAYOUT) > $@

tmux-script: $(TMUX_SCRIPT) ;

tmux-clean: bin
	$(PG_AUTOCTL) do tmux clean           \
         --root $(TMUX_TOP_DIR)           \
         --first-pgport $(FIRST_PGPORT)   \
         --nodes $(NODES)                 \
         --citus-workers $(WORKERS)       \
         --citus-secondaries $(NODES_SECONDARY) \
         $(CLUSTER_OPTS)

tmux-session: bin
	$(PG_AUTOCTL) do tmux session         \
         --root $(TMUX_TOP_DIR)           \
         --first-pgport $(FIRST_PGPORT)   \
         --nodes $(NODES)                 \
         --async-nodes $(NODES_ASYNC)     \
         --node-priorities $(NODES_PRIOS) \
         --sync-standbys $(NODES_SYNC_SB) \
         --citus-workers $(WORKERS)       \
         --citus-secondaries $(NODES_SECONDARY) \
         $(CLUSTER_OPTS)                  \
         --binpath $(BINPATH)             \
         --layout $(TMUX_LAYOUT)

tmux-compose-session:
	$(PG_AUTOCTL) do tmux compose session \
         --root $(TMUX_TOP_DIR)           \
         --first-pgport $(FIRST_PGPORT)   \
         --nodes $(NODES)                 \
         --async-nodes $(NODES_ASYNC)     \
         --node-priorities $(NODES_PRIOS) \
         --sync-standbys $(NODES_SYNC_SB) \
         --citus-workers $(WORKERS)       \
         --citus-secondaries $(NODES_SECONDARY) \
         $(CLUSTER_OPTS)                  \
         --binpath $(BINPATH)             \
         --layout $(TMUX_LAYOUT)

cluster: install tmux-clean
	# This is explicitly not a target, otherwise when make uses multiple jobs
	# tmux-clean and tmux-session can have a race condidition where tmux-clean
	# removes the files that are just created by tmux-session.
	$(MAKE) tmux-session

compose:
	$(MAKE) tmux-compose-session

valgrind-session: build-test
	docker run                             \
	    --name $(TEST_CONTAINER_NAME) 	   \
		$(DOCKER_RUN_OPTS)			       \
		$(TEST_CONTAINER_NAME)			   \
	    make -C /usr/src/pg_auto_failover  \
	     VALGRIND=1 					   \
	     TMUX_TOP_DIR=/tmp/tmux 	       \
		 NODES=$(NODES) 				   \
		 NODES_ASYNC=$(NODES_ASYNC)        \
		 NODES_PRIOS=$(NODES_PRIOS)        \
		 NODES_SYNC_SB=$(NODES_SYNC_SB)    \
		 CLUSTER_OPTS=$(CLUSTER_OPTS)      \
		 TMUX_EXTRA_COMMANDS=$(TMUX_EXTRA_COMMANDS) \
		 TMUX_LAYOUT=$(TMUX_LAYOUT)        \
	     tmux-session

azcluster: all
	$(PG_AUTOCTL) do azure create         \
         --prefix $(AZURE_PREFIX)         \
         --region $(AZURE_REGION)         \
         --location $(AZURE_LOCATION)     \
         --nodes $(NODES)

# make azcluster has been done before, just re-attach
az: all
	$(PG_AUTOCTL) do azure tmux session

azdrop: all
	$(PG_AUTOCTL) do azure drop

.PHONY: all clean check install docs tikz
.PHONY: monitor clean-monitor check-monitor install-monitor
.PHONY: bin clean-bin install-bin
.PHONY: build-test run-test spellcheck lint linting ci-test
.PHONY: tmux-clean cluster compose
.PHONY: azcluster azdrop az
.PHONY: build-image
.PHONY: build-test-pg10 build-test-pg11 build-test-pg142
.PHONY: build-test-pg13 build-test-pg14
.PHONY: build build-pg10 build-pg11 build-pg12 build-pg13 build-pg14
