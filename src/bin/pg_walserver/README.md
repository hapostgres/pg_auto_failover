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
directly off disk (`pg_walserver.ini`, `archiver-hba.conf`,
`archiver-passwd`, and per-route bookkeeping files -- see below). Three of
its own sub-commands (`setup`, `fetch-systemid`, `basebackup`; see
"New client-side sub-commands" below) can now create and keep those files
current directly, driven from the command line -- `docs/ref/pg_walserver.
rst`'s own worked example uses `setup` first, falling back to `pg_
basebackup`/`pg_receivewal`/`psql` by hand only for what those three don't
cover yet (ongoing WAL capture, HBA, the passwd file). `DESIGN-standalone-
archiving.md` in this same directory designs the rest of that story: the
`archive`/`CHECK_FILE`/`ARCHIVE_FILE` push side and an embedded, supervised
WAL capturer, so `pg_walserver` can eventually be a complete,
production-grade archiver entirely on its own -- not implemented yet, a
design to review first.

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
line in `archiver-hba.conf`.

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

- `IDENTIFY_SYSTEM` (`cmd_identify_system.c`) -- reports the route's system
  identifier (read from a small `archiver-systemid` file under the route's
  directory), current timeline and `xlogpos`, and `dbname` (`NULL` unless
  the client's startup packet used `replication=database`, matching real
  `pg_receivewal`'s expectations exactly, see `walsender.h`).
- `SHOW <name>` (`cmd_show.c`) -- currently only `wal_segment_size`, needed
  by `pg_basebackup`/`pg_receivewal` to size their own reads; reports the
  real 16MB PostgreSQL default (this project does not support a
  non-default WAL segment size).
- `BASE_BACKUP [options...]` (`cmd_base_backup.c`) -- streams the route's
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
  route's WAL cache directory as `CopyData` messages, physical replication
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
  the route's directory as an ordinary `CopyOut`. It exists because a
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
  all: `RowDescription(status text)` + `DataRow('missing'|'matches'|
  'differs')` + `CommandComplete`, the same shape `SHOW` already uses. The
  client (`pg_walserver archive`, see "The archive push side" below)
  computes the size and CRC32C of its own *local* file and sends both
  here; the reply says whether that's already what's on disk under this
  name, without moving a single byte of file content. See DESIGN-
  standalone-archiving.md's "The push side" section for the full design.
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
  also accept a base backup's own `<24hex>.<8hex>.backup` history file --
  DESIGN-standalone-archiving.md's Gap #1), hard size cap at the route's
  own `wal_segment_size` (`ws_route_wal_segment_size()`) plus
  `WS_ARCHIVE_FILE_SIZE_SLACK` (1 MiB), checked as bytes arrive so an
  oversized push never gets to write the whole thing to disk.

Anything else parses to `WS_CMD_UNKNOWN` and gets a clean `ErrorResponse`
(SQLSTATE `42601`) rather than a crash or a hung connection -- the
connection remains usable for the next command afterwards
(`test_004_grammar_edge_cases` in the tap spec exercises exactly this).

### FETCH_FILE's client

Earlier in this PR's own history, `pg_walserver` shipped both sides of
`FETCH_FILE`: the server handler above, and a `pg_walserver fetch-file`
CLI sub-command (a one-shot libpq client) meant to be `execv()`'d by
`pg_autoctl restore command` in the later archiving PR. Review concluded
that design was backwards: `pg_walserver` should be a server binary, full
stop, and a client used only by `pg_autoctl` belongs where `pg_autoctl` can
call it directly, in-process, with no subprocess/`execv()` indirection at
all.

The client's logic has been moved, unchanged in substance, to
`src/bin/common/fetch_client.c`/`fetch_client.h` as `ws_fetch_file_client()`.
It was a clean move rather than a rewrite because the client never actually
depended on any `pg_walserver`-internal header: it opens a plain
`PQconnectdbParams()` connection, runs `FETCH_FILE '<name>'` as a simple
query via `PQexec()`, and drains the `CopyOut` with `PQgetCopyData()` --
ordinary libpq, the same way `pg_basebackup` itself would. It never touched
`framing.h` or any other `pg_walserver`-private wire-format code (that code
is what makes the *server* side pg_walserver-specific; the client is just
another libpq application). `src/bin/common/` is already linked by both
`pg_autoctl` and `pg_walserver` (see `Makefile.common`'s `COMMON_SRC`
wildcard), so the move required no new build wiring beyond removing the
file from `pg_walserver`'s own `LOCAL_SRC` list.

`pg_walserver` itself has **no** `fetch-file` sub-command any more: its
`CommandLine` tree (`cli_root.c`) is down to `serve` (the default) and
`scram-secret`. The only caller `ws_fetch_file_client()` was ever going to
have -- `pg_autoctl restore command` -- lives in the later archiving PR and
is not part of this one; that PR calls it directly as a C function. This
PR's own test suite (see "Testing" below) does not exercise
`ws_fetch_file_client()` at all: `test_002_fetch_file` drives the
*server-side* `FETCH_FILE` command with a plain `psql -c "FETCH_FILE ..."`,
which never touches this client code. There is no throwaway test binary
added for it either -- a manual harness for a function with its one real
caller in a different PR did not seem worth inventing; the archiving PR's
own tests are expected to exercise it through `pg_autoctl restore command`
end to end.

## Replication slots

`CREATE_REPLICATION_SLOT`/`READ_REPLICATION_SLOT`/`DROP_REPLICATION_SLOT`
support physical slots only (`cmd_replication_slot.c`); logical slots are
rejected with SQLSTATE `0A000`.

A "slot" here is **not** a real PostgreSQL replication slot -- there is no
live PostgreSQL server underneath `pg_walserver` for one to exist on. It is
a small bookkeeping marker file, `.slot_<name>`, written directly in the
route's own directory, containing one line: `restart_lsn=<lsn>`. This is
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
  file's own filename under the route directory, so this validation is
  also what stops a client from writing anywhere else on disk (no `/`, no
  `..`, no leading `.`, nothing that isn't in the class above);
- at most `WS_MAX_SLOTS_PER_ROUTE` (64) slots per route -- each is a file,
  counted with a directory scan (`count_slots()`) at `CREATE` time, and a
  route beyond the cap gets a clean `53400` error rather than an unbounded
  pile of marker files;
- `CREATE` of an existing slot is always an error (`42710`); it never
  resets an existing slot's `restart_lsn`, unlike an accidental
  double-`CREATE` silently reusing one would.

What this deliberately does **not** do yet: a replication slot here carries
no WAL-retention enforcement at all. A real PostgreSQL replication slot's
entire point is holding `pg_wal` back from recycling segments a slot's
consumer hasn't consumed yet; this `.slot_*` marker file records a
`restart_lsn` but nothing currently *reads* it to decide what may be
pruned from a route's WAL cache. That is intentionally left as the
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

`archiver-hba.conf` is a deliberately small subset of `pg_hba.conf`: one
rule per line, `TYPE ROUTE USER ADDRESS METHOD`, first match wins, and
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

`ROUTE` is `all`, or a route key exactly as it appears in `pg_walserver.ini` (see
"The routes file" below) -- an opaque string `hba.c` never parses, splits,
or gives any filesystem meaning to. pg_auto_failover's own convention is
`"<formation>/<group>"` (e.g. `default/0`), because it reads well and is
already guaranteed unique across a whole deployment, but the `/` in it
carries no special meaning here at all: `hba.c` compares it against a
rule's `ROUTE` field with a plain string `==`, the exact same way it would
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
route", backed by a `refresher.c` child process (the *only* process that
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
  `archiver-passwd` (`<user>:SCRAM-SHA-256$<iter>:<salt>$<stored>:<server>`),
  produced by `pg_walserver scram-secret` (password read from
  `PGPASSWORD`, never the command line). A user with no stored verifier
  still runs the *entire* exchange against a mock verifier
  (`scram_mock_verifier()`, seeded once before any connection is forked so
  every child answers identically) and fails exactly like a wrong password
  would -- so a client can't distinguish "unknown user" from "wrong
  password" by timing or response shape;
- `reject` -- refused outright, and no rule matching at all defaults to
  reject as well (`WS_AUTH_REJECT` is the zero value of `WsAuthMethod`).

Authentication runs **before** anything about the requested route is
revealed, exactly as PostgreSQL orders it: the HBA lookup uses the route
key the client asked for whether or not it turns out to be a real route,
and only *after* a successful authentication does an unknown route get
reported (SQLSTATE `3D000`, "database does not exist"). A rejection is one
generic message naming only the peer address and user (both already known
to the client), never the route -- and every client-supplied string is
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
whatsoever, and there is no HBA file, no routes file, and no TLS.

## The routes file (pg_walserver.ini)

`pg_walserver.ini` (`routes.c`/`routes.h`) is this server's own routing
table: one INI section per route it serves, mapping a route key -- matched
against the connection's `dbname`, i.e. what a real client puts in its
connection string's `dbname=` -- to a `path`, that route's own local
storage root. That's the *only* thing a route carries: which base backup
is current, a route's own system identifier, and the current WAL position
are deliberately **not** stored in the routes file. Every command that
needs one of those instead reads it fresh, straight off a small
purpose-built file directly under that same path, at connection time --
for instance `cmd_base_backup.c`'s own `basebackups/.latest` and
`cmd_identify_system.c`'s own `archiver-systemid`. `pg_walserver` itself
never talks to the monitor (see the "monitor" HBA removal above); this
per-route-directory split is what lets it stay that way while still always
answering with whatever is current.

`BASE_BACKUP`, `FETCH_FILE`, `START_REPLICATION`, and the replication-slot
commands all resolve the connection's route once (`routes_find()`, in
`accept_loop.c`'s `handle_connection()`) and then read/write only inside
that route's own directory -- there is no code path that lets a connection
authenticated against one route touch another route's files.

The parser is deliberately built directly on the vendored `ini.h`'s
low-level, dynamic-section API (`ini_load()`/`ini_section_count()`/...)
rather than this project's own `ini_file.c` wrapper: that wrapper's
`IniOption` model assumes a fixed, compile-time-known set of section/key
names, which does not fit a file whose sections are one per route, under
whatever key an operator (or a driver such as pg_auto_failover) picked --
unknown in advance, and changing over the life of the server.

### Route keys are opaque strings, not paths

A route key is never parsed, split on `/`, or given any filesystem meaning
of its own anywhere in this codebase -- it is matched by a plain string
`==` against `dbname` (`routes_find()`) and, independently, against
`archiver-hba.conf`'s own `ROUTE` field (`hba_lookup()`), and nowhere else.
pg_auto_failover's own convention, `"<formation>/<group>"` (e.g.
`default/0`), *looks* like a path, but it is not one, and never becomes
one: the only thing that ever determines an actual directory on disk is
the route's own explicit `path` property, written by whoever maintains
`pg_walserver.ini` (a human, or `service_archiver_reconciler.c` in the later
archiving PR). This is a deliberate design choice, not an oversight -- see
the wildcard route below for why substituting a route key straight into a
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

### The wildcard route (`*`)

One route key is special: `WS_ROUTES_WILDCARD_KEY` (`"*"`, `routes.h`) is a
catch-all fallback, used when a connection's `dbname` matches no route of
its own. The syntax and precedence are deliberately the same as
PgBouncer's own `[databases]` `"*"` entry
(<https://www.pgbouncer.org/config.html>), on the theory that anyone who
has already run a PgBouncer knows exactly what to expect here:

```ini
# an explicit route always wins over the wildcard, exactly like PgBouncer
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
supplied before HBA or SCRAM ever ran, and a route key is explicitly
allowed to contain `/` (see above) -- so a naive `%r`-style substitution
would turn pg_auto_failover's own key convention into a path-traversal
primitive the moment a client sent a crafted `dbname`. `routes_find()`
does not do this: every `dbname` that falls through to `"*"` shares that
one configured `path` verbatim, never a per-key subdirectory synthesized
on the fly. The wildcard is what makes `pg_walserver` usable with zero
multiplexing ceremony outside pg_auto_failover: a single-cluster
deployment can skip per-route sections entirely, keep just one `[*]`
section in `pg_walserver.ini`, and never has to learn or type a special `dbname`
value at all.

`archiver-hba.conf`'s own `ROUTE` matching is completely independent of
this: an HBA rule's `ROUTE` field is always compared against the literal
`dbname` the client sent, never against whichever `WsRoute` `routes_find()`
happened to resolve it to. A `hostssl all ...` rule already admits any
route, wildcard-resolved or not; a rule scoped to one specific route key
still only matches that literal key, exactly as before.

### Routing beyond `dbname`: TLS SNI, for a real physical standby

`routes_find()`'s `dbname`-based matching above assumes the client gets to
choose its `dbname`. A real Postgres physical standby doesn't: its own
`libpqwalreceiver.c` (`libpqrcv_connect()`) always sends the literal
`dbname=replication` for a physical replication connection, discarding
whatever `primary_conninfo`'s own `dbname=` says. One route, this is no
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

- `pg_walserver.ini` gains a `hostname` route property (`routes.h`'s
  `WsRoute.hostname`, `routes.c`'s parsing), set via `pg_walserver setup
  --hostname <name>`.
- `tls.c`'s `ws_tls_get_sni_hostname()` reads it back with the simple,
  post-handshake `SSL_get_servername(activeSsl, TLSEXT_NAMETYPE_host_name)`.
  Real PostgreSQL's own matching backend feature (`ssl_sni` GUC,
  `be-secure-openssl.c`'s `sni_clienthello_cb()`) instead reads it from
  inside the raw ClientHello callback, via the lower-level
  `SSL_client_hello_get0_ext()` -- OpenSSL's own documented advice, needed
  there because that feature *switches the served certificate* based on
  the name, which is ordering-sensitive during the handshake. `pg_walserver`
  never switches certificates by SNI (one certificate serves every route),
  so the simpler, safe-after-the-fact read is enough here.
- `auth.c`'s `ws_authenticate()` resolves a route in three steps: an exact
  `dbname` match first (unchanged, and always tried first: a hand-written
  `dbname=<route key>` connection keeps working exactly as before, with or
  without TLS), then, only for a TLS connection, the SNI hostname, then the
  `"*"` wildcard. `archiver-hba.conf`'s own `ROUTE` matching stays exactly
  as described above -- always against the literal `dbname`, never against
  whichever route SNI resolved to.
- More than one *named* route (i.e. more than one section besides `"*"`)
  with no TLS configured is a hard error: `cli_root.c`'s `cli_serve_run()`
  refuses to start (`log_fatal`/`exit(1)`) rather than silently leaving a
  second route unreachable by any real standby. A single named route keeps
  working with no TLS at all -- `dbname` alone is already unambiguous.
- `pg_walserver setup` prepares for this automatically: adding a *second*
  named route creates a self-signed certificate for `--pgdata`  (reusing
  `pg_create_self_signed_cert()`, `src/bin/common/pgctl.c`, unchanged) the
  moment it's needed, and warns if that second route was set up without
  `--hostname` (it would then only ever be reachable via its `dbname`, or
  the `"*"` wildcard, never by a real standby).

A client TLS certificate's CN is a second, unimplemented alternative to
SNI for the same problem (`sslcert`/`sslkey` also flow through unmodified
for a physical replication connection) -- see
`DESIGN-standalone-archiving.md`'s "Routing beyond `dbname`" section.

## New client-side sub-commands (setup / fetch-systemid / basebackup)

Three sub-commands, alongside `serve`/`scram-secret`, all sharing
`cli_upstream.c`'s own `--route`/`--path`/`--upstream`/`--host`/`--port`/
`--user` resolution (an explicit flag always wins over a route's own
`pg_walserver.ini` properties, the same layering `restore_command_
resolve()`, `pg_autoctl/restore_command.c`, already uses):

- **`fetch-systemid`** (`cli_fetch_systemid.c`) -- connects to the
  upstream via `pgctl_identify_system()` (`src/bin/common/pgctl.c`, a real
  replication-mode `IDENTIFY_SYSTEM`, reused unchanged) and writes
  `archiver-systemid` atomically. Refuses to overwrite an already-recorded
  *different* identifier unless `--force`: the same "never silently
  replace what's already there" principle PostgreSQL's own
  `archive_command` overwrite-safety rule applies elsewhere, here applied
  to a route's own identity.
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
  `pg_walserver.ini` section for `--route` (refusing to silently change an
  existing one's path unless `--force`), optionally records a `--hostname`
  for SNI-based routing (see above -- and auto-creates a self-signed
  certificate the moment a *second* named route needs one to stay
  reachable), then calls `fetch-systemid`'s own logic (whose
  `pgctl_identify_system()` connection doubles as this step's
  role-permission check: PostgreSQL refuses a replication-mode connection
  for a role lacking `REPLICATION` at the *backend* level, independent of
  HBA -- an earlier version of this file ran a separate plain-SQL
  `pg_roles.rolreplication` check first, removed because that connection
  targets an ordinary database, which the replication role's own HBA rule
  usually does not admit at all), and, with `--with-basebackup`, calls
  `basebackup`'s own logic -- synchronously, not returning until the first
  base backup has actually succeeded, so "setup finished" means the route
  is genuinely ready to serve.

None of the three touch `archiver-hba.conf` or `archiver-passwd` -- a
deliberately separate concern an operator (or `pg_autoctl`, later) still
configures on its own, see `docs/ref/pg_walserver.rst`'s own worked
example for the full sequence including those.

## `create-cert`: a self-signed TLS certificate on demand

`pg_walserver create-cert --pgdata <path> --hostname <name> [--force]`
(`cli_create_cert.c`) writes `<pgdata>/server.crt`/`server.key` via
`pg_create_self_signed_cert()` (`src/bin/common/pgctl.c`) -- the exact
function `setup`'s own `ensure_tls_for_multiple_routes()` calls
automatically the moment a second named route needs a certificate (see
above); `ws_create_cert_run()` (`cli_create_cert.c`) is the one shared
helper both now call, so the call-and-log sequence isn't duplicated
between the automatic and the by-hand path. Useful whenever an operator
wants TLS in place from the very first route (a single route served over
a reachable network still benefits from encryption, even though SNI
routing itself doesn't need it yet), or wants to replace an existing
self-signed certificate. Refuses to overwrite an already-existing
`server.crt`/`server.key` unless `--force` -- the same "never silently
replace what's already there" principle as `cli_fetch_systemid.c`'s own
systemid overwrite check, applied here to the certificate files instead.

## The archive push side: `CHECK_FILE` + `ARCHIVE_FILE` + `pg_walserver archive`

Alongside the pull-oriented tools above, `pg_walserver` also accepts a
*push*: `CHECK_FILE`/`ARCHIVE_FILE` (see "The wire protocol" above for
their wire shape and overwrite-safety rule) and the `pg_walserver archive`
client sub-command that drives them, meant to run as (part of) a Postgres
`archive_command`. See DESIGN-standalone-archiving.md's "The push side:
CHECK_FILE + ARCHIVE_FILE" section for the design this implements in full,
including the two judgment calls it left open: `CHECK_FILE`'s wire shape
(resolved as the lean `SHOW`-like row described above) and the bounded
intra-invocation recheck's exact timing (resolved as `cli_archive.c`'s own
`WS_ARCHIVE_RECHECK_COUNT`/`WS_ARCHIVE_RECHECK_SLEEP_SECONDS`: two
rechecks, one second apart).

`pg_walserver archive <path-to-file> <filename> --route <key> --host
<host> [--port <port>] [--user <name>] [--sslmode <mode>]`
(`cli_archive.c`) implements the design's own 4-step sequence per
invocation:

1. Compute the local file's own size and CRC32C (`ws_file_crc32c()`,
   `ws_util.c`, backed by the same `INIT_CRC32C`/`COMP_CRC32C`/
   `FIN_CRC32C` facility (`port/pg_crc32c.h`) real Postgres and
   `pg_autoctl`'s own `nodespec.c` already use) -- one sequential local
   read, no network cost.
2. `CHECK_FILE`. `matches` -> exit 0 immediately, zero bytes sent -- what
   makes this safe to run *alongside* something else already feeding the
   same route (an external `pg_receivewal`, or, once it lands, the
   embedded pull capturer) without ever duplicating a transfer once that
   something else has actually delivered the segment.
3. `missing`/`differs`: a short, bounded, intra-invocation recheck (sleep,
   re-`CHECK_FILE`, up to `WS_ARCHIVE_RECHECK_COUNT` times) before pushing
   for real. The design describes skipping this wait on a route known to
   be push-only (no `capture = pull` configured) -- `pg_walserver.ini` has
   no `capture` property yet in this codebase (that lands with the
   embedded pull capturer itself, a separate, later piece of work), so
   this client always does the short recheck for now; revisit this the
   moment `capture` exists to consult, per `cli_archive.c`'s own comment.
4. Push via `ARCHIVE_FILE` only once that still resolves to "missing" or
   "differs".

Exit code matches PostgreSQL's own `archive_command` contract exactly: `0`
on success (including "already matches", step 2's early exit), nonzero
with a clean stderr message on any failure, so PostgreSQL retries forever
-- this project's own precedent for "the caller (Postgres) is our retry
loop, one attempt per invocation, no local retry-count state" applies here
exactly as it does wherever else in this codebase an `archive_command`-
shaped contract is honored.

Deliberately does **not** reuse `cli_upstream.c`'s `cli_resolve_upstream()`
as-is: that helper resolves a `WsUpstreamTarget` (a `NodeAddress` +
`SSLOptions` shaped for `prepare_primary_conninfo()`, i.e. a real
*Postgres* connection) the way `fetch-systemid`/`basebackup` connect *out*
from `pg_walserver` to an upstream Postgres instance. `archive` connects
the other way, to a different kind of server entirely: it runs *on* the
Postgres primary itself, as `archive_command`, connecting *to*
`pg_walserver`'s own replication-protocol server -- a plain libpq
connection issuing `CHECK_FILE`/`ARCHIVE_FILE` as simple queries, exactly
like `src/bin/common/fetch_client.c`'s own `FETCH_FILE` client, with no
`ReplicationSource`/`pgctl.c` involved at all. `cli_archive.h`'s own
`WsArchiveTarget` mirrors `cli_upstream.h`'s flag *names*
(`--route`/`--host`/`--port`/`--user`) for consistency, but is resolved
directly in `cli_archive.c` rather than through `cli_resolve_upstream()`.

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

## Testing (tests/tap/specs/pg_walserver_standalone.pgaf)

There is no archiver integration in this PR's own stack for a test to
drive `pg_walserver` through -- no `pg_autoctl create archiver`, no
reconciler writing routes/HBA files, no monitor schema. So the tap spec
builds the smallest possible harness instead, ahead of the archiver
feature that will eventually make all of this automatic:

- `pg_walserver setup --with-basebackup` does most of the work in one
  call: creates the route's own directory, writes the `pg_walserver.ini`
  section (`path` + `upstream`), fetches node1's real system identifier
  into `archiver-systemid`, and takes the route's first base backup --
  exactly the sequence `docs/ref/pg_walserver.rst`'s own worked example
  now leads with;
- a hand-crafted `archiver-hba.conf` (a single `host all all
  127.0.0.1/32 trust` rule, scoped to the loopback peer every step in this
  spec actually connects from -- authentication itself is exercised
  elsewhere at the unit level, this spec exercises the wire protocol;
  `setup` deliberately never touches HBA, see its own header comment);
- real WAL captured off a real `pg_auto_failover`-managed primary (node1)
  by the stock OS `pg_receivewal` (not this project's own vendored copy,
  which belongs to a different PR's stack) into the route's directory,
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
   concurrently with the original capturer still running against node1;
   forcing a new segment on node1 proves both a segment still being
   written (a `.partial` file) is picked up correctly and the two captured
   copies end up byte-identical.
5. `test_004_grammar_edge_cases` -- a double-quoted slot name with an
   embedded escaped quote (`"foo""bar"`) is rejected by
   `slot_name_is_safe()` with a clean `42602`, proving the real grammar
   (not a hand-rolled parser) lexed the identifier correctly; and a
   garbage command produces a clean `ErrorResponse` without crashing the
   server or leaving the connection unusable for the next, real command.
6. `test_005_wildcard_route` -- adds a second route, reachable only
   through pg_walserver.ini's `"*"` wildcard (see "The routes file" above), with
   its own distinct system identifier; a `dbname` matching no explicit
   section resolves to it, while `default/0` -- which still has its own
   explicit section -- keeps resolving to its own path, proving an exact
   match always wins over the wildcard.
7. `test_006_real_standby_with_core_tools` -- a real, unmodified
   `pg_basebackup` client takes a `BASE_BACKUP` from `pg_walserver` of the
   exact backup `setup --with-basebackup` produced (proving
   `cmd_base_backup.c` actually serves it over the wire, not just that the
   file exists on disk, which `test_000` above only checks); a real standby
   -- `primary_conninfo` pointed at `pg_walserver`, `standby.signal`,
   nothing but stock PostgreSQL configuration and commands -- then streams
   live changes from it via a genuine walreceiver, not `pg_receivewal`, and
   is finally promoted. Needs one extra route: a real physical replication
   connection's walreceiver always sends the literal `dbname=replication`
   on the wire regardless of what `primary_conninfo` says (PostgreSQL's own
   `libpqrcv_connect()` overrides it unconditionally, `libpqwalreceiver.c`'s
   own comment: "the database name is ignored by the server in replication
   mode, but specify 'replication' for .pgpass lookup") -- so this step
   gives the route a second, literal-`"replication"` alias pointing at the
   same path, exactly as a deployment serving real physical standbys by
   name (rather than through the `"*"` wildcard) would need to.

The suite runs 7/7 green; none of the first five steps needed to change
for the removal of the `"monitor"` HBA keyword or the `fetch-file` CLI
sub-command (Tasks 1 and 2 of the PR review round that produced this
README) -- they were already written to avoid exercising either path.

### Testing SNI-based routing (tests/tap/specs/pg_walserver_sni_routing.pgaf)

A second, separate spec covers the "Routing beyond `dbname`: TLS SNI"
feature above with two real, independent routes on two `/etc/hosts`
aliases (`routeA.internal`/`routeB.internal`) resolving to the same
`pg_walserver`, both addressed with the exact same, useless
`dbname=replication` a real physical standby always sends -- proving the
disambiguation is genuinely happening by hostname, not by coincidence.
Four steps: adding a second named route via `setup --hostname` creates a
self-signed certificate automatically (`test_001`); a client presenting
each hostname over TLS is routed to that route and no other, both ways
(`test_002`); a connection with no resolvable hostname and no wildcard
fails cleanly instead of falling through to either real route
(`test_003`); and removing the certificate makes `pg_walserver serve`
refuse to start at all with two named routes configured (`test_004`).
Runs 4/4 green.

### Testing the archive push side (tests/tap/specs/pg_walserver_archive_command.pgaf)

A third, separate spec covers `CHECK_FILE`/`ARCHIVE_FILE` and the
`pg_walserver archive`/`create-cert` sub-commands above, entirely
monitor-independent as the design requires (see "The archive push side"
above). Four steps: `CHECK_FILE` reports `missing` for a filename nothing
has ever archived (`test_001`); `pg_walserver archive` pushes a brand new
file via `ARCHIVE_FILE` (byte-identical to the source on disk afterwards),
then run again against the exact same source file it reports `matches`
and skips the push entirely -- exit 0 both times, the idempotency property
PostgreSQL's own `archive_command` contract requires (`test_002`); pushing
a *different* file under the same already-archived name is cleanly
rejected (nonzero exit, the original bytes on disk untouched --
overwrite-safety proven end to end, not just at the `CHECK_FILE` layer)
(`test_003`); and `create-cert` creates a fresh certificate, refuses a
second call without `--force`, and overwrites cleanly with it
(`test_004`). Runs 4/4 green.
