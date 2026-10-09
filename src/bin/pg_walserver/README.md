# pg_walserver

`pg_walserver` is a standalone replication-protocol server: it speaks just
enough of the PostgreSQL frontend/backend wire protocol, on the
replication-connection side, to serve `pg_basebackup`, `pg_receivewal` and a
real standby's own walreceiver directly out of a directory tree it owns
(WAL segments, base backups, small bookkeeping files) instead of out of a
live `postmaster`.

This file documents the design of the code as it exists in this PR. It is
not a tutorial and not an operations guide; it exists so the next engineer
reading this directory does not have to reverse-engineer the "why" from the
"what".

## Naming: why pg_walsender became pg_walserver

This binary was `pg_walsender` for most of this PR's own history, renamed
once it grew a real write path (`ARCHIVE_FILE` push) and an embedded pull
receivewal worker alongside its original `BASE_BACKUP`/`FETCH_FILE`/
`START_REPLICATION` read side -- "sender" undersold half its job the
moment either landed. Alternatives checked and rejected before picking
`pg_walserver` ("sender" replaced by the more general "server"):
`pg_archiver`/`pg_walarchive`-shaped names, which collide with both
PostgreSQL's own glossary term "the archiver process" (the backend that
invokes `archive_command`) and this project's own planned pg_auto_failover
archiver feature; and a pgBackRest-inspired `pg_walrepo`/`pg_walstore`-
style coinage (pgBackRest's own vocabulary for its storage side,
"repository"/"repo", is genuinely well precedented, but adds a new name
over a borrowed one, where a minimal one-word swap already says exactly
what this binary is). External archiving tools in this space (pgBackRest,
Barman, WAL-G) don't speak PostgreSQL's native replication wire protocol
at all -- they're `archive_command`/`restore_command` hooks managing
storage over their own protocols -- which is the actual differentiator
`pg_walserver` has over any of them: `pg_basebackup`, `pg_receivewal`, and
a real standby's own walreceiver can already talk to it with zero custom
client. The rename (`5f51514`) landed in this same PR rather than being
deferred to a separate one, on the theory that continuing to iterate on an
already-open PR through review is the normal shape of review.

## What this PR is, and isn't

This PR ships `pg_walserver` and nothing that plugs it into `pg_autoctl`
yet. There is:

- no "archiver" node kind in `pg_autoctl`,
- no monitor SQL schema for it,
- no `pg_autoctl restore command`,
- no process supervision wiring it into `pg_autoctl`'s own service
  framework.

All of that lands in a later, separate PR (informally "the archiving PR"
throughout this codebase's comments). This PR is the standalone piece:
`pg_walserver` builds, runs, authenticates connections and serves the wire
protocol entirely on its own, driven by a handful of files it reads
directly off disk (`pg_walserver.ini`, `pg_walserver_hba.conf`,
`pg_walserver_passwd`, and per-cluster bookkeeping files -- see below). Several of
its own sub-commands (`setup`, `fetch-systemid`, `basebackup`, `archive-wal`,
`restore-wal`, `create-cert`; see "New client-side sub-commands" below) can
now create and keep those files current, and push/pull WAL, directly from
the command line -- `docs/ref/pg_walserver.rst`'s own worked example uses
`setup` (embedded receivewal on by default), `archive-wal`, and `restore-wal`,
falling back to `pg_basebackup`/`pg_receivewal`/`psql` by hand only for
what those don't cover (HBA, the passwd file, a real continuously-streaming
standby). `pg_walserver` can already be a complete, standalone archiver
entirely on its own this way; what's left for the later "archiving PR" is
wiring all of this into `pg_autoctl`'s own process supervision and monitor
schema, so `pg_autoctl archive command`/`pg_autoctl restore command` can
participate in the monitor's own quorum/archiver-node bookkeeping instead
of running by hand.

One concrete consequence of that scoping shows up in `hba.c`/`hba.h`: an
earlier iteration of this PR had a `"monitor"` HBA `ADDRESS` keyword backed
by a `refresher.c` child process that queried two monitor-side SQL
functions (`pgautofailover.get_group_hosts_hash`,
`pgautofailover.get_group_hosts`) to auto-admit registered cluster nodes.
Those functions do not exist anywhere in this PR's own dependency chain --
this PR has no monitor extension at all -- so the feature was removed
wholesale rather than kept as a half-wired dead end. It will be
reintroduced, properly, once the archiving PR's monitor schema exists for
it to query. Until then, every host allowed to connect needs an explicit
line in `pg_walserver_hba.conf`.

## Process model

One process accepts connections (`accept_loop.c`) and `fork()`s a child per
accepted connection, with no `exec()` -- the same cheap-concurrency model as
PostgreSQL's own postmaster/`BackendStartup()`/`BackendMain()` split. Each
child runs the full lifecycle of exactly one connection: startup
negotiation (`startup.c`), TLS if requested (`tls.c`), HBA-driven
authentication (`hba.c`, `auth.c`), and then the simple-query command loop
a replication connection actually uses (`accept_loop.c`'s
`handle_connection()`). Live children are tracked in a plain pid array and
reaped in the main loop (`reap_children()`), the same as the postmaster's
own `CleanupBackend()` -- there is no `SIGCHLD` handler and no shared
counter. `WS_MAX_CONNECTIONS` (64) bounds how many children can be alive at
once; beyond that a new connection is refused outright.

Every child runs under an absolute authentication deadline
(`--auth-timeout`, default 30 seconds): `alarm()` plus a `SIGALRM` handler
that calls `_exit()` (only async-signal-safe calls there). It covers the
startup packet, the TLS handshake, HBA evaluation (including any DNS
lookups) and the SCRAM exchange, and is disarmed the moment authentication
succeeds. This mirrors PostgreSQL's own `authentication_timeout`.

## The wire protocol

`pg_walserver` implements the read side of the frontend/backend protocol by
hand (`framing.c`/`framing.h`): message framing, `RowDescription`/`DataRow`,
`ReadyForQuery`, `ErrorResponse`, `CopyOutResponse`/`CopyData`/`CopyDone`,
and the `AuthenticationSASL*` messages SCRAM needs. There is no reusable
frontend-linkable server-side protocol library anywhere in PostgreSQL to
build on here (checked against upstream `pqcomm.c`, `backend_startup.c`,
`repl_gram.y`, `walsender.c` -- all backend-only), so this is a genuine,
from-the-wire-format reimplementation, not a linking exercise. See
`walsender.h`'s own header comment for the fuller version of this note.

The commands a connected client can issue on a `Query` ('Q') message are:

- `IDENTIFY_SYSTEM` (`cmd_identify_system.c`) -- reports the cluster's system
  identifier (read from a small `pg_walserver_systemid` file under the cluster's
  directory), current timeline and `xlogpos`, and `dbname` (`NULL` unless
  the client's startup packet used `replication=database`, matching real
  `pg_receivewal`'s expectations exactly, see `walsender.h`).
- `SHOW <name>` (`cmd_show.c`) -- `wal_segment_size`, needed by
  `pg_basebackup`/`pg_receivewal` to size their own reads (reports the real
  16MB PostgreSQL default; this project does not support a non-default WAL
  segment size), and `data_directory_mode` (a fixed `"0700"`). Also `receivewal`,
  this project's own extension with no PostgreSQL equivalent: reports the
  connected cluster's own `receivewal` setting, `"pull"` or `"none"`, straight
  from `clusters.h`'s `WsCluster.receivewalPull` -- how `pg_walserver archive-wal`
  (see "The archive push side" below) learns, per invocation, which of its
  two behaviors to run.
- `BASE_BACKUP [options...]` (`cmd_base_backup.c`) -- streams the cluster's
  current base backup back as a tar archive, in the same
  `CopyOutResponse`/tagged-`CopyData` framing real PostgreSQL uses,
  including the PG15+ "archive framing" (`'n'`/`'d'`/`'m'` tags in one
  CopyOut) vs. the pre-PG15 two-CopyOut-streams shape, picked at compile
  time based on which real PostgreSQL headers this build of `pg_walserver`
  itself was built against (`CBB_USE_ARCHIVE_FRAMING`). See that file's own
  header comment for the full, wire-traced sequence diagram.
- `TIMELINE_HISTORY <tli>` (`cmd_timeline_history.c`) -- returns the
  `<tli>.history` file's content as a single row, exactly as a real
  walsender does.
- `CREATE_REPLICATION_SLOT` / `READ_REPLICATION_SLOT` /
  `DROP_REPLICATION_SLOT` (`cmd_replication_slot.c`) -- see "Replication
  slots" below.
- `START_REPLICATION [SLOT <name>] <startlsn> [TIMELINE <tli>]`
  (`cmd_start_replication.c`) -- streams WAL bytes straight out of the
  cluster's WAL cache directory as `CopyData` messages, physical replication
  only. It deliberately does not vendor `xlogreader.c`: streaming raw bytes
  needs no WAL *record* decoding, only byte-range bookkeeping over
  TLI+segno, which this file does directly (see its own header comment).
  It follows a segment that is still being written (a `.partial` file) the
  same way a real primary's walsender follows one, which is what lets
  `test_003` in the tap spec (below) prove a segment can be tailed live.
- `FETCH_FILE '<name>'` (`cmd_fetch_file.c`) -- this project's **own**
  extension to the grammar, with no PostgreSQL equivalent. It streams one
  file (a complete WAL segment or a `.history` file, nothing else --
  `ws_fetch_filename_is_servable()` is an allow-list, not a filter) out of
  the cluster's directory as an ordinary `CopyOut`. It exists because a
  `restore_command` is invoked as a brand-new short-lived process per WAL
  segment, with no session to reuse and no walsender-style long streaming
  connection to amortize over -- a plain one-shot request/response fits
  that shape far better than pretending it is a `START_REPLICATION` stream.
  It rides on the same connection machinery as every other command: same
  HBA rules, same TLS, same SCRAM, same libpq wire format, so any libpq
  client gets it "for free". This PR ships only the **server side** of
  `FETCH_FILE`, `cmd_fetch_file.c`. Its client used to be a
  `pg_walserver fetch-file` CLI sub-command in this same binary; it has
  been moved out, see "FETCH_FILE's client" below.
- `CHECK_FILE '<name>' <size> crc32c:<hex>` (`cmd_check_file.c`) -- another
  of this project's own extensions, a cheap query with no file transfer at
  all: `RowDescription(status text, fallback text)` + `DataRow('missing'|
  'matches'|'differs', 'yes'|'no')` + `CommandComplete`, the same shape
  `SHOW` already uses. The client (`pg_walserver archive-wal`, see "The
  archive push side" below) computes the size and CRC32C of its own
  *local* file and sends both here; the reply says whether that's already
  what's on disk under this name, without moving a single byte of file
  content. `fallback` is the smart-fallback signal: "yes" when this
  cluster's own embedded receivewal worker has already streamed *past*
  `<name>` (its own last-observed position, "receivewal-progress", is at
  a later segment or a later timeline) while `<name>` itself never
  arrived -- a hole streaming can never retroactively fill, typically a
  timeline switch left a segment behind on the old timeline -- telling
  `archive-wal` to push it directly via `ARCHIVE_FILE` right away instead
  of waiting on a retry loop that would otherwise never succeed. "no"
  otherwise (including whenever there isn't a live progress reading to
  compare against, the safe default). See "The archive push side" below
  for the full design.
- `ARCHIVE_FILE '<name>'` (`cmd_archive_file.c`) -- a `CopyIn` (client to
  server): the actual push, used only when `CHECK_FILE` said `missing` or
  `differs`. The server never trusts a client's own `CHECK_FILE` checksum
  as proof of anything -- once the whole `CopyIn` has been received, it
  re-derives the overwrite-safety decision from the real bytes just
  received vs. whatever is already on disk under that name (`ws_file_
  crc32c()`, `ws_util.c`): identical -> success (an idempotent retry,
  matching PostgreSQL's own `archive_command` contract, which explicitly
  requires this), different -> a clean rejection, nothing there yet -> a
  same-directory temporary file plus atomic `rename()`. Same allow-list as
  `FETCH_FILE`'s read side (`ws_fetch_filename_is_servable()`, extended to
  also accept a base backup's own `<24hex>.<8hex>.backup` history file,
  since real Postgres archives it exactly like a WAL segment), hard size
  cap at the cluster's
  own `wal_segment_size` (`ws_cluster_wal_segment_size()`) plus
  `WS_ARCHIVE_FILE_SIZE_SLACK` (1 MiB), checked as bytes arrive so an
  oversized push never gets to write the whole thing to disk.

Anything else parses to `WS_CMD_UNKNOWN` and gets a clean `ErrorResponse`
(SQLSTATE `42601`) rather than a crash or a hung connection -- the
connection remains usable for the next command afterwards
(`test_004_grammar_edge_cases` in the tap spec exercises exactly this).

### FETCH_FILE's client: `fetch_client.c` and `pg_walserver restore-wal`

`pg_walserver` does not ship a `fetch-file` CLI sub-command at all -- the
client side of `FETCH_FILE` is a plain function, `ws_fetch_file_client()`
(`fetch_client.c`/`fetch_client.h`), called directly, in-process, by
`pg_walserver restore-wal` (`cli_restore_wal.c`), with no
subprocess/`execv()` indirection. It opens a connection via
`src/bin/common/pgsql.c`'s generic connect/retry facility, runs
`FETCH_FILE '<name>'` as a simple query via `PQexec()`, and drains the
`CopyOut` with `PQgetCopyData()` -- ordinary libpq, the same way
`pg_basebackup` itself would, just speaking one extra, project-specific
command. It lives in `src/bin/pg_walserver/` rather than
`src/bin/common/`: it's pg_walserver's own `FETCH_FILE` protocol it
speaks, domain-specific logic, not a generic reusable utility, even though
the connection plumbing underneath it (`pgsql.c`) is.

`ws_fetch_file_client()`'s real, current caller is `pg_walserver
restore-wal`, a thin wrapper used directly as a standalone deployment's
own `restore_command`, mirroring `pg_walserver archive-wal`'s own role as
`archive_command` on the push side (`push_client.c`'s
`ws_push_file_client()`, same shape). This PR's own test suite (see
"Testing" below) exercises `ws_fetch_file_client()` two ways:
`pg_walserver_standalone.pgaf`'s `test_002_fetch_file` drives the
*server-side* `FETCH_FILE` command directly with a plain
`psql -c "FETCH_FILE ..."`, never touching this client code, while
`pg_walserver_archive_command.pgaf` exercises `pg_walserver restore-wal`
itself end to end.

## Replication slots

`CREATE_REPLICATION_SLOT`/`READ_REPLICATION_SLOT`/`DROP_REPLICATION_SLOT`
support physical slots only (`cmd_replication_slot.c`); logical slots are
rejected with SQLSTATE `0A000`.

A "slot" here is **not** a real PostgreSQL replication slot -- there is no
live PostgreSQL server underneath `pg_walserver` for one to exist on. It is
a small bookkeeping marker file, `.slot_<name>`, written directly in the
cluster's own directory, containing one line: `restart_lsn=<lsn>`. This is
enough to give real clients (`pg_receivewal --slot`, a standby's
walreceiver with `primary_slot_name`) the illusion of a slot they can
create, read back and drop, matching the shape of the real
`CREATE_REPLICATION_SLOT`/`READ_REPLICATION_SLOT` result rows exactly
(`slot_name`/`consistent_point`/`snapshot_name`/`output_plugin` for create;
`slot_type`/`restart_lsn`/`restart_tli` for read, including the
all-`NULL`-row-not-an-error behavior real PostgreSQL has for a
`READ_REPLICATION_SLOT` on a slot that does not exist).

Naming and safety constraints, enforced by `slot_name_is_safe()`:

- `[a-z0-9_]{1,63}`, the same character set PostgreSQL's own
  `ReplicationSlotValidateName()` enforces (`WS_SLOT_NAME_LEN_MAX` is
  `NAMEDATALEN - 1`);
- this is not just cosmetic: the name is used verbatim in the marker
  file's own filename under the cluster directory, so this validation is
  also what stops a client from writing anywhere else on disk (no `/`, no
  `..`, no leading `.`, nothing that isn't in the class above);
- at most `WS_MAX_SLOTS_PER_CLUSTER` (64) slots per cluster -- each is a file,
  counted with a directory scan (`count_slots()`) at `CREATE` time, and a
  cluster beyond the cap gets a clean `53400` error rather than an unbounded
  pile of marker files;
- `CREATE` of an existing slot is always an error (`42710`); it never
  resets an existing slot's `restart_lsn`, unlike an accidental
  double-`CREATE` silently reusing one would.

What this deliberately does **not** do yet: a replication slot here carries
no WAL-retention enforcement at all. A real PostgreSQL replication slot's
entire point is holding `pg_wal` back from recycling segments a slot's
consumer hasn't consumed yet; this `.slot_*` marker file records a
`restart_lsn` but nothing currently *reads* it to decide what may be
pruned from a cluster's WAL cache. That is intentionally left as the
prune/retention milestone's job -- the comment in
`cmd_replication_slot.h` points at `prune_archiver_wal()` in the (not yet
existing, in this PR) monitor SQL schema as where that enforcement is
expected to land, in the later archiving PR.

## The replication grammar (repl_scanner.l / repl_gram.y)

The command language above (`IDENTIFY_SYSTEM`, `BASE_BACKUP ...`, etc.) is
parsed by a real flex/bison grammar, `repl_scanner.l`/`repl_gram.y`, ported
from PostgreSQL's own `src/backend/replication/{repl_scanner.l,repl_gram.y}`
-- same token set, same `<xq>`/`<xd>` quoting states (single-quoted
strings, double-quoted identifiers, with the same escaping rules, e.g. a
doubled `""` inside a quoted identifier is a literal `"`), same grammar
shape for every command this project implements. This was a deliberate
choice over a hand-rolled parser: a replication client's own command
strings can contain quoted slot names, and a hand-rolled tokenizer is
exactly the kind of code that quietly mishandles an embedded quote or a
digit-leading identifier under a slightly unusual input -- `test_004` in
the tap spec exists specifically to prove the real grammar gets a
double-quoted slot name with an embedded escaped quote right, something a
prior, now-deleted hand-written `parse_slot_name()` did not reliably do.

What's different from the real grammar: the semantic actions build a plain
`WsCommand` struct (`repl_command.h`) directly, field by field, with a
fixed-size `WsCommandOption` array standing in for a backend `DefElem`
`List` -- there is no `Node`/`palloc()`/`List` machinery in this frontend
project to build a real parse tree with. Because every connection already
runs in its own forked child (see "Process model" above), the generated
scanner/parser don't need to be reentrant the way the backend's embedded
copy does; plain global `yylex()`/`yyparse()` state is safe here, the same
approach this project's own `pgaftest/test_spec_scan.l`/`test_spec_parse.y`
already use.

What's dropped from the real grammar: `ALTER_REPLICATION_SLOT`,
`UPLOAD_MANIFEST`, and logical `START_REPLICATION` (the `PROTOCOL_VERSION`/
`PUBLICATION_NAMES` clauses and everything downstream of them) are not
implemented -- `WsCommandType` simply has no member for them, so there is
nothing for their grammar productions to build, and they were removed
rather than kept as dead productions.

What's added: `FETCH_FILE '<name>'`, this project's own extension (see
above), including its own lexer state: `<fname>`. It is entered right
after the `FETCH_FILE` keyword specifically because a WAL segment name
(e.g. `0000000100000000000000B`) or a `.history` filename is a mix of
digits and letters that starts with a digit -- the real grammar's ordinary
identifier/keyword rules don't recognize that shape as a token at all, so
a dedicated exclusive lexer state that matches "digits and letters" as a
single `<fname>` token is what lets `FETCH_FILE` take a bare, unquoted
filename argument the way the other commands take identifiers.

The generated `repl_gram.c`/`repl_gram.h`/`repl_scanner.c` are committed
(see the `Makefile`'s own comment on `make generate`), the same pattern
`pgaftest`'s `test_spec_parse.y`/`test_spec_scan.l` already use in this
project, so building never requires `bison`/`flex` to be installed.

## HBA (hba.c / hba.h)

`pg_walserver_hba.conf` is a deliberately small subset of `pg_hba.conf`: one
rule per line, `TYPE CLUSTER USER ADDRESS METHOD`, first match wins, and
*anything* that isn't a clean match -- no match, a missing/oversize/
unreadable file, or a single malformed line anywhere in the file -- fails
closed and rejects the connection. A malformed line is never skipped over:
skipping a line that was meant to say `reject` would silently open a door.

The tokenizer (`next_hba_token()`/`hba_read_logical_line()`) is a direct,
line-by-line mirror of PostgreSQL's own `next_token()`/
`tokenize_auth_file()` in `src/backend/libpq/hba.c` -- not a flex/bison
grammar, unlike the replication command language above -- because that is
what real PostgreSQL itself uses for `pg_hba.conf`: double-quoted fields
(so a field can contain whitespace or a literal `#`, with a doubled `""`
meaning a literal `"`), an unquoted `#` starting a comment that runs to
line's end, and backslash-terminated physical lines joined into one
logical line before tokenizing. What's intentionally left out, because
this project's own HBA format doesn't use it: comma-separated lists,
`@file` inclusion, and regular expressions.

`CLUSTER` is `all`, or a cluster key exactly as it appears in `pg_walserver.ini` (see
"The clusters file" below) -- an opaque string `hba.c` never parses, splits,
or gives any filesystem meaning to. pg_auto_failover's own convention is
`"<formation>/<group>"` (e.g. `default/0`), because it reads well and is
already guaranteed unique across a whole deployment, but the `/` in it
carries no special meaning here at all: `hba.c` compares it against a
rule's `CLUSTER` field with a plain string `==`, the exact same way it would
compare `"archive1"` or any other key an operator picked by hand.

Supported `ADDRESS` forms, tried in `rule_address_matches()`:

- `all` -- matches any peer;
- an `IP/prefix` (contains `/`) -- CIDR match (`ipaddrInCIDR()`);
- `samehost` / `samenet` -- as in PostgreSQL, matching the server's own
  address or subnet (`ipaddrIsSameHostOrNet()`);
- a bare hostname -- resolved forward, every A/AAAA answer compared
  (`ipaddrHostMatchesAddress()`);
- `.domain.suffix` -- forward-confirmed reverse DNS, but checked against
  *every* PTR answer for the peer, not just the first one the way
  PostgreSQL's own check does (`suffix_matches()`); PostgreSQL's
  first-answer-only behavior is wrong for hosts and Docker networks that
  legitimately have several reverse names.

**Removed in this PR**: an earlier iteration had a `"monitor"` `ADDRESS`
keyword, matching "every node the monitor currently lists for this
cluster", backed by a `refresher.c` child process (the *only* process that
talked to the monitor, over libpq with this project's own `PGSQL`
wrapper/retry policy) and a `monitor_hosts.c` reader/matcher for the local
cache it maintained (`archiver-nodes.list`, one writer/many-readers, an
`AF_UNIX` datagram socket for "please revalidate" requests, a negative
cache on monitor failure, fail-closed past a hard staleness ceiling -- see
this PR's own git history for the full design if it's useful context). It
depended on two monitor-side SQL functions,
`pgautofailover.get_group_hosts_hash()` and
`pgautofailover.get_group_hosts()`, that simply do not exist in this PR's
own stack (no monitor extension is part of it at all), so the whole
feature was dead weight here. It has been removed in full -- `refresher.c`/
`refresher.h`, `monitor_hosts.c`/`monitor_hosts.h`, the accept loop's
forking/reaping/stopping of the refresher child and its datagram socket,
and every parameter that only existed to thread a monitor URI path or
refresh-socket path down to `hba_lookup()` (`WsAuthConfig`'s
`monitorUriPath`/`refreshSockPath` fields, `hba_lookup()`'s and
`rule_address_matches()`'s matching parameters). Writing `monitor` as a
literal `ADDRESS` today is no longer special-cased at all: it is tokenized
and matched exactly like any other bare hostname, i.e. it will be resolved
via DNS like any string that happens to say "monitor" -- unsurprising,
ordinary behavior for a keyword this build no longer recognizes, not a new
rejected-keyword error path. A `"monitor"`-backed automatic-admission
keyword is expected to return once the archiving PR's monitor schema
exists for it to query.

## Authentication

Two methods, chosen by the first matching HBA rule's `METHOD` field:

- `trust` -- accepted outright;
- `scram-sha-256` -- a real SCRAM-SHA-256 exchange (RFC 5802), server side
  implemented in `common/scram.c` (adapted from PostgreSQL's own backend
  SCRAM code) and driven here by `auth.c`'s `scram_authenticate()`. It
  speaks the exact `AuthenticationSASL`/`SASLInitialResponse`/
  `AuthenticationSASLContinue`/`SASLResponse`/`AuthenticationSASLFinal`
  sequence real libpq expects, including offering `SCRAM-SHA-256-PLUS`
  (`tls-server-end-point` channel binding, RFC 5929) first when the
  connection is encrypted. Verifiers are stored one per line in
  `pg_walserver_passwd` (`<user>:SCRAM-SHA-256$<iter>:<salt>$<stored>:<server>`),
  produced by `pg_walserver scram-secret` (password read from
  `PGPASSWORD`, never the command line). A user with no stored verifier
  still runs the *entire* exchange against a mock verifier
  (`scram_mock_verifier()`, seeded once before any connection is forked so
  every child answers identically) and fails exactly like a wrong password
  would -- so a client can't distinguish "unknown user" from "wrong
  password" by timing or response shape;
- `reject` -- refused outright, and no rule matching at all defaults to
  reject as well (`WS_AUTH_REJECT` is the zero value of `WsAuthMethod`).

Authentication runs **before** anything about the requested cluster is
revealed, exactly as PostgreSQL orders it: the HBA lookup uses the cluster
key the client asked for whether or not it turns out to be a real cluster,
and only *after* a successful authentication does an unknown cluster get
reported (SQLSTATE `3D000`, "database does not exist"). A rejection is one
generic message naming only the peer address and user (both already known
to the client), never the cluster -- and every client-supplied string is
sanitized (control characters stripped, length capped) before it is
logged, so a hostile client can't forge log lines.

TLS (`tls.c`/`tls.h`) follows the same `SSLRequest`/`'S'`-or-`'N'`/handshake
sequence real PostgreSQL uses, over OpenSSL directly (there is no
frontend-linkable TLS server anywhere in PostgreSQL/libpgcommon to reuse;
the backend's own is `be-secure-openssl.c`, backend-only). It is enabled by
`<pgdata>/server.crt`/`server.key` (or `--ssl-cert-file`/`--ssl-key-file`);
without them the server answers plain `'N'` to every `SSLRequest` and
`hostssl` HBA lines simply never match (an operator sees this immediately:
`hba_write_default_if_missing()`'s own default file comments on it, and
`cli_serve_run()` logs a warning at startup). A passphrase-protected key is
a clean startup error, never an interactive prompt.

Both the TLS handshake and the whole SCRAM exchange run entirely inside the
connection's absolute authentication deadline described in "Process
model" above -- `ws_auth_deadline_set()`/`ws_auth_deadline_clear()`
(`ws_util.c`) track it for anything (currently just the `SIGALRM` arming in
`accept_loop.c`) that needs to reason about it, and a client message before
authentication succeeds is capped at `WS_MAX_AUTH_MESSAGE_LEN` (1KiB) so an
oversized message can't be used to allocate against that deadline.

Without any `--pgdata` at all, `pg_walserver` refuses to start unless
`--insecure` is explicitly given (manual testing only, never on a
reachable network): every dbname is then accepted with no authentication
whatsoever, and there is no HBA file, no clusters file, and no TLS.

## Client certificate authentication (`clientcert=verify-full`)

An HBA rule's `METHOD` field may be followed by one more, optional field:
`clientcert=verify-full`, mirroring real PostgreSQL's own `pg_hba.conf`
"clientcert" option (`src/backend/libpq/hba.c` upstream, `ClientCertMode`/
`clientCertFull`):

```
# TYPE     CLUSTER     USER              ADDRESS       METHOD         [clientcert]
hostssl    all         archiver_repl     10.0.0.0/8    scram-sha-256  clientcert=verify-full
hostssl    default/0   pitr_restore      192.0.2.0/24  trust          clientcert=verify-full
```

The TLS peer certificate's Subject CN must equal `USER` exactly -- no user
name mapping, this project has none, matching the rest of this file's own
"keep it simple" HBA philosophy (see hba.h's own header comment on comma
lists and `@file` inclusion). How it composes with `METHOD` follows real
PostgreSQL's own documented behavior for the combination:

- `trust` + `clientcert=verify-full`: the certificate check *is* the whole
  authentication (there is no `cert` `METHOD` value in this project's own
  small HBA dialect -- `trust` plus the qualifier is how the same case is
  spelled here);
- `scram-sha-256` + `clientcert=verify-full`: **both** must succeed --
  genuine two-factor, the certificate checked first (cheap, no SCRAM
  round-trip wasted on a connection that was never going to pass anyway),
  the SCRAM exchange run only once it has. A connection presenting no
  certificate, or one whose CN does not match, is rejected outright with a
  clean `ErrorResponse` (SQLSTATE `08000`) -- it never silently falls
  through to the SCRAM exchange as if the qualifier had not been there.

### `verify-ca` is not implemented, on purpose

Real PostgreSQL also has `clientcert=verify-ca`: the certificate must chain
to a trusted CA, with no CN check. Checked against upstream's own
`CheckCertAuth()` (`src/backend/libpq/auth.c`) and `hba.c`'s
`ClientCertMode`/`clientCertName` handling: the *CA trust* check
`verify-ca` performs is already done by the TLS layer itself, unconditionally,
for any connection that presents a certificate at all, the moment
`ssl_ca_file` (here, `--ssl-ca-file`, see below) is configured -- `verify-ca`
on an HBA line restates a check that already ran during the handshake, for
every connection, regardless of which line ends up matching. It is real
PostgreSQL's own way of saying "yes, require what TLS already requires
here", not a distinct check of its own. `pg_walserver` has the same
property (`tls.c`'s `ws_tls_server_load_ca()`, below): once a CA is loaded,
every handshake already validates any client certificate presented against
it, rejecting a bad one outright, `clientcert=` or no. Implementing
`verify-ca` as a separate HBA keyword here would therefore either do
nothing a bare `hostssl` line without `clientcert` doesn't already imply
once a CA is loaded, or (worse) be misread as "no client certificate is
required at all without it" -- which is false: this project's `--ssl-ca-file`
already governs that, independent of any HBA line. `verify-full` is the one
case that adds an actual, line-specific check (the CN comparison), so it is
the only one implemented.

### TLS setup: `--ssl-ca-file`, and requesting a client certificate

`clientcert=verify-full` needs the server to have actually asked for, and
been able to validate, a client certificate in the first place -- otherwise
there is nothing for it to check. This reuses `tls.c`'s existing OpenSSL
`SSL_CTX` plumbing (the same one SNI-based cluster addressing, above, already
reads handshake metadata from) rather than adding a second TLS code path:

- `--ssl-ca-file` (default `<pgdata>/ca.crt`, the same
  `--ssl-cert-file`/`--ssl-key-file` default-path convention) names a PEM
  bundle of trusted CA certificates, mirroring real PostgreSQL's own
  `ssl_ca_file` GUC. `ws_tls_server_load_ca()` (`tls.c`) loads it with
  `SSL_CTX_load_verify_locations()` and switches `SSL_CTX_set_verify()`
  from the default (no client certificate requested at all) to
  `SSL_VERIFY_PEER` -- *requesting* a client certificate on every future
  handshake, but not `SSL_VERIFY_FAIL_IF_NO_PEER_CERT`: a client presenting
  none still completes the handshake, exactly like real PostgreSQL's own
  `be_tls_open_server()` the moment `ssl_ca_file` is set. Whether a
  certificate was actually *required* is decided per-connection, afterward,
  by which HBA rule matches -- the TLS layer only ever offers to check one,
  never mandates one on its own. A client that does present a certificate
  not signed by a CA in the bundle fails the handshake outright (OpenSSL's
  own chain validation, via the `verify_cb` callback returning whatever
  OpenSSL's own `preverify_ok` already decided).
- `--ssl-ca-file` is optional, like TLS itself: with no usable CA file, TLS
  still works exactly as before (SNI, `scram-sha-256`, everything else),
  only `clientcert=verify-full` cannot be satisfied. `cli_serve_run()`
  (`cli_root.c`) checks this at startup (and `ws_reload_config()`,
  `accept_loop.c`, again on every `SIGHUP`): a ruleset containing any
  `clientcert=verify-full` line with no CA loaded is refused outright
  (`hba_ruleset_requires_client_cert()`, `hba.c`) -- the same fail-closed
  principle this project applies to a malformed HBA line, or to more than
  one named cluster with no TLS at all, applied here to a rule that could
  never actually be satisfied.
- `ws_tls_get_peer_cert_cn()` (`tls.c`) reads the connecting client's own
  certificate CN, once the handshake (which validated it against the CA
  above, if one was presented) has already completed:
  `SSL_get_peer_certificate()` + `X509_NAME_get_text_by_NID(subject,
  NID_commonName, ...)`, the exact same plain-OpenSSL, post-handshake-read
  pattern `ws_tls_get_sni_hostname()` already established for reading
  TLS-handshake metadata in this codebase -- no new pattern introduced.
  `auth.c`'s `ws_client_cert_matches()` calls it and compares the result
  against the connecting role name, the same name SCRAM/`trust` already
  use, with a plain, case-sensitive string equality.

This is manual client certificate provisioning (an operator, or a script,
generates the CA and each per-role client certificate/key with `openssl`
directly), exactly like this project's own `create-cert` is a *server*
certificate convenience with no CA/client-cert equivalent yet -- `pg_walserver`
does not (yet) ship a `create-client-cert`-style helper; see
`tests/tap/specs/pg_walserver_clientcert.pgaf`'s own `setup{}` block for the
`openssl` invocations a real deployment would run by hand.

## The clusters file (pg_walserver.ini)

`pg_walserver.ini` (`clusters.c`/`clusters.h`) is this server's own cluster
table: one INI section per cluster it serves, mapping a cluster key -- matched
against the connection's `dbname`, i.e. what a real client puts in its
connection string's `dbname=` -- to a `path`, that cluster's own local
storage root. That's the *only* thing a cluster carries: which base backup
is current, a cluster's own system identifier, and the current WAL position
are deliberately **not** stored in the clusters file. Every command that
needs one of those instead reads it fresh, straight off a small
purpose-built file directly under that same path, at connection time --
for instance `cmd_base_backup.c`'s own `basebackups/.latest` and
`cmd_identify_system.c`'s own `pg_walserver_systemid`. `pg_walserver` itself
never talks to the monitor (see the "monitor" HBA removal above); this
per-cluster-directory split is what lets it stay that way while still always
answering with whatever is current.

Three further properties are optional, each read as a default the way
`path` never is: `upstream` (a libpq connection string to the instance
this cluster archives from, read by `fetch-systemid`/`basebackup`/`setup`
and by the embedded receivewal worker below), `hostname` (TLS SNI-based
cluster addressing, see "Addressing a cluster beyond `dbname`" below), and
`receivewal` (`receivewal = pull` opts this cluster into the embedded
receivewal worker -- see "The embedded receivewal worker" below; absent,
unaffected).

`BASE_BACKUP`, `FETCH_FILE`, `START_REPLICATION`, and the replication-slot
commands all resolve the connection's cluster once (`clusters_find()`, in
`accept_loop.c`'s `handle_connection()`) and then read/write only inside
that cluster's own directory -- there is no code path that lets a connection
authenticated against one cluster touch another cluster's files.

The parser is deliberately built directly on the vendored `ini.h`'s
low-level, dynamic-section API (`ini_load()`/`ini_section_count()`/...)
rather than this project's own `ini_file.c` wrapper: that wrapper's
`IniOption` model assumes a fixed, compile-time-known set of section/key
names, which does not fit a file whose sections are one per cluster, under
whatever key an operator (or a driver such as pg_auto_failover) picked --
unknown in advance, and changing over the life of the server.

### Cluster keys are opaque strings, not paths

A cluster key is never parsed, split on `/`, or given any filesystem meaning
of its own anywhere in this codebase -- it is matched by a plain string
`==` against `dbname` (`clusters_find()`) and, independently, against
`pg_walserver_hba.conf`'s own `CLUSTER` field (`hba_match()`), and nowhere else.
pg_auto_failover's own convention, `"<formation>/<group>"` (e.g.
`default/0`), *looks* like a path, but it is not one, and never becomes
one: the only thing that ever determines an actual directory on disk is
the cluster's own explicit `path` property, written by whoever maintains
`pg_walserver.ini` (a human, or `service_archiver_reconciler.c` in the later
archiving PR). This is a deliberate design choice, not an oversight -- see
the wildcard cluster below for why substituting a cluster key straight into a
filesystem path would be actively dangerous, given that the key is
whatever an unauthenticated client's `dbname` says it is until HBA and
SCRAM have run.

This project's whole design predates the archiver: `pg_walserver_
standalone.pgaf` (see "Testing" below) never mentions a "formation" or a
"group" anywhere, and its own `pg_walserver.ini` uses `default/0` as nothing
more than an arbitrary string a human chose to also type into `psql`'s
`dbname=` parameter. Any string works exactly the same way -- a bare
cluster name, a customer id, a UUID -- pg_auto_failover is one driver of
this file, not a requirement it imposes on it.

### The wildcard cluster (`*`)

One cluster key is special: `WS_CLUSTERS_WILDCARD_KEY` (`"*"`, `clusters.h`) is a
catch-all fallback, used when a connection's `dbname` matches no cluster of
its own. The syntax and precedence are deliberately the same as
PgBouncer's own `[databases]` `"*"` entry
(<https://www.pgbouncer.org/config.html>), on the theory that anyone who
has already run a PgBouncer knows exactly what to expect here:

```ini
# an explicit cluster always wins over the wildcard, exactly like PgBouncer
[default/0]
path = /var/lib/postgres/pgaf/default/0

# any dbname that isn't "default/0" above falls through to here
[*]
path = /var/lib/archiver/shared
```

One deliberate difference from PgBouncer: PgBouncer's own wildcard
*substitutes* the requested name into its fallback connection string
(`"bar"` behaves as `"bar = host=foo dbname=bar"`) -- safe there, because
the result is just another `dbname` handed to a real PostgreSQL server,
which validates it on its own. Doing the same thing here would mean
building a *filesystem path* out of a string an unauthenticated client
supplied before HBA or SCRAM ever ran, and a cluster key is explicitly
allowed to contain `/` (see above) -- so a naive `%r`-style substitution
would turn pg_auto_failover's own key convention into a path-traversal
primitive the moment a client sent a crafted `dbname`. `clusters_find()`
does not do this: every `dbname` that falls through to `"*"` shares that
one configured `path` verbatim, never a per-key subdirectory synthesized
on the fly. The wildcard is what makes `pg_walserver` usable with zero
multiplexing ceremony outside pg_auto_failover: a single-cluster
deployment can skip per-cluster sections entirely, keep just one `[*]`
section in `pg_walserver.ini`, and never has to learn or type a special `dbname`
value at all.

`pg_walserver_hba.conf`'s own `CLUSTER` matching is completely independent of
this: an HBA rule's `CLUSTER` field is always compared against the literal
`dbname` the client sent, never against whichever `WsCluster` `clusters_find()`
happened to resolve it to. A `hostssl all ...` rule already admits any
cluster, wildcard-resolved or not; a rule scoped to one specific cluster key
still only matches that literal key, exactly as before.

### Addressing a cluster beyond `dbname`: TLS SNI, for a real physical standby

`clusters_find()`'s `dbname`-based matching above assumes the client gets to
choose its `dbname`. A real Postgres physical standby doesn't: its own
`libpqwalreceiver.c` (`libpqrcv_connect()`) always sends the literal
`dbname=replication` for a physical replication connection, discarding
whatever `primary_conninfo`'s own `dbname=` says. One cluster, this is no
problem -- there is nothing to disambiguate. More than one, `dbname` alone
can no longer tell them apart for a real standby, only for a hand-written
`psql`/`pg_basebackup` invocation that sets `dbname` itself.

`pg_walserver` closes this gap the same way HTTPS virtual hosting does:
TLS's own Server Name Indication (SNI) extension, sent by any TLS client
during the handshake, before a single Postgres protocol byte is
exchanged -- `libpq`'s `sslsni` (on by default) sends the connection's
`host` value this way (`fe-secure-openssl.c`), so a real standby's
`primary_conninfo` already carries exactly the signal needed, in its
`host=` setting, with no client-side change at all.

- `pg_walserver.ini` gains a `hostname` cluster property (`clusters.h`'s
  `WsCluster.hostname`, `clusters.c`'s parsing), set via `pg_walserver setup
  --hostname <name>`.
- `tls.c`'s `ws_tls_get_sni_hostname()` reads it back with the simple,
  post-handshake `SSL_get_servername(activeSsl, TLSEXT_NAMETYPE_host_name)`.
  Real PostgreSQL's own matching backend feature (`ssl_sni` GUC,
  `be-secure-openssl.c`'s `sni_clienthello_cb()`) instead reads it from
  inside the raw ClientHello callback, via the lower-level
  `SSL_client_hello_get0_ext()` -- OpenSSL's own documented advice, needed
  there because that feature *switches the served certificate* based on
  the name, which is ordering-sensitive during the handshake. `pg_walserver`
  never switches certificates by SNI (one certificate serves every cluster),
  so the simpler, safe-after-the-fact read is enough here.
- `auth.c`'s `ws_authenticate()` resolves a cluster in three steps: an exact
  `dbname` match first (unchanged, and always tried first: a hand-written
  `dbname=<cluster key>` connection keeps working exactly as before, with or
  without TLS), then, only for a TLS connection, the SNI hostname, then the
  `"*"` wildcard. `pg_walserver_hba.conf`'s own `CLUSTER` matching stays exactly
  as described above -- always against the literal `dbname`, never against
  whichever cluster SNI resolved to.
- More than one *named* cluster (i.e. more than one section besides `"*"`)
  with no TLS configured is a hard error: `cli_root.c`'s `cli_serve_run()`
  refuses to start (`log_fatal`/`exit(1)`) rather than silently leaving a
  second cluster unreachable by any real standby. A single named cluster keeps
  working with no TLS at all -- `dbname` alone is already unambiguous.
- `pg_walserver setup` prepares for this automatically: adding a *second*
  named cluster creates a self-signed certificate for `--pgdata`  (reusing
  `pg_create_self_signed_cert()`, `src/bin/common/pgctl.c`, unchanged) the
  moment it's needed, and warns if that second cluster was set up without
  `--hostname` (it would then only ever be reachable via its `dbname`, or
  the `"*"` wildcard, never by a real standby).

A client TLS certificate's CN is a second, unimplemented alternative to
SNI for the same problem: `sslcert`/`sslkey` in a conninfo are also not
touched by `libpqrcv_connect()`'s `dbname` override, flowing through from
the original conninfo untouched for a real physical standby exactly as for
any other client. A cluster could be assigned its own client certificate (a
distinct CN per cluster), read during the TLS handshake
(`SSL_get_peer_certificate()` + `X509_NAME_get_text_by_NID(subject,
NID_commonName, ...)`, both plain OpenSSL, already reachable from `tls.c`)
as a cluster-selection input independent of `dbname` entirely -- the same
*mechanism* PostgreSQL's own `clientcert=verify-full` HBA option already
uses to map a certificate to a role, applied to cluster selection instead of
authentication. Not designed in detail or scheduled; SNI already resolves
the same underlying limitation (`test_006_real_standby_with_core_tools` in
`pg_walserver_standalone.pgaf` first surfaced it: a real physical standby's
walreceiver always sends the literal `dbname` `"replication"`,
`libpqrcv_connect()`'s own unconditional override, never a real cluster key)
for every deployment this PR's own test suites exercise.

## New client-side sub-commands (setup / fetch-systemid / basebackup)

Three sub-commands, alongside `serve`/`scram-secret`, all sharing
`cli_upstream.c`'s own `--cluster`/`--path`/`--upstream`/`--host`/`--port`/
`--user` resolution (an explicit flag always wins over a cluster's own
`pg_walserver.ini` properties):

- **`fetch-systemid`** (`cli_fetch_systemid.c`) -- connects to the
  upstream via `pgctl_identify_system()` (`src/bin/common/pgctl.c`, a real
  replication-mode `IDENTIFY_SYSTEM`, reused unchanged) and writes
  `pg_walserver_systemid` atomically. Refuses to overwrite an already-recorded
  *different* identifier unless `--force`: the same "never silently
  replace what's already there" principle PostgreSQL's own
  `archive_command` overwrite-safety rule applies elsewhere, here applied
  to a cluster's own identity.
- **`basebackup`** (`cli_basebackup.c`) -- takes a real base backup via
  `pg_basebackup_fetch()` (`src/bin/common/pgctl.c`; ported forward from
  where it already existed on a separate, more-advanced branch -- see that
  commit's own history for the original split and its HBA-readiness retry
  preflight, both reused as-is here), into
  `<path>/basebackups/basebackup-<UTC timestamp>/`, validates the result
  (`backup_label`/`PG_VERSION` both present -- `pg_basebackup` itself
  already guarantees a well-formed `backup_label` on a zero exit, so this
  is a defense against a partial result, not a re-parse of it), and only
  then atomically swaps `basebackups/.latest`.
- **`setup`** (`cli_setup.c`) -- the wizard: writes/validates the
  `pg_walserver.ini` section for `--cluster` (refusing to silently change an
  existing one's path unless `--force`), optionally records a `--hostname`
  for SNI-based cluster addressing (see above -- and auto-creates a self-signed
  certificate the moment a *second* named cluster needs one to stay
  reachable) and/or `--receivewal pull` (writes the cluster's own
  `receivewal = pull` property, see "The embedded receivewal worker" below --
  `setup` only ever *records* the intent, it never itself starts or
  touches the receivewal worker; that happens the next time `serve` starts), then
  calls `fetch-systemid`'s own logic (whose `pgctl_identify_system()`
  connection doubles as this step's role-permission check: PostgreSQL
  refuses a replication-mode connection for a role lacking `REPLICATION`
  at the *backend* level, independent of HBA -- an earlier version of this
  file ran a separate plain-SQL `pg_roles.rolreplication` check first,
  removed because that connection targets an ordinary database, which the
  replication role's own HBA rule usually does not admit at all). This
  role-permission check is already the stricter of the two an earlier
  design draft once weighed (a real `replication=true` connection vs. a
  plain `pg_roles.rolreplication` read): `pgctl_identify_system()` *is* a
  real replication-mode connection, so a role that passes it is already
  proven to work end to end with `pg_basebackup`/`pg_receivewal`, catching
  HBA misconfiguration on the *upstream* side too, not just the role's own
  attribute. `setup` never takes a base backup itself any more (it used to,
  behind a now-removed `--with-basebackup` flag -- see "Bootstrapping a
  cluster's first base backup" below for why, and for what replaced it): its
  very last step is to reload an already-running `serve` for the same
  `--pgdata`, if there is one (`<pgdata>/pg_walserver.pid`, the same
  `read_pidfile()`/`SIGHUP` shape `reload` itself uses, see "Config reload"
  below), so it picks up the new/changed cluster immediately; with no server
  running, the config just written simply takes effect the next time
  `serve` starts -- logged, not an error, a normal and expected case (e.g.
  setting a cluster up before `serve` has ever been started for this
  `--pgdata`).

None of the three touch `pg_walserver_hba.conf` or `pg_walserver_passwd` -- a
deliberately separate concern an operator (or `pg_autoctl`, later) still
configures on its own, see `docs/ref/pg_walserver.rst`'s own worked
example for the full sequence including those.

## Bootstrapping a cluster's first base backup

`setup` used to take a cluster's first base backup itself, synchronously,
behind a `--with-basebackup` flag -- removed. `pg_walserver serve` takes it
instead, automatically, once, for any currently-configured cluster that is
still missing one (`cli_basebackup_cluster_has_backup()`, cli_basebackup.c:
`<path>/basebackups/.latest` exists and is non-empty): right after startup
(once `ws_receivewal_start_all()` has started every cluster's own real
receivewal worker), and right after a successful `SIGHUP` reload (once
`ws_receivewal_reload()` has reconciled the receivewal worker set against the newly
reloaded clusters) -- `accept_loop.c`'s own `ws_bootstrap_missing_backups()`
is the single entry point both call. These are the *only* two moments this
ever happens; there is no other trigger, and no recurring/scheduled
backup of any kind -- see this section's own "Recurring backups are not
this project's job" paragraph below.

This replaced an earlier design (this PR's own history, see `git log` on
`receivewal.c`/`cli_setup.c`) where `setup --with-basebackup` primed a
*throwaway* embedded receivewal worker just long enough to prove the base backup's
own start LSN was covered, then tore it down before `serve` ever started
its own real one for the same cluster. That design worked, but existed only
to compensate for base backups being taken too early -- before `serve`,
and therefore before any real receivewal worker, had ever run for the cluster at all.
Moving the base backup itself into `serve` removes the problem at its
source: by the time `serve` ever decides a cluster needs a bootstrap backup,
its own real, supervised receivewal worker for that cluster (if `receivewal = pull`) has
already been started, so there is always a genuine one to wait on directly
-- no throwaway primer, no teardown dance, no separate "priming" code path
to keep in sync with the real one.

For a `receivewal = pull` cluster, `ws_bootstrap_missing_backups()` first waits
(bounded, `backup_bootstrap.c`'s own `WS_BOOTSTRAP_STREAM_WAIT_*`
constants) for that cluster's own real receivewal worker to show genuine on-disk
evidence of streaming (`wal_dir_has_any_segment()`, `wal_dir_scan.c` --
true even for a still-growing `.partial` segment, so a caller doesn't spin
until an entire segment happens to fill) before ever taking the backup --
the same "the backup's own start LSN must already be covered by captured
WAL" property the removed primer used to guarantee, now proven against the
real receivewal worker instead of a throwaway one. A cluster with no `receivewal = pull`
has no such wait: its `archive-wal`-driven push has no equivalent gap to
close (see "The archive push side" below), so the backup is taken right
away.

Taking the backup itself reuses `pg_walserver basebackup`'s own already-
public logic (`ws_basebackup_execute()`, `cli_basebackup.c`) directly,
in-process, after a plain `fork()` (`backup_bootstrap.c`'s own
`ws_backup_bootstrap_start()`) -- deliberately **not** a new hidden
`internal service basebackup` entry point mirroring the embedded
receivewal worker's own `fork()`+`execv()`-into-a-hidden-sub-command shape
(`cli_internal.c`): that shape exists so a *restarted, long-lived* service
picks up a replaced binary on disk without the supervising `serve` process
itself needing to restart, a live-upgrade concern that simply does not
apply to a one-time, transient job that runs once and exits. A plain
`fork()` calling straight into existing, already-tested logic is the
simplest correct fit, and keeps the reaping story simple too: the forked
child is tracked in `accept_loop.c`'s own `bootstrapChildren` array,
reaped through the exact same single wildcard reaper every other child in
this process already goes through (see "The single wildcard reaper"
above) -- without that tracking, an otherwise perfectly normal exit would
be misreported as an "unknown subprocess" error.

Retries are bounded, not indefinite: `backup_bootstrap.c`'s own
`WS_BOOTSTRAP_BACKUP_MAX_ATTEMPTS` (3), a short fixed delay between
attempts, all within that one forked child's own lifetime -- deliberately
not the full `process_supervisor.h` `MaxR`/`MaxT` ring-buffer machinery
`receivewal.c`'s own long-lived receivewal worker services use (see "Restart backoff"
above): that machinery is built for a service restarted many times over a
process's whole lifetime, tracking restarts against a sliding time window,
which is more than a single one-shot child that runs once needs. On final
failure, a clear error is logged and the child exits; the cluster keeps
serving whatever it already has (nothing about this ever brings the cluster,
or `pg_walserver` itself, down), and it is retried again automatically the
next time `serve` starts or reloads -- if the underlying problem (e.g. the
upstream still being unreachable) hasn't cleared by then, the operator's
own manual `pg_walserver basebackup` invocation is what closes the gap in
the meantime.

**Recurring backups are not this project's job.** This one-time bootstrap
attempt, at either of its two trigger points, is the only "automatic" base
backup behavior `pg_walserver` has or will have. Keeping a cluster's backup
current after that first, automatic one -- on a schedule, after a certain
amount of WAL, or on any other policy -- is deliberately left to the
operator's own `pg_walserver basebackup` invocation (by hand, or from
their own cron job around it): this project provides the facility, not the
scheduling policy, the same philosophy `archive-cleanup` (below) follows
for WAL retention.

## `archive-cleanup`: WAL and base-backup retention

`pg_walserver archive-cleanup --cluster <name> --pgdata <path> | --path
<dir> [--keep-count <N>] [--keep-age <interval>] [--dry-run] [--force]`
(`cli_archive_cleanup.c`) removes WAL segments/`.partial`/`.backup` files
and base backups a cluster no longer needs, mirroring real PostgreSQL's own
`pg_archivecleanup` contrib tool -- same filename-prefix-extraction
algorithm for `.partial`/`.backup` files, same ignore-the-timeline
string comparison (`SetWALFileNameForCleanup()`/`CleanupPriorWALFiles()`,
`src/bin/pg_archivecleanup/pg_archivecleanup.c`) -- extended to also
retire base backups, which stock `pg_archivecleanup` explicitly does not
do (it has no notion of a base backup, and therefore no way to know
whether deleting a given segment would leave one unrestorable).

**Retention is infinite by default**, the same way vanilla PostgreSQL
never runs `pg_archivecleanup` on its own -- it is always operator-wired,
via `archive_cleanup_command` or a cron job. `archive-cleanup` follows the
exact same philosophy `pg_walserver basebackup` itself already does for
recurring backups (see "Bootstrapping a cluster's first base backup" above,
its own "Recurring backups are not this project's job" paragraph):
`pg_walserver` provides the facility, never the scheduling policy, and
never deletes anything on its own initiative. At least one of
`--keep-count`/`--keep-age` is therefore required; running with neither
is refused outright rather than silently deleting everything, which is
what "no constraint" would otherwise mean.

**Whichever of `--keep-count`/`--keep-age` is more conservative (keeps
more) wins** when both are given: a base backup, or a WAL segment, is
only ever removed once *both* constraints independently agree it may
go -- never when either flag alone would still want it kept. Concretely,
each flag computes its own "keep" set (the N most recent backups for
`--keep-count`, everything newer than the cutoff for `--keep-age`); the
final kept set is their union, and the WAL retention cutoff is the
starting segment of the oldest backup in that union. This is the same
"intersection of what may be deleted, not union of it" logic pgBackRest
and general-purpose retention tools use, applied here as a union of what
must be kept.

`basebackups/.latest` -- whichever backup it currently names -- and every
WAL segment that backup's own `backup_label` requires (`read_backup_
label()`, `cmd_base_backup.c`, the same parser `cmd_base_backup.c` itself
uses to answer a real `BASE_BACKUP` request) are never removed by either
rule, regardless of age or count: the currently-latest backup must always
stay restorable. Independently of the count/age retention decision, a
non-latest backup whose own required starting WAL segment is *already*
missing on disk (e.g. from a previous, partial `archive-cleanup` run, or
external interference) is also removed, logged as "superseded" rather
than "past the retention cutoff" -- it is already unrestorable, so there
is nothing left to protect by keeping its directory around.

`--keep-age <interval>` requires an explicit suffix -- `h` (hours), `d`
(days), `w` (weeks), or `m` (calendar months) -- there is no bare-number
default, since a bare number's unit would be ambiguous. `m` is real
calendar-month arithmetic (`struct tm` plus `timegm()`, entirely in
`src/bin/common`-reachable frontend code -- this project links no backend
date/time code at all), not a fixed 30-day approximation, since month
lengths vary; going back one month from a day that doesn't exist in the
target month (e.g. March 31st minus one month, since February 31st
doesn't exist) normalizes forward the same way `mktime()`/`timegm()`
always normalizes an out-of-range `struct tm`.

Reuses `wal_dir_scan.h`'s own `ws_cluster_wal_segment_size()`/
`wal_segment_filename()` for WAL segment math (no re-derivation of this
project's own WAL filename parsing), and `cmd_base_backup.c`'s own
`read_backup_label()` (exported for this purpose) to learn each backup's
required starting WAL position -- the same file `BASE_BACKUP` itself
already parses to answer a real client. `--dry-run`/`-n` (matching
`pg_archivecleanup`'s own flag) logs exactly what would be removed, and
why (age cutoff, count cutoff, or superseded), without removing anything.

**A pre-flight WAL-continuity check runs before any deletion at all**
(`ws_check_wal_continuity()`, `cli_archive_cleanup.c`), in both `--dry-run`
and a real run. The count/age retention math above decides *which*
backups and WAL are kept; this check separately verifies that every kept
backup's own required starting WAL segment can actually still walk
forward, with no missing segment, to wherever it needs to reach -- the
next newer kept backup's own start segment, or, for the newest kept
backup, the newest WAL segment actually present on disk. A gap can exist
for reasons that have nothing to do with this run's own retention cutoff
at all (an `archive_command` outage, a disk problem, manual tampering, or
even a previous `archive-cleanup` run under different flags), and this
tool must never silently go on to delete other files while leaving a
backup it is supposedly protecting unusable for PITR.

A timeline switch between two kept segments is not by itself a gap:
`"%08X.history"` files (the same shape `cmd_timeline_history.c` already
serves) record, for the timeline they belong to, the parent timeline and
the exact LSN the switch happened at. The check walks that ancestry chain
from the newer boundary's own timeline down to the older one, splits the
segment-number range at each recorded switchpoint, and requires every
segment number in range to be present under whichever timeline owned it
at that point -- never flagging a false gap merely because two adjacent
kept segments' timeline bytes differ.

If a problem is found, `archive-cleanup` refuses the **entire** operation
by default: a specific `log_error`/`log_fatal` names the backup and the
missing segment/range, nothing is deleted at all (not even the otherwise-
safe parts -- a cron job silently doing a partial cleanup that still
degrades the DR posture is exactly the failure mode being guarded
against), and the process exits non-zero. `--dry-run` still runs this
check and reports exactly the same problem, without needing `--force` and
without ever deleting anything (a dry run never deletes regardless).

**`--force`** bypasses this specific refusal only -- it has no effect on
what `--keep-count`/`--keep-age` themselves decide to remove. It exists
for an operator who has independently verified some other way that
proceeding is safe (e.g. an independent backup elsewhere, or a known/
accepted gap) -- **a default, unattended cron job should never blindly
pass `--force`**.

## `create-cert`: a self-signed TLS certificate on demand

`pg_walserver create-cert --pgdata <path> --hostname <name> [--force]`
(`cli_create_cert.c`) writes `<pgdata>/server.crt`/`server.key` via
`pg_create_self_signed_cert()` (`src/bin/common/pgctl.c`) -- the exact
function `setup`'s own `ensure_tls_for_multiple_clusters()` calls
automatically the moment a second named cluster needs a certificate (see
above); `ws_create_cert_run()` (`cli_create_cert.c`) is the one shared
helper both now call, so the call-and-log sequence isn't duplicated
between the automatic and the by-hand path. Useful whenever an operator
wants TLS in place from the very first cluster (a single cluster served over
a reachable network still benefits from encryption, even though SNI-based
cluster addressing itself doesn't need it yet), or wants to replace an existing
self-signed certificate. Refuses to overwrite an already-existing
`server.crt`/`server.key` unless `--force` -- the same "never silently
replace what's already there" principle as `cli_fetch_systemid.c`'s own
systemid overwrite check, applied here to the certificate files instead.

## The archive push side: `CHECK_FILE` + `ARCHIVE_FILE` + `pg_walserver archive-wal`

**`archive_command`, not (yet) an `archive_library` module.** PostgreSQL 15
added the archive modules API (`archive_library`, see `contrib/basic_archive`
for the reference implementation): a loadable C module the archiver process
calls directly, in-process, per segment, instead of forking a shell command.
Its classic advantage over `archive_command` is holding a connection open
*across* invocations -- the same way an `archive_command` script commonly
keeps an SSH connection alive between calls rather than reconnecting every
time -- so a system generating a lot of WAL at peak activity doesn't pay a
fresh connection/handshake cost (here, a fresh connection to `pg_walserver`
itself) for every single segment. `archive-wal` today reconnects each
invocation, exactly like a plain `archive_command` script would; a
persistent-connection mode of operation, closer to what an `archive_library`
module would give us, is planned as later work rather than built now. An
`archive_library` module would also need to be compiled against the exact
server version and installed (with a restart) on every Postgres instance
being archived -- server-side coupling `archive-wal` as a plain external
command avoids, and a reason to prefer solving the reconnection cost with a
persistent-connection mode over adopting the module API outright.

Alongside the pull-oriented tools above, `pg_walserver` also accepts a
*push*: `CHECK_FILE`/`ARCHIVE_FILE` (see "The wire protocol" above for
their wire shape and overwrite-safety rule) and the `pg_walserver archive-wal`
client sub-command that drives them, meant to run as (part of) a Postgres
`archive_command`. This section documents that design as built.

A cluster with `receivewal = pull` configured has its own embedded, supervised
`pg_receivewal` (see "The embedded receivewal worker" below) writing straight
into that cluster's own directory. A push from `archive-wal` racing that
receivewal worker's own write for the same final filename, with no coordination
between the two, is unsafe. `archive-wal` avoids the race by ordinarily never
pushing at all on such a cluster: it runs `CHECK_FILE` only, ever, and leaves
delivering the segment entirely to the receivewal worker. A cluster with no
`receivewal = pull` has no such writer to race, so `archive-wal` pushes via
`ARCHIVE_FILE` only, ever, with no `CHECK_FILE` round trip first.

The one exception on a `receivewal = pull` cluster is `CHECK_FILE`'s own
smart-fallback signal (`cmd_check_file.c`): a streaming worker can only
ever move forward, so a segment it has already streamed *past* without
ever producing -- almost always a timeline switch that left a segment
behind on the old timeline -- is a genuine hole it can never
retroactively fill, not the ordinary "hasn't caught up yet" case
PostgreSQL's own retry loop already handles. `CHECK_FILE` detects this
cheaply, from the receivewal worker's own last-observed position
(`<path>/receivewal-progress`, already written for `pg_walserver ps`/
`status`/`list clusters` to display -- no extra directory scan added to
this hot path), and tells `archive-wal` to push the file directly via
`ARCHIVE_FILE` in that one case, rather than waiting on a retry loop that
would otherwise never succeed and leave WAL piling up on the primary.

`pg_walserver archive-wal <path-to-file> <filename> --cluster <name> --host
<host> [--port <port>] [--user <name>] [--sslmode <mode>]`
(`cli_archive.c`) picks between these two disjoint behaviors automatically,
every invocation, from the connected cluster's own actual `receivewal` setting
-- `SHOW receivewal` (`cmd_show.c`, an extension to the existing `SHOW` wire
command alongside `wal_segment_size`), never a manually-set client flag,
which would silently go stale the moment an operator changes the cluster's
`receivewal` setting without also updating every `archive_command` line
referencing it:

- **`receivewal = pull`**: `CHECK_FILE` only, ordinarily. `matches` -> exit 0,
  nothing to push. `missing`/`differs` with `fallback = no` -> exit 1 with a
  clean stderr message, no sleep, no retry loop, no `ARCHIVE_FILE` call at
  all -- PostgreSQL's own `archive_command` retry loop is the entire retry
  mechanism, calling `archive-wal` again later, cheaply, until the
  receivewal worker catches up. `missing`/`differs` with `fallback = yes`
  (the receivewal worker has already streamed past this exact file, a hole
  it can never retroactively fill) -> pushes it directly via `ARCHIVE_FILE`
  right away instead, exactly like the no-`receivewal` case below, then
  exits 0/nonzero on that push's own result.
- **no `receivewal = pull`** (absent or `receivewal = none`): `ARCHIVE_FILE`
  only, unconditionally pushing the full file every invocation, computing
  its local size and CRC32C (`file_crc32c()`, `src/bin/common/
  file_crc32c.c`, shared with `pg_autoctl`'s own `nodespec.c` file-change
  detection, backed by the same `INIT_CRC32C`/`COMP_CRC32C`/`FIN_CRC32C`
  facility real Postgres itself uses) only for the `CHECK_FILE` path, not
  this one. The server's
  own overwrite-safety in `cmd_archive_file.c` (compare real bytes on disk
  vs. real bytes just received; identical -> idempotent success, different
  -> reject) already makes this safe and idempotent on PostgreSQL's own
  retries with no pre-check needed.

Exit code matches PostgreSQL's own `archive_command` contract exactly: `0`
on success, nonzero with a clean stderr message on any failure, so
PostgreSQL retries forever -- this project's own precedent for "the caller
(Postgres) is our retry loop, one attempt per invocation, no local
retry-count state" applies here exactly as it does wherever else in this
codebase an `archive_command`-shaped contract is honored (e.g.
`archiver_confirm.c`).

Deliberately does **not** reuse `cli_upstream.c`'s `cli_resolve_upstream()`
as-is: that helper resolves a `WsUpstreamTarget` (a `NodeAddress` +
`SSLOptions` shaped for `prepare_primary_conninfo()`, i.e. a real
*Postgres* connection) the way `fetch-systemid`/`basebackup` connect *out*
from `pg_walserver` to an upstream Postgres instance. `archive-wal` connects
the other way, to a different kind of server entirely: it runs *on* the
Postgres primary itself, as `archive_command`, connecting *to*
`pg_walserver`'s own replication-protocol server -- a plain libpq
connection issuing `CHECK_FILE`/`ARCHIVE_FILE` as simple queries, exactly
like `fetch_client.c`'s own `FETCH_FILE` client, with no
`ReplicationSource`/`pgctl.c` involved at all. The shared `WsWalServerTarget`
(`cli_wal_target.h`) mirrors `cli_upstream.h`'s flag *names*
(`--cluster`/`--host`/`--port`/`--user`) for consistency, but is resolved
directly by `cli_wal_target_getopt()` (`cli_wal_target.c`) rather than
through `cli_resolve_upstream()`. `restore-wal` (below) and
`archive-cleanup` (see "`archive-cleanup`" below) share this exact same
struct and parser -- `WsArchiveTarget`/`WsRestoreTarget` used to be two
byte-for-byte-identical structs, one per command, before being folded into
this one.

### The restore side: `pg_walserver restore-wal`

The read-side counterpart, `pg_walserver restore-wal <filename>
<destination-path> --cluster <name> --host <host> [--port <port>] [--user
<name>] [--sslmode <mode>]` (`cli_restore_wal.c`), is meant to be used
directly as (part of) a Postgres `restore_command`. Both `archive-wal` and
`restore-wal` are named with an explicit "-wal" suffix, not the bare
"archive"/"restore", precisely because `pg_walserver` already has a
separate `basebackup` sub-command -- see `cli_archive.h`'s own header
comment for the full naming rationale (shared by both).

Unlike `archive-wal`, `restore-wal` implements no protocol of its own: it
is a thin CLI wrapper around `fetch_client.c`'s own
`ws_fetch_file_client()` (see "FETCH_FILE's client" above), which does the
actual `FETCH_FILE '<name>'` round trip and the same-directory-temp-file-
plus-`rename()` dance that keeps a killed/interrupted restore from leaving
a partial file where Postgres expects a complete one. It mirrors
PostgreSQL's own `restore_command` substitution order, `%f` (the bare
filename recovery wants next) then `%p` (the local path to write it to) --
the reverse of `archive_command`'s own `%p %f` order `archive-wal` takes.
Exit code matches PostgreSQL's own `restore_command` contract exactly: `0`
with the file written on success, nonzero with a clean stderr message
otherwise -- including the ordinary "not found" case recovery hits at the
end of the available WAL, which this client does not try to distinguish
from any other failure (see `cli_restore_wal.h`'s own header comment).

`restore-wal` connects to `pg_walserver` itself, exactly like `archive-wal`
and for the exact same reason (see "Deliberately does **not** reuse
`cli_upstream.c`'s `cli_resolve_upstream()`" just above) -- it therefore
does not reuse `cli_upstream.c` either, and shares `cli_archive.h`'s own
`WsWalServerTarget`/`cli_wal_target_getopt()` (`cli_wal_target.h`/`.c`) for
the same `--cluster`/`--host`/`--port`/`--user`/`--sslmode` flags, rather
than duplicating them.

## The embedded receivewal worker (receivewal.c)

A cluster with `receivewal = pull` (`pg_walserver.ini`, written by hand or by
`pg_walserver setup --receivewal pull`) gets its own forked, supervised
`pg_receivewal` child the moment `serve` starts -- no external
`pg_receivewal` process, no separate supervisor unit, nothing else to
wire up. `pg_walserver --pgdata ... serve` alone, with one cluster's
`upstream`/`receivewal = pull` set, is a complete archiving daemon on its
own. This section documents the design as built.

**`fork()` + `execv()` of this same binary, mirroring `pg_autoctl`'s own
long-lived-service pattern.** Each receivewal worker child is started by forking,
then `execv()`-ing `pg_walserver` itself, re-entered as the hidden
`pg_walserver internal service pg-receivewal --cluster <key> --upstream
<conninfo> --path <dir>` sub-command (`cli_internal.c`), which calls
`pg_receivewal_main()` (the vendored entry point, `src/bin/common/
vendor/pg_receivewal/pg_receivewal_entry.h` -- see "Vendor relocation"
below) in-process -- still no real `pg_receivewal` binary needs to be
installed/on `$PATH` anywhere. This mirrors `pg_autoctl`'s own
`service_postgres_ctl_start()`-style services exactly (`cli_do_root.c`'s
own `"pg_autoctl internal service postgres|listener|node-active"`), not
`accept_loop.c`'s own per-*connection* fork-with-no-exec model: a
deliberately different lifecycle (see "Supervision shape" below), and a
deliberately different choice from a bare `fork()` too -- `execv()`-ing
the binary from disk is what makes a restarted receivewal worker safe to run as
part of a container's PID 1, the same property that already makes
`pg_autoctl` "safe to use as PID 1 in Docker/Kubernetes containers where
replacing the binary and sending SIGTERM would lose the container": if
the `pg_walserver` binary on disk has been replaced in place, a restarted
receivewal worker automatically picks up the new version without the supervising
`serve` process itself needing to be replaced or restarted. `main.c`
resolves this binary's own absolute path once at startup
(`set_program_absolute_path()`, `src/bin/common/file_utils.c`, already
existed for `pg_autoctl`'s own identical need) so this `execv()` call
always has a real, absolute path to re-exec, not a bare/relative `argv[0]`
that only happened to resolve via `$PATH` once, at the parent's own
startup.

**Supervision shape: one long-lived child per active cluster, not
per-connection, built on this project's own generic child-process
supervisor.** A receivewal worker child is alive for the server's *whole*
lifetime, independent of any client connection, and needs restart-on-
crash -- unlike a connection child, simply reaped and forgotten the
moment it exits. Rather than a bespoke `fork()`/`waitpid()`/backoff loop,
`receivewal.c` builds one `ProcessService` (`src/bin/common/process_
supervisor.h`) per `receivewal = pull` cluster and hands them to that file's
own generic supervisor: `ws_receivewal_start_all()` forks every configured
receivewal worker once, at `serve` startup (`cli_root.c`'s `cli_serve_run()`,
right after `pg_walserver.ini`/HBA validation succeeds -- the same place
`ensure_tls_for_multiple_clusters()`-adjacent logic already runs);
`ws_receivewal_tick()` is called once per `ws_accept_loop()` iteration to
detect a dead receivewal worker and restart it (see "The single wildcard reaper"
below for why this is also where connection children get reaped now);
`ws_receivewal_stop_all()` signals every receivewal worker to stop cleanly (`SIGINT`,
matching what upstream `pg_receivewal` itself documents as its own
clean-stop signal -- the same signal `pg_autoctl`'s own
`service_archiver_pgreceivewal_ctl.c` sends its own pg_receivewal child,
for the same reason; **never** `SIGKILL` on the first try) as
`ws_accept_loop()` itself shuts down, with a bounded wait and a `SIGKILL`
escalation for anything still alive past it -- no orphaned
`pg_receivewal` process ever survives `pg_walserver`'s own exit.
`process_supervisor.h`'s own header comment explains why this is a
*decoupled extraction* of `pg_autoctl`'s own `supervisor.c` generic core,
not a literal relocation of that file (too deeply entangled with
`pg_autoctl`'s own keeper/monitor/node-spec subsystem to move wholesale).

**The single wildcard reaper.** `accept_loop.c`'s own connection-child
bookkeeping used to run its own, independent `waitpid()` loop -- exactly
the shape that already caused a real bug during this feature's own
development: two independent reapers (one in `accept_loop.c`, one for the
receivewal worker children) each calling `waitpid()` for children of the *same*
process can race for the same exited child's status, and whichever call
happens to run first silently consumes it, permanently hiding that
child's death from the other (`waitpid()` on an already-reaped pid
returns `-1`/`ECHILD`, not the exit status the loser needed) -- the exact
bug `pg_autoctl`'s own `service_archiver_pgreceivewal_ctl.c` header
comment already describes hitting, for the analogous reason, on the
pgaf-integrated side. Fixed structurally, not by coincidence: `ws_receivewal_
tick()` (`receivewal.c`) is now the *only* place in the whole process allowed
to call `waitpid(-1, ...)` (via `process_supervisor_tick()`'s own wildcard
loop underneath it); `accept_loop.c`'s own connection children are
threaded through as that call's `otherChildExited` callback instead of
running a second wildcard wait of their own. This also gives
`pg_walserver` PID-1-safe orphan reaping for free: a grandchild
reparented to it by the kernel (only possible when running as PID 1
inside a container) is logged at INFO and otherwise ignored, exactly like
`pg_autoctl`'s own supervisor already does for itself.

**Restart backoff.** A receivewal worker that cannot reach a dead/unreachable
upstream must not hot-loop forever re-forking (whose own `GetConnection()`
failure `exit()`s immediately on the very first connection attempt, with
no internal retry of its own). Rather than inventing new constants,
`process_supervisor.h`'s own Erlang-inspired MaxR/MaxT policy is reused
here verbatim (`PROCESS_SUPERVISOR_MAX_RETRY`/`_MAX_TIME`, the same
values -- 5 restarts per 300 seconds -- `pg_autoctl`'s own
`SUPERVISOR_SERVICE_MAX_RETRY`/`_MAX_TIME` already use for the identical
problem): a service restarted more than `MaxR` times within the last
`MaxT` seconds stops being restarted. One deliberate policy difference
from `pg_autoctl`'s own `supervisor.c`: giving up on one receivewal worker here
does **not** bring down `pg_walserver` itself or any other cluster's own
receivewal worker -- unlike `pg_autoctl`, where each service is essential to the
single node it manages, `pg_walserver` may be serving several independent
clusters at once, and one cluster's truly broken upstream should not stop
every other cluster it is otherwise serving correctly.

**Vendor relocation.** `vendor/pg_receivewal/` used to live under
`src/bin/pg_autoctl/`, linked only by `pg_autoctl`; it now lives under
`src/bin/common/vendor/pg_receivewal/` so both `pg_autoctl` (the
pgaf-integrated archiver's own capture controller) and `pg_walserver`
(this file) build the identical vendored sources rather than duplicating
them -- the same move `fetch_client.c` already went through earlier in
this PR (see "FETCH_FILE's client" above) when `pg_autoctl` needed
something `pg_walserver` used to own. `Makefile.common`'s own
`PG_RECEIVEWAL_VENDOR_DIR`/`PG_RECEIVEWAL_VENDOR_OBJS` own the shared
include path and object-build rule (named `pgrw-*.o`, not `vendor-*.o`, so
this pattern rule can never collide with a caller's own
`vendor-%.o: .../vendor/%.c` rule, e.g. this project's own `vendor/tar.c`
above); each of `pg_autoctl`'s and `pg_walserver`'s own Makefiles links
`$(PG_RECEIVEWAL_VENDOR_OBJS)` plus the same static libs any frontend
replication-protocol client needs (`-lpgfeutils -lpgcommon -lpgport`).

## Config reload (pidfile, SIGHUP, `pg_walserver reload`)

`serve` writes its own pid to `<pgdata>/pg_walserver.pid` (a plain one-line
pidfile, written directly rather than through `src/bin/common/pidfile.h`'s
own `create_pidfile()` -- that function writes pg_autoctl's own multi-line
supervisor pidfile format and requires the `PGDATA` environment variable,
neither of which fits pg_walserver's single-process, `--pgdata`-driven
model; `read_pidfile()`/`remove_pidfile()`, the generic single-PID half of
that same API, are reused as-is), removed again on clean shutdown. `pg_
walserver reload --pgdata <path>` (`cli_root.c`) reads that pidfile and
sends `SIGHUP`, the same shape as `pg_ctl reload` -- exit 0 once the signal
was delivered, nonzero with a clear error for a missing, stale, or
unreadable pidfile. It ignores `SIGHUP` in its own, one-shot process first,
before sending it on, for the same reason `pg_autoctl`'s own
`cli_pg_autoctl_reload()`/`cli_service_reload()` do (`cli_common.c`): a
freshly exec'd one-shot command installs no `SIGHUP` handler of its own,
and could in principle reuse the pid of a just-exited process, in which
case an unhandled `SIGHUP` would terminate it before it ever sends
anything.

`pg_walserver.ini` and `pg_walserver_hba.conf` are parsed once, at `serve`
startup, into an in-memory `WsServerConfig.clusters`/`WsAuthConfig.
hbaRuleSet` (`accept_loop.h`) -- every connection reads that same snapshot,
none of them re-parses either file off disk itself. `SIGHUP` (`ws_accept_
loop()`'s own main loop, alongside `receivewal.c`'s `ws_receivewal_tick()`) calls
`ws_reload_config()`, which re-reads both files (`clusters_load()`, `hba_
parse_file()`) and swaps them in **only when both parse successfully**,
exactly like real PostgreSQL's own `SIGHUP`-triggered `ProcessConfigFile()`:
a bad reload is refused, logged clearly, and the previous, still-valid
configuration keeps serving every connection -- never a half-applied one.
Every forked connection child is unaffected either way, since it only ever
reads whatever snapshot was already installed the moment it was forked.

What is live-reloadable this way:

- **clusters** (`pg_walserver.ini`): added, removed, and changed clusters
  (`path`/`upstream`/`hostname`/`receivewal`) are logged by key, one line per
  change, plus a one-line summary;
- **the HBA ruleset** (`pg_walserver_hba.conf`): logged as a rule-count-plus-
  content comparison (a full rule-by-rule diff was judged not worth the
  extra complexity) -- "unchanged (N rules)" or "changed (N rules before,
  M after)";
- **the embedded receivewal worker set** (`receivewal.c`'s `ws_receivewal_reload()`):
  reconciled against the newly reloaded clusters, without ever restarting a
  receivewal worker whose own cluster did not change -- a cluster that newly has
  `receivewal = pull` gets a receivewal worker started; one that lost it, or whose
  cluster disappeared entirely, gets its receivewal worker stopped (`SIGINT`); one
  whose `upstream`/`path` changed while `receivewal = pull` stayed on is
  stopped and, once reaped, automatically restarted with the new values by
  the same `PROCESS_RP_PERMANENT` restart-on-exit path `ws_receivewal_tick()`
  already runs for a crashed receivewal worker -- it cannot retarget an
  already-forked/exec'd `pg_receivewal` child in place, so this is always a
  stop-then-start, never a live retarget. Every start/stop/restart decision
  is logged.

What is **not** reloaded by `SIGHUP`: the TLS certificate/key. `tls.c`
builds a single, process-wide `SSL_CTX` once at startup; hot-swapping it
safely (in the middle of connections that may already be mid-handshake)
was judged a bigger, riskier lift than the rest of this feature justifies.
A rotated `server.crt`/`server.key` needs a real restart of `pg_walserver`
for now -- a one-line note to this effect is logged the first time `SIGHUP`
is ever handled. `pg_walserver_passwd` (SCRAM verifiers) is unaffected by any
of this either way: it was already read fresh on every authentication
attempt, before this feature existed, and still is.

## Inventory and process visibility: `ps`, `ls`, `status`, `list clusters/backups/wal`

Six read-only sub-commands, none of which mutate anything on disk: `ps`
and `status` report on a running `serve`'s own process state; `ls`
inventories pg_walserver's own configuration footprint; `list
clusters`/`list backups`/`list wal` inventory the archived data itself.

### Cross-process visibility: how `ps`/`status` see inside a running `serve`

`pg_walserver ps`/`pg_walserver status` run as brand-new, one-shot
processes, entirely separate from whatever `pg_walserver serve` process
may be running for the same `--pgdata` -- they cannot read `receivewal.c`'s
own in-process `receivewalClusters`/`receivewalServices` arrays, or
`accept_loop.c`'s own `bootstrapChildren` array, because those simply do
not exist in a different process's address space.

Two mechanisms were available. `/proc` scraping would work for *half* of
the picture: every embedded receivewal worker child is `exec()`'d as
`pg_walserver internal service pg-receivewal --cluster <key> ...`
(`receivewal.c`), so its cluster key is recoverable from `/proc/<pid>/cmdline`
by any process willing to walk `/proc`. It does not work for the other
half: the one-shot bootstrap-backup job (`backup_bootstrap.c`) is a
*plain* `fork()`, with no `exec()` and therefore no distinguishable
`/proc/<pid>/cmdline` at all -- only `serve`'s own in-process
`bootstrapChildren[]` bookkeeping (`accept_loop.c`) knows its pid-to-cluster
mapping. Restart counts and precise start times have no `/proc`
equivalent either way: they live in `process_supervisor.h`'s own
in-memory `ProcessRestartCounters` ring buffer.

This PR uses the second mechanism instead: a small, plain-text state file,
`<pgdata>/pg_walserver_ps.status` (`ps_state.h`/`ps_state.c`), that
`serve` itself keeps current -- written once at startup (before the first
connection is even accepted, so `ps` has something accurate to read
immediately), once per main accept-loop tick (at least once a second,
alongside the existing `ws_receivewal_tick()` call, see `accept_loop.c`'s own
`refresh_ps_state()`), and once more right after a successful `SIGHUP`
reload. It records `serve`'s own pid and start time, one line per tracked
embedded receivewal worker (cluster, pid, path, start time, restart count -- all
of it already available inside `receivewal.c`, via the small
`ws_receivewal_get_status()` accessor this PR adds), and one line per
in-flight bootstrap backup job (`accept_loop.c`'s own
`ws_bootstrap_get_status()`). Deliberately plain "key = value" lines, the
same shape `wal_dir_scan.c`'s own `archiver-position` cache file already
uses, rather than introducing a JSON writer into `pg_walserver` for this
alone. `ps`/`status` read it back with `ws_ps_state_read()`, then apply
their own `kill(pid, 0)` check against every pid it names -- the state
file is refreshed at most once a second, so a pid it remembers could, in
the narrow window since the last refresh, have already exited; a stale
entry is never trusted at face value.

Liveness of `serve` itself -- both for `ps`/`status` and for `list
clusters`' own "is this cluster's receivewal worker running" column -- reuses
`src/bin/common/pidfile.c`'s existing `read_pidfile()` unchanged: a real
`kill(pid, 0)` check, with a stale pidfile removed automatically, the
exact same function `pg_walserver reload` already relies on. `serve` not
running at all is never an error for `ps`/`status`/`list clusters`: each
one checks the pidfile first and prints a clean "not running" (or "n/a")
answer instead.

### Live receiving LSN: relaying the vendored `pg_receivewal`'s own hooks through to `ps`/`status`/`list clusters`

The vendored `pg_receivewal` (`src/bin/common/vendor/pg_receivewal/`) calls
two hooks from its own `stop_streaming()` check-in on every server
status-update round: `pgaf_wal_segment_closed_hook(xlogpos, timeline)` the
moment a WAL segment finishes, and `pgaf_wal_progress_hook(xlogpos,
timeline)` on every other check-in (far more often, and NOT guaranteed to
land on a genuine WAL record boundary -- observability only). Both are wired
up in `cli_internal.c`'s `cli_internal_pg_receivewal_run()` to the same
callback, throttled to roughly once a second (matching `refresh_ps_state()`'s
own cadence).

That callback runs inside the receivewal worker's own subprocess (a separate
`fork()`+`execv()` of this same binary, see "The embedded receivewal worker"
below) -- it shares no memory with `serve`, so it cannot write straight into
`accept_loop.c`'s own in-process bookkeeping the way `ws_receivewal_get_status()`
does for pid/restart-count. It relays its (lsn, timeline) the same way `serve`
itself relays pid/restart bookkeeping to `ps`/`status`: a small, throttled,
per-cluster file, `<cluster path>/receivewal-progress` (`wal_dir_scan.h`'s
`ws_receivewal_progress_write()`/`ws_receivewal_progress_read()`), which
`accept_loop.c`'s own `refresh_ps_state()` tick reads back and folds into the
`WsPsReceivewalEntry` it publishes (new `lsn`/`lsnTimeline`/`lsnObservedAt`
fields, `ps_state.h`) -- the *existing* cross-process mechanism this section
already describes above, just carrying one more small fact.

Deliberately a *separate* file from `wal_dir_scan.c`'s own
`archiver-position` cache (`wal_position_cache_read()`): that file's
contract, relied on by `cmd_start_replication.c`/`cmd_replication_slot.c`/
`cmd_base_backup.c`/`cmd_identify_system.c` elsewhere in this codebase, is a
safe, record-boundary "resume from here" position -- `pgaf_wal_progress_hook`'s
own raw stream position must never be mistaken for that. `receivewal-progress`
is display-only: it feeds `ps`'s per-worker line, `status`'s per-cluster
summary, and `list clusters`' own "WAL END" column (in preference to that
column's existing `wal_dir_find_latest()` segment-boundary scan, when a
cluster's receivewal worker is running and has reported a reading -- falling
back to the scan otherwise).

### `list clusters`: computing the covered WAL range

The start LSN of a cluster's currently covered WAL range comes straight
from its latest base backup's own `backup_label` ("START WAL LOCATION"),
read with `cmd_base_backup.c`'s own `read_backup_label()` -- already
parsed, already tested elsewhere in this codebase (`archive-cleanup`
reuses it too), no reason to duplicate it a third time.

The end LSN reuses `wal_dir_scan.c`'s own `wal_dir_find_latest()` as-is:
a single `opendir()`/`readdir()` pass over the cluster's directory, picking
out the highest-numbered *complete* WAL segment filename -- this
project's one existing "what is the newest WAL we have" answer, already
used by `IDENTIFY_SYSTEM`/`CREATE_REPLICATION_SLOT`. It is a directory
scan, not a probe that `stat()`s a handful of candidate filenames forward
from a last-known position. A forward-probing implementation was
considered (and is what the original design sketch for this feature
called for), but was not built as a second, parallel implementation next
to `wal_dir_find_latest()`: a WAL cache directory holds nothing but
WAL/`.partial`/`.backup`/`.history` files, so one linear `readdir()` over
it costs one syscall loop no matter how the answer is derived, and `list
clusters` is an interactive, occasional operator command, not a
per-connection hot path where avoiding a full scan would actually matter.
Reusing the existing, tested function directly was judged the better
trade than maintaining two independent "find the latest WAL" code paths.

### `list backups`/`list wal`: no incremental cache, by design decision made under time pressure

`list backups` reuses `archive-cleanup`'s own backup enumeration
directly: `cli_archive_cleanup.c`'s previously-private `WsCleanupBackup`
struct and `load_backups()` function are now `WsBackupInfo` and
`ws_backup_list_load()`, exported via `cli_archive_cleanup.h`, unchanged
in behavior. `list wal` reuses `wal_dir_scan.c`'s own filename shapes via
a small new exported classifier, `ws_wal_dir_classify_filename()` (WAL
segment / `.partial` / `.backup` / `.history` / other), rather than
re-deriving `cli_archive_cleanup.c`'s own private, differently-purposed
`wal_prefix_from_name()`.

Neither of these two sub-commands' own aggregate answers (segment counts,
total bytes, the full backup/`.history` inventory) is cheaply derivable
from a targeted probe the way `wal_dir_find_latest()`'s single "newest
segment" answer is above -- they need to read every relevant directory
entry at least once. The design this feature started from called for a
small, per-cluster, *incrementally* maintained metadata cache file,
updated by each of the existing code paths that already write into a
cluster's own directory: the embedded receivewal worker on each completed
segment (`receivewal.c`'s vendored `pg_receivewal`), `archive-wal`/
`ARCHIVE_FILE` on each push (`cmd_archive_file.c`), the bootstrap-backup
code on completion (`backup_bootstrap.c`), and `archive-cleanup` on
removal (`cli_archive_cleanup.c`).

That incremental cache was **not** implemented in this pass, and this is
a deliberate, flagged scope cut, not a silent omission: wiring a cache
update into four independent, already-shipped write paths, correctly,
and without risking a stale or inconsistent cache surviving a crash
mid-update, is real, nontrivial plumbing (each of those four call sites
would need its own "update the cache, and do so safely if the process
dies between the write and the cache update" story) that did not fit
safely in the time available for this change. What is implemented instead
is the documented fallback explicitly allowed for this case: `list
backups`/`list wal` compute their answer fresh, by scanning the matching
cluster's own directory, on every invocation -- always correct, cached only
for the lifetime of that one invocation (one directory scan feeds every
row printed for that cluster, never re-scanned per row within the same
run). This is fast enough for the realistic case (a WAL cache holding a
retention window's worth of segments, thousands at most at the default
16MB segment size) that an operator running this by hand would not notice
the difference; if `list wal`/`list backups` against a very large,
long-retention archive ever becomes an actual bottleneck, the incremental
cache file sketched above is the natural next step, reusing this same
`WsWalStats`/`WsBackupInfo` shape as its own on-disk schema.

## The vendored ustar writer (vendor/tar.c)

`vendor/tar.c` is vendored from PostgreSQL's own `src/port/tar.c` --
`tarCreateHeader()`/`tarPaddingBytesRequired()` and the ustar
checksum/header-field logic `tar_stream.c` builds `BASE_BACKUP`'s tar
archives on top of directly, rather than re-deriving the ustar byte layout
by hand. It has no backend dependency (only `c.h`/`pgtar.h`), which is
already proven frontend-safe: it's exactly what `pg_basebackup`'s own
client-side tar handling, and the backend's `basebackup.c`, both build on.

Unlike `pg_receivewal`'s own vendored copy (vendored whole, in a different
PR, and kept byte-for-byte identical to upstream so future diffs against
PostgreSQL stay trivial), `vendor/tar.c` here is reformatted to this
project's own brace style via `citus_indent` -- logic unchanged from
upstream (checked against a real PostgreSQL checkout), only whitespace/
brace placement differs. The difference in vendoring policy is deliberate,
not an oversight: `pg_receivewal`'s vendored tree is large and
update-in-place from upstream releases, where byte-for-byte diffing
against a future PostgreSQL version is the point; `vendor/tar.c` here is a
single small file this project's own style checker (`ci/style.sh` /
`citus_indent --check`) would otherwise permanently flag as a style
violation on every CI run, so it is reformatted once, up front, and
future updates re-reformat rather than fighting the style checker forever.

## Fit as a future pg_auto_failover building block

This PR ships `pg_walserver` standalone (see "What this PR is, and isn't"
above); the following is recorded here, not built, for whoever designs the
later "archiving PR" that actually wires it into `pg_autoctl`:

- `fetch-systemid`, `basebackup`, and the embedded receivewal worker
  (`receivewal.c`) should become **the** implementation, not a second one
  living alongside `pg_autoctl`'s own, separately-maintained
  `archiver_systemid.c`/`service_archiver_basebackup.c`/
  `service_archiver_pgreceivewal_ctl.c`. `pg_autoctl`'s own future
  `service_archiver_reconciler.c` (already responsible for writing
  `pg_walserver.ini` in that later PR's own design) should grow to also
  write `upstream`/`receivewal` and call into this same logic in-process, the
  same way a future `pg_autoctl restore command` is expected to call
  `ws_fetch_file_client()` directly, in-process, once that PR's own
  monitor-backed quorum/archiver-node participation exists on top of it --
  `pg_walserver restore-wal` (see "FETCH_FILE's client" above) already
  proves the in-process, no-`execv()` shape works today, standalone.
- A natural future integration point exists at `pg_autoctl`'s own
  `archiver_confirm.c` (`archiver_confirm_run()`, which currently just
  `return 1`s -- retry forever -- when its monitor-backed
  `wal_archived()` check says "not yet"): whether, when, and how that PR
  ever calls into `CHECK_FILE`/`ARCHIVE_FILE` from there, as a fallback,
  is explicitly **not** decided by this PR (see "The archive push side"
  above's own "Scope note") -- this PR's job is only to make sure that
  primitive exists, is monitor-independent, and is worth reusing when that
  decision gets made. Also left to that PR: what `archiver_confirm_is_
  wal_segment()`'s current blanket skip of `.backup`/`.history`/
  `.partial` files should become once `ARCHIVE_FILE` can carry `.backup`
  files too (see "The wire protocol" above's own `ARCHIVE_FILE` entry).
- Push (either `ARCHIVE_FILE` or the embedded receivewal worker) only ever
  fires on **completed** segments (coarser RPO than continuous streaming
  replication) and is asynchronous relative to commit, so it cannot
  participate in `synchronous_standby_names` quorum the way a live
  streaming connection with flush-position feedback can. For a future
  archiver's role as a failover-aware quorum member, the pull/streaming
  path (the embedded receivewal worker, or a real standby's own walreceiver via
  `START_REPLICATION`) stays load-bearing; push is a strong
  *defense-in-depth backstop* there (never lose a segment even if the
  streaming receivewal worker is down for a while), and a perfectly sufficient
  *sole* mechanism for the simpler, non-HA standalone case this file is
  otherwise about.

## Testing (tests/tap/specs/pg_walserver_standalone.pgaf)

There is no archiver integration in this PR's own stack for a test to
drive `pg_walserver` through -- no `pg_autoctl create archiver`, no
reconciler writing clusters/HBA files, no monitor schema. So the tap spec
builds the smallest possible harness instead, ahead of the archiver
feature that will eventually make all of this automatic:

- `pg_walserver setup --no-receivewal` does most of the work in one call:
  creates the cluster's own directory, writes the `pg_walserver.ini` section
  (`path` + `upstream`), and fetches node1's real system identifier into
  `pg_walserver_systemid` -- exactly the sequence
  `docs/ref/pg_walserver.rst`'s own worked example now leads with.
  `pg_walserver serve`, started a few steps later, takes the cluster's first
  base backup automatically at startup (see "Bootstrapping a cluster's
  first base backup" above). `--no-receivewal` opts out of the embedded pull
  receivewal worker, on by `setup`'s own default now (see "The embedded pull
  receivewal worker" below): this spec drives its own external, stock
  `pg_receivewal` into this exact cluster directory a few lines below, and
  the embedded receivewal worker would otherwise fork a second process racing it
  for the same segment files;
- a hand-crafted `pg_walserver_hba.conf` (a single `host all all
  127.0.0.1/32 trust` rule, scoped to the loopback peer every step in this
  spec actually connects from -- authentication itself is exercised
  elsewhere at the unit level, this spec exercises the wire protocol;
  `setup` deliberately never touches HBA, see its own header comment);
- real WAL captured off a real `pg_auto_failover`-managed primary (node1)
  by the stock OS `pg_receivewal` (not this project's own vendored copy,
  which belongs to a different PR's stack) into the cluster's directory,
  which `pg_walserver` then serves out of directly.

`pg_walserver` itself is started as a plain background process on node2
(`pg_walserver --pgdata /tmp/ws --port 6543`), and every step after that
talks to it exclusively through real clients: `psql` issuing raw
replication-protocol commands (`IDENTIFY_SYSTEM`, `SHOW`, `FETCH_FILE`,
`CREATE_REPLICATION_SLOT`, deliberately malformed input) and a real
`pg_receivewal` doing an actual `START_REPLICATION` (plus, in the last
step, a real standby driven by nothing but stock PostgreSQL commands). The
seven steps:

1. `test_000_sync_files_from_node1` -- assembles the hand-crafted
   fixtures above and starts `pg_walserver`.
2. `test_001_identify_system_and_show` -- `IDENTIFY_SYSTEM` reports
   node1's real system identifier; `SHOW wal_segment_size` reports 16MB.
3. `test_002_fetch_file` -- `FETCH_FILE` returns the exact bytes of a
   captured WAL segment (byte-for-byte compared via `stat`). This step
   drives the *server-side* `FETCH_FILE` handler with a plain
   `psql -c "FETCH_FILE ..."`; it does not use, and is unaffected by the
   removal of, the `fetch-file` client CLI (see "FETCH_FILE's client"
   above).
4. `test_003_start_replication_from_pg_walserver` -- a real
   `pg_receivewal` runs `START_REPLICATION` against `pg_walserver` itself,
   concurrently with the original receivewal worker still running against node1;
   forcing a new segment on node1 proves both a segment still being
   written (a `.partial` file) is picked up correctly and the two captured
   copies end up byte-identical.
5. `test_004_grammar_edge_cases` -- a double-quoted slot name with an
   embedded escaped quote (`"foo""bar"`) is rejected by
   `slot_name_is_safe()` with a clean `42602`, proving the real grammar
   (not a hand-rolled parser) lexed the identifier correctly; and a
   garbage command produces a clean `ErrorResponse` without crashing the
   server or leaving the connection unusable for the next, real command.
6. `test_005_wildcard_cluster` -- adds a second cluster, reachable only
   through pg_walserver.ini's `"*"` wildcard (see "The clusters file" above), with
   its own distinct system identifier; a `dbname` matching no explicit
   section resolves to it, while `default/0` -- which still has its own
   explicit section -- keeps resolving to its own path, proving an exact
   match always wins over the wildcard.
7. `test_006_real_standby_with_core_tools` -- a real, unmodified
   `pg_basebackup` client takes a `BASE_BACKUP` from `pg_walserver` of the
   exact backup `serve` bootstrapped automatically at startup (proving
   `cmd_base_backup.c` actually serves it over the wire, not just that the
   file exists on disk, which `test_000` above only checks); a real standby
   -- `primary_conninfo` pointed at `pg_walserver`, `standby.signal`,
   nothing but stock PostgreSQL configuration and commands -- then streams
   live changes from it via a genuine walreceiver, not `pg_receivewal`, and
   is finally promoted. Needs one extra cluster: a real physical replication
   connection's walreceiver always sends the literal `dbname=replication`
   on the wire regardless of what `primary_conninfo` says (PostgreSQL's own
   `libpqrcv_connect()` overrides it unconditionally, `libpqwalreceiver.c`'s
   own comment: "the database name is ignored by the server in replication
   mode, but specify 'replication' for .pgpass lookup") -- so this step
   gives the cluster a second, literal-`"replication"` alias pointing at the
   same path, exactly as a deployment serving real physical standbys by
   name (rather than through the `"*"` wildcard) would need to.

The suite runs 7/7 green; none of the first five steps needed to change
for the removal of the `"monitor"` HBA keyword or the `fetch-file` CLI
sub-command (Tasks 1 and 2 of the PR review round that produced this
README) -- they were already written to avoid exercising either path.

### Testing SNI-based routing (tests/tap/specs/pg_walserver_sni_routing.pgaf)

A second, separate spec covers the "Addressing a cluster beyond `dbname`: TLS
SNI" feature above with two real, independent clusters on two `/etc/hosts`
aliases (`clusterA.internal`/`clusterB.internal`) resolving to the same
`pg_walserver`, both addressed with the exact same, useless
`dbname=replication` a real physical standby always sends -- proving the
disambiguation is genuinely happening by hostname, not by coincidence.
Four steps: adding a second named cluster via `setup --hostname` creates a
self-signed certificate automatically (`test_001`); a client presenting
each hostname over TLS resolves to that cluster and no other, both ways
(`test_002`); a connection with no resolvable hostname and no wildcard
fails cleanly instead of falling through to either real cluster
(`test_003`); and removing the certificate makes `pg_walserver serve`
refuse to start at all with two named clusters configured (`test_004`).
Runs 4/4 green.

### Testing the archive push side (tests/tap/specs/pg_walserver_archive_command.pgaf)

A third, separate spec covers `CHECK_FILE`/`ARCHIVE_FILE` and the
`pg_walserver archive-wal`/`restore-wal`/`create-cert` sub-commands above,
entirely monitor-independent as the design requires (see "The archive push
side" above). It configures two clusters: `arch/0`, set up with
`--no-receivewal` (this spec pushes its own small, deterministic fake "WAL
segments" by hand under real WAL-segment-shaped names -- the embedded
receivewal worker would otherwise fork and pull real WAL from node1 into the same
directory, under the same names, racing what the spec itself writes), and
`arch/1`, left at `setup`'s own `receivewal = pull` default, with a real
embedded `pg_receivewal` actually pulling WAL off node1 -- proving
`archive-wal`'s two disjoint behaviors against a cluster genuinely
configured each way. Six steps: `CHECK_FILE` reports `missing` for a
filename nothing has ever archived (`test_001`); against `arch/0`,
`pg_walserver archive-wal` pushes a brand new file via `ARCHIVE_FILE`
unconditionally (byte-identical to the source on disk afterwards), then
run again against the exact same source file it pushes again,
unconditionally, still exit 0 both times -- the idempotency property
PostgreSQL's own `archive_command` contract requires, provided end to end
by `cmd_archive_file.c`'s own overwrite-safety, not by any client-side
check (`test_002`); pushing a *different* file under the same
already-archived name on `arch/0` is cleanly rejected (nonzero exit, the
original bytes on disk untouched) (`test_003`); `create-cert` refuses a
plain call against the certificate `setup{}` already created automatically
(`arch/1` being a second named cluster), and `--force` overwrites it
cleanly, twice, CN reflecting each `--hostname` (`test_004`);
`pg_walserver restore-wal` fetches the file
`test_002` pushed back out, via a real `FETCH_FILE` round trip
(byte-identical to the original), finally giving `ws_fetch_file_client()`
(`fetch_client.c`) a real, exercised caller -- restoring a
name nothing ever archived fails cleanly, with no partial file left behind
(`test_005`); and, against `arch/1`, `archive-wal` run against the segment
its embedded receivewal worker is still writing (identified by its own `.partial`
file) exits 1 immediately via `CHECK_FILE` alone, `ARCHIVE_FILE` never
called and the final file never appearing as a side effect -- then, once a
forced WAL switch on node1 lets the receivewal worker actually finish that exact
segment, a subsequent `archive-wal` invocation against the identical bytes
reports `matches` and exits 0, still without ever calling `ARCHIVE_FILE`
(`test_006`). Runs 6/6 green.

### Testing the embedded receivewal worker (tests/tap/specs/pg_walserver_capture.pgaf)

A fourth, separate spec covers `receivewal.c`'s embedded receivewal worker above.
Its own setup{} runs `pg_walserver setup --receivewal pull` (writing
`receivewal = pull` into the cluster's own section) and starts an independent,
externally-run "reference" `pg_receivewal` capturing the same primary into
a separate directory -- every step below diffs the embedded receivewal worker's own
output against that reference, the same byte-identical-output bar
`pg_walserver_standalone.pgaf`'s own `test_003` already proves for
`START_REPLICATION`. Three steps: a live `pg_receivewal` child is running
under `pg_walserver`'s own pid (identified by its own process title,
`"pg_walserver: receivewal <cluster>"`, set by `start_one_receivewal_child()`'s
own `set_ps_title()` call -- not a pidfile, `pg_walserver` keeps none for
it) with no external `pg_receivewal` ever invoked for this cluster, and a
forced WAL switch lands a byte-identical segment in the cluster's own
directory and in the reference capture (`test_001`); `kill -9`-ing the
receivewal worker child gets it restarted automatically, under a new pid, still
parented by `pg_walserver` itself, with receivewal continuing byte-identical
across the restart (`test_002`); and stopping `pg_walserver` itself
(`SIGTERM`) cleanly stops the receivewal worker child too -- no orphaned process
left running (`test_003`). Runs 3/3 green.

### Testing pg_walserver as a container's real PID 1 (tests/tap/specs/pg_walserver_pid1.pgaf)

A fifth, separate spec covers the one thing none of the specs above can:
`pg_walserver` genuinely running as PID 1 inside a container, not merely
supervised by something else that happens to be PID 1 (every other
pg_walserver spec runs it as an ordinary background process under node2's
own pg_autoctl-managed container). This exercises `process_supervisor.c`'s
own PID-1 orphan-reaping path (see "The single wildcard reaper" above) for
real: node2's own container `command` is overridden to `exec pg_walserver`
directly, with no `pg_autoctl`, shell wrapper, or init system above it at
all, so a reparented grandchild really does land on `pg_walserver`'s own
`waitpid(-1, ...)` call, not on some other init. Three steps: the embedded
receivewal worker's own parent pid really is `1` (`/proc/<pid>/stat`), proving
`pg_walserver` itself is genuinely this container's PID 1, not a process
merely running inside one (`test_001`); `kill -9`-ing the receivewal worker gets it
restarted automatically, still parented by pid 1 (`test_002`); and
`docker compose stop` (a plain `SIGTERM` to the container, exactly what
`docker stop` sends) cleanly stops both `pg_walserver` and its receivewal worker
child -- proven from the container's own log lines (`receivewal.c`'s own
`ws_receivewal_stop_all()` and `ws_accept_loop()`'s own shutdown message),
never from process absence alone: once PID 1 exits, the kernel tears down
the whole PID namespace regardless of how orderly the shutdown was, so "no
orphan left" can't by itself distinguish an orderly stop from a forced one
the way the log lines -- and the explicit absence of a "sending SIGKILL"
escalation line -- can (`test_003`). WAL-receivewal byte-for-byte correctness
itself is already proven by `pg_walserver_capture.pgaf` above; this spec's
only job is the process-supervision/signal-handling contract that changes
specifically when `pg_walserver` is PID 1 instead of an ordinary child.
Runs 3/3 green.

### Testing the full standalone story end to end (tests/tap/specs/pg_walserver_pitr_and_secondary.pgaf)

A sixth spec exercises the `postgres <name>`/`pg_walserver <name>` DSL sugar
kinds themselves (`test_spec_parse.y`'s `postgres_line`/`pg_walserver_line`)
for the first time in this suite -- every spec above builds an equivalent
node by hand inside an ordinary pg_auto_failover-managed formation instead.
Four roles: a plain, unmanaged `postgres primary` (no monitor, no formation
anywhere in this spec), archived by `archive_command` and continuously
pulled by `pg_walserver server`'s own embedded receivewal worker (receivewal on by
default, proven running the whole time via its own process title, the same
check `pg_walserver_capture.pgaf` already uses); a point-in-time-recovery
target built with a real, unmodified `pg_basebackup` against `server` plus
`restore_command = pg_walserver restore-wal`, recovered to a named restore
point (`recovery_target_name`, not a timestamp -- this DSL has no
cross-container value capture, and a restore point's name is the only value
that needs to cross from the primary's own step to the recovery target's
own configuration); and a real, continuously-streaming secondary built with
`pg_walserver basebackup` (the CLI sub-command) pointed at `server` itself
rather than at a real primary, proving that tool is generically usable
against anything speaking the wire protocol -- which surfaces one real
wrinkle worth knowing: `pg_walserver basebackup`'s own internal
`pg_basebackup_fetch()` (`src/bin/common/pgctl.c`) never sends an explicit
`dbname`, so libpq defaults it to whatever `--user` resolves to; `server`'s
own `pg_walserver.ini` carries a `"*"` wildcard cluster (mapped at the same
path as its named `"pitr"` cluster) specifically so that default, and a real
standby's own always-`"replication"` `dbname`, both resolve without any
further configuration. See the spec file's own header comment for the full
design and the reasoning behind each of these four roles. Runs 4/4 green.

### Testing the pidfile and SIGHUP reload (tests/tap/specs/pg_walserver_reload.pgaf)

A seventh, separate spec covers the pidfile and SIGHUP-driven config reload
described in "Config reload" above. Five steps: the pidfile
(`<pgdata>/pg_walserver.pid`) holds the real, running pid (`test_001`);
editing `pg_walserver.ini` to add a `"*"` wildcard cluster and running
`pg_walserver reload` makes that cluster immediately reachable, with no
server restart, while the original cluster keeps working too (`test_002`);
corrupting `pg_walserver_hba.conf` with a malformed line makes `reload` log a
clear parse error while the server keeps serving its prior, still-valid
HBA ruleset -- a request against the cluster that already worked before the
bad edit still succeeds (`test_003`); `pg_walserver reload` against a
stale pidfile (an already-exited pid) fails cleanly with a nonzero exit,
and `read_pidfile()` removes the stale file as a side effect (`test_004`);
and giving the cluster `receivewal = pull` via reload starts its embedded pull
receivewal worker with no server restart, and removing it again stops that same
receivewal worker child (`test_005`). Runs 5/5 green.
