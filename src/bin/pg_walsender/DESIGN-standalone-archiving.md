# Design: pg_walsender becomes pg_walserver, a complete standalone archiving story

This is a **design document, not a description of shipped code** (contrast
with `README.md`, which documents this PR as it exists). Nothing here is
implemented yet. It exists to record the design before any of it lands, and
to be reviewed/amended before implementation starts, in the order given in
"Phasing" at the end.

Includes a rename: `pg_walsender` (this PR's name) becomes `pg_walserver`
once it grows past being purely a *sender* -- see "Naming" near the end for
the rationale and the open question of when the rename itself actually
lands.

## Why

`pg_walsender` today is a pure *server*: it answers `BASE_BACKUP`,
`FETCH_FILE`, `START_REPLICATION` and the replication-slot commands out of
files that already exist under a route's directory. Nothing about
*producing* those files is `pg_walsender`'s job -- an operator (see this
PR's own `docs/ref/pg_walsender.rst` "A complete standalone example") has
to hand-run `pg_basebackup`, hand-write `archiver-systemid`, and hand-run
`pg_receivewal` themselves, supervised by nothing but a bare `nohup ... &`.

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
   `pg_walsender` itself, both in the standalone story and in the
   pgaf-integrated one (`service_archiver_pgreceivewal_ctl.c`, a
   completely separate implementation on the `pg_autoctl` side).

## Naming: pg_walsender becomes pg_walserver

Everything this document adds means the binary both *sends* (`BASE_BACKUP`/
`FETCH_FILE`/`START_REPLICATION`, this PR's own scope) and *receives*
(`ARCHIVE_FILE` push, the embedded pull capturer) -- "sender" alone
undersells half its job the moment any of this lands. Checked before
picking a replacement:

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
  a new name over borrowed one; **`pg_walserver` -- "sender" replaced by
  the more general "server" -- was chosen instead**: a minimal, one-word
  rename that still says exactly what it is (a server for WAL and base
  backups, in both directions), without inventing new vocabulary or
  colliding with either PostgreSQL's own terms or this project's planned
  archiver feature.

When the rename itself actually lands (now, on the already-open PR #1193,
vs. together with phase 1 below) is still open -- see "Open questions".

## routes.ini: a new `upstream` property

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
  which, `ARCHIVE_FILE`/`ARCHIVE_STATUS` (below) are always reachable for
  any route regardless of `capture`, gated purely by `archiver-hba.conf`
  like every other command.

Naming: `upstream` was chosen over `primary_conninfo` (misleading -- the
source may be a standby) or `source`/`target` (ambiguous about direction);
it also happens to already be PostgreSQL's own vocabulary for "the server
this one replicates from" in cascading replication.

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

The wizard: writes/validates the `routes.ini` section (refuses a route key
that already exists unless `--force`; refuses a `path` that is not
creatable/writable), **checks the given role actually has `REPLICATION`**
(a real gap today -- nothing currently verifies this before `pg_basebackup`/
`pg_receivewal` fail on it later, cryptically), calls `fetch-systemid`'s
logic, and with `--with-basebackup` forks `basebackup` into the background
so `setup` itself returns promptly rather than blocking on a
multi-gigabyte copy.

## The push side: reuse the existing confirm mechanism where one exists

**This is not a green field.** The pgaf-integrated design already has a
working, monitor-backed "has this segment already landed?" check --
`pg_autoctl archive command %f` (`archiver_confirm.c`) asks
`pgautofailover.wal_archived(formation, group, walfilename)`, a
quorum-aware check (`archiverquorum` distinct archivers, not just one)
backed by `pgautofailover.archiver_wal`, itself populated by the pull
side's own reporting pipeline (`archiver_wal_notify.c`'s socket +
`service_archiver_wal_scanner.c`'s backstop scan, batched into the monitor
via `report_wal_received_bulk()`). Today that check has **no push
fallback at all**: `archiver_confirm_run()`'s `if (!confirmed) return 1`
just asks PostgreSQL to retry later, forever, relying entirely on the pull
side eventually catching up. It's also worth noting `archiver_confirm_run()`
currently skips `.backup`/`.history`/`.partial` files outright
(`archiver_confirm_is_wal_segment()` only matches a plain 24-hex segment)
-- a real gap once push actually needs to cover them too (PostgreSQL's own
contract expects `.backup` files in the archive, see above).

So there are two tiers, not one:

- **pgaf-integrated** (a monitor exists): keep `wal_archived()` as the
  fast, quorum-aware check exactly as it is today -- it is *better* than
  anything `pg_walserver` could compute on its own (it already knows about
  every archiver in the group, not just one). What's missing is purely the
  **push fallback**: `archiver_confirm_run()`, when `wal_archived()` says
  "not yet" but an archiver membership does exist (it already probes for
  this via `ARCHIVE_CONFIRM_PROBE_NAME`), should push the segment directly
  to that archiver via a new common-lib client function --
  `ws_archive_file_client()` in `src/bin/common/`, symmetric to
  `ws_fetch_file_client()`, called in-process exactly the same way
  `restore_command.c` already calls the fetch side. This closes the
  currently-real gap where a stalled/crashed pull-side capturer has no
  backstop at all, with no new monitor schema and no new confirm logic --
  only a new thing to *do* once the existing check says "not yet".
- **standalone** (no monitor at all): `pg_walserver`'s own new
  `ARCHIVE_STATUS '<name>' <size> crc32c:<hex>` wire command is the
  monitor-less equivalent of `wal_archived()` for exactly this case --
  `RowDescription(status text)` + `DataRow('missing'|'matches'|'differs')`
  + `CommandComplete`, no file transfer, using CRC32C because it's what
  `pg_basebackup`'s own backup manifest already defaults to and is already
  implemented (hardware-accelerated) in every PostgreSQL build. This tier
  genuinely has no existing equivalent to reuse -- there's no monitor to
  ask -- so it's the one piece of this section that's actually new.

Either way, the follow-up push itself is the same **`ARCHIVE_FILE
'<name>'`** (`CopyIn`): the server re-derives its own overwrite-safety
decision from the actual bytes it receives (never trusts a checksum a
client merely claims -- that would let a client lie its way past the
safety check), allow-list extended to include `.backup` (closing the gap
above on both tiers at once, since `ws_fetch_filename_is_servable()`'s
shape is what both the read and write sides should share), hard size cap
at the route's `wal_segment_size` + slack.

`pg_walserver archive %p %f`'s own logic per invocation, standalone case:

1. Compute the local file's size + CRC32C (one sequential local read, no
   network cost).
2. `ARCHIVE_STATUS`. `matches` → exit 0 immediately: the pull side already
   has this exact segment, zero bytes sent.
3. `missing`/`differs`: rather than persisting a new cross-invocation
   retry-count file (a state-tracking mechanism this codebase doesn't use
   for this class of problem -- `archiver_confirm.c`'s own comment is
   explicit that "the caller (Postgres) is our retry loop", one attempt
   per invocation, no local counters), do a short, *bounded, intra-
   invocation* recheck when the route has `capture = pull` configured
   (there's a real race: `archive_command` and the pull side both react to
   "this segment just closed" at roughly the same moment, so a fixed
   small wait -- capped in the low seconds, never approaching PostgreSQL's
   own retry cadence -- resolves most of the ties without adding any
   persistent state): sleep briefly, re-issue `ARCHIVE_STATUS` once or
   twice, then push for real if it still isn't there. A push-only route
   (no `capture = pull`) skips the wait entirely and pushes immediately.
4. Push (`ARCHIVE_FILE`) only once that resolves to "still missing".

This means a route can have `capture = pull` and be an `archive_command`
target at the same time with no coordination between the two beyond
`ARCHIVE_STATUS` -- each path is independently sufficient on its own, and
running both costs one cheap checksum-only round trip per segment instead
of a full transfer, in the common case where the pull side is healthy.

## The pull side: an embedded, supervised WAL capturer

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
  `pg_autoctl` needed something `pg_walsender` used to own.
- Net result: `pg_walserver --pgdata ... serve` alone, with one route's
  `upstream`/`capture = pull` set, is a complete archiving daemon on its
  own -- no external `pg_receivewal` process, no separate supervisor unit,
  nothing to wire up beyond `routes.ini` itself.

## Fit as a pg_auto_failover building block

- `fetch-systemid`, `basebackup`, and the embedded capturer should become
  **the** implementation, not a second one living alongside
  `archiver_systemid.c`/`service_archiver_basebackup.c`/
  `service_archiver_pgreceivewal_ctl.c` on the `pg_autoctl` side.
  `service_archiver_reconciler.c` already writes `routes.ini`; it should
  grow to also write `upstream`/`capture` and call into this same logic
  in-process, the same way `restore_command.c` now calls
  `ws_fetch_file_client()` directly instead of the `execv()`-based design
  it started with.
- The concrete integration point for push is `archiver_confirm.c`:
  `archiver_confirm_run()` gains a call to the new `ws_archive_file_client()`
  (common/) exactly where it currently just `return 1`s on an unconfirmed
  segment with a known archiver membership -- a small, additive change to
  code that already exists, not a new subsystem.
- Push (either tier) only ever fires on **completed** segments (coarser
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

- Exact `ARCHIVE_STATUS` wire shape (SQL-looking row vs. a plain
  single-value reply) -- lean towards matching `SHOW`'s existing shape for
  consistency with the rest of the grammar.
- The bounded intra-invocation recheck's exact timing (how long, how many
  rechecks) -- needs to be short enough to never look like a hang to an
  operator watching `archive_command` run, long enough to actually catch
  the common near-simultaneous-completion race with a healthy pull side.
- `archiver_confirm_is_wal_segment()` currently treats `.backup`/`.history`/
  `.partial` as "not a WAL segment, skip" (return 0 unconditionally) --
  once push needs to cover `.backup`/`.history` too (PostgreSQL's own
  contract expects them in the archive), this needs to become "check
  `wal_archived()`-equivalent coverage for these too", not stay a blanket
  skip. Needs its own look at what the monitor schema would need to track
  history/backup files in `pgautofailover.archiver_wal` alongside plain
  segments.
- Whether `pg_walserver setup`'s role-permission check should also try an
  actual `replication=true` connection (closer to what `pg_basebackup`
  itself will do) in addition to `pg_roles.rolreplication`, to catch HBA
  misconfiguration on the *upstream* side too, not just the role's own
  attribute.
- Whether the `pg_walsender` → `pg_walserver` rename lands now (touching
  the already-open, already-reviewed PR #1193 purely for the rename) or
  together with phase 1 below, when the name change actually starts being
  justified by new functionality.

## Phasing

1. `routes.ini`'s `upstream`, plus `fetch-systemid`/`basebackup`/`setup`:
   no new wire protocol, lower risk, and everything else below depends on
   `upstream` existing.
2. `ARCHIVE_STATUS` + `ARCHIVE_FILE` + `pg_walserver archive`: the new
   wire surface, with its real security/idempotency requirements
   (overwrite-safety, size caps, allow-list extension).
3. The embedded pull capturer: the largest single piece (vendor
   relocation, per-route supervision).
