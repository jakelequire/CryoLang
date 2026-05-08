# sqlite-import — git-pinned dependency demo

A small notebook-style program that exercises the [cqlite](https://github.com/jakelequire/cqlite)
SQLite bindings end-to-end. The example doubles as a smoke test for
Cryo's `[dependencies]` feature — `cqlite` is pulled from GitHub at
the version pinned in `cryoconfig.lock`, not from a local path.

## What it does

- Opens `notes.db` (created in cwd) via `Connection::open`.
- Sets up a `notes(id, body, priority, done)` schema.
- Seeds five rows inside a transaction with parameterized inserts.
- Pretty-prints the table.
- Runs an `UPDATE … WHERE body = ?` and a `DELETE … WHERE priority < 2`.
- Reads `COUNT(*)` back.
- Prints the final state.

Every cqlite handle (`Connection`, `Statement`, `Transaction`) is
released explicitly via its `drop` method, including on the error
paths — see `bail` in `src/main.cryo`.

## Running

```sh
cd examples/sqlite-import
cryo fetch        # populates the global cache and writes cryoconfig.lock
cryo run          # builds + executes; produces notes.db in cwd
```

`cryo build` alone is enough to produce `build/bin/sqlite-import`
without invoking it. Subsequent runs reuse the cached cqlite checkout
and the lockfile-pinned SHA — no network round trip.

## Configuration

`cryoconfig`:

```toml
[compiler]
link_libs = ["sqlite3"]   # cqlite's link_libs is not transitive

[dependencies]
cqlite = { git = "https://github.com/jakelequire/cqlite.git", version = "0.1.0" }
```

Caret ranges work too:

```toml
cqlite = { git = "https://github.com/jakelequire/cqlite.git", version = "^0.1.0" }
```

…or pin to a branch / tag / SHA:

```toml
cqlite = { git = "...", branch = "main" }
cqlite = { git = "...", tag    = "v0.1.0" }
cqlite = { git = "...", rev    = "2d5c4f6b" }
```

## Expected output

```
----------------------------------------------------------------
  cqlite demo -- linked against libsqlite3 3.37.2
----------------------------------------------------------------
  [init]    opening notes.db
  [seed]    inserting 5 notes inside a transaction
  [ok]      committed 5 rows
  [query]   SELECT * FROM notes ORDER BY priority DESC
           +------+------------------------------+----------+------+
           |  id  |  body                        | priority | done |
           +------+------------------------------+----------+------+
           |    5 | Ship Cryo dep manager        |        5 | no   |
           |    4 | Tag cqlite v0.1.0            |        4 | no   |
           |    1 | Buy milk                     |        3 | no   |
           |    2 | Walk the dog                 |        2 | no   |
           |    3 | Read SICP, ch. 4             |        1 | no   |
           +------+------------------------------+----------+------+
  [update]  marking 'Tag cqlite v0.1.0' as done
  [ok]      updated 1 row(s)
  [delete]  DELETE FROM notes WHERE priority < 2
  [ok]      deleted 1 row(s)
  [stats]   4 note(s) remain
  [query]   final state, ORDER BY id ASC
           +------+------------------------------+----------+------+
           |  id  |  body                        | priority | done |
           +------+------------------------------+----------+------+
           |    1 | Buy milk                     |        3 | no   |
           |    2 | Walk the dog                 |        2 | no   |
           |    4 | Tag cqlite v0.1.0            |        4 | yes  |
           |    5 | Ship Cryo dep manager        |        5 | no   |
           +------+------------------------------+----------+------+
----------------------------------------------------------------
  [done]    demo complete
```
