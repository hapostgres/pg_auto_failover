NODES ?= 3

up:
	docker compose up

build:
	docker compose build

down:
	docker compose down --volumes --remove-orphans
	docker volume ls | awk '/local/ { print $$2 }' | xargs docker volume rm

upgrade:
	docker compose up -d --no-deps --build monitor

state:
	docker compose exec monitor pg_autoctl show state

watch:
	docker compose exec monitor watch -n 0.2 pg_autoctl show state
