CONTAINER_NAME = pg_auto_failover:citus

all: build down up ;

scale: build scale-down scale-up ;

build:
	docker build -t $(CONTAINER_NAME) -f Dockerfile ../..
	docker compose build

scale-up:
	docker compose -f docker-compose-scale.yml up --scale coord=2 --scale worker=6

scale-down:
	docker compose -f docker-compose-scale.yml down

up:
	docker compose up

down:
	docker compose down

state:
	docker compose exec monitor pg_autoctl show state

failover:
	docker compose exec monitor pg_autoctl perform failover --group 1

nodes:
	docker compose exec coord psql -d analytics -c 'table pg_dist_node'

.PHONY: all scale build scale-up scale-down up down state failover nodes
