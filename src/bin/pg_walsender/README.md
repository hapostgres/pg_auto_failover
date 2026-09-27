# pg_walsender

`pg_walsender` is a standalone replication-protocol server: it speaks just
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

This PR ships `pg_walsender` and nothing that plugs it into `pg_autoctl`
yet. There is:

- no "archiver" node kind in `pg_autoctl`,
- no monitor SQL schema for it,
- no `pg_autoctl restore command`,
- no process supervision wiring it into `pg_autoctl`'s own service
  framework.

All of that lands in a later, separate PR (informally "the archiving PR"
throughout this codebase's comments). This PR is the standalone piece:
`pg_walsender` builds, runs, authenticates connections and serves the wire
protocol entirely on its own, driven by a handful of files it reads
directly off disk (`archiver-routes.ini`, `archiver-hba.conf`,
`archiver-passwd`, and per-route bookkeeping files -- see below), all of
which an operator (or, in the later PR, `pg_autoctl` itself) is expected to
create and keep current -- today, entirely by hand or by shelling out to
`pg_basebackup`/`pg_receivewal`/`psql` themselves (see
`docs/ref/pg_walsender.rst`'s own worked example). `DESIGN-standalone-
archiving.md` in this same directory designs `pg_walsender` growing
sub-commands of its own for that (`setup`, `fetch-systemid`, `basebackup`,
`archive`) plus an embedded WAL capturer, so it can be a complete,
production-grade archiver on its own, not just this PR's minimal
`serve`-only server -- not implemented yet, a design to review first.

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

`pg_walsender` implements the read side of the frontend/backend protocol by
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
  time based on which real PostgreSQL headers this build of `pg_walsender`
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
  `pg_walsender fetch-file` CLI sub-command in this same binary; it has
  been moved out, see "FETCH_FILE's client" below.

Anything else parses to `WS_CMD_UNKNOWN` and gets a clean `ErrorResponse`
(SQLSTATE `42601`) rather than a crash or a hung connection -- the
connection remains usable for the next command afterwards
(`test_004_grammar_edge_cases` in the tap spec exercises exactly this).

### FETCH_FILE's client

Earlier in this PR's own history, `pg_walsender` shipped both sides of
`FETCH_FILE`: the server handler above, and a `pg_walsender fetch-file`
CLI sub-command (a one-shot libpq client) meant to be `execv()`'d by
`pg_autoctl restore command` in the later archiving PR. Review concluded
that design was backwards: `pg_walsender` should be a server binary, full
stop, and a client used only by `pg_autoctl` belongs where `pg_autoctl` can
call it directly, in-process, with no subprocess/`execv()` indirection at
all.

The client's logic has been moved, unchanged in substance, to
`src/bin/common/fetch_client.c`/`fetch_client.h` as `ws_fetch_file_client()`.
It was a clean move rather than a rewrite because the client never actually
depended on any `pg_walsender`-internal header: it opens a plain
`PQconnectdbParams()` connection, runs `FETCH_FILE '<name>'` as a simple
query via `PQexec()`, and drains the `CopyOut` with `PQgetCopyData()` --
ordinary libpq, the same way `pg_basebackup` itself would. It never touched
`framing.h` or any other `pg_walsender`-private wire-format code (that code
is what makes the *server* side pg_walsender-specific; the client is just
another libpq application). `src/bin/common/` is already linked by both
`pg_autoctl` and `pg_walsender` (see `Makefile.common`'s `COMMON_SRC`
wildcard), so the move required no new build wiring beyond removing the
file from `pg_walsender`'s own `LOCAL_SRC` list.

`pg_walsender` itself has **no** `fetch-file` sub-command any more: its
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
live PostgreSQL server underneath `pg_walsender` for one to exist on. It is
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

`ROUTE` is `all`, or a route key exactly as it appears in `routes.ini` (see
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
  produced by `pg_walsender scram-secret` (password read from
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

Without any `--pgdata` at all, `pg_walsender` refuses to start unless
`--insecure` is explicitly given (manual testing only, never on a
reachable network): every dbname is then accepted with no authentication
whatsoever, and there is no HBA file, no routes file, and no TLS.

## The routes file (routes.ini)

`archiver-routes.ini` (`routes.c`/`routes.h`) is this server's own routing
table: one INI section per route it serves, mapping a route key -- matched
against the connection's `dbname`, i.e. what a real client puts in its
connection string's `dbname=` -- to a `path`, that route's own local
storage root. That's the *only* thing a route carries: which base backup
is current, a route's own system identifier, and the current WAL position
are deliberately **not** stored in the routes file. Every command that
needs one of those instead reads it fresh, straight off a small
purpose-built file directly under that same path, at connection time --
for instance `cmd_base_backup.c`'s own `basebackups/.latest` and
`cmd_identify_system.c`'s own `archiver-systemid`. `pg_walsender` itself
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
`routes.ini` (a human, or `service_archiver_reconciler.c` in the later
archiving PR). This is a deliberate design choice, not an oversight -- see
the wildcard route below for why substituting a route key straight into a
filesystem path would be actively dangerous, given that the key is
whatever an unauthenticated client's `dbname` says it is until HBA and
SCRAM have run.

This project's whole design predates the archiver: `pg_walsender_
standalone.pgaf` (see "Testing" below) never mentions a "formation" or a
"group" anywhere, and its own `routes.ini` uses `default/0` as nothing
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
on the fly. The wildcard is what makes `pg_walsender` usable with zero
multiplexing ceremony outside pg_auto_failover: a single-cluster
deployment can skip per-route sections entirely, keep just one `[*]`
section in `routes.ini`, and never has to learn or type a special `dbname`
value at all.

`archiver-hba.conf`'s own `ROUTE` matching is completely independent of
this: an HBA rule's `ROUTE` field is always compared against the literal
`dbname` the client sent, never against whichever `WsRoute` `routes_find()`
happened to resolve it to. A `hostssl all ...` rule already admits any
route, wildcard-resolved or not; a rule scoped to one specific route key
still only matches that literal key, exactly as before.

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

## Testing (tests/tap/specs/pg_walsender_standalone.pgaf)

There is no archiver integration in this PR's own stack for a test to
drive `pg_walsender` through -- no `pg_autoctl create archiver`, no
reconciler writing routes/HBA files, no monitor schema. So the tap spec
builds the smallest possible harness by hand instead, ahead of the
archiver feature that will eventually make all of this automatic:

- a hand-crafted `archiver-routes.ini` and `archiver-hba.conf` (a single
  `host all all 127.0.0.1/32 trust` rule, scoped to the loopback peer every
  step in this spec actually connects from -- authentication itself is
  exercised elsewhere at the unit level, this spec exercises the wire
  protocol);
- a hand-written `archiver-systemid` file, built by asking node1 directly
  over its own already-open connection (`pg_control_system()`), since this
  point in this project's own `pgaftest` history predates a cross-container
  file-copy primitive;
- real WAL captured off a real `pg_auto_failover`-managed primary (node1)
  by the stock OS `pg_receivewal` (not this project's own vendored copy,
  which belongs to a different PR's stack) into the route's directory,
  which `pg_walsender` then serves out of directly.

`pg_walsender` itself is started as a plain background process on node2
(`pg_walsender --pgdata /tmp/ws --port 6543`), and every step after that
talks to it exclusively through real clients: `psql` issuing raw
replication-protocol commands (`IDENTIFY_SYSTEM`, `SHOW`, `FETCH_FILE`,
`CREATE_REPLICATION_SLOT`, deliberately malformed input) and a real
`pg_receivewal` doing an actual `START_REPLICATION`. The six steps:

1. `test_000_sync_files_from_node1` -- assembles the hand-crafted
   fixtures above and starts `pg_walsender`.
2. `test_001_identify_system_and_show` -- `IDENTIFY_SYSTEM` reports
   node1's real system identifier; `SHOW wal_segment_size` reports 16MB.
3. `test_002_fetch_file` -- `FETCH_FILE` returns the exact bytes of a
   captured WAL segment (byte-for-byte compared via `stat`). This step
   drives the *server-side* `FETCH_FILE` handler with a plain
   `psql -c "FETCH_FILE ..."`; it does not use, and is unaffected by the
   removal of, the `fetch-file` client CLI (see "FETCH_FILE's client"
   above).
4. `test_003_start_replication_from_pg_walsender` -- a real
   `pg_receivewal` runs `START_REPLICATION` against `pg_walsender` itself,
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
   through routes.ini's `"*"` wildcard (see "The routes file" above), with
   its own distinct system identifier; a `dbname` matching no explicit
   section resolves to it, while `default/0` -- which still has its own
   explicit section -- keeps resolving to its own path, proving an exact
   match always wins over the wildcard.

The suite runs 6/6 green; none of the first five steps needed to change
for the removal of the `"monitor"` HBA keyword or the `fetch-file` CLI
sub-command (Tasks 1 and 2 of the PR review round that produced this
README) -- they were already written to avoid exercising either path.
