NODES ?= 3

build:
	docker compose build

up:
	for v in volm vol1 vol2 vol3; do docker volume create $$v; done
	docker compose up

down:
	docker compose down --volumes --remove-orphans
	for v in volm vol1 vol2 vol3; do docker volume rm $$v; done

upgrade:
	docker compose up -d --no-deps --build monitor

state:
	docker compose exec monitor pg_autoctl show state

watch:
	docker compose exec monitor watch -n 0.2 pg_autoctl show state
