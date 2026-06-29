# sqlite-demo

A scratch project for **inspecting the Cryo binding file the generator produced
from SQLite's `sqlite3.h`**, and running a small demo against it.

## The generated binding

`bindings/Sqlite.cryo` is the file `cryo vendor` generated via libclang from
`sqlite3.h` — **no hand-written FFI**. From one header it bound:

- **292 functions** as `extern "C"` (bare link symbols), e.g.
  `sqlite3_open`, `sqlite3_exec`, `sqlite3_prepare_v2`, `sqlite3_column_int`.
- **31 `type struct`s**, including opaque handles (`sqlite3`, `sqlite3_stmt`)
  used as `sqlite3*` / `sqlite3_stmt*`.
- **394 `const`s** from object-like `#define`s (`SQLITE_OK`, `SQLITE_ROW`, …).
- A **function-pointer** parameter (`sqlite3_exec`'s callback →
  `(void*, i32, i8**, i8**) -> i32`).
- **3 `va_list` parameters** (`sqlite3_vmprintf`, `sqlite3_vsnprintf`,
  `sqlite3_str_vappendf`) — bound to the first-class `va_list` type. Search the
  file for `: va_list`.

The generator's honesty report flagged the unbindable constructs rather than
dropping them silently: the compound `SQLITE_*` error-code macros (e.g.
`SQLITE_IOERR_READ`, defined as `(SQLITE_IOERR | (N<<8))`) appear as `[skip]`
entries at `cryo vendor` time, not in the bindings.

`bindings/Sqlite.cryo.deps` is the include-graph sidecar used for cache
invalidation; `cryo-vendor.json` is the manifest the binding was generated from.

## Running it

The binding is wired in as a local source root (`source_paths = ["bindings"]`),
so this project is self-contained for inspection — it does **not** need the
global vendor registry:

```sh
cd scratch/sqlite-demo
cryo run
# -> SQLite 3.x.y via generated Cryo bindings: 4 rows, sum = 1042
```

`-lsqlite3` (declared in `[link]`) is what the vendor registry would normally
inject automatically; here it's spelled out since we bypass the registry.

> Note: this checks the generated `.cryo` *into* the project only so you can
> read it. In the real workflow nobody commits vendor bindings — they are
> generated per machine/triple into the cryo cache and resolved via
> `import vendor::Sqlite`.
