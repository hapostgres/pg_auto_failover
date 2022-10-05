FROM debian:bullseye-slim

ARG PGVERSION=14

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
  && rm -rf /var/lib/apt/lists/*

# we use apt.postgresql.org
RUN curl https://www.postgresql.org/media/keys/ACCC4CF8.asc | apt-key add -
RUN echo "deb http://apt.postgresql.org/pub/repos/apt bullseye-pgdg main ${PGVERSION}" > /etc/apt/sources.list.d/pgdg.list
RUN echo "deb-src http://apt.postgresql.org/pub/repos/apt bullseye-pgdg main ${PGVERSION}" > /etc/apt/sources.list.d/pgdg.src.list

RUN apt-get update \
  && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    postgresql-client-${PGVERSION} \
  && rm -rf /var/lib/apt/lists/*

RUN adduser --disabled-password --gecos '' docker
RUN adduser docker sudo
RUN echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

USER docker
ENTRYPOINT while true; do date -R; sleep 60; done
