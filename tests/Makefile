include ../Makefile.docker

PYTEST = $(shell which pytest || which pytest3)

# Tests for the monitor
TESTS_MONITOR  = test_extension_update
TESTS_MONITOR += test_installcheck
TESTS_MONITOR += test_monitor_disabled
TESTS_MONITOR += test_replace_monitor
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
TESTS_SSL += test_debian_clusters

# Tests for multiple standbys
TESTS_MULTI  = test_multi_async
TESTS_MULTI += test_multi_ifdown
TESTS_MULTI += test_multi_maintenance
TESTS_MULTI += test_multi_standbys

# Citus tests (TEST=citus)
TESTS_CITUS  = test_basic_citus_operation
TESTS_CITUS += test_citus_cluster_name
TESTS_CITUS += test_citus_force_failover
TESTS_CITUS += test_citus_multi_standbys
TESTS_CITUS += test_nonha_citus_operation
TESTS_CITUS += test_citus_skip_pg_hba

# TEST_ARGUMENT: the pytest file(s) to run.
# TEST= (empty) runs all; named groups expand to their file lists.
TEST ?=
ifeq ($(TEST),)
	TEST_ARGUMENT = *.py
else ifeq ($(TEST),multi)
	TEST_ARGUMENT = $(TESTS_MULTI:%=%.py)
else ifeq ($(TEST),single)
	TEST_ARGUMENT = $(TESTS_SINGLE:%=%.py)
else ifeq ($(TEST),monitor)
	TEST_ARGUMENT = $(TESTS_MONITOR:%=%.py)
else ifeq ($(TEST),ssl)
	TEST_ARGUMENT = $(TESTS_SSL:%=%.py)
else ifeq ($(TEST),citus)
	TEST_ARGUMENT = $(TESTS_CITUS:%=%.py)
else
	TEST_ARGUMENT = $(TEST).py
endif

# make test; run pytest inside the testing Docker image (invoked by run-test).
.PHONY: test
test:
ifeq ($(TEST),tablespaces)
	$(MAKE) -C tablespaces run-test
else
	sudo -E env "PATH=${PATH}" USER=$(shell whoami) \
		$(PYTEST) \
		-v \
		-s \
		-x \
		${TEST_ARGUMENT}
endif

# make run-test; build the test image then run pytest inside it.
.PHONY: run-test
run-test: build-pytest-pg$(PGVERSION)
	docker run \
		--name $(TEST_CONTAINER_NAME) \
		$(DOCKER_RUN_OPTS) \
		$(TEST_CONTAINER_NAME):pg$(PGVERSION) \
		make -C /usr/src/pg_auto_failover/tests test \
		PGVERSION=$(PGVERSION) TEST='$(TEST)'

# make run-test-prebuilt; like run-test but skips the build step.
# Used in CI after the image has been loaded from a prior job's artifact.
.PHONY: run-test-prebuilt
run-test-prebuilt:
ifeq ($(TEST),tablespaces)
	$(MAKE) -C tablespaces run-test-prebuilt
else
	docker run \
		--name $(TEST_CONTAINER_NAME) \
		$(DOCKER_RUN_OPTS) \
		$(TEST_CONTAINER_NAME):pg$(PGVERSION) \
		make -C /usr/src/pg_auto_failover/tests test \
		PGVERSION=$(PGVERSION) TEST='$(TEST)'
endif
