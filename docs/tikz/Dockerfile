ARG DEBIAN_TAG="bullseye-slim"

FROM debian:${DEBIAN_TAG} as luatex

ARG DEBIAN_TAG

LABEL org.opencontainers.image.authors = "Data Bene <https://data-bene.io/>"
LABEL org.opencontainers.image.source = "https://github.com/hapostgres/pg_auto_failover/tree/main/docs/tikz/Dockerfile"
LABEL org.opencontainers.image.version = "${DEBIAN_TAG}"
LABEL org.opencontainers.image.licenses = "PostgreSQL License"
LABEL org.opencontainers.image.description = "Docs builder with latexmk and pdftocairo"

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      make \
      latexmk \
      poppler-utils \
      texlive \
      texlive-luatex \
      texlive-latex-extra \
      texlive-fonts-extra \
 && apt-get autoremove \
 && apt-get clean \
 && rm -rf /var/lib/apt/lists/*

FROM luatex

WORKDIR /tikz

ENV TERM=dumb
ENV TEXMFHOME = /.cache/
ENV TEXMFVAR = /.cache/texmf-var/
ENV TEXMFCONFIG = $TEXMFSYSCONFIG

RUN luaotfload-tool --update
