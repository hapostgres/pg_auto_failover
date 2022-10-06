CONTAINER_NAME = pg_auto_failover:tutorial

all: build down up;

build:
	docker build -t $(CONTAINER_NAME) -f Dockerfile ../..
	docker-compose build

up:
	docker-compose up

down:
	docker-compose down --volumes --remove-orphans
	rm -rf {monitor,node1,node2,node3}/pgaf/

state:
	docker-compose exec monitor pg_autoctl show state

switchover:
	docker-compose exec monitor pg_autoctl perform switchover
