# Licensed under the PostgreSQL License.
#
# pg_walserver -- the archiver's own replication-protocol server. Standalone
# binary: links none of pg_autoctl's service code, only src/bin/common/,
# src/bin/lib/ and pg_autoctl/state.c (see OBJS below), so it can be exec'd
# and tested independently of pg_autoctl (see
# src/bin/pg_autoctl/service_archiver.c's own "colocated fast path" note).

PG_WALSERVER = ./pg_walserver

SRC_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Must be set before include so that targets in Makefile.common don't
# become the default goal when included before the all: rule below.
.DEFAULT_GOAL := all

include $(SRC_DIR)../common/Makefile.common

override CFLAGS += -I$(SRC_DIR) -I$(SRC_DIR)vendor -I$(SRC_DIR)../pg_autoctl

# -----------------------------------------------------------------------
# Sources that live in this directory
# -----------------------------------------------------------------------
LOCAL_SRC = main.c cli_root.c cli_common.c accept_loop.c startup.c auth.c framing.c repl_command.c \
            repl_gram.c repl_scanner.c \
            routes.c hba.c tls.c cmd_identify_system.c cmd_show.c cmd_base_backup.c \
            tar_stream.c cmd_fetch_file.c cmd_check_file.c cmd_archive_file.c \
            cmd_timeline_history.c cmd_replication_slot.c \
            cmd_start_replication.c wal_dir_scan.c ws_util.c receivewal.c \
            backup_bootstrap.c fetch_client.c push_client.c \
            cli_serve.c cli_scram_secret.c cli_reload.c cli_stop.c \
            cli_upstream.c cli_wal_target.c cli_fetch_systemid.c cli_basebackup.c cli_setup.c \
            cli_cluster.c \
            cli_create_cert.c cli_archive.c cli_restore_wal.c cli_archive_cleanup.c cli_internal.c \
            ps_state.c cli_ps.c cli_ls.c cli_status.c cli_list.c

LOCAL_OBJS = $(patsubst %.c,%.o,$(LOCAL_SRC))

# -----------------------------------------------------------------------
# Flex / Bison: the replication command grammar (repl_gram.y/repl_scanner.l,
# adapted from PostgreSQL's own src/backend/replication/{repl_gram.y,
# repl_scanner.l} -- see repl_command.h). Same pattern as
# src/bin/pgaftest/Makefile's test_spec_parse.y/test_spec_scan.l: the
# generated .c/.h are committed (bison/flex format, see .gitattributes'
# -citus-style entries for them) so building doesn't require bison/flex to
# be installed; "make generate" is the only supported way to refresh them.
# -----------------------------------------------------------------------
FLEX  = flex
BISON = bison

repl_scanner.c: repl_scanner.l repl_gram.h
	$(FLEX) -o $@ $<

repl_gram.c repl_gram.h: repl_gram.y
	$(BISON) -d -o repl_gram.c $<

.PHONY: generate
generate: repl_gram.c repl_gram.h repl_scanner.c

# repl_scanner.c includes repl_gram.h (bison-generated); make sure it exists
# before a parallel build tries to compile the scanner.
repl_scanner.o: repl_gram.h

# Bison/flex generated files have unavoidable unused tables and helper
# functions that vary by generator version; suppress those warnings so
# pg_config --cflags -Werror doesn't break the build (same as pgaftest's
# own GENERATED_CFLAGS).
GENERATED_CFLAGS = $(CFLAGS) \
    -Wno-unused-variable \
    -Wno-unused-function \
    -Wno-unused-const-variable \
    -Wno-return-type \
    -Wno-implicit-fallthrough \
    -Wno-error

repl_gram.o: repl_gram.c
	@if test ! -d $(DEPDIR); then mkdir -p $(DEPDIR); fi
	$(CC) $(GENERATED_CFLAGS) -c -MMD -MP -MF$(DEPDIR)/repl_gram.Po -o $@ $<

repl_scanner.o: repl_scanner.c
	@if test ! -d $(DEPDIR); then mkdir -p $(DEPDIR); fi
	$(CC) $(GENERATED_CFLAGS) -c -MMD -MP -MF$(DEPDIR)/repl_scanner.Po -o $@ $<

# -----------------------------------------------------------------------
# Vendored PostgreSQL source (see vendor/tar.c's own header comment) --
# compiled as vendor-%.o to keep it visually distinct from this project's
# own code.
# -----------------------------------------------------------------------
VENDOR_SRC = tar.c
VENDOR_OBJS = $(patsubst %.c,vendor-%.o,$(VENDOR_SRC))

vendor-%.o: $(SRC_DIR)vendor/%.c
	@if test ! -d $(DEPDIR); then mkdir -p $(DEPDIR); fi
	$(CC) $(CFLAGS) -c -MMD -MP -MF$(DEPDIR)/vendor-$(*F).Po -o $@ $<

shared-state.o: $(SRC_DIR)../pg_autoctl/state.c
	@if test ! -d $(DEPDIR); then mkdir -p $(DEPDIR); fi
	$(CC) $(CFLAGS) -I$(SRC_DIR)../pg_autoctl -c -MMD -MP -MF$(DEPDIR)/shared-state.Po -o $@ $<

OBJS  = $(LOCAL_OBJS) $(VENDOR_OBJS)
# common/ipaddr.c (forward/reverse DNS with its retry policy, reused here for
# HBA address matching) sits on common/pgsql.c, which needs parson and
# pg_autoctl's state.c (NodeStateFromString): linked, not reimplemented.
OBJS += lib-log.o lib-commandline.o lib-snprintf.o lib-strerror.o lib-parson.o shared-state.o
OBJS += $(COMMON_LIB)

# vendor/pg_receivewal/ (src/bin/common/vendor/pg_receivewal/, see that
# directory's own pg_receivewal.c header comment) -- an in-process
# pg_receivewal, forked and called directly by receivewal.c's own embedded
# receivewal worker. Its build rule (PG_RECEIVEWAL_VENDOR_OBJS) is shared with
# pg_autoctl's own vendor-linking (service_archiver_pgreceivewal_ctl.c) via
# Makefile.common, so both binaries build the identical vendored sources.
OBJS += $(PG_RECEIVEWAL_VENDOR_OBJS)

# vendor/pg_receivewal/ needs the same static libs any real frontend client
# of the replication protocol links against -- libpgfeutils for option_
# parse_int()/option_utils.h, libpgcommon/libpgport for everything
# receivelog.c/streamutil.c/walmethods.c build on top of. pg_autoctl's own
# Makefile adds the same libs for the same reason.
LIBS += -lpgfeutils -lpgcommon -lpgport

INCLUDES = $(wildcard $(SRC_DIR)*.h)

all: $(COMMON_LIB) $(PG_WALSERVER) ;

$(PG_WALSERVER): $(OBJS) $(INCLUDES)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -f $(OBJS) $(PG_WALSERVER)
	rm -rf $(DEPDIR)

# Remove generated parser/scanner sources only when explicitly requested.
# The committed .c/.h files allow building without bison/flex installed and
# without risking version-skew in Docker (see the Flex/Bison section above).
distclean: clean
	rm -f repl_gram.c repl_gram.h repl_scanner.c

install: $(PG_WALSERVER)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(PG_WALSERVER) $(DESTDIR)$(BINDIR)

.PHONY: all clean install
