ARG DEBIAN_TAG="bullseye-slim"

FROM debian:${DEBIAN_TAG} as python-slim

ARG DEBIAN_TAG

LABEL org.opencontainers.image.authors = "Data Bene <https://data-bene.io/>"
LABEL org.opencontainers.image.source = "https://github.com/hapostgres/pg_auto_failover/tree/main/docs/Dockerfile"
LABEL org.opencontainers.image.version = "${DEBIAN_TAG}"
LABEL org.opencontainers.image.licenses = "PostgreSQL License"
LABEL org.opencontainers.image.description = "A better python3 slim with pip"

RUN apt-get update \
 && apt-get install --no-install-recommends --yes \
      make \
      python3-minimal \
      python3-pip \
 && apt-get autoremove \
 && apt-get clean \
 && rm -rf /var/lib/apt/lists/*

FROM python-slim

WORKDIR /docs
COPY requirements.txt .
RUN python3 -m pip install --upgrade --no-cache-dir pip install -r requirements.txt

CMD ["python3", "--help"]
