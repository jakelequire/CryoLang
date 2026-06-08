# HANDOFF — two pre-existing Win64 (Windows) runtime bugs

Two real, independent bugs that break basic functionality of cross-compiled
Windows (`x86_64-pc-windows-gnu`) binaries under wine. Both are **pre-existing**
(not introduced by recent work) and surfaced while testing the `fs::read_dir`
Windows fix. Neither affects Linux. Listed in priority order.

> Context: the compiler already cross-builds to `cryo.exe` and the stdlib's
> sync/thread/process/net/test::runner and (now) `fs::read_dir` are Windows-ported
> and wine-verified. These two bugs are the next blockers for general Windows use.

---

## How to reproduce / verify (read this first)

A plain Cryo program cross-builds to Windows with **no** special env (only the
*compiler* needs `CRYO_CC`/`CRYO_AR`):

```sh
cd /tmp/win && mkdir -p /tmp/win   # any scratch dir with a cryoconfig + main.cryo
CRYO_STDLIB=/workspaces/CryoLang/stdlib /workspaces/CryoLang/bin/cryo build \
    --target=x86_64-pc-windows-gnu --no-incremental
WINEDEBUG=-all wine build/.../<name>.exe       # find the .exe under build/
```

Minimal `cryoconfig`:
```
[project]
project_name = "win"
output_dir = "build"
[[bin]]
name = "win"
entry_point = "main.cryo"
[compiler]
optimize = O2
```

**CRITICAL verification gotcha:** because **Bug 1 makes `fmt::println` with args
print garbage on Windows, you cannot use `fmt` output to debug anything under
wine** (including Bug 2). Verify behaviour through **process exit codes** instead
(`return` a code from `main`, read `echo $?` after `wine`). That is exactly how
the `fs::read_dir` fix was validated. Plain `fmt::println("no args")` *does* work
(only the variadic path is broken), so it's fine for coarse "got here" markers.

---

## BUG 1 — `fmt::println` (any printf-style call with arguments) is broken on Win64

### Symptom
On Windows under wine, every formatted print produces garbage; on Linux it's
correct. Plain (no-arg) prints are fine.

```
fmt::println("one int = %d", 42);      // wine: "one int = -30409112"   linux: "one int = 42"
fmt::println("two = %d %d", 7, 9);     // wine: "two = -30409112 1073851030"
fmt::println("str = [%s]", cstr);      // wine: "str = [<the format string itself>]"
```
The `%s` printing the format string back is the tell: **the varargs are read
from the wrong slots** — a va_list setup/forwarding error, not a formatting bug.

### Impact
HIGH. *All* formatted output is unusable on Windows (`print`/`println`/`eprint`/
`eprintln`, and almost certainly `format`/`sprintf`/`fprintf` too — verify). This
is bigger than it looks; most programs print.

### Root cause (where to look)
The chain: `stdlib/fmt/_module.cryo::println(fmt, args...)` →
`intrinsics::vprintf(fmt, args)`. A Cryo `args...` function lowers its bucket to a
**va_list whose shape is ABI-specific**; `vprintf`/`vfprintf` forward that va_list
to C. The Win64 path of that machinery is the suspect — it appears to have been
*written but never exercised with real args under wine until now* (see the
"the seam will not move" comment in `abi.cryo`).

Key files:
- `compiler/src/compiler/codegen/abi.cryo` — `AbiKind::Win64`, `is_win64()`,
  `va_list_alloca_type()` (returns `i8*` for Win64 vs `[24 x i8]` for SysV),
  and `forward_va_list()` (Win64 loads the `i8*` and passes **by value**; SysV
  passes the alloca **by address**). **Start here** — a wrong shape or wrong
  forwarding form here corrupts every va_arg.
- `compiler/src/compiler/codegen/llvm_types.cryo:~478` —
  `emit_va_start_for_variadic` (sets up the va_list in a variadic fn's prologue).
- `compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo:802` —
  `emit_format_runtime` (the `format`/`vasprintf` wrapper; same `forward_va_list`
  seam — a good, self-contained place to study/print the emitted IR).
- `intrinsics::vprintf` is declared in `stdlib/core/intrinsics.cryo:182`
  (`vprintf(format: string, va: void*)`); find where the compiler lowers the
  `Vprintf` intrinsic (it forwards the Cryo va_list pointer to C `vprintf`).

### Suggested approach
1. Cross-build a tiny `fmt::println("%d %d", 1, 2)` program, build with
   `--emit-llvm`, and read the emitted IR for `println` + the `format`/`vprintf`
   wrapper on the **msvc/win64** target vs the linux target. Diff the va_list
   alloca type, `llvm.va_start`, and the `vasprintf`/`vprintf` call's third arg.
2. Compare against what clang emits for the equivalent C
   (`int f(const char*f,...){ va_list v; va_start(v,f); return vprintf(f,v);}`)
   targeting `x86_64-pc-windows-gnu`. On Win64 the va_list is a plain `char*`
   and `va_start` just points it at the first stack vararg; mismatches there are
   the likely bug.
3. Verify via exit code: have the Win64 program compute something from the args
   it printed (e.g. sum two `%d` args via a non-fmt path) and `return` it.

---

## BUG 2 — `File::write` / `File::create` silently fail on Windows (wrong `O_*` flag values)

### Symptom
`File::create(p)` / `fs::write(p, bytes)` do not create the file on Windows; the
open fails (or the file is created delete-on-close). Reproduced: a program that
`fs::write("td/f.txt", ...)` under wine left `td/f.txt` **absent**.

### Impact
MEDIUM-HIGH. File creation/writing via `std::fs::File` is broken on Windows.

### Root cause (CONFIRMED — same class as the dirent-offset bug)
`stdlib/ffi/libc.cryo` defines the open flags as **glibc** values:
```
O_WRONLY=1  O_RDWR=2  O_CREAT=64 (0o100)  O_TRUNC=512  O_APPEND=1024
```
MSVCRT/mingw `_open` uses **different** values:
```
_O_WRONLY=0x01  _O_RDWR=0x02  _O_APPEND=0x08  _O_CREAT=0x100  _O_TRUNC=0x200
_O_EXCL=0x400   _O_TEMPORARY=0x40   _O_TEXT=0x4000   _O_BINARY=0x8000
```
So glibc `O_CREAT = 64 = 0x40` is **`_O_TEMPORARY`** on MSVCRT — `File::create`'s
`O_WRONLY|O_TRUNC|O_CREAT` (`stdlib/fs/file.cryo::to_flags`, line ~90) sets
*temporary*, not *create*, so `_open` never creates the file. (Also `O_APPEND
=1024=0x400` collides with `_O_EXCL`.) Two coincidental matches mask it
partially: `O_WRONLY`/`O_RDWR`/`O_TRUNC` happen to agree.

### Suggested approach (mirror the `fs::read_dir` Windows fix)
Gate the `O_*` constants per-OS, the same way `stdlib/fs/dir.cryo` now gates the
dirent offsets and `canonicalize`:
- Provide MSVCRT flag values on Windows (`![target(windows)]`) and glibc on unix.
  Either gate the constants in `ffi/libc.cryo`, or compute the flag word in a
  gated helper that `OpenOptions::to_flags()` calls.
- **Also OR in `_O_BINARY` (0x8000) on Windows** or `_open` defaults to text mode
  (CRLF translation) and corrupts binary writes.
- `DEFAULT_CREATE_MODE` (the 3rd `open` arg) is the umask-style mode; MSVCRT
  `_open` interprets it as `_S_IREAD|_S_IWRITE` — sanity-check it too.

### Verify
Pre-create nothing; have the Win64 program `fs::write("out.bin", bytes)` then
`File::open` + read it back and `return 0` only if the bytes round-trip. Check
`wine ...; echo $?`, and confirm `out.bin` exists on disk afterward.

---

## What's already in place to help
- `make selfhost-check` `[w1]`–`[w4]` exercises the Windows cross + wine path
  (stdlib + compiler cross-selfhost are byte-identical).
- `scripts/fetch-windows-llvm.sh` provisions `.toolchains/llvm-win/` (LLVM-C.dll,
  import lib, `clang.exe`, `llvm-ar.exe`) — gitignored.
- The per-OS gating pattern: `![target(unix)]` / `![target(windows)]` on
  **free functions and constants** (ConfigGating only sees file-scope decls; it
  can't gate struct fields or method bodies). See `stdlib/net/sys.cryo` and the
  recently-fixed `stdlib/fs/dir.cryo` for worked examples.
- The Win64 ABI seam lives entirely in `compiler/src/compiler/codegen/abi.cryo`
  (`AbiClassifier`) — Bug 1's fix almost certainly belongs there.

## Not in scope here (separately tracked)
`net::tls` on Windows — the code is portable OpenSSL FFI; it needs an
OpenSSL-for-mingw build + DLLs + a live-handshake test (a vendoring/infra task,
not a code bug).
