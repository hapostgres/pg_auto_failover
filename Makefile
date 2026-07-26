# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

.DEFAULT_GOAL := all

include Makefile.docker

#
# Main build targets
#
.PHONY: all
all: monitor bin ;

.PHONY: bin
bin: version
	$(MAKE) -C src/bin/ all

.PHONY: monitor
monitor:
	$(MAKE) -C src/monitor/ all

.PHONY: install
install: install-monitor install-bin ;

.PHONY: install-bin
install-bin: bin
	$(MAKE) -C src/bin/ install

.PHONY: install-monitor
install-monitor: monitor
	$(MAKE) -C src/monitor/ install

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

.PHONY: maintainer-clean
maintainer-clean: clean-monitor clean-version clean-bin ;

#
# SQL regression tests (pg_regress installcheck)
#
.PHONY: check check-monitor
check: check-monitor ;
check-monitor: install-monitor
	$(MAKE) -C src/monitor/ installcheck

# Run installcheck inside a Docker container using the base image.
# pg_virtualenv manages the temporary cluster; see Makefile.installcheck.
# Usage: make installcheck [PGVERSION=17]
.PHONY: installcheck
installcheck: version
	docker run --rm \
	  -v "$(CURDIR):/usr/src/pg_auto_failover" \
	  -w /usr/src/pg_auto_failover \
	  $(BASE) \
	  make -f Makefile.installcheck installcheck PGVERSION=$(PGVERSION)

#
# Python test suite — delegate to tests/Makefile
#
.PHONY: test ci-test run-test run-test-prebuilt
test ci-test run-test run-test-prebuilt:
	$(MAKE) -C tests $@ PGVERSION=$(PGVERSION) TEST='$(TEST)'

#
# INDENT/LINT/SPELLCHECK
#
# citus_indent is run via its official Docker image (citus/stylechecker:no-py)
# so that the local version exactly matches CI.  When the local citus_indent
# binary is already in PATH (e.g. inside the stylechecker container) the plain
# binary is used instead, which is equally authoritative there.
#
# To check or auto-fix locally without installing citus_indent:
#   make docker-check    # check only
#   make docker-indent   # auto-fix
CITUS_INDENT_DOCKER = docker run --rm \
	-v "$(CURDIR):/workdir" \
	-w /workdir \
	citus/stylechecker:no-py \
	citus_indent

.PHONY: indent
indent:
	citus_indent
	black --exclude=ci/tools .

.PHONY: docker-indent docker-check
docker-indent:
	$(CITUS_INDENT_DOCKER)
docker-check:
	$(CITUS_INDENT_DOCKER) --check

.PHONY: lint linting spellcheck
lint linting: spellcheck ;
spellcheck:
	$(CITUS_INDENT_DOCKER) --check
	black --exclude=ci/tools --check .
	ci/banned.h.sh

#
# DOCS
#
FSM      = docs/fsm.png
PDF      = ./docs/_build/latex/pg_auto_failover.pdf
DOCS_PORT = 8000
PG_AUTOCTL = PG_AUTOCTL_DEBUG=1 ./src/bin/pg_autoctl/pg_autoctl

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

.PHONY: tikz
tikz:
	$(MAKE) -C docs/tikz all

.PHONY: build-docs serve-docs
build-docs:
	docker build -t pg_auto_failover:docs -f Dockerfile.docs .
serve-docs: build-docs
	docker run --rm -it -p $(DOCS_PORT):8000 pg_auto_failover:docs
