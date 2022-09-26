CONTAINER_NAME = pg_auto_failover:citus

all: build up;

build:
	docker build -t $(CONTAINER_NAME) -f Dockerfile ../..

up:
	docker-compose up --scale coord=2 --scale worker=6

down:
	docker-compose down

state:
	docker-compose exec monitor pg_autoctl show state

failover:
	docker-compose exec monitor pg_autoctl perform failover --group 1

nodes:
	docker-compose exec coord psql -d analytics -c 'table pg_dist_node'
