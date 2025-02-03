FROM debian:bookworm-slim

ARG PGVERSION=17

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
     python3 \
  && rm -rf /var/lib/apt/lists/*

# we use apt.postgresql.org
RUN install -d /usr/share/postgresql-common/pgdg
RUN curl -o /usr/share/postgresql-common/pgdg/apt.postgresql.org.asc --fail https://www.postgresql.org/media/keys/ACCC4CF8.asc
RUN echo "deb [signed-by=/usr/share/postgresql-common/pgdg/apt.postgresql.org.asc] https://apt.postgresql.org/pub/repos/apt bookworm-pgdg main ${PGVERSION}" > /etc/apt/sources.list.d/pgdg.list
RUN echo "deb-src [signed-by=/usr/share/postgresql-common/pgdg/apt.postgresql.org.asc] https://apt.postgresql.org/pub/repos/apt bookworm-pgdg main ${PGVERSION}" > /etc/apt/sources.list.d/pgdg.src.list

RUN apt-get update \
  && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    postgresql-client-${PGVERSION} \
  && rm -rf /var/lib/apt/lists/*

RUN adduser --disabled-password --gecos '' docker
RUN adduser docker sudo
RUN echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

COPY ./app.py /usr/src/app/app.py

USER docker
ENTRYPOINT [ "/usr/src/app/app.py" ]
