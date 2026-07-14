# tests/upgrade/Makefile
#
# Builds the two Docker images needed for the upgrade spec and runs it.
#
# Usage:
#   make                        build both images and run the upgrade spec
#   make pgaf-current           build pgaf:current from PREV_TAG
#   make pgaf-next              build pgaf:next from current working tree
#   make run                    run tests/tap/specs/upgrade.pgaf
#   make clean                  remove the two images
#
# Customisation:
#   PREV_TAG   — git tag to build pgaf:current from  (default: auto-detected)
#   PGVERSION  — Postgres major version baked into the image  (default: 16)
#
# PGVERSION defaults to 16 because v2.1 (the current PREV_TAG) only supports
# up to PG16 — its monitor extension explicitly rejects PG >= 17.  Override
# with PGVERSION=17 only when testing against a PREV_TAG that supports PG17+.
#
# Auto-detection of PREV_TAG: reads PG_AUTOCTL_EXTENSION_VERSION from
# defaults.h to get the current extension version (e.g. "2.2"), then
# finds the most recent tag whose version is strictly lower.  The tag
# matching assumes the form "vMAJOR.MINOR".
#
# Examples:
#   make PREV_TAG=v2.0                 # test 2.0→2.2 on default PG16
#   make PREV_TAG=v2.1 PGVERSION=16    # explicit PG16

PGVERSION  ?= 16
PGAFTEST   ?= pgaftest
SPEC       := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/../tap/specs/upgrade.pgaf)
REPO_ROOT  := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/../..)
DOCKERFILE := $(REPO_ROOT)/Dockerfile

# Current extension version from source
CURRENT_EXT := $(shell grep 'PG_AUTOCTL_EXTENSION_VERSION ' \
    $(REPO_ROOT)/src/bin/pg_autoctl/defaults.h \
    | grep -o '"[^"]*"' | tr -d '"')

# Previous release tag: the highest vX.Y tag whose X.Y < CURRENT_EXT.
# Works for two-component versions (2.1, 2.2, …).
# Override with PREV_TAG=v2.0 if the heuristic gives the wrong answer.
_PREV_TAG_DETECT := $(shell \
    cur=$(CURRENT_EXT); \
    git -C $(REPO_ROOT) tag --sort=-version:refname \
        | grep -E '^v[0-9]+[.][0-9]+$$' \
        | grep -v "^v$$cur$$" \
        | head -1)
PREV_TAG ?= $(_PREV_TAG_DETECT)

.PHONY: all pgaf-current pgaf-next run clean

all: pgaf-next pgaf-current run

## Build pgaf:current — the previous release, used as the starting point.
## Built from the git archive of PREV_TAG so that uncommitted changes in
## the working tree do not contaminate the "old" image.
##
## git archive produces a clean tree without git history, so git-version.h
## (a generated file) is absent.  We extract the archive into a tempdir,
## synthesise git-version.h from the tag name, then docker build the dir.
pgaf-current:
	@if [ -z "$(PREV_TAG)" ]; then \
	    echo "ERROR: could not auto-detect PREV_TAG; set it explicitly:"; \
	    echo "  make PREV_TAG=v2.1"; \
	    exit 1; \
	fi
	@echo "Building pgaf:current from $(PREV_TAG) (extension version from that tag)"
	$(eval _TMP := $(shell mktemp -d))
	git -C $(REPO_ROOT) archive $(PREV_TAG) | tar -x -C $(_TMP)
	printf '#define GIT_VERSION "%s"\n' '$(PREV_TAG)' \
	    > $(_TMP)/src/bin/pg_autoctl/git-version.h
	cp $(REPO_ROOT)/Dockerfile $(_TMP)/Dockerfile
	docker build \
	    --build-arg PGVERSION=$(PGVERSION) \
	    --target run \
	    -t pgaf:current-base \
	    $(_TMP)
	rm -rf $(_TMP)
	docker build \
	    --build-arg BASE_IMAGE=pgaf:current-base \
	    --build-arg NEXT_IMAGE=pgaf:next \
	    -f $(dir $(abspath $(lastword $(MAKEFILE_LIST))))Dockerfile.current \
	    -t pgaf:current \
	    $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

## Build pgaf:next — the current working tree (the new version being tested).
pgaf-next:
	@echo "Building pgaf:next from working tree (extension version $(CURRENT_EXT))"
	docker build \
	    --build-arg PGVERSION=$(PGVERSION) \
	    --target run \
	    -t pgaf:next \
	    $(REPO_ROOT)

## Run the upgrade spec.
## Requires pgaf:current and pgaf:next to already be built.
run:
	@echo "Running upgrade spec: $(SPEC)"
	$(PGAFTEST) run $(SPEC)

## Remove the two upgrade test images.
clean:
	-docker rmi pgaf:current pgaf:next 2>/dev/null || true
