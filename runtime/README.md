# CryoLang Runtime Library

The CryoLang Runtime Library provides the standard library implementation for CryoLang programs. It's implemented as a C library with external linkage for easy integration with the compiler's LLVM backend.

## 📁 Structure

```
runtime/
├── include/
│   └── cryo_runtime.h      # Main runtime header with all function declarations
├── src/                    # Modular implementation
│   ├── io.c               # I/O functions (print, file operations)
│   ├── string.c           # String manipulation
│   ├── math.c             # Mathematical functions
│   ├── collections.c      # Array and collection utilities
│   ├── system.c           # System functions (time, random, type conversion)
│   └── memory.c           # Memory management utilities
├── cryo_runtime.c         # Unified compilation unit
├── makefile              # Runtime build system
└── README.md             # This file
```

## 🛠️ Building

Build the runtime libraries:
```bash
cd runtime
make all          # Build both static and shared libraries
make install      # Install to ../bin/ directory
```

Individual targets:
```bash
make static       # Build only static library (.a)
make shared       # Build only shared library (.so/.dll)
make clean        # Remove build artifacts
```

## 📚 Standard Library Functions

### I/O Functions
- `cryo_print(str)` - Print string to stdout
- `cryo_println(str)` - Print string with newline
- `cryo_read_file(filename)` - Read entire file as string
- `cryo_write_file(filename, content)` - Write string to file
- `cryo_file_exists(filename)` - Check if file exists

### String Functions
- `cryo_string_length(str)` - Get string length
- `cryo_string_to_upper(str)` - Convert to uppercase
- `cryo_string_to_lower(str)` - Convert to lowercase
- `cryo_string_substring(str, start, length)` - Extract substring
- `cryo_string_find(str, substr)` - Find substring index
- `cryo_string_contains(str, substr)` - Check if string contains substring
- `cryo_string_split(str, delimiter)` - Split string into array
- `cryo_string_join(array, delimiter)` - Join string array

### Math Functions
- `cryo_abs(x)`, `cryo_pow(base, exp)`, `cryo_sqrt(x)` - Basic math
- `cryo_sin(x)`, `cryo_cos(x)`, `cryo_tan(x)` - Trigonometry
- `cryo_log(x)`, `cryo_log10(x)` - Logarithms
- `cryo_round(x)`, `cryo_floor(x)`, `cryo_ceil(x)` - Rounding
- `cryo_min_int/float(a, b)`, `cryo_max_int/float(a, b)` - Min/max

### Array/Collection Functions
- `cryo_array_length(arr)` - Get array length
- `cryo_array_push(arr, element)` - Add element to end
- `cryo_array_pop(arr)` - Remove and return last element
- `cryo_array_get/set(arr, index, element)` - Access elements
- `cryo_array_sum/min/max_int(arr)` - Array operations

### System Functions
- `cryo_current_time_millis()` - Current timestamp
- `cryo_current_date()`, `cryo_current_time()` - Date/time strings
- `cryo_sleep_millis(ms)` - Sleep for milliseconds
- `cryo_random_int/float/bool()` - Random number generation
- `cryo_set_random_seed(seed)` - Set random seed

### Type Conversion
- `cryo_int/float/bool/char_to_string()` - Convert to string
- `cryo_string_to_int/float/bool()` - Parse from string
- `cryo_int_to_float()`, `cryo_float_to_int()` - Type casting

## 🔗 Integration with Compiler

The runtime library is designed to be linked with compiled CryoLang programs:

### During Compilation (Future LLVM Backend)
```cpp
// In compiler, emit calls to runtime functions
// CryoLang: print("Hello, World!")
// LLVM IR: call void @cryo_println(i8* getelementptr(...))
```

### Linking Phase
```bash
# Link against the runtime library
clang program.o -L./bin -lcryoruntime -o program
```

### Symbol Table Integration
The compiler's symbol table should be pre-populated with runtime function signatures:

```cpp
void CompilerInstance::initialize_standard_library() {
    // Add built-in functions during compiler initialization
    _symbol_table->add_builtin_function("print", {"string"}, "void", "cryo_print");
    _symbol_table->add_builtin_function("println", {"string"}, "void", "cryo_println");
    // ... etc
}
```

## 🔄 Development Workflow

1. **Design Phase** (✅ Complete)
   - Define function signatures in `cryo_runtime.h`
   - Plan module organization

2. **Implementation Phase** (✅ Complete)
   - Implement functions in modular `.c` files
   - Create unified compilation unit

3. **Integration Phase** (🔄 Next Steps)
   - Add built-in functions to compiler's symbol table
   - Update parser to recognize standard library calls
   - Implement type checking for built-ins

4. **Testing Phase** (⏳ Future)
   - Create test cases for runtime functions
   - Validate integration with compiler

5. **Code Generation Phase** (⏳ Future with LLVM)
   - Emit LLVM calls to runtime functions
   - Handle linking and library resolution

## 🎯 Current Status

- ✅ **Runtime Design**: Complete function API
- ✅ **Runtime Implementation**: All core functions implemented
- ✅ **Build System**: Makefile with static/shared library generation
- ⏳ **Compiler Integration**: Next phase - symbol table integration
- ⏳ **Testing**: Awaiting compiler integration
- ⏳ **Code Generation**: Future LLVM backend implementation

The runtime library is ready for integration with the CryoLang compiler's symbol table and type checking system!