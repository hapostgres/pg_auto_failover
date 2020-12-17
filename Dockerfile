FROM debian:buster-slim as build-test

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
    libreadline-dev \
    libpam-dev \
    zlib1g-dev \
    libxml2-dev \
    libxslt1-dev \
    libselinux1-dev \
    make \
    openssl \
    pipenv \
    python3-nose \
    python3 \
	python3-setuptools \
	python3-psycopg2 \
	python3-pyroute2 \
    sudo \
    tmux \
    watch \
    lsof \
    psutils \
    postgresql-common \
    postgresql-server-dev-11 \
	&& rm -rf /var/lib/apt/lists/*

# install Postgres 13 (current in bullseye), bypass initdb of a "main" cluster
RUN echo 'create_main_cluster = false' | sudo tee -a /etc/postgresql-common/createcluster.conf
RUN apt-get update\
	&& apt-get install -y --no-install-recommends postgresql-11 \
	&& rm -rf /var/lib/apt/lists/*

RUN adduser --disabled-password --gecos '' docker
RUN adduser docker sudo
RUN adduser docker postgres
RUN echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

WORKDIR /usr/src/pg_auto_failover

COPY Makefile ./
COPY ./src/ ./src
RUN make -s clean && make -s install -j8

COPY ./tests/ ./tests

USER docker
ENV PATH /usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/lib/postgresql/11/bin
ENV PGVERSION 11
ENV PG_AUTOCTL_DEBUG 1


FROM debian:stable-slim as run

RUN apt-get update \
  && apt-get install -y --no-install-recommends \
    make \
    sudo \
    tmux \
    watch \
    lsof \
    psutils \
    postgresql-common \
    postgresql-server-dev-11 \
	&& rm -rf /var/lib/apt/lists/*

# install Postgres 13 (current in bullseye), bypass initdb of a "main" cluster
RUN echo 'create_main_cluster = false' | sudo tee -a /etc/postgresql-common/createcluster.conf
RUN apt-get update\
	&& apt-get install -y --no-install-recommends postgresql-11 \
	&& rm -rf /var/lib/apt/lists/*

RUN adduser --disabled-password --gecos '' docker
RUN adduser docker sudo
RUN adduser docker postgres
RUN echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

COPY --from=build-test /usr/lib/postgresql/11/lib/pgautofailover.so /usr/lib/postgresql/11/lib
COPY --from=build-test /usr/share/postgresql/11/extension/pgautofailover* /usr/share/postgresql/11/extension/
COPY --from=build-test /usr/lib/postgresql/11/bin/pg_autoctl /usr/local/bin

USER docker
ENV PATH /usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/lib/postgresql/11/bin
ENV PGVERSION 11
ENV PG_AUTOCTL_DEBUG 1

CMD pg_autoctl do tmux session --nodes 3
