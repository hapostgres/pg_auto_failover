.DEFAULT_GOAL := all

# You can set these variables from the command line.
SPHINXOPTS    =
SPHINXBUILD   = sphinx-build
SPHINXPROJ    = pg_auto_failover
SOURCEDIR     = .
BUILDDIR      = _build

DEPS = $(wildcard *.rst) $(wildcard ref/*.rst)

# If USE_DOCKER variable is defined, we're working with docker and use the tools
# from image/container instead of current OS.
ifdef USE_DOCKER
SPHINX_IMAGE = hapostgres/sphinx-citus:latest
endif

.PHONY: all
all: tikz html

.PHONY: clean
clean:
	rm -f docker.id
	rm -rf _build _html
	$(MAKE) -C tikz $@

.PHONY: maintainer-clean
maintainer-clean: clean
	$(MAKE) -C tikz $@
ifdef USE_DOCKER
	docker image rm $(SPHINX_IMAGE)
endif

ifdef USE_DOCKER
SPHINXBUILD = docker run --rm -u $(shell id -u):$(shell id -g) -v $(SOURCEDIR)/:/docs $(SPHINX_IMAGE) sphinx-build
docker.id: Dockerfile requirements.txt
	docker build --iidfile docker.id --file $< -t $(SPHINX_IMAGE) .
else
docker.id: ;
endif

.PHONY: help
help:
	@$(SPHINXBUILD) -M help "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)

.PHONY: html
html: $(BUILDDIR)/html

.PHONY: tikz
tikz:
	$(MAKE) -C tikz all

# Catch-all target: route all unknown targets to Sphinx using the new
# "make mode" option.  $(O) is meant as a shortcut for $(SPHINXOPTS).
$(BUILDDIR)/html: $(DEPS) docker.id
	echo $?
	$(SPHINXBUILD) -M html "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)
