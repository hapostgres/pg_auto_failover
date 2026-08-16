# Run pgaftest schedules the same way CI does (test_pgaftest job in
# .github/workflows/ci.yml), but locally and in parallel via `make -jN`.
#
#   make schedule-quick-pg17          # one (schedule, PGVERSION) pair
#   make -j8 schedules                # the full CI matrix, 8 at a time
#   make -j24 schedules                # ... 24 at a time, on a beefy box
#
# Each pair runs in its own $(TMPDIR) AND its own COMPOSE_PROJECT_NAME
# (pgaftest defaults the project name to the spec's basename, e.g.
# "basic_operation" -- identical across every PGVERSION running the same
# schedule, so two concurrent jobs for the same schedule at different
# PGVERSIONs collide on container/volume/network names without this).
# So concurrent `docker compose` stacks never collide on host bind-mount
# paths or project names.
#
# Each pair's full output also goes to its own file under $(RUN_LOGDIR),
# so it survives -jN output interleaving and can be reviewed afterwards
# (e.g. `less logs/citus-1-pg17.log`, `grep -L 'tests passed' logs/*.log`
# to find failures).
#
# Each pair also writes its own $(RUN_LOGDIR)/<pair>.result — one line of
# "status pair duration_seconds" — instead of appending to a shared file,
# so concurrent -jN runs never race on a single results file. `make report`
# (after the run, or `make -jN schedules report`) aggregates those into a
# table sorted slowest-first, with pass/fail totals and overall wall time.
#
# Images are expected to already exist locally, built via `make build`
# at the repo root (pg_auto_failover:pg$(PGVERSION) + pgaf:pgaftest).

include ../../Makefile.docker

RUN_TMPROOT ?= /tmp/pgaftest-jobs
RUN_LOGDIR  ?= $(REPO_ROOT)tests/tap/logs

# schedule:PGVERSION pairs, mirroring test_pgaftest's matrix in ci.yml.
FAST_SCHEDULES     = quick node archiver ssl
FAST_PGVERSIONS    = 14 15 16 17 18 19
SLOW_PG17_SCHEDULES = multi-alternate multi-misc multi-async node-fsm-gaps archiver-multi citus-1 citus-2

MATRIX  = $(foreach s,$(FAST_SCHEDULES),$(foreach v,$(FAST_PGVERSIONS),$(s)-pg$(v)))
MATRIX += $(foreach s,$(SLOW_PG17_SCHEDULES),$(s)-pg17)

.PHONY: schedules
schedules: $(patsubst %,schedule-%,$(MATRIX)) ;

# schedule-<name>-pg<version>: run one schedule against one PG version.
.PHONY: schedule-%
schedule-%:
	$(eval PAIR    := $*)
	$(eval PGV     := $(lastword $(subst -pg, ,$(PAIR))))
	$(eval SCHNAME := $(patsubst %-pg$(PGV),%,$(PAIR)))
	$(eval JOBTMP    := $(RUN_TMPROOT)/$(PAIR))
	$(eval LOGFILE   := $(RUN_LOGDIR)/$(PAIR).log)
	$(eval RESULTFILE := $(RUN_LOGDIR)/$(PAIR).result)
	mkdir -p $(JOBTMP) && chmod 777 $(JOBTMP)
	mkdir -p $(RUN_LOGDIR)
	START=$$(date +%s); \
	docker run --rm \
	  --user root \
	  --group-add $(shell stat -c '%g' /var/run/docker.sock) \
	  -v /var/run/docker.sock:/var/run/docker.sock \
	  -v $(JOBTMP):$(JOBTMP) \
	  -e TMPDIR=$(JOBTMP) \
	  -v $(REPO_ROOT):/work:ro \
	  -w /work \
	  -e PGAF_IMAGE=pg_auto_failover:pg$(PGV) \
	  -e PGVERSION=$(PGV) \
	  -e PGAFTEST_HOST_WORK_DIR=$(REPO_ROOT) \
	  -e COMPOSE_PROJECT_NAME=pgaf-$(PAIR) \
	  pgaf:pgaftest \
	  pgaftest run --schedule tests/tap/schedules/$(SCHNAME).sch \
	  > $(LOGFILE) 2>&1; \
	RC=$$?; \
	END=$$(date +%s); \
	DURATION=$$((END - START)); \
	if [ $$RC -eq 0 ]; then STATUS=PASS; else STATUS=FAIL; fi; \
	echo "$$STATUS $(SCHNAME) $(PGV) $$DURATION" > $(RESULTFILE); \
	echo "$$STATUS  $(PAIR)  ($${DURATION}s, log: $(LOGFILE))"; \
	exit $$RC

# report: aggregate every *.result written by schedule-% into one table,
# slowest first, with pass/fail totals. Safe to run mid-flight (partial
# results) or after the full matrix completes.
.PHONY: report
report:
	@{ \
	  echo "STATUS  SCHEDULE                PGVERSION  DURATION"; \
	  for f in $(RUN_LOGDIR)/*.result; do \
	    [ -e "$$f" ] || continue; \
	    read -r STATUS SCH PGV DUR < "$$f"; \
	    printf "%-6s  %-22s  %-9s  %5ss\n" "$$STATUS" "$$SCH" "$$PGV" "$$DUR"; \
	  done | sort -k4 -rn; \
	  echo; \
	  TOTAL=$$(ls $(RUN_LOGDIR)/*.result 2>/dev/null | wc -l); \
	  PASSED=$$(grep -l '^PASS' $(RUN_LOGDIR)/*.result 2>/dev/null | wc -l); \
	  FAILED=$$(grep -l '^FAIL' $(RUN_LOGDIR)/*.result 2>/dev/null | wc -l); \
	  WALL=$$(awk '{sum+=$$4} END {print sum+0}' $(RUN_LOGDIR)/*.result 2>/dev/null); \
	  echo "$$PASSED/$$TOTAL passed, $$FAILED failed -- $${WALL}s summed schedule runtime (not wall-clock: schedules ran concurrently)"; \
	}

.PHONY: clean-schedules
clean-schedules:
	rm -rf $(RUN_TMPROOT) $(RUN_LOGDIR)

# spec-<name>-pg<version>: run one spec file directly (pgaftest run
# tests/tap/specs/<name>.pgaf), bypassing schedule files entirely. Meant
# for local dev runs: `make -j24 -f spec.mk specs SPEC_MATRIX=...` gives
# much finer-grained parallelism than schedule-level -jN, since a
# schedule serializes all its specs in one docker run. Same per-pair
# TMPDIR/COMPOSE_PROJECT_NAME/log/result isolation as schedule-%.
.PHONY: spec-%
spec-%:
	$(eval PAIR    := $*)
	$(eval PGV     := $(lastword $(subst -pg, ,$(PAIR))))
	$(eval SPECNAME := $(patsubst %-pg$(PGV),%,$(PAIR)))
	$(eval JOBTMP    := $(RUN_TMPROOT)/$(PAIR))
	$(eval LOGFILE   := $(RUN_LOGDIR)/$(PAIR).log)
	$(eval RESULTFILE := $(RUN_LOGDIR)/$(PAIR).result)
	mkdir -p $(JOBTMP) && chmod 777 $(JOBTMP)
	mkdir -p $(RUN_LOGDIR)
	START=$$(date +%s); \
	docker run --rm \
	  --user root \
	  --group-add $(shell stat -c '%g' /var/run/docker.sock) \
	  -v /var/run/docker.sock:/var/run/docker.sock \
	  -v $(JOBTMP):$(JOBTMP) \
	  -e TMPDIR=$(JOBTMP) \
	  -v $(REPO_ROOT):/work:ro \
	  -w /work \
	  -e PGAF_IMAGE=pg_auto_failover:pg$(PGV) \
	  -e PGVERSION=$(PGV) \
	  -e PGAFTEST_HOST_WORK_DIR=$(REPO_ROOT) \
	  -e COMPOSE_PROJECT_NAME=pgaf-$(PAIR) \
	  pgaf:pgaftest \
	  pgaftest run tests/tap/specs/$(SPECNAME).pgaf \
	  > $(LOGFILE) 2>&1; \
	RC=$$?; \
	END=$$(date +%s); \
	DURATION=$$((END - START)); \
	if [ $$RC -eq 0 ]; then STATUS=PASS; else STATUS=FAIL; fi; \
	echo "$$STATUS $(SPECNAME) $(PGV) $$DURATION" > $(RESULTFILE); \
	echo "$$STATUS  $(PAIR)  ($${DURATION}s, log: $(LOGFILE))"; \
	exit $$RC

.PHONY: specs
specs: $(patsubst %,spec-%,$(SPEC_MATRIX)) ;
