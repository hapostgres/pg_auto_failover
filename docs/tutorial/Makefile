CONTAINER_NAME = pg_auto_failover:tutorial

all: build down up;

build:
	docker compose build

up:
	docker compose up app monitor node1 node2

node3:
	docker compose up -d node3

down:
	docker compose down

state:
	docker compose exec monitor pg_autoctl show state

switchover:
	docker compose exec monitor pg_autoctl perform switchover

watch:
	docker compose exec monitor pg_autoctl watch
