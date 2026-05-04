# Handoff — generic-method-on-non-generic-struct specialization gap

**Branch:** `new-stdlib`
**Last verified:** selfhost-check passes, IR md5 `2f1d162e87c2eb56f907ed8e0a313310`. Pin (`bin/cryo`) is current.

## What's working

The previous session's body-strip fix landed in
`compiler/src/compiler/codegen/decl_codegen.cryo` `codegen_function_epilogue`.
It walks every basic block in the just-finished function and emits
`unreachable` into any block that is empty *and* unterminated (typical
source: the merge block of `loop { ... }` with no `break`). Without it
the body-validator strips the whole function to a `declare`, cascading
silent `undefined reference` errors at link time.

That fix resolved most of the user's sandbox link errors:
`read_fd`, `write_fd`, `close_fd`, `serve` (and friends) all link.

## What's still broken — the actual handoff problem

A trivial HTTP-server sandbox still fails to link with **3 undefined
references**, all in the same family. Reproducer:

```bash
mkdir -p /tmp/cryo-http-repro/src
cat > /tmp/cryo-http-repro/cryoconfig <<'EOF'
[project]
project_name = "http_repro"
target_type  = "executable"
entry_point  = "src/main.cryo"
source_dir   = "src"
output_dir   = "build"
EOF
cat > /tmp/cryo-http-repro/src/main.cryo <<'EOF'
namespace Main;

import std::net::http::server;
import std::net::http::request;
import std::net::http::response;
import std::net::http::status;
import std::collections::str;

function handler(req: Request) -> Response {
    return Response::text(StatusCode::ok(), Str::from_raw("hi" as u8*, 2));
}

function main() -> int {
    match (serve("127.0.0.1:0" as string, handler)) {
        Result::Ok(_)  => { }
        Result::Err(_) => { }
    }
    return 0;
}
EOF
cd /tmp/cryo-http-repro && /home/phock/Programming/apps/CryoLang/bin/cryo build
```

Linker says (excerpted):

```
undefined reference to
  C$3std.11collections.6string.526String$LN$L3std.5alloc.9allocator.11GlobalAlloc$G$G-4hash$F$s_Rv$Rv

undefined reference to
  C$3std.3net.4http.7request.7Request-5parse$FRN$L1R$G$RN$L...

undefined reference to
  C$3std.3net.4http.8response.8Response-8write_to$F$s_RN$L1W$G$RN$L...
```

Tells: the parameter slots in those mangled names contain `1R`, `1W`,
or `Rv` — i.e. the **generic param name leaked into the symbol** instead
of being substituted with the concrete type the call uses. That only
happens when the generic method was **never specialized**, so codegen
falls back to a mangle built from the template.

## Root cause (confirmed)

All three callees are **generic methods on non-generic owner types**:

| Owner                 | Method            | Owner kind         |
|-----------------------|-------------------|--------------------|
| `Request`             | `parse<R>`        | non-generic struct |
| `Response`            | `write_to<W>`     | non-generic struct |
| `String<GlobalAlloc>` | `hash<H>`         | spec'd struct (different sub-bug) |

The monomorphizer's method-call inference path is in
`compiler/src/compiler/types/monomorphizer.cryo`:

- `try_infer_method_call` (line 1696) — handles instance `obj.m(...)`
- `try_infer_function_call` (line 1500) — handles `Type::m(...)` and bare `f(...)`

For instance calls (`response.write_to(stream)`), the lookup is:
- `find_spec_impl_method` (line 1635) — walks `spec_entries` (only spec'd generic types)
- `find_trait_impl_method_for_target` (line 1665) — walks `trait_impl_blocks` (primitives)

Neither path covers **inherent methods on non-generic structs**, which is
where `Request::parse<R>` and `Response::write_to<W>` live (they're
declared inline in `type struct Response { ... }` in
`stdlib/net/http/response.cryo:79` and `stdlib/net/http/request.cryo:63`).

For static calls (`Request::parse(stream)`), `try_infer_function_call`
handles `ScopeResolution` callees (line 1520) but only finds free
functions in the template registry — static methods on non-generic
structs aren't there either.

## The String hash sub-bug (related but separate)

For `String<GA>::hash<H>`: `String<A>` IS in `spec_entries`, so the
existing path *should* find the hash method and specialize it. But the
emitted call site has `Rv` (Reference void) where `&DefaultHasher`
should be. Worse: `stdlib/.bin/obj/std__collections__string.ll` emits
zero String methods at all — only the `format`/`panic` boilerplate.
String spec methods are emitted in their *consumer* modules instead
(per-module specialization model), but `String<GA>::hash` is never
emitted anywhere.

The `Rv` encoding comes from `encode_type_ref`
(`compiler/src/compiler/resolver/mangled_name.cryo:575`) returning `"v"`
when a TypeRef is invalid or `arena.lookup` returns null. So somewhere
the H parameter's `resolved_type` is `Reference(invalid)` rather than
`Reference(GenericParam(H))`. That's likely the trait method's template
registration running with H unbound in its resolution context.

## Required fix scope

1. **`try_infer_method_call`** — add a third lookup path after
   `find_spec_impl_method` / `find_trait_impl_method_for_target` that
   handles inherent methods on **non-generic struct/class types**.
   Source: walk inline methods stored on the StructDecl AST node, or
   walk `entries[].impl_blocks` keyed by the receiver's `qualified_name`
   in `GenericRegistry`. Note: in the stdlib, methods are commonly
   declared *inline* on the struct (`type struct Foo { static m() {} }`)
   rather than in separate `implement Foo { ... }` blocks — handle
   both forms.

2. **`try_infer_function_call`** — for `ScopeResolution` callees, after
   the free-function template lookup fails, try a **static-generic-method
   lookup**: resolve `scope_name` to a struct/class type and look for a
   generic static method matching `member_name`.

3. **String hash sub-bug** — separately, figure out why the generic-
   method template's H param has `Reference(invalid)` resolved_type.
   Either fix the template's resolution context to bind H, or make
   `find_spec_impl_method`'s spec actually fire (so the template's
   broken mangle never gets used). This is the harder piece because the
   bug is in trait-method template resolution, not in the
   inherent-method lookup gap above.

## Useful entry points

- `compiler/src/compiler/types/monomorphizer.cryo:1696` —
  `try_infer_method_call` (the function to extend with case 1)
- `compiler/src/compiler/types/monomorphizer.cryo:1500` —
  `try_infer_function_call` (extend with case 2)
- `compiler/src/compiler/types/monomorphizer.cryo:1635` —
  `find_spec_impl_method` (template for the new inherent-method walker)
- `compiler/src/compiler/types/generic_registry.cryo:49` —
  `entries[].impl_blocks` storage (the lookup target)
- `compiler/src/compiler/resolver/mangled_name.cryo:575` —
  `encode_type_ref` (where the `Rv` for H comes from)
- `compiler/src/compiler/codegen/ir_generator.cryo:2116` — Strategy 0
  for instance-method codegen; reads `node.resolved_method` from mono
  pass and builds the mangled name. If `resolved_method` stays null, it
  falls through to the Strategy 1+ name-based lookups, which is what
  produces the leaked-generic-param mangles.

## How to verify a fix

```bash
# 1. The minimal repro must link:
cd /tmp/cryo-http-repro && /home/phock/Programming/apps/CryoLang/bin/cryo build

# 2. Selfhost must stay byte-identical:
make selfhost-check

# 3. The previously-stripped String/Request/Response methods should be
#    DEFINED (not just declared) in their consumer modules:
nm /tmp/cryo-http-repro/build/obj/std__net__http__server.o | grep -E "parse|write_to"
nm /tmp/cryo-http-repro/build/obj/std__core__panic.o      | grep -E "String.*hash"
```

## Things NOT to do

- Don't "rename to non-generic" workarounds in the stdlib (we'd lose
  the actual `Request::parse<R: Read>` polymorphism). User explicitly
  wants the compiler fixed, not the stdlib reshaped.
  See `feedback_stdlib_api_stance` memory.
- Don't skip the body-strip epilogue fixup added in the prior session;
  it's load-bearing for many other call sites.
- Don't re-introduce `printf`-based debug logging — the codebase was
  swept to `cdebug`/`Utils::Logger` gated on `--debug` / cryoconfig
  `debug = true`. New diagnostic prints should use `cdebug`.
