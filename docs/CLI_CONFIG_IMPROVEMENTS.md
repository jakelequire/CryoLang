# CLI and Configuration Improvements for Library Support

This document outlines recommended improvements to the Cryo CLI and `cryoconfig` system to properly support building libraries (like the runtime) and more complex project structures.

## Current State

### `cryoconfig` Format (Current)

```ini
[project]
project_name = "myapp"
output_dir = "build"
target_type = "executable"   # Not fully implemented

[compiler]
debug = false
optimize = true
args = []

[dependencies]
# Future feature
```

### Current Limitations

1. **target_type not implemented**: Always builds executable regardless of setting
2. **No stdlib_mode support**: Can't build stdlib/runtime components
3. **No source file specification**: Assumes `src/main.cryo`
4. **No library linking control**: Can't disable stdlib linking
5. **No LLVM output control**: Can't specify bitcode-only compilation

---

## Proposed Improvements

### 1. Extended `CryoConfig` Structure

```cpp
// include/CLI/ConfigParser.hpp

struct CryoConfig
{
    // [project] section
    std::string project_name;
    std::string output_dir = "build";
    std::string target_type = "executable";  // "executable", "static_library", "shared_library"
    std::string entry_point = "src/main.cryo";
    std::string source_dir = "src";

    // [compiler] section
    bool debug = false;
    bool optimize = true;
    bool stdlib_mode = false;      // NEW: --stdlib-mode flag
    bool no_std = false;           // NEW: Don't link stdlib
    bool emit_llvm = false;        // NEW: Emit LLVM bitcode
    bool pic = true;               // NEW: Position-independent code
    std::vector<std::string> args;

    // [sources] section - NEW
    std::vector<std::string> source_files;  // Explicit file list
    std::vector<std::string> include_patterns;  // Glob patterns

    // [build] section - NEW
    bool use_llvm_link = false;    // Use llvm-link for libraries
    bool use_llvm_ar = false;      // Use llvm-ar for static libs
    std::vector<std::string> link_libraries;

    // [dependencies] section
    std::unordered_map<std::string, std::string> dependencies;
};
```

### 2. Extended `cryoconfig` Format

```ini
# Full cryoconfig specification

[project]
project_name = "cryo-runtime"
output_dir = "build"
target_type = "static_library"    # executable | static_library | shared_library
entry_point = "entry.cryo"        # Main file (for executables)
source_dir = "."                  # Root source directory

[compiler]
debug = false
optimize = true
stdlib_mode = true                # Compile in stdlib mode
no_std = true                     # Don't link standard library
emit_llvm = true                  # Emit LLVM bitcode
pic = true                        # Position-independent code
args = ["--custom-flag"]          # Additional compiler args

[sources]
# Explicit source file list (in compilation order)
files = [
    "version.cryo",
    "platform/linux.cryo",
    "memory.cryo",
    "panic.cryo",
    "init.cryo",
    "entry.cryo"
]
# Or use glob patterns
include = ["*.cryo", "platform/*.cryo"]
exclude = ["test_*.cryo"]

[build]
llvm_link = true                  # Use llvm-link to combine bitcode
llvm_ar = true                    # Create static archive
link_libraries = ["pthread", "m"] # System libraries to link

[dependencies]
# Package dependencies (future)
# some_package = "1.0.0"
```

### 3. CLI Argument Additions

```cpp
// src/CLI/CLI.cpp - Add these flags to CompileCommand

// Stdlib mode flag
argument(CLIArgument("stdlib-mode", "Compile in stdlib mode", false).flag());

// No-std flag
argument(CLIArgument("no-std", "Don't link standard library", false).flag());

// Library output flags
argument(CLIArgument("static-lib", "Build as static library", false).flag());
argument(CLIArgument("shared-lib", "Build as shared library", false).flag());

// LLVM control
argument(CLIArgument("pic", "Generate position-independent code", false).flag());
```

### 4. BuildCommand Improvements

```cpp
// src/CLI/Commands.cpp - BuildCommand::build_project

int BuildCommand::build_project(const ParsedArgs &args)
{
    // ... existing code ...

    // Handle target type
    Cryo::Linker::CryoLinker::LinkTarget target;
    if (config.target_type == "static_library") {
        target = Cryo::Linker::CryoLinker::LinkTarget::StaticLibrary;
    } else if (config.target_type == "shared_library") {
        target = Cryo::Linker::CryoLinker::LinkTarget::SharedLibrary;
    } else {
        target = Cryo::Linker::CryoLinker::LinkTarget::Executable;
    }

    // Handle stdlib mode
    if (config.stdlib_mode) {
        compiler->set_stdlib_compilation_mode(true);
    }

    // Handle no-std
    if (config.no_std) {
        compiler->set_stdlib_linking(false);
        compiler->set_auto_imports_enabled(false);
    }

    // Handle source files
    std::vector<std::string> source_files;
    if (!config.source_files.empty()) {
        // Use explicit file list
        source_files = config.source_files;
    } else {
        // Auto-discover sources
        source_files = discover_source_files(config.source_dir, config.include_patterns);
    }

    // Compile each file
    for (const auto& src : source_files) {
        if (!compiler->compile_file(src)) {
            // Handle error
        }
    }

    // Generate output based on target type
    if (target == LinkTarget::StaticLibrary) {
        return build_static_library(compiler, output_path, config);
    } else if (target == LinkTarget::SharedLibrary) {
        return build_shared_library(compiler, output_path, config);
    } else {
        return build_executable(compiler, output_path, config);
    }
}

int BuildCommand::build_static_library(/*...*/)
{
    // 1. Compile all sources to bitcode
    // 2. Use llvm-link to combine
    // 3. Use llc to create object file
    // 4. Use llvm-ar to create archive
}
```

### 5. New ConfigParser Sections

```cpp
// src/CLI/ConfigParser.cpp

bool ConfigParser::parse_config(const std::string &config_path, CryoConfig &config)
{
    // ... existing code ...

    // Parse [sources] section
    else if (current_section == "sources")
    {
        if (key == "files")
        {
            config.source_files = parse_array(value);
        }
        else if (key == "include")
        {
            config.include_patterns = parse_array(value);
        }
    }
    // Parse [build] section
    else if (current_section == "build")
    {
        if (key == "llvm_link")
        {
            config.use_llvm_link = parse_bool(value);
        }
        else if (key == "llvm_ar")
        {
            config.use_llvm_ar = parse_bool(value);
        }
        else if (key == "link_libraries")
        {
            config.link_libraries = parse_array(value);
        }
    }

    // ... rest of parsing ...
}
```

---

## Example Configurations

### Runtime Library

```ini
[project]
project_name = "cryo-runtime"
target_type = "static_library"
source_dir = "."

[compiler]
stdlib_mode = true
no_std = true
optimize = true

[sources]
files = [
    "version.cryo",
    "platform/linux.cryo",
    "memory.cryo",
    "signal.cryo",
    "panic.cryo",
    "init.cryo",
    "entry.cryo",
    "lib.cryo"
]

[build]
llvm_link = true
llvm_ar = true
```

### Standard Library

```ini
[project]
project_name = "cryo-stdlib"
target_type = "static_library"
source_dir = "."

[compiler]
stdlib_mode = true
optimize = true

[sources]
include = ["**/*.cryo"]
exclude = ["runtime/**"]

[build]
llvm_link = true
llvm_ar = true
```

### User Application

```ini
[project]
project_name = "myapp"
target_type = "executable"

[compiler]
debug = true

[dependencies]
# Future: package dependencies
```

---

## Implementation Priority

1. **High**: `target_type` implementation (static_library, shared_library)
2. **High**: `stdlib_mode` and `no_std` flags
3. **Medium**: `[sources]` section with file lists
4. **Medium**: `[build]` section with LLVM tool control
5. **Low**: Glob pattern support for source discovery
6. **Low**: Package dependency resolution

---

## Migration Path

1. Keep backward compatibility - existing configs continue to work
2. Add new sections/fields as optional
3. Gradually deprecate hardcoded assumptions (e.g., `src/main.cryo`)
4. Document new features in CLI help and docs

