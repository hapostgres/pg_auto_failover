#
# Build pg_auto_failover for a specific Postgres major version.
#
# The heavy apt + Citus work lives in Dockerfile.base (image pgaf-base).
# This file adds:
#   build   — compiles pg_auto_failover + pgaftest, runs installcheck
#   test    — old Python test runner (kept for compatibility)
#   run     — minimal runtime image for test nodes
#   pgaftest — test-runner image (Docker CLI + pgaftest binary)
#
# Usage:
#   docker buildx build \
#     --build-arg PGVERSION=17 \
#     --build-arg BASE=ghcr.io/ORG/REPO/pgaf-base:bookworm \
#     --target run -t pgaf:run-pg17 .
#

ARG PGVERSION=17
ARG BASE=ghcr.io/hapostgres/pg_auto_failover/pgaf-base:bookworm

# ---------------------------------------------------------------------------
# build — compile pg_auto_failover against one specific Postgres version
# ---------------------------------------------------------------------------
FROM ${BASE} AS build

ARG PGVERSION
ENV PG_CONFIG=/usr/lib/postgresql/${PGVERSION}/bin/pg_config

WORKDIR /usr/src/pg_auto_failover

COPY Makefile* ./
COPY ./src/ ./src
COPY ./src/bin/pg_autoctl/git-version.h ./src/bin/pg_autoctl/git-version.h

# Touch bison/flex generated files so they appear newer than the grammar
# sources, preventing make from re-running bison (system bison version may
# differ from the one used to pre-generate the committed .c/.h files).
RUN if [ -d src/bin/pgaftest ]; then \
      touch src/bin/pgaftest/test_spec_parse.c \
            src/bin/pgaftest/test_spec_parse.h \
            src/bin/pgaftest/test_spec_scan.c; \
    fi

RUN make -s clean && make -s install -j$(nproc) BINDIR=/usr/local/bin
RUN pg_virtualenv -v ${PGVERSION} \
      -o "shared_preload_libraries=pgautofailover" \
      make -C src/monitor/ installcheck

# ---------------------------------------------------------------------------
# test — old Python test runner (kept for compatibility; new tests use pgaftest)
# ---------------------------------------------------------------------------
FROM build AS test

ARG PGVERSION

COPY ./tests/ ./tests
COPY ./valgrind ./valgrind
RUN chmod a+w ./valgrind

USER docker

ENV PG_AUTOCTL_DEBUG=1
ENV PATH=/usr/lib/postgresql/${PGVERSION}/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

# ---------------------------------------------------------------------------
# run — minimal runtime image for monitor and data-node containers.
#
# Starts fresh from debian:bookworm-slim; installs only the Postgres runtime
# packages (no build tools, no Citus source), then copies the compiled
# binaries and extension files from the build stage.
# ---------------------------------------------------------------------------
FROM debian:bookworm-slim AS run

ARG PGVERSION

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      bind9-host \
      ca-certificates \
      curl \
      dnsutils \
      gnupg \
      libcurl4-gnutls-dev \
      libncurses6 \
      libpq-dev \
      libzstd-dev \
      lsof \
      make \
      postgresql-common \
      psutils \
      sudo \
      tmux \
      watch \
 && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL https://www.postgresql.org/media/keys/ACCC4CF8.asc \
      | gpg --dearmor -o /usr/share/keyrings/pgdg-archive-keyring.gpg
RUN echo "deb [signed-by=/usr/share/keyrings/pgdg-archive-keyring.gpg] \
      http://apt.postgresql.org/pub/repos/apt bookworm-pgdg main ${PGVERSION}" \
      > /etc/apt/sources.list.d/pgdg.list

RUN echo 'create_main_cluster = false' >> /etc/postgresql-common/createcluster.conf

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      postgresql-${PGVERSION} \
 && rm -rf /var/lib/apt/lists/*

RUN adduser --disabled-password --gecos '' --home /var/lib/postgres docker \
 && adduser docker sudo \
 && adduser docker postgres \
 && echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

COPY --from=build /usr/lib/postgresql/${PGVERSION}/lib/citus*.so \
                  /usr/lib/postgresql/${PGVERSION}/lib/
COPY --from=build /usr/share/postgresql/${PGVERSION}/extension/citus* \
                  /usr/share/postgresql/${PGVERSION}/extension/
COPY --from=build /usr/lib/postgresql/${PGVERSION}/lib/pgautofailover.so \
                  /usr/lib/postgresql/${PGVERSION}/lib/
COPY --from=build /usr/share/postgresql/${PGVERSION}/extension/pgautofailover* \
                  /usr/share/postgresql/${PGVERSION}/extension/
COPY --from=build /usr/local/bin/pg_autoctl /usr/local/bin/

RUN mkdir -p /var/lib/postgres \
 && chown -R docker /var/lib/postgres

USER docker
ENV PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/lib/postgresql/${PGVERSION}/bin
ENV PG_AUTOCTL_DEBUG=1
ENV PGDATA=/var/lib/postgres/pgaf

# ---------------------------------------------------------------------------
# pgaftest — standalone test-runner image.
#
# Kept separate from the run image so no test tooling leaks into node images.
# Uses Docker-out-of-Docker: mounts the host Docker socket at runtime.
# ---------------------------------------------------------------------------
FROM debian:bookworm-slim AS pgaftest

ARG PGVERSION

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      ca-certificates \
      curl \
      gnupg \
      libcurl4-gnutls-dev \
      libncurses6 \
      libzstd-dev \
 && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL https://www.postgresql.org/media/keys/ACCC4CF8.asc \
      | gpg --dearmor -o /usr/share/keyrings/pgdg-archive-keyring.gpg
RUN echo "deb [signed-by=/usr/share/keyrings/pgdg-archive-keyring.gpg] \
      http://apt.postgresql.org/pub/repos/apt bookworm-pgdg main ${PGVERSION}" \
      > /etc/apt/sources.list.d/pgdg.list \
 && apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      libpq5 \
 && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL https://download.docker.com/linux/debian/gpg \
      | gpg --dearmor -o /usr/share/keyrings/docker-archive-keyring.gpg \
 && echo "deb [arch=$(dpkg --print-architecture) \
      signed-by=/usr/share/keyrings/docker-archive-keyring.gpg] \
      https://download.docker.com/linux/debian bookworm stable" \
      > /etc/apt/sources.list.d/docker.list \
 && apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      docker-ce-cli \
      docker-compose-plugin \
 && rm -rf /var/lib/apt/lists/*

COPY --from=build /usr/local/bin/pg_autoctl /usr/local/bin/
COPY --from=build /usr/local/bin/pgaftest /usr/local/bin/

WORKDIR /root
ENV PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
