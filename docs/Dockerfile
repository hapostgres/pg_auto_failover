ARG SPHINX_VERSION="7.2.6"

FROM python:slim as sphinxdoc

ARG SPHINX_VERSION

LABEL org.opencontainers.image.authors = "Sphinx Team <https://www.sphinx-doc.org/>"
LABEL org.opencontainers.image.documentation = "https://sphinx-doc.org/"
LABEL org.opencontainers.image.source = "https://github.com/sphinx-doc/sphinx-docker-images"
LABEL org.opencontainers.image.version = "${SPHINX_VERSION}"
LABEL org.opencontainers.image.licenses = "BSD-2-Clause"
LABEL org.opencontainers.image.description = "Base container image for Sphinx"

WORKDIR /docs
RUN apt-get update \
 && apt-get install --no-install-recommends --yes \
      graphviz \
      imagemagick \
      make \
 && apt-get autoremove \
 && apt-get clean \
 && rm -rf /var/lib/apt/lists/*

RUN python3 -m pip install --no-cache-dir --upgrade pip \
 && python3 -m pip install --no-cache-dir Sphinx==${SPHINX_VERSION} Pillow

FROM sphinxdoc

RUN python3 -m pip install --no-cache-dir --upgrade pip \
 && python3 -m pip install --no-cache-dir sphinx-rtd-theme docutils readthedocs-sphinx-search

WORKDIR /docs

CMD ["sphinx-build", "-M", "html", ".", "_build"]
