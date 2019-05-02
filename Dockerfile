FROM debian:buster-slim

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
    make \
    openssl \
    pipenv \
    postgresql-11 \
    postgresql-server-dev-11 \
    python-nose \
    python3 \
    python3-setuptools \
    sudo \
    && rm -rf /var/lib/apt/lists/*

RUN adduser --disabled-password --gecos '' docker
RUN adduser docker sudo
RUN echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

WORKDIR /usr/src/pg_auto_failover

COPY tests/Pipfile* tests/
RUN PIPENV_PIPFILE=tests/Pipfile pipenv install --system --deploy

COPY Makefile ./
COPY ./src/ ./src
RUN make clean && make install -j8

COPY ./tests/ ./tests

USER docker
ENV PATH $PATH:/usr/lib/postgresql/11/bin
