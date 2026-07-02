# CryoLSP — Work Handoff (2026-07-02)

Continuation notes for picking up the CryoLSP (language server) work. The LSP had
been neglected for months and disabled by the maintainer (memory leaks +
inconsistent `::`/`.` symbol resolution). We did a deep audit, then got it
building and working on Windows and fixed the top correctness bugs.

**Read alongside this file:**
- `tools/CryoLSP/AUDIT.md` — the full deep audit (findings §0–§4, roadmap §5) with
  a STATUS header summarizing what's fixed. This HANDOFF is the actionable
  continuation; AUDIT.md is the reference.

---

## TL;DR state

- **The LSP builds, launches, and works on Windows.** Verified end-to-end against
  `examples/03-fibonacci` over scripted stdio with a real percent-encoded VS Code
  URI: clean handshake, 0 spurious diagnostics, hover shows real signatures + doc
  comments, go-to-definition jumps correctly.
- **All changes are UNCOMMITTED** and **LSP-local** (`tools/CryoLSP/**` only;
  `compiler/` and `stdlib/` untouched — confirm with `git status`).
- **NOT yet validated on Linux/WSL.** We added `![target(unix)]` branches that
  only the Windows side was exercised on. A WSL build must confirm the unix
  branches compile before committing. See "Immediate next step."
- LSP lives at `tools/CryoLSP/` (~8.8k lines of Cryo). VS Code client is
  `tools/CryoAnalyzer/` (TypeScript) — not modified.

---

## Build & test harness (Windows, reproducible)

**Build** (from PowerShell — NOT Git Bash; the stdlib recipe needs cmd syntax):
```
$env:CRYO_CC = "gcc"
cd C:\Programming\apps\CryoLang\tools\CryoLSP
..\..\bin\cryo.exe build
```
Output: `tools/CryoLSP/build/cryolsp.exe`. Uses the pinned `bin/cryo.exe`.

**Runtime dependency:** the exe needs LLVM/clang DLLs on PATH:
`C:\Programming\apps\CryoLang\.toolchains\llvm-win\bin` (has `LLVM-C.dll`,
`libclang.dll`).

**Smoke-test harness** (Python; drives the server over stdio). This is how every
fix below was verified — reuse/extend it. Key points: frame with
`Content-Length: N\r\n\r\n<body>`; use a **percent-encoded** URI like real VS Code
(`file:///c%3A/...`); parse response frames with a real Content-Length parser.

```python
import subprocess, os, json
repo = r"C:\Programming\apps\CryoLang"
f = os.path.join(repo,"examples","03-fibonacci","src","main.cryo")
text = open(f, encoding='utf-8').read()
env = dict(os.environ)
env["PATH"] = os.path.join(repo,".toolchains","llvm-win","bin") + os.pathsep + env["PATH"]
exe = os.path.join(repo,"tools","CryoLSP","build","cryolsp.exe")
fwd = f.replace("\\","/"); u = "file:///" + fwd[0].lower() + "%3A" + fwd[2:]
def frame(b): b=b.encode(); return b"Content-Length: %d\r\n\r\n%s"%(len(b),b)
def n(m,p): return frame(json.dumps({"jsonrpc":"2.0","method":m,"params":p}))
def r(i,m,p): return frame(json.dumps({"jsonrpc":"2.0","id":i,"method":m,"params":p}))
msgs=(r(1,"initialize",{"processId":None,"rootUri":None,"capabilities":{}})+n("initialized",{})
    + n("textDocument/didOpen",{"textDocument":{"uri":u,"languageId":"cryo","version":1,"text":text}})
    + r(2,"textDocument/hover",{"textDocument":{"uri":u},"position":{"line":33,"character":13}})
    + r(4,"textDocument/definition",{"textDocument":{"uri":u},"position":{"line":33,"character":13}})
    + r(9,"shutdown",{})+n("exit",{}))
p=subprocess.run([exe],input=msgs,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env,timeout=180)
data=p.stdout; i=0
while True:
    he=data.find(b"\r\n\r\n",i)
    if he<0: break
    hdr=data[i:he].decode(); cl=int([l.split(":",1)[1] for l in hdr.split("\r\n") if l.lower().startswith("content-length")][0])
    bs=he+4; m=json.loads(data[bs:bs+cl].decode()); i=bs+cl
    print(m.get("id"), m.get("method"), json.dumps(m.get("result"))[:200] if "result" in m else "")
print("EXIT:", p.returncode)
```
Traps for the harness itself:
- msys `/tmp` (Git Bash heredoc) != Python `/tmp`. Write scratch files with an
  absolute Windows path or the session scratchpad, not `/tmp`.
- `examples/03-fibonacci` is a 1-file project WITH a `cryoconfig`, so it exercises
  the real **project-mode** compile (stdlib populated) but stays fast. Good default
  fixture. A file with no reachable `cryoconfig` falls to single-file mode where
  cross-file types are invalid and hover mostly returns null.

**Compiler sanity build** (should always work; proves you didn't break the dep):
```
$env:CRYO_CC="gcc"; cd C:\Programming\apps\CryoLang\compiler; ..\bin\cryo.exe build
```

---

## What was fixed this session (all in `tools/CryoLSP/`)

Ordered build → runtime → correctness. File:line references are post-edit.

1. **Compile blocker — colliding `Diagnostic` wire struct.** `protocol/lsp.cryo`
   defined `type struct Diagnostic` (+ `DiagnosticSeverity`) whose *bare name*
   collided with the compiler's `Compiler::Diag::Diagnostic`. Cryo's resolver
   exposes a dependency's type names globally, so bare `Diagnostic` inside the
   compiler library (trait_checker.cryo, expr_parser.cryo, …) bound to the LSP's
   struct → cascading errors, LSP couldn't compile the dep. The wire struct was
   **dead** (diagnostics are built straight from `Compiler::Diag::Diagnostic` in
   `protocol/conv.cryo`), so we deleted it. `Diagnostic` was the ONLY LSP↔compiler
   type-name collision (verified by intersecting all type decls).

2. **Link blocker — missing libclang.** The compiler dep now transitively links
   libclang (Bindgen). Updated `tools/CryoLSP/cryoconfig` from a bare `[link]` to
   per-OS overlays mirroring `compiler/cryoconfig`. NOTE the Windows search path is
   `../../.toolchains/llvm-win/lib` — one directory deeper than the compiler's
   `../.toolchains/...` (LSP is at `tools/CryoLSP/`).

3. **Framing dead on arrival — `push` vs `push_byte`.** A recent stdlib refactor
   made `String::push<T>(item)` append a value's *display text*; for a `u8` that's
   its **decimal digits**, not the raw byte (stdlib doc: "use `push_byte` for a raw
   byte"). The framing reader reads stdin one byte at a time via `out.push(byte)`,
   so `Content-Length: 75` was stored as `"6711111011610111011645..."` → header
   parse failed → server bailed on frame 1. Fixed all 12 single-byte sites to
   `push_byte` (framing.cryo, session.cryo, completion/definition/hover/
   keyword_docs/semantic_tokens). **This class of bug can recur** — any new
   byte-level `String.push(<u8>)` is wrong; use `push_byte`.

4. **Windows text-mode stdio.** stdin/stdout defaulted to text mode → `\n`→`\r\n`
   on write (`Content-Length: N\r\r\n`) and would corrupt reads. Added
   `_setmode(fd, _O_BINARY)` for fd 0/1 in `main.cryo::set_stdio_binary()`,
   target-gated (`![target(windows)]` real, `![target(unix)]` no-op) with an inline
   `extern "C"` for `_setmode`. Input survived pre-fix only because JSON escapes all
   newlines; output was actively corrupt.

5. **Cross-document use-after-free (AUDIT §1a) — compile-epoch guard.** All
   sessions share ONE process-global AST arena (`compiler/.../codegen/ast_arena.cryo`,
   `g_ast_arena`) that is `reset()` at the start of every compile
   (`instance.cryo:818`). Editing doc A recycled the arena backing doc B's cached
   `ctx` → later hover/etc. on B walked freed memory (a likely source of the
   "inconsistent resolution" symptom). Fixed **LSP-locally** (did NOT touch the
   pinned compiler): `session.cryo` has a module-global `g_compile_epoch` bumped by
   every `recompile()`; each `CompilerSession` stamps `epoch`; new
   `SessionMap::ensure_fresh(docs, uri)` recompiles the doc if its epoch lags the
   global (i.e. another doc reset the arena since). Wired into
   hover/completion/definition/semantic_tokens; `code_lens` signature changed to
   take `(docs, mut sessions, params)` and the dispatch in `server.cryo` updated.

6. **`file://` URI handling on Windows (AUDIT §F-enc-adjacent) — the real reason
   stdlib/hover produced nothing.** `Conv::uri_to_fs_path` merely stripped
   `file://`, yielding `/C:/...` (invalid Windows path) and never percent-decoding
   (`%3A`). So `discover_stdlib_root` / `discover_project_cryoconfig` /
   module lookups all failed → `module 'std::core::intrinsics' not found` cascade →
   null hovers. Now it percent-decodes and, target-gated, strips the leading slash
   before a drive letter (`/C:/x`→`C:/x`); return type changed `Str`→`String`
   (decoding needs allocation) and all 7 callers updated to own+drop. The reverse
   builder (`Conv::uri_string_from_fs_path`, now public + used by definition.cryo's
   two `build_*_location` helpers) emits `file:///C:/...` (extra slash so the drive
   isn't parsed as a host). Both directions target-gated.

---

## IMMEDIATE NEXT STEP — validate on Linux/WSL

Before any commit. We only compiled the Windows branches of the target-gated code
(`main.cryo::set_stdio_binary`, `conv.cryo::normalize_fs_path`,
`conv.cryo::uri_authority_slash`). Build the LSP under WSL/Linux to confirm the
`![target(unix)]` branches compile and the server still works:
```
# in WSL, from repo root
cd tools/CryoLSP && ../../bin/cryo build      # (Linux pinned cryo)
```
Then run the smoke harness (Linux paths, `file:///home/...` URIs — no drive
letter, so `normalize_fs_path`/`uri_authority_slash` are the no-op path). Per
the maintainer's workflow, run build/test/selfhost serially; `make` from
PowerShell on Windows, selfhost via WSL.

Watch for: unused-const warnings in `definition.cryo` (`FILE_SCHEME`/
`FILE_SCHEME_LEN` may now be unused after the reverse-builder refactor — remove if
so); any `Str` vs `String` type mismatch the Windows build didn't surface.

---

## Remaining critical work (priority order)

Full detail + line refs in `tools/CryoLSP/AUDIT.md` §5. Summary:

1. **Debounce / coalesce the per-keystroke full recompile (AUDIT §1b) — biggest
   remaining usability + memory issue.** `handlers/text_sync.cryo::did_change`
   runs a full whole-project compile on EVERY keystroke (no debounce, no cancel);
   for a large project that's ~1 GB + full frontend pass per keystroke — the
   "eats memory, had to turn it off" symptom. The arena reset bounds steady-state
   memory, so this is mostly CPU/latency + transient RSS. Hard part: the server is
   a blocking single-threaded stdio read loop (`server.cryo::run`), so debouncing
   needs timed/non-blocking stdin (poll/select on POSIX, PeekNamedPipe/
   WaitForSingleObject on Windows) or a coalescing "drain pending notifications
   before compiling" scheme. This is a focused, platform-specific piece — we
   deferred it deliberately. Also add `$/cancelRequest` handling while here.

2. **Small monotonic leaks (AUDIT §1e, §1f).**
   - `server/diag_render_cache.cryo`: `by_id` map never evicts; the id is a hash of
     (file, line, col, code) so it changes as edits shift positions → unbounded.
     Fix: track ids per-URI and clear a URI's prior set at the start of each
     publish (`conv.cryo::build_publish_params` has the uri; thread a
     `begin_publish(uri)` / per-uri id list into `DiagRenderCache`). Key is opaque,
     so you must track ids per uri — can't filter by key.
   - `conv.cryo::build_diag_id` returns a leaked raw `string` (+ leaked
     intermediates) once per diagnostic per publish; callers (conv.cryo,
     code_lens.cryo) never free it. Return an owned `String` or intern it.
   - `hover.cryo` per-request `malloc`s never freed (`escape_for_hover`,
     `clean_doc_for_hover`, `i64_to_text`, `first_segment_after`) — a per-request
     arena retires the whole class.

3. **Per-recompile ctx-header leak (AUDIT §1c).** `session.cryo:154` leaks the
   `CompilationContext` + its 7 boxed sub-components (libc-allocated pre-arena) on
   every recompile; small but unbounded, and not reclaimed on `didClose`. The
   reclamation machinery exists (`cryo_ast_arena_release` + the release path used by
   the build driver) but the LSP entry bypasses it. Proper fix is a per-session
   arena (invasive to the pinned compiler — design carefully, validate against the
   self-host gate on both OSes). Lower-risk partial: call the existing release on
   session drop.

4. **UTF-16 position encoding (AUDIT §F4/§3).** LSP `character` is UTF-16 code
   units; `server/line_index.cryo` + handlers + semantic tokens treat it as bytes →
   wrong positions on any non-ASCII line. Cheapest correct fix: read
   `params.general.positionEncodings` in `lifecycle.cryo::handle_initialize`, and if
   the client offers `"utf-8"` advertise `capabilities.positionEncoding = "utf-8"` —
   then the existing byte math is correct. Otherwise implement real UTF-16 counting.

5. **`::` vs `.` symbol-resolution unification (AUDIT §2) — the original
   complaint.** `.` resolves semantically (type arena + decl_index); `::` resolves
   by scraping text before the `::` and string-matching decl names, reimplemented
   divergently across hover/completion/definition. Largest design item: give
   `ScopeResolutionNode` real sub-spans (§F5), unify all three handlers on one
   AST/type-based scope-resolution routine (§F1/§F6), AST-based trigger + prefix
   support (§F2), stop masking failures as keyword lists (§F3), add union
   completion (§F7), consolidate the duplicated helpers (§F12).

6. **Protocol hardening (AUDIT §3).** Clamp `Content-Length` before allocating
   (framing.cryo DoS, §H1); route framing through a `BufReader` (§M1). Production
   capabilities to add over time: documentSymbol, references, rename,
   signatureHelp, inlayHint, formatting, semantic-token range/delta (§M4).

---

## Gotchas / traps learned

- **`String.push(<u8>)` appends decimal text, not a raw byte** — use `push_byte`.
  This silently broke framing; audit any new byte-level push.
- **Cryo resolver leaks a dependency's type names into the global name pool.** Any
  new `type` in the LSP whose bare name matches a compiler type will break the
  compiler-library build again. (Root-cause fix is compiler-side, out of scope; the
  LSP workaround is to keep wire-type names unique.)
- **Windows needs binary stdio and drive-aware URI handling** — both now done, but
  keep it in mind for any new stdio or path code.
- **f-string `{someStr.as_str()}` printed byte decimals** during debugging — was a
  symptom of #3 (the buffer really held digits), not an f-string bug; don't chase
  it as a formatting issue.
- Build: `make test` doesn't rebuild `build/cryo` — the LSP build uses the pinned
  `bin/cryo.exe` directly, which is fine here.
- The maintainer dislikes module-level free functions; prefer methods / namespaced
  statics on the owning type.

---

## Files touched this session (all uncommitted)

```
tools/CryoLSP/cryoconfig                       # libclang link overlays
tools/CryoLSP/src/main.cryo                    # binary-mode stdio (target-gated)
tools/CryoLSP/src/protocol/lsp.cryo            # deleted dead Diagnostic wire struct
tools/CryoLSP/src/protocol/framing.cryo        # push_byte
tools/CryoLSP/src/protocol/conv.cryo           # uri_to_fs_path decode/normalize + reverse builder
tools/CryoLSP/src/server/session.cryo          # epoch guard + ensure_fresh + push_byte + uri owner
tools/CryoLSP/src/server/server.cryo           # code_lens dispatch (docs, mut sessions)
tools/CryoLSP/src/handlers/hover.cryo          # ensure_fresh + push_byte + uri owner
tools/CryoLSP/src/handlers/completion.cryo     # ensure_fresh + push_byte + uri owner
tools/CryoLSP/src/handlers/definition.cryo     # ensure_fresh + push_byte + uri owner + reverse builders
tools/CryoLSP/src/handlers/semantic_tokens.cryo# ensure_fresh + push_byte + uri owner
tools/CryoLSP/src/handlers/keyword_docs.cryo   # push_byte
tools/CryoLSP/src/handlers/code_lens.cryo      # ensure_fresh + docs param + uri owner
tools/CryoLSP/AUDIT.md                          # audit + STATUS header (not code)
```
No `compiler/` or `stdlib/` changes. `HANDOFF.md` (this file) + the audit are the
only docs.
