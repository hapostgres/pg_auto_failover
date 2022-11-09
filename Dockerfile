#
# Using --build-arg PGVERSION=11 we can build pg_auto_failover for any
# target version of Postgres. In the Makefile, we use that to our advantage
# and tag test images such as pg_auto_failover_test:pg14.
#
ARG PGVERSION=14

#
# Define a base image with all our build dependencies.
#
# This base image contains all our target Postgres versions.
#
FROM debian:bullseye-slim as base

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

RUN pip3 install pyroute2>=0.5.17

RUN adduser --disabled-password --gecos '' docker
RUN adduser docker sudo
RUN adduser docker postgres
RUN echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

FROM base as citus

ARG PGVERSION
ARG CITUSTAG=v11.1.2

ENV PG_CONFIG /usr/lib/postgresql/${PGVERSION}/bin/pg_config

RUN git clone -b ${CITUSTAG} --depth 1 https://github.com/citusdata/citus.git /usr/src/citus
WORKDIR /usr/src/citus

RUN ./configure
RUN make -s clean && make -s -j8 install

#
# On-top of the base build-dependencies image, now we can build
# pg_auto_failover for a given --build-arg PGVERSION target version of
# Postgres.
#
FROM citus as build

ARG PGVERSION

ENV PG_CONFIG /usr/lib/postgresql/${PGVERSION}/bin/pg_config

WORKDIR /usr/src/pg_auto_failover

COPY Makefile ./
COPY ./src/ ./src
COPY ./src/bin/pg_autoctl/git-version.h ./src/bin/pg_autoctl/git-version.h
RUN make -s clean && make -s install -j8


#
# Given the build image above, we can now run our test suite targetting a
# given version of Postgres.
#
FROM build as test

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
FROM debian:bullseye-slim as run

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
ENV PATH /usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/lib/postgresql/${PGVERSION}/bin
ENV PG_AUTOCTL_DEBUG 1
ENV PGDATA /var/lib/postgres/pgaf

CMD pg_autoctl do tmux session --nodes 3 --binpath /usr/local/bin/pg_autoctl
