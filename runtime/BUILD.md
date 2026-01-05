# Building the Cryo Runtime

The Cryo Runtime is the foundational layer that provides program initialization, panic handling, memory management, and platform abstraction for Cryo programs.

## Configuration

The runtime uses a `cryoconfig` file to define its build settings:

```ini
[project]
project_name = "cryo-runtime"
output_dir = "build"
target_type = "static_library"
entry_point = "lib.cryo"
source_dir = "."

[compiler]
stdlib_mode = true
no_std = true
optimize = true
```

## Build Command (Planned)

Once the CLI improvements are implemented:

```bash
cd runtime
cryo build
```

The compiler will:
1. Detect `cryoconfig` in the current directory
2. Discover all `.cryo` files in `source_dir`
3. Build dependency graph from imports
4. Compile in correct order with `--stdlib-mode` and `--no-std`
5. Link into a static library using LLVM tools

## Current Build (Using Makefile)

Until `cryo build` supports libraries, use the Makefile:

```bash
# From project root
make runtime
```

## Output Files

| File | Description |
|------|-------------|
| `build/libcryo-runtime.a` | Static library archive |
| `build/cryo-runtime.o` | Object file for linking |

## Source Files

The runtime consists of these modules:

| File | Purpose |
|------|---------|
| `lib.cryo` | Runtime module entry point |
| `version.cryo` | Runtime version information |
| `entry.cryo` | True main() entry point |
| `init.cryo` | Runtime initialization/shutdown |
| `panic.cryo` | Panic handling with hooks |
| `memory.cryo` | Global allocator and statistics |
| `signal.cryo` | Signal handling (SIGSEGV, etc.) |
| `atexit.cryo` | Exit handler registration |
| `platform.cryo` | Platform module re-export |
| `platform/_module.cryo` | Platform detection and abstraction |
| `platform/linux.cryo` | Linux-specific implementations |

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
1. Initialize runtime subsystems (memory, signals, etc.)
2. Call _user_main_(argc, argv)
3. Run atexit handlers
4. Shutdown runtime
5. Return exit code
```

## Key Design Decisions

1. **Compiler-managed builds**: No build scripts needed - `cryo build` handles everything
2. **Automatic file discovery**: Compiler finds all `.cryo` files in source_dir
3. **Import-based ordering**: Compiler determines correct compilation order from imports
4. **Static library output**: Runtime is linked into every Cryo executable

