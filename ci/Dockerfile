FROM debian:bullseye-slim

RUN apt-get update \
  && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
     build-essential \
     ca-certificates \
     curl \
     git \
     gawk \
     make \
     cmake \
     python3 \
     sudo \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /usr/src/pg_auto_failover/ci
COPY tools.mk ./

WORKDIR /usr/src/pg_auto_failover
RUN make -C ci -f tools.mk tools
