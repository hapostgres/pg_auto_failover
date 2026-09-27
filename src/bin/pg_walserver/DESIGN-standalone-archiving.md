# Design: making pg_walserver a complete standalone archiving story

This is a **design document, not a description of shipped code** (contrast
with `README.md`, which documents this PR as it exists). Nothing described
past this point is implemented yet. It exists to record the design before
any of it lands, and to be reviewed/amended before implementation starts,
in the order given in "Phasing" at the end.

This binary was renamed from `pg_walsender` to `pg_walserver` as part of
writing this document -- see "Naming" below for why, before the rest of
this document needed to keep saying "sender" while describing the exact
thing that makes that name inaccurate.

## Why

`pg_walserver` today (as `pg_walsender`, before the rename) is a pure
*server*: it answers `BASE_BACKUP`, `FETCH_FILE`, `START_REPLICATION` and
the replication-slot commands out of files that already exist under a
route's directory. Nothing about *producing* those files is its job -- an
operator (see this PR's own `docs/ref/pg_walserver.rst` "A complete
standalone example") has to hand-run `pg_basebackup`, hand-write
`archiver-systemid`, and hand-run `pg_receivewal` themselves, supervised by
nothing but a bare `nohup ... &`.

That is not a production-grade story on its own, compared to what real
PostgreSQL continuous archiving actually needs (see "PostgreSQL's own
contract" below) or to what a real product (pgBackRest, WAL-G, Barman)
offers as a single daemon. This document designs `pg_walserver` into one.

## PostgreSQL's own contract (what any complete archiver must honor)

From `https://www.postgresql.org/docs/current/continuous-archiving.html`,
the parts that constrain this design:

- **`archive_command`**: exit 0 iff success; PostgreSQL retries forever on
  nonzero. It **must refuse to overwrite a differing file**, but **must
  return success if the file already exists and is byte-identical**
  (idempotent retry after a crash mid-archive is expected and normal).
  Only fires on **completed** segments -- the worst-case data-loss window
  with archive_command alone is one segment (16MB default) plus
  `archive_timeout`.
- **Base backups**: `pg_basebackup` is the recommended tool. The backend
  itself writes a `.backup` *history file* into the archive the moment any
  base backup completes (`pg_basebackup` or the low-level API, it does not
  matter which) -- that file is archived too, same as a WAL segment.
- **`restore_command`**: a "not found" response (nonzero exit) is normal,
  not an error, during recovery's final segment. Also asked for `.history`
  files, and -- per the above -- an archive can legitimately contain
  `.backup` files too.
- **Timelines**: every promotion creates one; its `.history` file must be
  in the archive for any later recovery across it to work.

## Gaps this exposes in the current design

1. `ws_fetch_filename_is_servable()` (`cmd_fetch_file.c`) only allows a
   24-hex WAL segment or `<tli>.history` -- **no `.backup` files**. Both
   sides of a real archive-push path need this closed.
2. Nothing implements the overwrite-safety/idempotency rule at all: there
   is no path today where an untrusted client writes into a route's
   directory.
3. WAL *capture* (the pull side) is entirely external and unsupervised by
   `pg_walserver` itself, both in the standalone story and in the
   pgaf-integrated one (`service_archiver_pgreceivewal_ctl.c`, a
   completely separate implementation on the `pg_autoctl` side).

## Naming: why pg_walsender became pg_walserver

Everything this document adds means the binary both *sends* (`BASE_BACKUP`/
`FETCH_FILE`/`START_REPLICATION`, this PR's own pre-existing scope) and
*receives* (`ARCHIVE_FILE` push, the embedded pull capturer) --
`pg_walsender`'s own "sender" undersold half its job the moment any of this
was going to land. Checked before picking a replacement:

- PostgreSQL's own glossary already has precise, specific meanings for
  "WAL sender process", "WAL receiver process", and "the archiver process"
  (the backend that invokes `archive_command`) -- that last one also
  happens to be what this project's own planned pg_auto_failover feature
  informally calls itself throughout the codebase's comments ("the
  archiving PR", a future "archiver" node kind). Any `pg_archiver`/
  `pg_walarchive`-shaped name would collide with both.
- External tools in this space (pgBackRest, Barman, WAL-G) don't speak
  PostgreSQL's native replication wire protocol at all -- they're CLI hooks
  from `archive_command`/`restore_command` managing storage over their own
  protocols (SSH, S3, custom binary). That's the actual differentiator
  here: `pg_basebackup`, `pg_receivewal`, and a real standby's walreceiver
  can already talk to this binary with zero custom client. pgBackRest's
  own vocabulary for its storage side, "repository"/"repo", was considered
  and is genuinely well-precedented, but a `pg_walrepo`-style coinage adds
  a new name over a borrowed one; **`pg_walserver` -- "sender" replaced by
  the more general "server" -- was chosen instead**: a minimal, one-word
  rename that still says exactly what it is (a server for WAL and base
  backups, in both directions), without inventing new vocabulary or
  colliding with either PostgreSQL's own terms or this project's planned
  archiver feature.

The rename landed immediately, in this same PR (#1193) -- continuing to
iterate on an already-open PR through review is the normal shape of
review, not a reason to defer a one-word rename to a separate one.

## pg_walserver.ini: a new `upstream` property

```ini
[mycluster]
path     = /var/lib/archiver/mycluster
upstream = host=primary user=archiver_repl sslmode=require
capture  = pull
```

- `upstream`: a libpq connection string to the instance this route
  archives from. Read as a *default* by every sub-command below, always
  overridable by an explicit `--upstream`/`--host`/`--port`/`--user` flag
  on the command line -- the same precedence `restore_command_resolve()`
  already uses (explicit flag > config > nothing).
- `capture`: `pull` opts this route into `pg_walserver`'s own embedded WAL
  capturer (below); absent, the route is archive_command-push-only, or fed
  by something else entirely (an external `pg_receivewal`, or the
  pgaf-integrated `pg_autoctl` capturer) -- `pg_walserver` does not care
  which, `ARCHIVE_FILE`/`CHECK_FILE` (below) are always reachable for
  any route regardless of `capture`, gated purely by `archiver-hba.conf`
  like every other command.

Naming: `upstream` was chosen over `primary_conninfo` (misleading -- the
source may be a standby) or `source`/`target` (ambiguous about direction);
it also happens to already be PostgreSQL's own vocabulary for "the server
this one replicates from" in cascading replication.

## Routing beyond `dbname`: a real protocol limitation, and two ways around it

**Update: the TLS SNI option below is implemented** (`routes.c`'s
`routes_find_by_hostname()`, `tls.c`'s `ws_tls_get_sni_hostname()`,
`auth.c`'s three-tier resolution, `pg_walserver setup --hostname`, and
`serve`'s own refusal to start with more than one named route and no TLS)
-- see the README's "New client-side sub-commands" and "Routing" sections
and `tests/tap/specs/pg_walserver_sni_routing.pgaf` for the design as
built. The client certificate CN option remains just a design note below,
not built.

Testing `test_006_real_standby_with_core_tools` (this PR's own tap spec)
surfaced a genuine PostgreSQL wire-protocol fact, not a bug: a real
*physical* replication connection's walreceiver never sends whatever
`dbname` a `primary_conninfo` carries. `libpqrcv_connect()`
(`src/backend/replication/libpqwalreceiver/libpqwalreceiver.c`) builds its
own connection parameters by expanding the given conninfo as a `dbname`
parameter, then -- for physical (non-logical) replication -- appending a
*second*, literal `dbname=replication`, which libpq's own last-value-wins
parameter handling makes the one actually sent, unconditionally:

> The database name is ignored by the server in replication mode, but
> specify "replication" for .pgpass lookup.

(`pg_basebackup`/`pg_receivewal`, by contrast, build their *own* startup
packet directly from whatever `-d`/`--dbname` was given, with no such
override -- which is exactly why every other command/example in this
document routes by `dbname` successfully. Only a real standby's own
internal walreceiver connection is affected.)

Consequence: `dbname`-based routing (the `pg_walserver.ini` mechanism this
whole document is otherwise about) **cannot** select a route by name for a
real physical standby, ever -- only the `"*"` wildcard, or a route whose
key literally is `"replication"`, is reachable by one (`test_006` gives its
own route exactly this second alias, see its own comment). Fine for a
single-route deployment; a real limitation the moment more than one route
needs to be reachable by real standbys specifically, not just
`pg_basebackup`/`pg_receivewal`/`archive_command` clients that can set an
arbitrary `dbname` themselves.

Two protocol-native mechanisms, both already used by real PostgreSQL
today, route on data the `dbname=replication` override never touches --
because both happen at or before the TLS handshake, layers below where
that override lives:

- **TLS SNI (Server Name Indication)**: libpq already sends it by default.
  `fe-secure-openssl.c`'s `PQconnectPoll()` calls `SSL_set_tlsext_host_
  name(conn->ssl, host)` whenever `sslsni` (a real, documented libpq
  parameter, default **on**) is enabled and `host` isn't a literal IP --
  the exact `host` from the connection string, sent in the TLS
  `ClientHello`, before a single byte of the Postgres protocol itself is
  exchanged. PostgreSQL's own backend already has a matching, documented,
  currently-`off`-by-default *server*-side feature for exactly this:
  `ssl_sni`/`hosts_file` (customarily `pg_hosts.conf`,
  `postgresql.conf`'s own docs), backed by `sni_clienthello_cb()`
  (`src/backend/libpq/be-secure-openssl.c`) parsing the SNI extension out
  of the raw `ClientHello` bytes (via `SSL_client_hello_get0_ext()`, not
  the simpler `SSL_get_servername()`, deliberately -- OpenSSL's own advice
  against the latter's callback-ordering fragility) to pick a per-hostname
  TLS configuration. `pg_walserver`'s own `tls.c` already links raw
  OpenSSL directly (`SSL_CTX_new()`/`SSL_accept()`, no backend code, no
  libpq-fe code) -- reading the SNI hostname the same way and using it as
  an *additional* route-selection input (a per-route hostname, resolved by
  DNS or `/etc/hosts` to the same `pg_walserver` IP, exactly like
  SNI-based HTTPS virtual hosting already works) is architecturally
  straightforward, no new dependency.
- **TLS client certificate CN**: `sslcert`/`sslkey` in a conninfo are
  *not* touched by `libpqrcv_connect()`'s dbname override -- they flow
  through from the original, expanded conninfo untouched, for a real
  physical standby exactly as for any other client. A route could be
  assigned its own client certificate (a distinct CN per route), and
  `pg_walserver` could read it during the TLS handshake
  (`SSL_get_peer_certificate()` + `X509_NAME_get_text_by_NID(subject,
  NID_commonName, ...)`, both plain OpenSSL, already reachable from
  `tls.c`) as a route-selection input independent of `dbname` entirely.
  This is the same *mechanism* PostgreSQL's own `clientcert=verify-full`
  HBA option already uses to map a certificate to a role -- applying it to
  route selection instead of authentication is a natural extension, not a
  new idea.

Both are additive to `dbname`-based routing, not a replacement for it:
`pg_basebackup`/`pg_receivewal`/`archive_command` clients keep working
exactly as they do today, and either mechanism only needs to be consulted
when `dbname` resolves to nothing better than the wildcard. Not designed
in detail or scheduled into a phase yet -- flagged here because it directly
answers the limitation `test_006` surfaced, and because both are grounded
in mechanisms real PostgreSQL already ships, not speculative additions.

## New client-side sub-commands

### `pg_walserver fetch-systemid --route <key> [--upstream ...]`

One-shot: connects, runs `SELECT system_identifier FROM pg_control_system()`,
writes `<path>/archiver-systemid` atomically (`write_file_atomic()`).
New safety check nothing does today: if a systemid file **already exists
with a different value**, refuse rather than silently overwrite -- a
route's identity changing underneath it is exactly the class of
administrator error PostgreSQL's own archive_command overwrite rule
guards against elsewhere; this is the same principle applied here.

### `pg_walserver basebackup --route <key> [--upstream ...]`

Wraps a real `pg_basebackup -D <path>/basebackups/<label>` (label =
UTC timestamp, matching `service_archiver_basebackup.c`'s own scheme so
both paths stay compatible), validates the result (`read_backup_label()`
already exists and already does this parsing), and only *then* swaps
`<path>/basebackups/.latest` atomically. Never touches `.latest` on
failure -- a route always keeps serving its previous, known-good backup
until a new one actually completes.

### `pg_walserver setup --route <key> --path <dir> --upstream <conninfo> [--with-basebackup]`

The wizard: writes/validates the `pg_walserver.ini` section (refuses a route key
that already exists unless `--force`; refuses a `path` that is not
creatable/writable), **checks the given role actually has `REPLICATION`**
(a real gap today -- nothing currently verifies this before `pg_basebackup`/
`pg_receivewal` fail on it later, cryptically), calls `fetch-systemid`'s
logic, and with `--with-basebackup` forks `basebackup` into the background
so `setup` itself returns promptly rather than blocking on a
multi-gigabyte copy.

## The push side: `CHECK_FILE` + `ARCHIVE_FILE`, entirely monitor-independent

**Update: this section is implemented** (`cmd_check_file.c`, `cmd_archive_
file.c`, `cli_archive.c`'s `pg_walserver archive`, `cli_create_cert.c`'s
`pg_walserver create-cert`) -- see the README's "The wire protocol",
"`create-cert`: a self-signed TLS certificate on demand" and "The archive
push side" sections for the design as built, and `tests/tap/specs/
pg_walserver_archive_command.pgaf` for its own test coverage. Both open
questions below are resolved as built: `CHECK_FILE`'s wire shape is the
lean `SHOW`-like `RowDescription`/`DataRow`/`CommandComplete` row this
section already leaned towards, and the bounded intra-invocation recheck
is two rechecks, one second apart (`cli_archive.c`'s own
`WS_ARCHIVE_RECHECK_COUNT`/`WS_ARCHIVE_RECHECK_SLEEP_SECONDS`), always run
today rather than skipped on a known push-only route -- `pg_walserver.ini`
has no `capture` property yet in this codebase (that lands with the
embedded pull capturer, still not built, see "Phasing" below), so
`capture`-aware skipping of the recheck is left as a future refinement
once the pull capturer exists to consult.

**Scope note, because it matters more than the wire details below:** this
is a `pg_walserver`-only, standalone primitive. It does not touch, replace,
or need to know about the pgaf-integrated design's own existing,
monitor-backed "has this segment already landed?" check -- `pg_autoctl
archive command %f` (`archiver_confirm.c`) already asks
`pgautofailover.wal_archived(formation, group, walfilename)`, a
quorum-aware check (`archiverquorum` distinct archivers, not just one)
backed by `pgautofailover.archiver_wal`, itself populated by the pull
side's own reporting pipeline (`archiver_wal_notify.c`'s socket +
`service_archiver_wal_scanner.c`'s backstop scan). That mechanism is
untouched by this design, stays exactly as it is, and is *better* than
anything `pg_walserver` computes on its own the moment a monitor exists
(it already knows about every archiver in the group, not just one it's
directly talking to). Whether the future pgaf-integration PR ever calls
into what's built here -- as a fallback when `wal_archived()` says "not
yet", say -- is a decision for that PR, not this one. This PR's job is
just to make `pg_walserver` a complete, self-contained tool on its own,
with no monitor and no Postgres catalog of WAL files anywhere in the
picture: connect directly to `pg_walserver`, check what it actually has
on disk, push what it doesn't.

Two new commands, alongside `FETCH_FILE`:

- **`CHECK_FILE '<name>' <size> crc32c:<hex>`** -- a cheap query, no file
  transfer at all: `RowDescription(status text)` +
  `DataRow('missing'|'matches'|'differs')` + `CommandComplete`, the same
  shape `SHOW`/`IDENTIFY_SYSTEM` already use. The client (`pg_walserver
  archive`, running as `archive_command` on the source instance) computes
  the size and a CRC32C of the *local* file (the `%p` path, still sitting
  in the source's own `pg_wal/`) and sends that -- CRC32C because it's
  what `pg_basebackup`'s own backup manifest already defaults to, and is
  already implemented, hardware-accelerated, in every PostgreSQL build:
  computing it over a single sequential local read costs nothing next to
  a network round trip.
- **`ARCHIVE_FILE '<name>'`** -- `CopyIn`: the actual push, used only when
  `CHECK_FILE` said `missing` or `differs`. The server never trusts the
  client's own `CHECK_FILE` checksum as proof of anything -- that would
  let a client lie its way past the safety check -- it re-derives its own
  overwrite-safety decision from the real bytes it receives: identical to
  what's already on disk → success (idempotent retry, matching
  PostgreSQL's own `archive_command` contract exactly); different →
  reject. Same allow-list as `FETCH_FILE`'s read side, extended to also
  accept `.backup` files (closing the gap noted above, since
  `ws_fetch_filename_is_servable()`'s shape is what both directions should
  share), hard size cap at the route's `wal_segment_size` + slack.

`pg_walserver archive %p %f`'s own logic per invocation:

1. Compute the local file's size + CRC32C (one sequential local read, no
   network cost).
2. `CHECK_FILE`. `matches` → exit 0 immediately, zero bytes sent -- this is
   what makes `pg_walserver archive` safe to run *alongside* something
   else already feeding the same route (an embedded or external
   `pg_receivewal`, see below) without ever duplicating a transfer once
   that something else has actually delivered the segment.
3. `missing`/`differs`: rather than persisting a new cross-invocation
   retry-count file (a state-tracking mechanism this codebase doesn't use
   for this class of problem -- `archiver_confirm.c`'s own comment is
   explicit that "the caller (Postgres) is our retry loop", one attempt
   per invocation, no local counters), do a short, *bounded,
   intra-invocation* recheck when the route has `capture = pull`
   configured (there's a real race: `archive_command` and the pull side
   both react to "this segment just closed" at roughly the same moment, so
   a fixed small wait -- capped in the low seconds, never approaching
   PostgreSQL's own retry cadence -- resolves most of the ties without
   adding any persistent state): sleep briefly, re-issue `CHECK_FILE` once
   or twice, then push for real if it still isn't there. A push-only route
   (no `capture = pull`) skips the wait entirely and pushes immediately.
4. Push (`ARCHIVE_FILE`) only once that resolves to "still missing".

This means a route can have `capture = pull` and be an `archive_command`
target at the same time with no coordination between the two beyond
`CHECK_FILE` -- each path is independently sufficient on its own
(`ARCHIVE_FILE` alone already satisfies PostgreSQL's full `archive_command`
contract for a route with no pull capture at all), and running both costs
one cheap checksum-only round trip per segment instead of a full transfer,
in the common case where the pull side is healthy.

## The pull side: an embedded, supervised WAL capturer

**Update: this section is implemented** (`capture.c`'s `ws_capture_
start_all()`/`ws_capture_tick()`/`ws_capture_stop_all()`, `routes.h`'s
`WsRoute.capturePull`, `pg_walserver setup --capture pull`) -- see the
README's "The embedded pull capturer" section for the design as built,
and `tests/tap/specs/pg_walserver_capture.pgaf` for its own test coverage.
This section's original wording below ("forked, supervised child") was
ambiguous about `fork()`-only vs. `fork()`+`exec()`; built as `fork()` +
`execv()` of `pg_walserver` itself, re-entered as a new hidden
`pg_walserver internal service pg-receivewal` sub-command (`cli_internal.
c`) that calls `pg_receivewal_main()` in-process -- mirroring
`pg_autoctl`'s own long-lived-service pattern (`service_postgres_ctl_
start()`) rather than a bare fork with no exec, so a restarted capturer
is safe as part of a container's PID 1. Supervision itself is built on a
new, shared, generic child-process supervisor (`src/bin/common/process_
supervisor.h`, a decoupled extraction of `pg_autoctl`'s own `supervisor.c`
core), not a bespoke loop. The vendor relocation below landed as its own,
isolated commit, ahead of the capturer itself, exactly as "Phasing" asked
for.

A route with `capture = pull` gets its own forked, supervised child
running the **already-vendored** `pg_receivewal` (PR #1191's
`src/bin/pg_autoctl/vendor/pg_receivewal/`, `pg_receivewal_main()`) against
`upstream`, capturing straight into the route's own directory -- the
exact flat layout `wal_dir_scan.c`/`cmd_start_replication.c` already
expect, so nothing downstream changes.

- Supervision follows the precedent already in this codebase for exactly
  this shape of problem: `accept_loop.c`'s own `hba.c` "monitor" feature
  runs a single supervised `refresher.c` child (start/reap/restart on
  death, one extra `AF_UNIX`-free case here since there's no IPC needed
  back to the parent). This is the same pattern, one capturer child per
  *active* `capture = pull` route instead of a singleton.
- **Vendor relocation**: `vendor/pg_receivewal/` currently lives under
  `src/bin/pg_autoctl/`, linked only by `pg_autoctl`. Once `pg_walserver`
  also needs it, it moves to `src/bin/common/vendor/pg_receivewal/` so
  both binaries build the identical sources rather than duplicating them
  -- the same move `fetch_client.c` already went through in this PR when
  `pg_autoctl` needed something `pg_walserver` used to own.
- Net result: `pg_walserver --pgdata ... serve` alone, with one route's
  `upstream`/`capture = pull` set, is a complete archiving daemon on its
  own -- no external `pg_receivewal` process, no separate supervisor unit,
  nothing to wire up beyond `pg_walserver.ini` itself.

## Fit as a pg_auto_failover building block

- `fetch-systemid`, `basebackup`, and the embedded capturer should become
  **the** implementation, not a second one living alongside
  `archiver_systemid.c`/`service_archiver_basebackup.c`/
  `service_archiver_pgreceivewal_ctl.c` on the `pg_autoctl` side.
  `service_archiver_reconciler.c` already writes `pg_walserver.ini`; it should
  grow to also write `upstream`/`capture` and call into this same logic
  in-process, the same way `restore_command.c` now calls
  `ws_fetch_file_client()` directly instead of the `execv()`-based design
  it started with.
- A natural future integration point exists at `archiver_confirm.c`
  (`archiver_confirm_run()` currently just `return 1`s, retry-forever, when
  `wal_archived()` says "not yet") -- but whether, when, and how the
  pgaf-integration PR ever calls into `CHECK_FILE`/`ARCHIVE_FILE` from
  there is explicitly *not* decided by this document or built in this PR;
  see "Scope note" above. This PR's job is only to make sure that
  primitive exists, is monitor-independent, and is worth reusing when that
  decision gets made.
- Push (either path) only ever fires on **completed** segments (coarser
  RPO than continuous streaming replication) and is asynchronous relative
  to commit, so it cannot participate in `synchronous_standby_names`
  quorum the way a live streaming connection with flush-position feedback
  can. For the archiver's role as a failover-aware quorum member, the
  pull/streaming path stays load-bearing; push is a strong
  *defense-in-depth backstop* there (never lose a segment even if the
  streaming capturer is down for a while), and a perfectly sufficient
  *sole* mechanism for the simpler, non-HA standalone case this document
  is otherwise about.

## Open questions to resolve before implementation

- Whether SNI-hostname and/or client-cert-CN routing (see "Routing beyond
  dbname" above) get designed and scheduled into a phase, given they're
  the only way for more than one route to be reachable by name by a real
  physical standby specifically.
- **Resolved, see the "push side" section's own "Update" note above:**
  exact `CHECK_FILE` wire shape (SQL-looking row vs. a plain single-value
  reply) -- lean towards matching `SHOW`'s existing shape for consistency
  with the rest of the grammar.
- **Resolved, see the same "Update" note:** the bounded intra-invocation
  recheck's exact timing (how long, how many rechecks) -- needs to be
  short enough to never look like a hang to an operator watching
  `archive_command` run, long enough to actually catch the common
  near-simultaneous-completion race with a healthy pull side.
- Whether `pg_walserver setup`'s role-permission check should also try an
  actual `replication=true` connection (closer to what `pg_basebackup`
  itself will do) in addition to `pg_roles.rolreplication`, to catch HBA
  misconfiguration on the *upstream* side too, not just the role's own
  attribute.

Explicitly out of scope for this document/PR, left for whoever designs the
pgaf-integration side's use of this: whether/how `archiver_confirm.c`
ever calls into `CHECK_FILE`/`ARCHIVE_FILE`, and what `archiver_confirm_
is_wal_segment()`'s current blanket skip of `.backup`/`.history`/`.partial`
files should become there. Both are that PR's decisions to make, informed
by this one existing as a building block, not this PR's to make for it.

## Phasing

1. `pg_walserver.ini`'s `upstream`, plus `fetch-systemid`/`basebackup`/`setup`:
   no new wire protocol, lower risk, and everything else below depends on
   `upstream` existing.
2. **Done.** `CHECK_FILE` + `ARCHIVE_FILE` + `pg_walserver archive`: the new
   wire surface, with its real security/idempotency requirements
   (overwrite-safety, size caps, allow-list extension) -- entirely
   monitor-independent, see "Scope note" above. `pg_walserver create-cert`
   also landed in this phase (a thin CLI wrapper around the same
   `pg_create_self_signed_cert()` call `setup` already made automatically).
3. **Done.** The embedded pull capturer: the largest single piece (vendor
   relocation, per-route supervision). Landed as several isolated commits:
   the `vendor/pg_receivewal/` relocation (`src/bin/pg_autoctl/` to
   `src/bin/common/`) on its own first; a generic, shared child-process
   supervisor (`src/bin/common/process_supervisor.c/h`, extracted from
   `pg_autoctl`'s own `supervisor.c`) next; then the capturer feature
   itself (`capture.c`, the hidden `pg_walserver internal service
   pg-receivewal` sub-command, the `capture` routes.ini property,
   `pg_walserver setup --capture pull`) built on top of both.
