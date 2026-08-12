# vendor_raylib

The headline acceptance project for the **vendor library system**:
a raylib window demo whose Cryo bindings *and*
native link flags are produced entirely by `cryo vendor` - no hand-written FFI,
no manual `[link]` entries.

It is a **live integration test** (`test.json` -> `"ignore": false`), but it is
**environment-gated** via `test.json` -> `"requires": ["vendor:RayLib", "display"]`.
The project runner *runs* it only when both prerequisites hold and otherwise
**skips it with a reason** (it is never counted as a failure), so a portable
`make test` on a headless machine without raylib stays green:

1. **`vendor:RayLib`** - raylib has been installed/built and registered once
   with `cryo vendor` (below), so host bindings exist in the registry.
2. **`display`** - an X11 (`$DISPLAY`) or Wayland (`$WAYLAND_DISPLAY`) display is
   available (the demo opens a real window; a headless runner skips it).

Binding *generation* from C headers (`binding_source: "headers"`, libclang) means
registration produces real bindings - no hand-written `bindings_file`.

## Running it by hand

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

`cryo vendor list` shows the registered library and the triples it has cached
bindings for; `cryo vendor remove RayLib` unregisters it, and `cryo vendor
clean` unregisters every library whose source directory is gone.

## Why no bindings are checked in

The point of the feature is that **no one hand-writes raylib bindings** - they
are generated from `raylib.h` per machine/triple and cached under the cryo
cache (`<cache>/vendor/raylib/<triple>/RayLib.cryo`), not committed here. This
project contains only the *consumer* (`src/main.cryo`) and the manifest
*template*.
