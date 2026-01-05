# Building the Cryo Runtime

The Cryo Runtime is the foundational layer that provides program initialization, panic handling, memory management, and platform abstraction for Cryo programs.

## Quick Build

```bash
# From the runtime directory
./build.sh

# Or with options
./build.sh --clean --verbose --debug
```

## Build Requirements

- **Cryo Compiler**: Must be built first (`make build` from project root)
- **LLVM Tools**: `llvm-link`, `llc`, `llvm-ar` (from LLVM toolchain)

## Build Process

1. **Compile**: Each `.cryo` file is compiled to LLVM bitcode (`.bc`) using `--stdlib-mode`
2. **Link**: All bitcode files are linked together with `llvm-link`
3. **Compile**: The combined bitcode is compiled to an object file with `llc`
4. **Archive**: The object file is archived into a static library with `llvm-ar`

## Output Files

| File | Description |
|------|-------------|
| `build/libcryoruntime.a` | Static library archive |
| `build/runtime.o` | Object file for linking |
| `build/runtime_combined.bc` | Combined LLVM bitcode |

## Source Files

The runtime consists of these modules (in dependency order):

| File | Purpose |
|------|---------|
| `version.cryo` | Runtime version information |
| `platform/linux.cryo` | Linux-specific implementations |
| `platform/_module.cryo` | Platform detection and abstraction |
| `platform.cryo` | Platform module re-export |
| `memory.cryo` | Global allocator and statistics |
| `atexit.cryo` | Exit handler registration |
| `signal.cryo` | Signal handling (SIGSEGV, etc.) |
| `panic.cryo` | Panic handling with hooks |
| `init.cryo` | Runtime initialization/shutdown |
| `entry.cryo` | True main() entry point |
| `lib.cryo` | Runtime module entry point |

## Integration with Cryo Programs

The runtime provides the true `main()` function. When a user writes:

```cryo
function main(argc: i32, argv: string*) -> i32 {
    println("Hello, World!");
    return 0;
}
```

The compiler renames this to `_user_main_()`, and the runtime's `main()` does:

```
1. Initialize runtime subsystems
2. Call _user_main_(argc, argv)
3. Shutdown runtime
4. Return exit code
```

## Current Limitations

The `cryoconfig` and `cryo build` command don't yet fully support building the runtime. The following features would be needed:

### Proposed `cryoconfig` Extensions

```ini
[project]
target_type = "static_library"   # vs "executable" or "shared_library"
entry_point = "entry.cryo"       # Optional: specify entry point
source_dir = "."                 # Source directory

[compiler]
stdlib_mode = true               # Enable --stdlib-mode
no_std = true                    # Don't link stdlib
emit_llvm = true                 # Emit bitcode

[build]
llvm_link = true                 # Use llvm-link for combining
llvm_ar = true                   # Use llvm-ar for archiving
pic = true                       # Position-independent code

[sources]
# Explicit ordering for dependencies
files = [
    "version.cryo",
    "memory.cryo",
    ...
]
```

### Proposed CLI Changes

```bash
# New target_type support
cryo build --target static_library

# New stdlib flags
cryo compile file.cryo --stdlib-mode --no-std

# Library output
cryo build -o libfoo.a
```

## Makefile Integration

For now, the runtime can be integrated with the main Makefile:

```makefile
# Add to makefile
NEW_RUNTIME_DIR = ./runtime
NEW_RUNTIME_BUILD_DIR = $(BIN_DIR)runtime
NEW_RUNTIME_LIB = $(NEW_RUNTIME_BUILD_DIR)/libcryoruntime.a

new-runtime:
    @cd $(NEW_RUNTIME_DIR) && ./build.sh --clean
    @cp $(NEW_RUNTIME_DIR)/build/libcryoruntime.a $(NEW_RUNTIME_LIB)
    @cp $(NEW_RUNTIME_DIR)/build/runtime.o $(BIN_DIR)stdlib/runtime.o
```

