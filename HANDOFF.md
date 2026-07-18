# HANDOFF — D1 "Canonical type identity": swap-relocate landed, one characterized residual

> Written 2026-07-18 for a fresh agent picking up on a new machine. The previous
> session took D1 from "canon_type_id is a load-bearing wall" to **single-id
> generic identity actually implemented and working**, plus resolution-level
> canonicalization and a common-root mono fix. The whole test suite is **1 error
> away from green**, and that 1 error is a *known member of a characterized class*
> (see §5). All changes are **UNCOMMITTED** on branch `ll-impl`, base commit
> `c5fabaae`. Jake (maintainer) is strict: correct architecture, not hacks. He has
> explicitly authorized changing architecture if that is what correctness requires.

---

## 0. TL;DR

- **Goal:** D1 — make `TypeID` equality == semantic equality (one canonical id per
  semantic type), so `canon_type_id`, the checker deref, and the other
  "canonicalize-on-read" bridges can be deleted.
- **Done + working:** (1) swap-relocate → `wrapper.id == concrete.id`;
  (2) resolution-level canonicalization of bare all-default types
  (`String` → `String<GlobalAlloc>`); (3) a common-root mono fix
  (`add_owner_param_bindings` carrier recovery) that fixed an entire codegen
  family; (4) unified struct-literal type resolution (one resolution authority);
  (5) a real latent-bug guard. See §3.
- **State:** `make test` = **1 error** (E0600, a method-generic enum-variant
  payload in `Option::map<U>` not re-concretizing). See §4.
- **The finding that matters:** every residual is the *same* root — Cryo
  monomorphizes by cloning the AST + substituting + **re-resolving/re-type-checking**
  each specialized body, and single-id identity keeps exposing spots where a
  **method-generic type doesn't re-concretize** in that re-resolution. This is a
  tail of one structural fragility, not independent bugs. See §5 + §6.
- **Decision pending (Jake's call):** patch the tail to green (pragmatic, unknown
  tail length) **vs** the industry-standard fix that eliminates the class — a
  substitution-stable **typed IR** + **type-check-once** mono (rustc-shaped). See §6.
- **`canon_type_id` is NOT yet deleted.** The D1 acceptance criterion (delete it +
  the checker deref while green) is still the finish line and is NOT met. Do not
  mark REPORT.md D1 off.

---

## 1. THE VALIDATION WORKFLOW — read before touching anything

Self-hosted compiler with a byte-identical bootstrap fixed-point gate. Windows
host, `CRYO_CC=gcc`. **Run `make` from PowerShell, NOT Git Bash** (Git Bash makes
the stdlib recipe die with `sh: syntax error: unexpected end of file` — learned the
hard way this session). Standard chained gate:

```powershell
$env:CRYO_CC='gcc'; make cryo          # 1. rebuild self-hosted compiler (uses the PIN to compile new source)
$env:CRYO_CC='gcc'; make test          # 2. unit + compile-fail + projects (runs the FRESH build/cryo.exe)
$env:CRYO_CC='gcc'; make selfhost-check # 3. MUST print "FIXED POINT OK" exactly 2x
make pin                                # 4. repin (Win host auto-delegates to WSL; both ELF+PE)
python scripts/verify-pin.py            # 5. sha256 pins match
```

Hard rules (each learned the hard way):
- **`make test` does NOT rebuild the compiler.** Run `make cryo` first or you gate a
  stale binary. Because `make test` runs the freshly-built compiler, a source-level
  change IS exercised end-to-end — this is why the §7 probe technique works.
- **A full `make cryo; make test` cycle is ~5-8 min.** Run it with
  `run_in_background: true` and read the tee'd log. The compiler CAN segfault while
  building the suite; capture output robustly.
- **Reading the tee log:** PowerShell `Tee-Object` output is UTF-16 in places — use
  `grep -a` (treat as text) or `Select-String`, not plain `grep`, when counting.
- **Any compiler-SOURCE change needs a repin** before commit. Only pure-comment
  changes are binary-neutral.
- **`make pin` from Windows auto-delegates to WSL**; "worktree is dirty" is expected.
- **NEVER `wsl --shutdown`** with detached WSL procs alive (wedges WSLService;
  recovery = admin `Restart-Service WSLService -Force`).
- **CRLF trap:** bulk `sed -i` via Git Bash strips CR → phantom "modified". Use the
  Edit tool, never bulk sed. (Files are CRLF; git shows autocrlf warnings — benign.)
- **Jake commits, not you.** You may repin (part of validation). Leave the tree
  clean + validated for his review.

Fast single-file scratch build (for probing):
`build/cryo.exe build file.cryo -o out.exe` with
`$env:CRYO_STDLIB='C:\Programming\apps\CryoLang\stdlib'`. **BUT single-file program
builds currently SEGFAULT in codegen on the residual (§4) — the E0600 path; a plain
`cryo build` of anything pulling stdlib hits it. Prefer `make test`, or a scratch
that avoids the crashing type, until §4 is fixed.** gdb works:
`gdb -batch -ex run -ex "bt 20" --args build/cryo.exe build file.cryo -o out.exe`
(release build → no symbols in the compiler frames, but LLVM-C frames are named).

---

## 2. THE ARCHITECTURE DELIVERED THIS SESSION (swap-relocate)

**Problem it solves.** A generic instantiation `Pair<i32>` was a *pre-mono*
`InstantiatedType` wrapper `(base, args)` at one arena id; the monomorphizer later
built a *separate concrete* `StructType Pair_i32` at a *different* id and set
`wrapper.resolved_type = concrete`. Two ids, one type. `canon_type_id` + the checker
deref existed to bridge that on read.

**What we did (A2a "swap-relocate").** At mono populate time, instead of allocating
the concrete at a fresh id, we **install the concrete at the wrapper's EXISTING id**
(`arena.types[W-1] = concrete`, id stays `W`), and **relocate the old
`InstantiatedType` node to a fresh id C** as a pure origin-carrier
(`concrete.origin_wrapper = C`, `carrier.resolved_type = W`). Net: `wrapper.id ==
concrete.id`; `find_inst_wrapping(W)` still returns a real InstantiatedType (the
carrier at C) carrying `generic_base` + `type_args`, so its callers are unchanged.
Because a nested arg like `Range<i32>` keeps ONE stable id across its wrapper→concrete
transition, the arg-id divergence that `canon_type_id` papered over vanishes at the
root.

**Where:** `arena.cryo` `reserve_spec_names` + `swap_wrapper_to_concrete`;
`monomorphizer.cryo` `specialize_with_entry` (reserve spec-names against the wrapper
BEFORE resolve so self-references in the spec body resolve to that id; swap the slot
at populate). Named struct/union/class/enum only; **function** instantiations keep the
old wrapper+`resolved_type` path.

---

## 3. WHAT IS DONE AND CORRECT (keep these; all UNCOMMITTED)

All 8 modified files, base `c5fabaae`. checker.cryo was touched then **fully
reverted** — it is intentionally NOT in the diff.

1. **`types/arena.cryo` (+81)** — `reserve_spec_names(wrapper, qualified_spec,
   bare_spec, kind)` registers the future concrete's names against the wrapper id
   (so spec-body self-refs resolve to the eventual concrete id); and
   `swap_wrapper_to_concrete(wrapper, qualified_spec, module, kind, is_union,
   display)` does the in-place slot flip + carrier relocation (§2). Idempotent if
   the slot is already concrete.

2. **`mono/monomorphizer.cryo` (+93)** — `specialize_with_entry` rewired: derive
   `target_kind`/`is_named`/`is_union` from `entry.node_kind`; on the swap path set
   `concrete_type = request.generic_type` (the wrapper id), call `reserve_spec_names`
   early, compute the display string, keep `this_type = wrapper` through
   `resolve_specialized_ast`, then at populate call `swap_wrapper_to_concrete` and
   `populate_concrete_type`. Function templates + a fallback keep the old
   create-fresh-concrete path. The `set_resolved_type`/`set_type_origin_wrapper` are
   only for the non-swap path now.

3. **`types/resolver.cryo` (+20)** — THE single-authority canonicalization for bare
   all-default types. The `resolve()` `Named` arm now wraps its result in
   `canonicalize_default_generic_type(...)` (already existed at `resolver.cryo:958`,
   was wired at only one site), so a bare `String` resolves to `String<GlobalAlloc>`
   — the same TypeID as the explicit form. `resolve_generic` resolves a `Named` base
   **directly via `resolve_named` (bypassing the canonicalization)** so explicit
   `String<i32>` instantiates the TEMPLATE, not `String<GlobalAlloc><i32>`.
   `default_expansion` pass is now largely redundant (a follow-up could delete it).

4. **`sema/type_utils.cryo` (+41)** — `concrete_instantiation_view(ty)` returns the
   origin carrier (an InstantiatedType with base+args) for a post-mono concrete
   aggregate; `instantiation_view(ty)` = `peel_to_instantiation` else
   `concrete_instantiation_view`. These bridge "concrete generic receiver" into the
   same base+args reasoning wrappers used pre-swap.

5. **`sema/method_binding.cryo` (+27)** — `find_generic_method_for_call` uses
   `instantiation_view` (so a concrete generic receiver keys method-generic
   resolution off the base template, not the concrete's own id);
   `resolve_method_return_with_explicit_args` recovers the owner's instantiation view
   at both the template-AST-lookup and owner-param-binding sites. (Fixed the
   `NonNull<X>::cast<Y>` method-generic-return collapse-to-first-spec bug.)

6. **`mono/call_specializer.cryo` (+35) — THE COMMON-ROOT FIX.**
   `add_owner_param_bindings`: under the swap, a concrete owner receiver's id now IS
   the (former) wrapper id, whose slot is the concrete; the old code read
   `e.instantiated_type` expecting an `InstantiatedType` and got the concrete →
   `inst = null` → owner params never bound → method-generic bodies' owner-typed
   constructions (`Option<T>::Some`, `NonNull<T>{}`) stayed abstract → E0600/E0638.
   Fix: recover the origin carrier via `find_inst_wrapping` for concrete owners
   (legacy spec-entry scan kept as fallback). **This fixed the entire owner-param
   codegen family at one seam** — the payoff of digging for the root.

7. **`sema/sema.cryo` (+65)** — unified struct-literal type resolution + the
   bare-default struct-literal canon. `resolve_struct_literal` now: name-based lookup
   FIRST (pure, no side effects, handles registered specs), then `lit_type =
   canonicalize_default_generic_type(struct_ref)` (so `String{}` → `String<GlobalAlloc>`),
   then — ONLY if the name lookup returned nothing — the canonical fallback
   `resolve_struct_literal_type(lit)`: resolve each `generic_arg`, bail if any is
   abstract (`contains_generic_param`), else `arena.create_instantiation(base,
   arg_refs)` (DEMAND-FREE — the same canonical slot the annotation minted; adding
   demand here perturbs sibling mono and strands owner bindings). This is what fixed
   `NonNull::cast<U>`'s `NonNull<U>{}` body literal (it was resolving to the bare
   template because the mangled-name lookup raced spec registration).

8. **`codegen/visit/enum_variant_emitter.cryo` (+6)** — `coerce_int_payload` now
   null-guards `val.raw` BEFORE `LLVMTypeOf(val.raw)` (was segfaulting inside
   LLVMTypeOf on a null payload instead of hitting the existing null check). A real
   latent bug; keep it. It converts the §4 crash into a graceful E0600.

**Gate history this session:** the swap alone took `make test` from a cascade
(197 E0900 / segfaults / 27 E0200) down to 1, via: method-generic-return fix →
resolution canon → struct-literal canon → owner-binding root fix → unified
struct-literal resolution. Each was validated by `make cryo; make test`.

---

## 4. THE CURRENT SINGLE ERROR

`make test` (from PowerShell) currently aborts with **exactly 1 error**:

```
error[E0600]: codegen: payload type of enum variant `std::core::option::Option::Some`
is unresolved (generic parameter not instantiated); the constructed value would lose
its payload
  (reported near tests/lang/lambdas.cryo module 280-281; span attribution is fuzzy)
```

Everything else — all of stdlib, all other lang/stdlib tests — **passes**. This
error was previously HIDDEN behind the `NonNull::cast<U>` E0200 (compilation aborted
at `ptr.cryo` before reaching this module); fixing cast<U> (§3.7) revealed it.

**Root (confirmed):** `Option::map<U>` (`stdlib/core/option.cryo:99-101`):
```cryo
map<U>(&this, f: (T) -> U) -> Option<U> {
    match (*this) { Option::Some(value) => { return Option::Some(f(value)); } ... }
}
```
`Option::Some(f(value))` builds `Option<U>` where `U` is the **method** generic
(inferred; the test does `some.map<i32>(lambda)` / `some.map(lambda)`). In the `$MG`
spec of `map<i32>` the enum-variant payload should re-concretize to `Option<i32>`
but stays `Option<U>` (abstract). The owner-binding fix (§3.6) covers *owner* params
(`T`), not the *method* generic `U`, and the closure passed to `map` tangles it
further (the closure-arg specialization interacts with the method-generic path).

This is the **enum-variant analog of cast<U>** (which was a struct-literal). Both:
a method-generic type inside a `$MG` spec body that doesn't re-concretize.

---

## 5. THE CHARACTERIZED RESIDUAL CLASS

Every failure this session, once traced, was the SAME root:

> Cryo monomorphizes by **cloning the specialized AST, substituting type args, and
> RE-RESOLVING / RE-TYPE-CHECKING the body** (`specialize_method` in
> `call_specializer.cryo`, `resolve_specialized_ast` in mono). That re-resolution is
> order- and identity-sensitive. Single-id identity (the swap) changed what
> receivers/types look like post-mono, exposing places where a **method-generic
> type in a spec body fails to re-concretize** — because the concretization depends
> on (a) the right owner/method bindings being recovered from a now-concrete
> receiver, and (b) nested instantiations being registered before the body is
> type-checked (a race).

Confirmed members of the class fixed this session: owner-param constructions
(§3.6), `NonNull::cast<U>` struct literal (§3.7), method-generic *return* collapse
(§3.5). Remaining member: `Option::map<U>` enum-variant payload (§4). There are
almost certainly a few more method-generic-in-`$MG`-body cases lurking behind it
(they only surface as compilation proceeds past each fix).

---

## 6. THE TWO PATHS FORWARD (Jake to decide)

### Path A — patch the tail to green (pragmatic)
Fix §4 (method-generic `U` re-concretization in `$MG` enum-variant payloads), then
whatever surfaces next, until `make test` + `selfhost-check` are green, then repin.
- **Where to start on §4:** `mono/call_specializer.cryo` `specialize_method`
  (~1892) and the post-spec re-walk (~1092-1122). Owner + method bindings ARE added
  there (owner via the now-fixed `add_owner_param_bindings` at 1103; method params at
  1113-1116). The gap is likely in how the ENUM-VARIANT construction
  (`Option::Some(f(value))`) re-derives its payload/owner type during
  `resolve_specialized_ast`, OR in how the function-typed param `f: (T)->U` re-resolves
  to `(i32)->i32` so `f(value): i32`. Check the enum-variant-constructor resolution in
  `sema` (member/call resolver) for a method-generic analog of the
  `instantiation_view` carrier recovery. Look at whether `f`'s substituted return type
  reaches the `Some(f(value))` inference.
- **Trap (verified 5x this session):** do NOT try to fix §4 by touching
  `resolve_struct_literal` or adding a struct-literal "expected-type rescue" — ANY
  change that resolves/adopts struct-literal types on the hot path RE-BREAKS the
  owner-binding family (bounces between cast<U> E0200 and Option E0600). §3.7 is
  already the minimal correct struct-literal change; leave it.
- Pro: ships a green compiler with the D1 wins. Con: symptom-patching a structural
  fragility; unknown tail length.

### Path B — the industry-standard fix (eliminates the class)
Type-check each generic **once** (abstractly), lower it to a **substitution-stable
typed IR**, and make monomorphization **pure lowering** (substitute concrete types
into the already-checked IR, NO re-type-check / re-resolve). rustc works this way;
the registration/ordering races in §5 become structurally impossible.
- Phases: (0) typed-IR node set that survives substitution; (1) lower a checked
  generic → typed IR once; (2) a mono collector computing the transitive Instance
  set; (3) codegen substitutes into the IR. Large, multi-session, touches the whole
  specialization pipeline (`mono/`, `sema/`, `codegen/`).
- Pro: correct, kills the class, is the real D1/TyCtxt shape. Con: big.

Jake authorized "changing architecture if that's what correctness requires," and
leaned toward correctness over "just done." Path B is the correct-architecture
answer; Path A is the get-green answer. **Recommend: confirm with Jake which, then
proceed.** If A gets green, D1's acceptance criterion (§8) is then in reach; if B,
D1 falls out naturally.

---

## 7. THE PROBE TECHNIQUE (still valid, use it)
Neuter a mechanism (`canon_type_id` → `return r.id;`, a checker deref → `if (false &&
…)`), `make cryo; make test; make selfhost-check`. If green → the mechanism is dead →
delete it. If it breaks (e.g. `canon_type_id` neuter historically → 36 E0900 on nested
iterators) → load-bearing → restore. Because `make test` runs the fresh build, this is
end-to-end. Batch neuters, then bisect.

---

## 8. D1 ACCEPTANCE CRITERION (the actual finish line — NOT yet met)
Mark REPORT.md D1 off ONLY when ALL hold, suite + FIXED POINT OK x2 green:
1. `canon_type_id` (`arena.cryo`) **deleted**, its 3 callers (`monomorphizer.cryo`
   make_key ~757, `sema.cryo:2714`, `type_resolution.cryo:1803`) use plain `.id`.
2. The checker `InstantiatedType → resolved_type` deref (`checker.cryo` ~131-144) +
   the structural-equality block (~280-298) **deleted** (id-equality suffices).
3. The ~15 `has_resolved_type()` deref-on-read sites either deleted or proven to be
   genuine laziness, not duality-bridging.
4. `check_compatibility` and `unify` no longer implement DIFFERENT equality
   relations (id-equality is the one relation).
5. A scratch test demonstrates: for any two TypeRefs denoting the same semantic
   type, `a.id == b.id` — including a pre-mono wrapper spelling vs its post-mono
   concrete.

Once `make test` is green (Path A or B), **re-run the §7 probe on `canon_type_id`**:
if it neuters clean, delete it + the checker bridges and re-gate. That's D1 done —
leave it for Jake to mark off.

---

## 9. GOTCHAS BANKED THIS SESSION
- **Run `make` from PowerShell, not Git Bash** (stdlib recipe dies under Git Bash).
- The struct-literal-vs-owner-binding conflict (§6 Path A trap) — do not re-litigate.
- Resolving generic args / instantiating during sema is fine for CONCRETE args
  (like `resolve_cast` does) but flags E0900 for ABSTRACT args (`Foo<?>`) — always
  gate on `contains_generic_param`, and prefer DEMAND-FREE `create_instantiation`
  (cache hit = the demanded slot) over `instantiate_for_module` in sema helpers.
- `in_symbolic_check` does NOT reliably mean "abstract args present" (template-body
  literals hit it false) — gate on arg concreteness, not that flag.
- Cryo AST-construction in sema: `new NamedAnnotation{name,span,pre_resolved}`,
  `new TypeAnnotation::Named(x)`, `new GenericAnnotation{base,args,span}`,
  `TypeAnnotation::clone_ptr(x)` (clone args before handing to a new node — they're
  owned, aliasing double-frees). Precedent at `sema.cryo:841`, `default_expansion.cryo:757`.
- `find_inst_wrapping(concrete)` = the carrier (post-swap); `peel_to_instantiation`
  returns null for a concrete — use `instantiation_view` when you need base+args
  from a possibly-concrete generic type.
- v1.0 lambdas need a braced body: `(x: T) -> { return e; }`. `function main() -> int`.
- Agent memory index: `~/.claude/projects/C--Programming-apps-CryoLang/memory/MEMORY.md`;
  D1 detail file `d1-canonical-type-identity-2026-07-17.md`.

---

## 10. IMMEDIATE NEXT STEPS FOR THE FRESH AGENT
1. `git status` — confirm the 8 modified files (§3) + this HANDOFF are present,
   uncommitted, base `c5fabaae`. checker.cryo must NOT be modified.
2. `$env:CRYO_CC='gcc'; make cryo` then `make test` (PowerShell, background) —
   confirm the state is exactly **1 error (E0600, §4)**.
3. Ask Jake: **Path A (patch to green) or Path B (typed-IR rearchitecture)** (§6).
4. If A: fix §4 per the pointers, re-gate, repeat until green + selfhost x2, repin,
   then run the §7 probe → delete `canon_type_id` + checker bridges (§8) → D1 done.
5. If B: produce the phased design first (§6 Path B), get Jake's sign-off, then build
   it incrementally behind the gate.
6. Never mark REPORT.md D1 off yourself — leave it for Jake.
