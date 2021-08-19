NODES ?= 3

PATCH = tests/upgrade/monitor-upgrade-1.7.patch
Q_VERSION = select default_version, installed_version
Q_VERSION += from pg_available_extensions where name = 'pgautofailover'

build:
	docker-compose build

patch:
	cd ../.. && git apply $(PATCH)

clean:
	cd ../.. && git apply --reverse $(PATCH)

up: create-volumes compose-up tail ;

down: compose-down rm-volumes ;

compose-down:
	docker-compose down --volumes --remove-orphans

compose-up:
	docker-compose up -d

tail:
	docker-compose logs -f

create-volumes:
	for v in volm vol1 vol2 vol3; do docker volume create $$v; done

rm-volumes:
	for v in volm vol1 vol2 vol3; do docker volume rm $$v; done

upgrade-monitor: patch
	docker-compose up -d --no-deps --build monitor

upgrade-nodes:
	docker-compose up -d --no-deps --build node3 node2
	docker-compose up -d --no-deps --build node1

state:
	docker-compose exec monitor pg_autoctl show state

version:
	docker-compose exec monitor pg_autoctl version
	docker-compose exec monitor psql -d pg_auto_failover -c "$(Q_VERSION)"

failover:
	docker-compose exec monitor pg_autoctl perform failover

watch:
	docker-compose exec monitor watch -n 0.2 pg_autoctl show state

.PHONY: build patch clean up down upgrade-monitor state watch
.PHONY: compose-down compose-up create-volumes rm-volumes
