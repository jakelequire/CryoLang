# vulkan-demo

The **stress test**: vendors the entire Vulkan core API from `vulkan_core.h` and
runs against the real Vulkan loader, headlessly (no GPU/display).

```sh
cd scratch/vulkan-demo
cryo run
# -> Vulkan loader: API 1.3.275, 24 instance extension(s) available
```

It queries the instance API version and the count of available instance
extensions — both work with no physical device present.

## What the generator chewed through

`bindings/Vk.cryo` was generated from `vulkan_core.h` (19,371 lines of C). The
resulting Cryo module is **18,451 lines**:

| | count |
|---|---|
| functions | 623 |
| structs | 1030 |
| enums | 245 |
| unions | 10 |
| consts | 1495 |
| function-pointer types | 1289 |

Report: **16 not bound, 539 approximated, 18 ignored** — the "approximated" are
overwhelmingly Vulkan's huge KHR/NV/EXT enum-alias duplication (e.g.
`VK_STRUCTURE_TYPE_*_KHR` aliasing the core value), diverted to alias consts
because a Cryo `type enum` needs unique discriminants, plus a handful of
bitfield runs lowered to a backing field + accessors.

## Two real issues this surfaced (and how they were handled)

1. **`vk_video` sibling headers.** Vulkan's video-codec structs reference
   `StdVideo*` types defined in `/usr/include/vk_video/` — a *sibling* of the
   `vulkan/` dir, not under it. Copying only `vulkan/` left those includes to
   resolve from the system path, where the importer's system-header filter
   dropped them, leaving ~20 dangling field types. Fix: copy `vk_video/` locally
   too (both trees are vendored under this project's manifest).

2. **A genuine compiler bug — fixed.** The generated module re-parses
   `![repr(C)] ![align(N)] type union ...` (e.g. `VkClearValue`,
   `VkClearColorValue`). The parser's `is_attachable_decl` omitted
   `UnionDeclaration`, so those directives fell onto the *module* and errored
   (E0151). The native-union feature had simply never wired unions into that
   list — a one-line fix in `parser.cryo`, now covered by the
   `aligned_union_directives_attach` regression test in
   `tests/tests/lang/unions.cryo`.

Both also reinforce a known follow-up: the vendor system needs a "treat this
include tree as project (non-system) headers" knob so libraries like this vendor
without manually copying their header trees.

## Layout

`bindings/Vk.cryo` is wired in via `source_paths`; `-lvulkan` is what the vendor
registry would inject. As with the other demos, the binding is checked in only
so you can read it — the real workflow generates it into the cache and resolves
`import vendor::Vk`.
