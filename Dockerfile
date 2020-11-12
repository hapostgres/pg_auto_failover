FROM debian:bullseye-slim

ENV PGDATA /var/lib/postgresql/data

RUN apt-get update \
  && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    git \
    iproute2 \
    libicu-dev \
    libkrb5-dev \
    libssl-dev \
    libedit-dev \
    libpam-dev \
    zlib1g-dev \
    libxml2-dev \
    libxslt1-dev \
    libselinux1-dev \
    make \
    openssl \
    pipenv \
    postgresql-12 \
    postgresql-server-dev-12 \
    python3-nose \
    python3 \
	python3-setuptools \
	python3-psycopg2 \
	python3-pyroute2 \
    sudo \
    && rm -rf /var/lib/apt/lists/*

RUN adduser --disabled-password --gecos '' docker
RUN adduser docker sudo
RUN echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

WORKDIR /usr/src/pg_auto_failover

COPY Makefile ./
COPY ./src/ ./src
RUN make -s clean && make -s install -j8

COPY ./tests/ ./tests

USER docker
ENV PATH $PATH:/usr/lib/postgresql/12/bin
ENV PGVERSION 12
