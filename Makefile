# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

.DEFAULT_GOAL := all

# Supported PostgreSQL versions:
PGVERSIONS = 13 14 15 16

# Default version:
PGVERSION ?= $(lastword $(PGVERSIONS))

# PostgreSQL cluster option
# could be "--skip-pg-hba"
CLUSTER_OPTS = ""

# XXXX This should be in Makefile.citus only
# but requires to clean up dockerfile and make targets related to citus first.
# Default Citus Data version
CITUSTAG ?= v13.0.0

# TODO should be abs_top_dir ?
TOP := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Get pg_auto_failover version from header file
VERSION_FILE = src/bin/pg_autoctl/git-version.h
ifeq ("$(wildcard $(VERSION_FILE))","")
DUMMY := $(shell git update-index -q --refresh)
GIT_DIRTY := $(shell test -z "`git diff-index --name-only HEAD --`" || echo "-dirty")
GIT_VVERSION := $(shell git describe --match "v[0-9]*" HEAD 2>/dev/null)
GIT_DVERSION := $(shell echo $(GIT_VVERSION) | awk -Fv '{print $$2"$(GIT_DIRTY)"}')
GIT_VERSION := $(shell echo $(GIT_DVERSION) | sed -e 's/-/./g')
else
GIT_VERSION := $(shell awk -F '[ "]' '{print $$4}' $(VERSION_FILE))
endif

# Azure only targets and variables are in a separate Makefile
include Makefile.azure

#
# LIST TESTS
#
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

# TEST indicates the testfile to run
# Included Makefile may define TEST_ARGUMENT (like for citus)
TEST ?=
ifeq ($(TEST),)
	TEST_ARGUMENT = --where=tests
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

#
# Main make targets
#
.PHONY: all
all: monitor bin ;

.PHONY: bin
bin: version
	$(MAKE) -C src/bin/ all

.PHONY: check-monitor
check: check-monitor ;

.PHONY: check-monitor
check-monitor: install-monitor
	$(MAKE) -C src/monitor/ installcheck

.PHONY: clean
clean: clean-monitor clean-bin ;

.PHONY: clean-bin
clean-bin:
	$(MAKE) -C src/bin/ clean

.PHONY: clean-monitor
clean-monitor:
	$(MAKE) -C src/monitor/ clean

.PHONY: clean-version
clean-version:
	rm -f $(VERSION_FILE)

.PHONY: install
install: install-monitor install-bin ;

.PHONY: install-bin
install-bin: bin
	$(MAKE) -C src/bin/ install

.PHONY: install-monitor
install-monitor: monitor
	$(MAKE) -C src/monitor/ install

.PHONY: maintainer-clean
maintainer-clean: clean-monitor clean-version clean-bin ;

.PHONY: monitor
monitor:
	$(MAKE) -C src/monitor/ all

.PHONY: version
version: $(VERSION_FILE) ;

$(VERSION_FILE):
	@echo "#define GIT_VERSION \""$(GIT_VERSION)"\"" > $@

#
# make ci-test; is run on the GitHub Action workflow
#
# In that environment we have a local git checkout of the code, and docker
# is available too. We run our tests in docker, except for the code linting
# parts which requires full access to the git repository, so linter tooling
# is installed directly on the CI vm.
#
.PHONY: ci-test
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
.PHONY: test
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
# INDENT/LINT/SPELLCHECK
#
# make indent; edits the code when necessary
.PHONY: indent
indent:
	citus_indent
	black .

# make lint; is an alias for make spellcheck
# make linting; is an alias for make spellcheck
.PHONY: lint linting
lint linting: spellcheck ;

# make spellcheck; runs our linting tools without editing the code, only
# reports compliance with the rules.
.PHONY: spellcheck
spellcheck:
	citus_indent --check
	black --check .
	ci/banned.h.sh

#
# DOCS
#
# Documentation and images
FSM = docs/fsm.png
PDF = ./docs/_build/latex/pg_auto_failover.pdf

# make serve-docs uses this port on localhost to expose the web server
DOCS_PORT = 8000

.PHONY: man
man:
	$(MAKE) -C docs man

.PHONY: pdf
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

.PHONY: docs
docs: $(FSM) tikz
	$(MAKE) -C docs html

.PHONY: build-docs
build-docs:
	docker build -t pg_auto_failover:docs -f Dockerfile.docs .

.PHONY: serve-docs
serve-docs: build-docs
	docker run --rm -it -p $(DOCS_PORT):8000 pg_auto_failover:docs

.PHONY: tikz
tikz:
	$(MAKE) -C docs/tikz all

#
# DOCKER
#

CONTAINER_NAME = pg_auto_failover
BUILD_CONTAINER_NAME = pg_auto_failover_build
TEST_CONTAINER_NAME = pg_auto_failover_test
DOCKER_RUN_OPTS = --privileged --rm

#
# Include Citus only for testing purpose
#
ifeq ($(TEST),citus)
  CITUS=1
endif

ifeq ($(CITUS),1)
include Makefile.citus
endif

# We use pg not PG in uppercase in the var name to ease implicit rules matching
BUILD_ARGS_pg13 = --build-arg PGVERSION=13 --build-arg CITUSTAG=v10.2.9
BUILD_ARGS_pg14 = --build-arg PGVERSION=14 --build-arg CITUSTAG=$(CITUSTAG)
BUILD_ARGS_pg15 = --build-arg PGVERSION=15 --build-arg CITUSTAG=$(CITUSTAG)
BUILD_ARGS_pg16 = --build-arg PGVERSION=16 --build-arg CITUSTAG=$(CITUSTAG)

# DOCKER BUILDS

BUILD_TARGETS       = $(patsubst %,build-pg%,$(PGVERSIONS))
.PHONY: build
build: $(BUILD_TARGETS) ;

.PHONY: build-demo
build-demo:
	docker build $(BUILD_ARGS_pg$(PGVERSION)) -t citusdata/pg_auto_failover:demo .

.PHONY: build-i386
build-i386: Dockerfile.i386
	docker build -t i386:latest -f Dockerfile.i386 .

.PHONY: build-image
build-image:
	docker build --target build -t $(BUILD_CONTAINER_NAME) .

.PHONY: $(BUILD_TARGETS)
$(BUILD_TARGETS): version
	docker build \
	  $(BUILD_ARGS_$(subst build-,,$@)) \
	  -t $(CONTAINER_NAME):$(subst build-,,$@) .

# DOCKER TESTS & CHECKS

BUILD_CHECK_TARGETS = $(patsubst %,build-check-pg%,$(PGVERSIONS))
BUILD_TEST_TARGETS  = $(patsubst %,build-test-pg%,$(PGVERSIONS))

.PHONY: build-check
build-check: $(BUILD_CHECK_TARGETS)

.PHONY: build-check
build-test: $(BUILD_TEST_TARGETS)

.PHONY: build-test-image
build-test-image: build-test-pg$(PGVERSION) ;

.PHONY: $(BUILD_TEST_TARGETS)
$(BUILD_TEST_TARGETS): version
	docker build \
	  $(BUILD_ARGS_$(subst build-test-,,$@)) \
	  --target test \
	  -t $(TEST_CONTAINER_NAME):$(subst build-test-,,$@) .

.SECONDEXPANSION:
.PHONY: $(BUILD_CHECK_TARGETS)
$(BUILD_CHECK_TARGETS): version $$(subst build-check-,build-test-,$$@)
	docker run --rm \
	  -t $(TEST_CONTAINER_NAME):$(subst build-check-,,$@) \
	  pg_autoctl version --json | jq ".pg_version" | xargs echo $(subst build-check-,,$@):

# make run-test; is the main testing entry point used to run tests inside
# our testing Docker container. The docker container depends on PGVERSION.
.PHONY: run-test
run-test: build-test-pg$(PGVERSION)
	docker run					                \
		--name $(TEST_CONTAINER_NAME)		    \
		$(DOCKER_RUN_OPTS)			            \
		$(TEST_CONTAINER_NAME):pg$(PGVERSION)   \
		make -C /usr/src/pg_auto_failover test	\
		PGVERSION=$(PGVERSION) TEST='${TEST}'

# expected to be run from within the i386 docker container
.PHONY: installcheck-i386
installcheck-i386:
	pg_autoctl run &
	pg_autoctl do pgsetup wait
	$(MAKE) -C src/monitor installcheck

.PHONY: run-installcheck-i386
run-installcheck-i386: build-i386
	docker run --platform linux/386 --rm -it --privileged i386 make installcheck-i386

#
# BE INTERACTIVE
#

FIRST_PGPORT ?= 5500

TMUX_EXTRA_COMMANDS ?= ""
TMUX_LAYOUT ?= even-vertical	# could be "tiled"
TMUX_TOP_DIR = ./tmux/pgsql
TMUX_SCRIPT = ./tmux/script-$(FIRST_PGPORT).tmux
TMUX_CITUS = ""

# PostgreSQL testing
## total count of Postgres nodes
NODES ?= 2
## count of replication-quorum false nodes
NODES_ASYNC ?= 0
## either "50", or "50,50", or "50,50,0" etc
NODES_PRIOS ?= 50
## TODO ???
NODES_SYNC_SB ?= -1

.PHONY: interactive-test
interactive-test:
	docker run --name $(CONTAINER_NAME) --rm -ti $(CONTAINER_NAME)

.PHONY: $(TMUX_SCRIPT)
$(TMUX_SCRIPT): bin
	mkdir -p $(TMUX_TOP_DIR)
	$(PG_AUTOCTL) do tmux script          \
         --root $(TMUX_TOP_DIR)           \
         --first-pgport $(FIRST_PGPORT)   \
         --nodes $(NODES)                 \
         --async-nodes $(NODES_ASYNC)     \
         --node-priorities $(NODES_PRIOS) \
         --sync-standbys $(NODES_SYNC_SB) \
         $(TMUX_CITUS)                    \
         $(CLUSTER_OPTS)                  \
         --binpath $(BINPATH)             \
		 --layout $(TMUX_LAYOUT) > $@

.PHONY: tmux-script
tmux-script: $(TMUX_SCRIPT) ;

.PHONY: tmux-clean
tmux-clean: bin
	$(PG_AUTOCTL) do tmux clean           \
         --root $(TMUX_TOP_DIR)           \
         --first-pgport $(FIRST_PGPORT)   \
         --nodes $(NODES)                 \
         $(TMUX_CITUS)                    \
         $(CLUSTER_OPTS)

.PHONY: tmux-session
tmux-session: bin
	$(PG_AUTOCTL) do tmux session         \
         --root $(TMUX_TOP_DIR)           \
         --first-pgport $(FIRST_PGPORT)   \
         --nodes $(NODES)                 \
         --async-nodes $(NODES_ASYNC)     \
         --node-priorities $(NODES_PRIOS) \
         --sync-standbys $(NODES_SYNC_SB) \
         $(TMUX_CITUS)                    \
         $(CLUSTER_OPTS)                  \
         --binpath $(BINPATH)             \
         --layout $(TMUX_LAYOUT)

.PHONY: tmux-compose-session
tmux-compose-session:
	$(PG_AUTOCTL) do tmux compose session \
         --root $(TMUX_TOP_DIR)           \
         --first-pgport $(FIRST_PGPORT)   \
         --nodes $(NODES)                 \
         --async-nodes $(NODES_ASYNC)     \
         --node-priorities $(NODES_PRIOS) \
         --sync-standbys $(NODES_SYNC_SB) \
         $(TMUX_CITUS)                    \
         $(CLUSTER_OPTS)                  \
         --binpath $(BINPATH)             \
         --layout $(TMUX_LAYOUT)

.PHONY: cluster
cluster: install tmux-clean
	# This is explicitly not a target, otherwise when make uses multiple jobs
	# tmux-clean and tmux-session can have a race condidition where tmux-clean
	# removes the files that are just created by tmux-session.
	$(MAKE) tmux-session

.PHONY: compose
compose:
	$(MAKE) tmux-compose-session

# Command line with DEBUG facilities
VALGRIND ?=
ifeq ($(VALGRIND),)
	BINPATH = ./src/bin/pg_autoctl/pg_autoctl
	PG_AUTOCTL = PG_AUTOCTL_DEBUG=1 ./src/bin/pg_autoctl/pg_autoctl
else
	BINPATH = $(abspath $(TOP))/src/tools/pg_autoctl.valgrind
	PG_AUTOCTL = PG_AUTOCTL_DEBUG=1 PG_AUTOCTL_DEBUG_BIN_PATH="$(BINPATH)" ./src/tools/pg_autoctl.valgrind
endif

VALGRIND_SESSION_TARGETS  = $(patsubst %,valgrind-session-pg%,$(PGVERSIONS))

.SECONDEXPANSION:
.PHONY: $(VALGRIND_SESSION_TARGETS)
$(VALGRIND_SESSION_TARGETS): version $$(subst valgrind-session-,build-test-,$$@)
	docker run
	  --name $(TEST_CONTAINER_NAME)                \
	  $(DOCKER_RUN_OPTS) -it                       \
	  $(TEST_CONTAINER_NAME):$(subst valgrind-session-,,$@)        \
	  make -C /usr/src/pg_auto_failover            \
	    VALGRIND=1                                 \
	    TMUX_TOP_DIR=/tmp/tmux                     \
	    NODES=$(NODES)                             \
	    NODES_ASYNC=$(NODES_ASYNC)                 \
	    NODES_PRIOS=$(NODES_PRIOS)                 \
	    NODES_SYNC_SB=$(NODES_SYNC_SB)             \
	    CLUSTER_OPTS=$(CLUSTER_OPTS)               \
	    TMUX_EXTRA_COMMANDS=$(TMUX_EXTRA_COMMANDS) \
	    TMUX_LAYOUT=$(TMUX_LAYOUT)                 \
	    tmux-session

.PHONY: valgrind-session
valgrind-session: valgrind-session-pg$(PGVERSION)
