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
3. **No source directory specification**: Assumes `src/main.cryo`
4. **No library linking control**: Can't disable stdlib linking
5. **No entry point configuration**: Hardcoded to `src/main.cryo`

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
    std::string entry_point = "src/main.cryo";  // NEW: configurable entry point
    std::string source_dir = "src";             // NEW: source directory root

    // [compiler] section
    bool debug = false;
    bool optimize = true;
    bool stdlib_mode = false;   // NEW: --stdlib-mode flag
    bool no_std = false;        // NEW: Don't link stdlib
    std::vector<std::string> args;

    // [dependencies] section
    std::unordered_map<std::string, std::string> dependencies;
};
```

### 2. Extended `cryoconfig` Format

```ini
[project]
project_name = "cryo-runtime"
output_dir = "build"
target_type = "static_library"    # executable | static_library | shared_library
entry_point = "lib.cryo"          # Main entry point file
source_dir = "."                  # Root source directory

[compiler]
debug = false
optimize = true
stdlib_mode = true                # Compile in stdlib mode
no_std = true                     # Don't link standard library
args = []                         # Additional compiler args

[dependencies]
# Package dependencies (future)
```

### 3. Automatic Source Discovery

When `cryo build` runs with a `cryoconfig`, the compiler should:

1. **Discover all `.cryo` files** in `source_dir` recursively
2. **Parse imports** to build a dependency graph
3. **Topologically sort** files by dependencies
4. **Compile in correct order** automatically

This means the compiler handles file management - no need for explicit file lists.

### 4. ConfigParser Updates

```cpp
// src/CLI/ConfigParser.cpp

// In parse_config(), add handling for new fields:
if (current_section == "project")
{
    if (key == "project_name")
        config.project_name = value;
    else if (key == "output_dir")
        config.output_dir = value;
    else if (key == "target_type")
        config.target_type = value;
    else if (key == "entry_point")      // NEW
        config.entry_point = value;
    else if (key == "source_dir")        // NEW
        config.source_dir = value;
}
else if (current_section == "compiler")
{
    if (key == "debug")
        config.debug = parse_bool(value);
    else if (key == "optimize")
        config.optimize = parse_bool(value);
    else if (key == "stdlib_mode")       // NEW
        config.stdlib_mode = parse_bool(value);
    else if (key == "no_std")            // NEW
        config.no_std = parse_bool(value);
    else if (key == "args")
        config.args = parse_array(value);
}
```

### 5. BuildCommand Updates

```cpp
// src/CLI/Commands.cpp - BuildCommand::build_project

int BuildCommand::build_project(const ParsedArgs &args)
{
    // ... parse cryoconfig ...

    // Determine entry point (use config or default)
    std::string entry_point = config.entry_point.empty()
        ? "src/main.cryo"
        : config.entry_point;

    // Use source_dir if specified
    std::string source_dir = config.source_dir.empty()
        ? "src"
        : config.source_dir;

    // Handle stdlib mode
    if (config.stdlib_mode) {
        compiler->set_stdlib_compilation_mode(true);
    }

    // Handle no-std
    if (config.no_std) {
        compiler->set_stdlib_linking(false);
        compiler->set_auto_imports_enabled(false);
    }

    // Discover source files in source_dir
    std::vector<std::string> source_files = discover_sources(source_dir);

    // For libraries: compile all sources, link into library
    // For executables: compile and link as before

    if (config.target_type == "static_library") {
        return build_static_library(compiler, source_files, output_path);
    } else if (config.target_type == "shared_library") {
        return build_shared_library(compiler, source_files, output_path);
    } else {
        return build_executable(compiler, entry_point, output_path);
    }
}

std::vector<std::string> BuildCommand::discover_sources(const std::string& source_dir)
{
    std::vector<std::string> sources;

    // Recursively find all .cryo files
    for (const auto& entry : std::filesystem::recursive_directory_iterator(source_dir)) {
        if (entry.path().extension() == ".cryo") {
            sources.push_back(entry.path().string());
        }
    }

    return sources;
}

int BuildCommand::build_static_library(
    CompilerInstance* compiler,
    const std::vector<std::string>& sources,
    const std::string& output_path)
{
    std::vector<std::string> object_files;

    // 1. Compile each source to bitcode
    for (const auto& src : sources) {
        std::string bc_path = get_bitcode_path(src);
        if (!compile_to_bitcode(compiler, src, bc_path)) {
            return 1;
        }
        object_files.push_back(bc_path);
    }

    // 2. Link bitcode files with llvm-link
    std::string combined_bc = output_dir + "/combined.bc";
    if (!llvm_link(object_files, combined_bc)) {
        return 1;
    }

    // 3. Compile to object with llc
    std::string obj_path = output_dir + "/" + project_name + ".o";
    if (!llc_compile(combined_bc, obj_path)) {
        return 1;
    }

    // 4. Create archive with llvm-ar
    if (!llvm_ar_create(obj_path, output_path)) {
        return 1;
    }

    return 0;
}
```

---

## Example Configurations

### Runtime Library

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

[dependencies]
```

### Standard Library

```ini
[project]
project_name = "cryo-stdlib"
output_dir = "build"
target_type = "static_library"
entry_point = "lib.cryo"
source_dir = "."

[compiler]
stdlib_mode = true
optimize = true

[dependencies]
```

### User Application

```ini
[project]
project_name = "myapp"
output_dir = "build"
target_type = "executable"

[compiler]
debug = true

[dependencies]
```

---

## Implementation Priority

1. **High**: Add `entry_point` and `source_dir` to CryoConfig
2. **High**: Add `stdlib_mode` and `no_std` flags to CryoConfig
3. **High**: Implement `target_type = "static_library"` in BuildCommand
4. **Medium**: Automatic source file discovery in source_dir
5. **Medium**: Dependency graph building and topological sort
6. **Low**: `target_type = "shared_library"` support

---

## Key Design Principles

1. **Compiler manages files**: No explicit file lists needed - the compiler discovers and orders files automatically
2. **Simple configuration**: Minimal required fields, sensible defaults
3. **Backward compatible**: Existing configs continue to work
4. **Convention over configuration**: `src/main.cryo` default, standard directory structure

