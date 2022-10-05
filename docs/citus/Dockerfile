FROM debian:bullseye-slim

ARG PGVERSION=14
ARG CITUS=postgresql-14-citus-11.1

RUN apt-get update \
  && apt-get install -y --no-install-recommends \
     ca-certificates \
     gnupg \
     make \
     curl \
     sudo \
     tmux \
     watch \
     lsof \
     psutils \
     postgresql-common \
     libpq-dev \
  && rm -rf /var/lib/apt/lists/*

# we use apt.postgresql.org
RUN curl https://www.postgresql.org/media/keys/ACCC4CF8.asc | apt-key add -
RUN echo "deb http://apt.postgresql.org/pub/repos/apt bullseye-pgdg main ${PGVERSION}" > /etc/apt/sources.list.d/pgdg.list
RUN echo "deb-src http://apt.postgresql.org/pub/repos/apt bullseye-pgdg main ${PGVERSION}" > /etc/apt/sources.list.d/pgdg.src.list

# bypass initdb of a "main" cluster
RUN echo 'create_main_cluster = false' | sudo tee -a /etc/postgresql-common/createcluster.conf
RUN apt-get update \
  && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    postgresql-${PGVERSION} \
    postgresql-server-dev-${PGVERSION} \
  && rm -rf /var/lib/apt/lists/*

# See https://packagecloud.io/citusdata/community/install
RUN curl -s https://packagecloud.io/install/repositories/citusdata/community/script.deb.sh | sudo bash

RUN apt-get update \
  && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
	postgresql-${PGVERSION} \
    ${CITUS} \
  && rm -rf /var/lib/apt/lists/*

RUN adduser --disabled-password --gecos '' docker
RUN adduser docker sudo
RUN adduser docker postgres
RUN echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

# build pg_auto_failover from local sources
RUN apt-get update \
  && apt-get build-dep -y --no-install-recommends postgresql-${PGVERSION} \
  && rm -rf /var/lib/apt/lists/*
WORKDIR /usr/src/pg_auto_failover

COPY Makefile ./
COPY src/ ./src
RUN make -s clean && make -s install -j8
RUN cp /usr/lib/postgresql/${PGVERSION}/bin/pg_autoctl /usr/local/bin

USER docker
ENV PG_AUTOCTL_DEBUG 1
