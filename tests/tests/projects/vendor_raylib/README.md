# vendor_raylib

The headline acceptance project for the **vendor library system**
(`.todo/VENDOR_PLAN.md` §6.1): a raylib window demo whose Cryo bindings *and*
native link flags are produced entirely by `cryo vendor` — no hand-written FFI,
no manual `[link]` entries.

It is **ignored** by the test suite (`test.json` → `"ignore": true`) and will
stay ignored until all of the following hold, at which point flipping the flag
turns it into a real integration test:

1. **Stage 2** of the vendor feature lands — libclang binding *generation* from
   C headers (today `cryo vendor` only copies a hand-written `bindings_file`;
   `binding_source: "headers"` here asks for real generation).
2. **raylib is installed/built** on the machine and registered once (below).
3. A **display** is available (the demo opens a real window; it cannot run on a
   headless CI runner as-is).

## Running it by hand (once Stage 2 is in)

```sh
# 1. Drop the manifest into your raylib checkout and register it. The
#    cryo-vendor.json next to this README is a ready-to-copy template.
cp cryo-vendor.json /path/to/raylib/
cryo vendor /path/to/raylib

# 2. Build and run this project. `import vendor::RayLib` resolves to the
#    generated bindings; -lraylib (+ the per-OS GL/X11/winmm set) is injected
#    automatically from the registry entry's link metadata.
cryo run
```

`cryo vendor list` shows the registered library and its per-triple cached
bindings; `cryo vendor remove RayLib` unregisters it.

## Why no bindings are checked in

The point of the feature is that **no one hand-writes raylib bindings** — they
are generated from `raylib.h` per machine/triple and cached under the cryo
cache (`<cache>/vendor/raylib/<triple>/RayLib.cryo`), not committed here. This
project contains only the *consumer* (`src/main.cryo`) and the manifest
*template*.
