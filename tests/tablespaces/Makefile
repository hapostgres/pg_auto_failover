PGVERSION ?= 10

build:
	$(MAKE) -C ../../ build-test-pg$(PGVERSION)
	docker-compose build --build-arg "PGVERSION=$(PGVERSION)"

teardown:
	docker-compose down --volumes --remove-orphans

tail:
	docker-compose logs -f

state:
	docker-compose exec monitor pg_autoctl show state

watch:
	docker-compose exec monitor pg_autoctl watch

test_01:
	docker-compose up -d node1
	TEST_NUM="01" docker-compose up --exit-code-from=test test

test_02:
	docker-compose up -d node2
	TEST_NUM="02" docker-compose up --exit-code-from=test test

test_03:
	# Pollute the data dir so pg_rewind fails
	docker-compose exec node2 mkdir '/var/lib/postgres/data/base/1/99999999'
	docker-compose exec monitor pg_autoctl perform failover
	TEST_NUM="03" docker-compose up --exit-code-from=test test

test_04:
	docker-compose pause node1
	TEST_NUM="04" docker-compose up --exit-code-from=test test

test_05:
	docker-compose unpause node1
	TEST_NUM="05" docker-compose up --exit-code-from=test test

test_06:
	docker-compose exec monitor pg_autoctl perform failover
	TEST_NUM="06" docker-compose up --exit-code-from=test test

test: build test_01 test_02 test_03 test_04 test_05 test_06

#  The test containers only connect to each other via the postgres protocol.
#    For this reason, the Makefile uses  `docker-compose exec` to perform
#    `pg_autoctl` commands, such as failover. Starting/stopping the pg_autoctl
#    process is also done using `docker-compose` so as not to complicate the
#    command used to run each container.
#  See CONTRIBUTING.md for more detailed instructions.
run-test: test teardown

.PHONY: build state watch run-test test tail teardown test_01 test_02 test_03 test_04 test_05 test_06
