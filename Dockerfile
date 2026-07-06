#
# Using --build-arg PGVERSION=14 we can build pg_auto_failover for any
# target version of Postgres. In the Makefile, we use that to our advantage
# and tag test images such as pg_auto_failover_test:pg14.
#
ARG PGVERSION=17

#
# Define a base image with all our build dependencies.
#
# This base image contains all our target Postgres versions.
#
FROM debian:bullseye-slim AS base

ARG PGVERSION

RUN apt-get update \
  && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    curl \
    gnupg \
    git \
    gawk \
    flex \
    bison \
    iproute2 \
    libcurl4-gnutls-dev \
    libicu-dev \
    libncurses-dev \
    libxml2-dev \
    zlib1g-dev \
    libedit-dev \
    libkrb5-dev \
    liblz4-dev \
    libncurses6 \
    libpam-dev \
    libreadline-dev \
    libselinux1-dev \
    libssl-dev \
    libxslt1-dev \
    libzstd-dev \
    uuid-dev \
    make \
    autoconf \
    openssl \
    pipenv \
    python3-nose \
    python3 \
    python3-setuptools \
    python3-psycopg2 \
    python3-pip \
    sudo \
    tmux \
    watch \
    lsof \
    psutils \
    psmisc \
    htop \
    less \
    mg \
    valgrind \
    postgresql-common \
 && rm -rf /var/lib/apt/lists/*

RUN curl https://www.postgresql.org/media/keys/ACCC4CF8.asc | apt-key add -
RUN echo "deb http://apt.postgresql.org/pub/repos/apt bullseye-pgdg main ${PGVERSION}" > /etc/apt/sources.list.d/pgdg.list

# bypass initdb of a "main" cluster
RUN echo 'create_main_cluster = false' | sudo tee -a /etc/postgresql-common/createcluster.conf
RUN apt-get update \
	&& DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
     postgresql-server-dev-${PGVERSION} \
     postgresql-${PGVERSION} \
	&& rm -rf /var/lib/apt/lists/*

RUN pip3 install 'pyroute2>=0.5.17,<0.7.0'

RUN adduser --disabled-password --gecos '' docker
RUN adduser docker sudo
RUN adduser docker postgres
RUN echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

FROM base AS citus

ARG PGVERSION
ARG CITUSTAG=v13.0.1

ENV PG_CONFIG=/usr/lib/postgresql/${PGVERSION}/bin/pg_config

RUN git clone -b ${CITUSTAG} --depth 1 https://github.com/citusdata/citus.git /usr/src/citus
WORKDIR /usr/src/citus

RUN ./configure
RUN make -s clean && make -s -j8 install

#
# On-top of the base build-dependencies image, now we can build
# pg_auto_failover for a given --build-arg PGVERSION target version of
# Postgres.
#
FROM citus AS build

ARG PGVERSION

ENV PG_CONFIG=/usr/lib/postgresql/${PGVERSION}/bin/pg_config

WORKDIR /usr/src/pg_auto_failover

COPY Makefile ./
COPY Makefile.azure ./
COPY Makefile.citus ./
COPY ./src/ ./src
COPY ./src/bin/pg_autoctl/git-version.h ./src/bin/pg_autoctl/git-version.h
# Touch bison/flex generated files so they appear newer than the grammar
# sources, preventing make from re-running bison (system bison version may
# differ from the one used to pre-generate the committed .c/.h files).
RUN for f in src/bin/pgaftest/test_spec_parse.c \
             src/bin/pgaftest/test_spec_parse.h \
             src/bin/pgaftest/test_spec_scan.c; do \
      [ -f "$$f" ] && touch "$$f" || echo "warning: $$f not found, bison will regenerate"; \
    done
RUN make -s clean && make -s install -j8


#
# Given the build image above, we can now run our test suite targetting a
# given version of Postgres.
#
FROM build AS test

ARG PGVERSION

COPY ./tests/ ./tests
COPY ./valgrind ./valgrind
RUN chmod a+w ./valgrind

USER docker

ENV PG_AUTOCTL_DEBUG 1
ENV PATH /usr/lib/postgresql/${PGVERSION}/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin


#
# And finally our "run" images with the bare minimum for run-time.
#
FROM debian:bullseye-slim AS run

ARG PGVERSION

RUN apt-get update \
  && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    ca-certificates \
	curl \
	gnupg \
    make \
    sudo \
    tmux \
	watch \
    libncurses6 \
    lsof \
    psutils \
    dnsutils \
    bind9-host \
	libcurl4-gnutls-dev \
    libzstd-dev \
	postgresql-common \
    libpq-dev \
	&& rm -rf /var/lib/apt/lists/*

RUN curl https://www.postgresql.org/media/keys/ACCC4CF8.asc | apt-key add -
RUN echo "deb http://apt.postgresql.org/pub/repos/apt bullseye-pgdg main ${PGVERSION}" > /etc/apt/sources.list.d/pgdg.list

# bypass initdb of a "main" cluster
RUN echo 'create_main_cluster = false' | sudo tee -a /etc/postgresql-common/createcluster.conf
RUN apt-get update\
	&& DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends postgresql-${PGVERSION} \
	&& rm -rf /var/lib/apt/lists/*

RUN adduser --disabled-password --gecos '' --home /var/lib/postgres docker
RUN adduser docker sudo
RUN adduser docker postgres
RUN echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

COPY --from=build /usr/lib/postgresql/${PGVERSION}/lib/citus*.so /usr/lib/postgresql/${PGVERSION}/lib
COPY --from=build /usr/share/postgresql/${PGVERSION}/extension/citus* /usr/share/postgresql/${PGVERSION}/extension/

COPY --from=build /usr/lib/postgresql/${PGVERSION}/lib/pgautofailover.so /usr/lib/postgresql/${PGVERSION}/lib
COPY --from=build /usr/share/postgresql/${PGVERSION}/extension/pgautofailover* /usr/share/postgresql/${PGVERSION}/extension/
COPY --from=build /usr/lib/postgresql/${PGVERSION}/bin/pg_autoctl /usr/local/bin

#
# In tests/upgrade/docker-compose.yml we use internal docker volumes in
# order to be able to restart the nodes and keep the data around. For that
# to work, we must prepare a mount-point that is owned by our target user
# (docker), so that once the volume in mounted there by docker compose,
# pg_autoctl has the necessary set of privileges.
#
RUN mkdir -p /var/lib/postgres \
 && chown -R docker /var/lib/postgres

USER docker
ENV PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/lib/postgresql/${PGVERSION}/bin
ENV PG_AUTOCTL_DEBUG=1
ENV PGDATA=/var/lib/postgres/pgaf

#
# debian image — like run, but with a Debian-style "main" cluster pre-created
# via pg_createcluster so that pg_autoctl can test adoption of the split-config
# layout (postgresql.conf lives in /etc/postgresql/${PGVERSION}/main/ outside
# PGDATA).  pg_autoctl node run detects the missing postgresql.conf in PGDATA,
# moves the conf files in, and proceeds normally — no entrypoint changes needed.
#
FROM run AS debian

ARG PGVERSION

USER root
RUN pg_createcluster \
      --user docker --group postgres \
      ${PGVERSION} main \
      -- --auth-local trust --auth-host trust \
 && chown docker /var/lib/postgresql/${PGVERSION}

USER docker
ENV PGDATA=/var/lib/postgresql/${PGVERSION}/main

#
# testrun image — like run, but adds postgresql-server-dev and the full source
# tree so that "make installcheck" works inside the monitor container.
# Used by installcheck.pgaf via "monitor image-target testrun".
#
FROM run AS testrun

ARG PGVERSION

USER root
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    make \
    postgresql-server-dev-${PGVERSION} \
 && rm -rf /var/lib/apt/lists/*

COPY --chown=docker ./src/ /usr/src/pg_auto_failover/src/

USER docker

#
# pgaftest image — standalone test-runner image.
#
# Kept entirely separate from the pg_auto_failover run image so that no
# test tooling leaks into the images used for monitor and data nodes.
#
# Runtime dependencies come from two sources:
#   - Docker's own apt repo  → docker-ce-cli + docker-compose-plugin (v2)
#   - PGDG apt repo          → libpq5 matching the build's PGVERSION
#
# The pg_auto_failover binaries are copied directly from the run stage;
# nothing is built here.
#
FROM debian:bullseye-slim AS pgaftest

ARG PGVERSION

# Minimal runtime libs (libssl, libcurl, libzstd) plus PGDG for libpq
RUN apt-get update \
  && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      ca-certificates \
      curl \
      gnupg \
      libcurl4-gnutls-dev \
      libncurses6 \
      libzstd-dev \
      sudo \
  && rm -rf /var/lib/apt/lists/*

# PGDG repo — needed for the libpq version that pg_autoctl and pgaftest link against
RUN curl https://www.postgresql.org/media/keys/ACCC4CF8.asc | apt-key add -
RUN echo "deb http://apt.postgresql.org/pub/repos/apt bullseye-pgdg main ${PGVERSION}" \
      > /etc/apt/sources.list.d/pgdg.list \
  && apt-get update \
  && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      libpq5 \
  && rm -rf /var/lib/apt/lists/*

# Docker's apt repo — provides docker-ce-cli and the compose v2 plugin
RUN curl -fsSL https://download.docker.com/linux/debian/gpg \
      | gpg --dearmor -o /usr/share/keyrings/docker-archive-keyring.gpg \
  && echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/docker-archive-keyring.gpg] \
      https://download.docker.com/linux/debian bullseye stable" \
      > /etc/apt/sources.list.d/docker.list \
  && apt-get update \
  && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      docker-ce-cli \
      docker-compose-plugin \
  && rm -rf /var/lib/apt/lists/*

RUN adduser --disabled-password --gecos '' --home /var/lib/postgres docker \
  && adduser docker sudo \
  && echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers \
  && mkdir -p /var/lib/postgres \
  && chown docker /var/lib/postgres

# Binaries from the pg_auto_failover run image
COPY --from=run /usr/local/bin/pg_autoctl /usr/local/bin/

# pgaftest binary from the build stage
COPY --from=build /usr/lib/postgresql/${PGVERSION}/bin/pgaftest /usr/local/bin/

USER docker
ENV PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
