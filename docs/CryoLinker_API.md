# CryoLinker API Documentation

## Overview

The CryoLinker is a comprehensive linking and executable generation system for the CryoLang compiler. It provides a clean interface for creating executables, libraries, and object files while managing runtime dependencies and external libraries.

## Core Features

### 1. Multi-Format Output Generation
- **Executables**: Native executable binaries with runtime linking
- **Shared Libraries**: Dynamic libraries (.so/.dll) for runtime loading  
- **Static Libraries**: Archive libraries (.a/.lib) for compile-time linking
- **Object Files**: Intermediate object files (.o/.obj) for custom linking

### 2. Runtime Integration
- Automatic detection and linking of CryoLang runtime library
- Configurable runtime library search paths
- Static runtime linking for portable executables

### 3. External Library Management
- Add custom object files to the linking process
- Include external static and dynamic libraries
- Configure library search paths
- Extensible for future package management

### 4. Cross-Platform Support
- Platform-specific linker driver selection (clang/gcc/msvc)
- Target-specific object file generation via LLVM
- Configurable target triples and architectures

## API Reference

### Constructor & Initialization

```cpp
CryoLinker(SymbolTable& symbol_table)
```
Creates a linker instance with the provided symbol table for symbol resolution.

### Primary Linking Interface

```cpp
bool link_modules(
    const std::vector<llvm::Module*>& modules,
    const std::string& output_path,
    LinkTarget target = LinkTarget::Executable
)
```
Main entry point for linking LLVM modules into the specified output format.

**Parameters:**
- `modules`: Vector of LLVM IR modules to link
- `output_path`: Path for the generated output file
- `target`: Output format (Executable, SharedLibrary, StaticLibrary, ObjectFile)

**Returns:** `true` on successful linking, `false` on failure

### External Dependencies

```cpp
void add_object_file(const std::string& object_path)
```
Add a custom object file to the linking process.

```cpp  
void add_library(const std::string& library_name, bool is_static = true)
```
Include an external library in the linking process.

```cpp
void add_library_path(const std::string& path)
```
Add a directory to the library search paths.

### Configuration

```cpp
void set_optimization_level(int level)  // 0-3
void add_linker_flag(const std::string& flag)
void set_entry_point(const std::string& entry_point)
```
Configure optimization level, custom linker flags, and executable entry point.

### Runtime Management

```cpp
bool initialize_runtime()
void register_runtime_function(const std::string& cryo_name, 
                              const std::string& native_name, 
                              llvm::Function* declaration)
```
Initialize runtime system and register runtime functions for symbol resolution.

### Error Handling

```cpp
bool has_errors() const
std::string get_last_error() const
void print_diagnostics(std::ostream& os) const
```
Comprehensive error reporting and diagnostics.

## Usage Examples

### Basic Executable Creation

```cpp
auto linker = std::make_unique<CryoLinker>(*symbol_table);
std::vector<llvm::Module*> modules = {ir_module};

if (linker->link_modules(modules, "my_program", CryoLinker::LinkTarget::Executable)) {
    std::cout << "Executable created successfully!" << std::endl;
} else {
    std::cout << "Linking failed: " << linker->get_last_error() << std::endl;
}
```

### Adding External Dependencies

```cpp
// Add a custom object file
linker->add_object_file("external/math_utils.o");

// Link with an external library
linker->add_library_path("/usr/local/lib");
linker->add_library("ssl", false); // dynamic linking
linker->add_library("crypto", false);

// Generate executable with dependencies
linker->link_modules(modules, "secure_app", CryoLinker::LinkTarget::Executable);
```

### Creating a Shared Library

```cpp
// Configure for shared library output
linker->add_linker_flag("-fPIC");
linker->set_optimization_level(2);

bool success = linker->link_modules(modules, "libmylib.so", 
                                  CryoLinker::LinkTarget::SharedLibrary);
```

## Runtime Library Integration

The CryoLinker automatically handles integration with the CryoLang runtime:

1. **Runtime Detection**: Searches for `libcryoruntime.a` in standard paths:
   - `./runtime/build/` (development builds)
   - `./bin/` (installed binaries)
   - `/usr/local/lib/` (system installation)
   - `/usr/lib/` (system libraries)

2. **Static Linking**: Links runtime statically for portable executables
3. **Symbol Resolution**: Resolves calls to runtime functions like `Std::Runtime::print_int`

## Extensibility

The CryoLinker is designed for future extensions:

### Planned Features
- **Package Management**: Integration with external package managers
- **Link-Time Optimization**: Whole-program optimization across modules  
- **Debug Information**: DWARF debug info generation
- **Profile-Guided Optimization**: PGO support for better performance
- **Cross-Compilation**: Target different architectures and platforms

### Extension Points
- **Custom Linker Drivers**: Support for additional system linkers
- **Output Formats**: Additional binary formats (WebAssembly, etc.)
- **Dependency Resolvers**: Custom dependency resolution strategies
- **Target Platforms**: Additional architecture and OS support

## Error Handling

The linker provides comprehensive error reporting:

- **Runtime Library Errors**: Missing or incompatible runtime libraries
- **Symbol Resolution**: Undefined symbols or linking conflicts
- **File System**: Missing files, permission errors, disk space
- **Linker Driver**: System linker failures and command-line errors

All errors are collected and can be accessed via `get_last_error()` or printed via `print_diagnostics()`.

## Performance Considerations

- **Incremental Linking**: Only rebuilds what's necessary
- **Parallel Processing**: Multi-threaded object file generation (planned)
- **Caching**: Object file and dependency caching (planned)
- **Optimization**: Configurable optimization levels for different use cases

## Platform Support

Current platform support:
- **Linux**: Full support with clang/gcc
- **Windows**: Planned (MSVC integration)  
- **macOS**: Planned (clang/ld integration)

The architecture is designed to be platform-agnostic with platform-specific implementations for linker drivers and object file formats.