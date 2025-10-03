# CryoLang Runtime Implementation Plan

## 🎯 **Objective**
Implement a manual memory management runtime system for CryoLang that provides automatic initialization, heap management, and clean shutdown without requiring a garbage collector.

## 🏗️ **Architecture Overview**

### **Runtime-First Model**
```
OS Entry → Runtime main() → User _user_main_() → Runtime cleanup → Exit
```

**Key Components:**
- **Runtime**: Provides true `main()`, heap management, panic handling
- **Standard Library**: Functions that call into runtime (malloc/free bridge)
- **User Code**: Renamed from `main()` to `_user_main_()` by compiler

---

## 📋 **Implementation Phases**

### **Phase 1: Basic Runtime Integration** ⭐ *START HERE*

#### **1.1 Modify Compiler Linking (CompilerInstance.cpp)**
**Goal**: Automatically link runtime.o with user code

**Location**: `src/Compiler/CompilerInstance.cpp:460+`

**Changes Needed:**
```cpp
if (_stdlib_linking_enabled) {
    // Add runtime first (contains true main)
    std::string runtime_path = "./bin/stdlib/runtime.o";
    if (std::filesystem::exists(runtime_path)) {
        _linker->add_object_file(runtime_path);
        if (_debug_mode) {
            std::cout << "[DEBUG] Added runtime.o: " << runtime_path << std::endl;
        }
    }
    
    // Then add stdlib
    std::string libcryo_path = "./bin/stdlib/libcryo.a";
    if (std::filesystem::exists(libcryo_path)) {
        _linker->add_object_file(libcryo_path);
    }
}
```

**Expected Linking Order:**
```
runtime.o → user_code.o → libcryo.a → -lm -lpthread
```

#### **1.2 Update Runtime Entry Point**
**Goal**: Runtime provides system main, calls user code

**File**: `stdlib/runtime/runtime.cryo`

**Current Status**: ✅ Already implemented as `_cryo_main_()`

**Note**: Need to change from `_cryo_main_` to `main` for OS compatibility

#### **1.3 Update Makefile Runtime Build**
**Goal**: Generate runtime.o instead of runtime.a for easier main symbol resolution

**Location**: `makefile:290+`

**Change**:
```makefile
# Generate runtime.o directly (not archive)
$(BIN_DIR)stdlib/runtime.o: $(RUNTIME_BC_FILES)
    @echo "Creating runtime object: $(BIN_DIR)stdlib/runtime.o"
    @llvm-link $(RUNTIME_BC_FILES) -o $(RUNTIME_BUILD_DIR)/runtime_combined.bc
    @llc -filetype=obj $(RUNTIME_BUILD_DIR)/runtime_combined.bc -o $(BIN_DIR)stdlib/runtime.o
    @echo "Runtime object created successfully"
```

---

### **Phase 2: Function Name Transformation**

#### **2.1 Implement Compiler Function Renaming**
**Goal**: Automatically rename user's `main()` → `_user_main_()`

**Approach Options:**
1. **AST Level** (Recommended): Rename during parsing
2. **Codegen Level**: Rename during LLVM IR generation
3. **User Explicit**: User writes `_user_main_` directly

**Implementation Location**: `src/Parser/Parser.cpp` or `src/Codegen/CodegenVisitor.cpp`

**Pseudo-code**:
```cpp
// In Parser or Codegen
if (function_name == "main" && !is_stdlib_mode && !no_runtime_flag) {
    function_name = "_user_main_";
    // Add extern declaration for _user_main_ if not present
}
```

#### **2.2 Add --no-runtime Flag Support**
**Goal**: Allow users to define true main() function

**Location**: `src/CLI/CLI.cpp`

**Changes**:
```cpp
// Add flag recognition
else if (flag_name == "no-runtime") {
    args.set_flag(flag_name, true);
}

// In CompilerInstance
bool _runtime_linking_enabled = true;
void disable_runtime_linking() { _runtime_linking_enabled = false; }
```

**Behavior**:
- `--no-runtime`: User provides `main()`, no function renaming, no runtime.o linking
- Default: Runtime provides `main()`, user provides `_user_main_()`

---

### **Phase 3: Memory Management Bridge**

#### **3.1 Create Memory API in Standard Library**
**Goal**: Bridge between user code and runtime heap functions

**File**: `stdlib/core/memory.cryo` (new or update existing)

```cryo
namespace std::Memory;

import <runtime>; // Import runtime functions

// Public API that calls runtime functions
function malloc(size: u64) -> void* {
    return std::Runtime::cryo_alloc(size);
}

function free(ptr: void*) -> void {
    std::Runtime::cryo_free(ptr);
    return;
}

function realloc(ptr: void*, new_size: u64) -> void* {
    // Implement using cryo_alloc + memcpy + cryo_free
    return null; // TODO
}

function calloc(count: u64, size: u64) -> void* {
    const total_size = count * size;
    const ptr = malloc(total_size);
    if (ptr != null) {
        // Zero the memory
        std::Intrinsics::__memset__(ptr, 0, total_size);
    }
    return ptr;
}
```

#### **3.2 Update Standard Library Dependencies**
**Goal**: Ensure stdlib can call runtime functions

**Files to Update**:
- `stdlib/io/stdio.cryo` - for any dynamic allocations
- `stdlib/strings/strings.cryo` - for string operations
- `stdlib/core/types.cryo` - for Array<T> implementation

---

### **Phase 4: Testing & Validation**

#### **4.1 Create Runtime Test Cases**
**File**: `test/runtime_tests.cryo`

```cryo
// Test basic allocation/deallocation
function _user_main_(argc: int, argv: string[]) -> int {
    // Test 1: Basic allocation
    const ptr1 = std::Memory::malloc(1024);
    if (ptr1 == null) return 1;
    
    // Test 2: Free
    std::Memory::free(ptr1);
    
    // Test 3: Multiple allocations
    const ptr2 = std::Memory::malloc(512);
    const ptr3 = std::Memory::malloc(256);
    
    std::Memory::free(ptr2);
    std::Memory::free(ptr3);
    
    // Test 4: Zero allocation
    const ptr4 = std::Memory::malloc(0);
    // Should return null
    
    return 0; // Success
}
```

#### **4.2 Memory Leak Detection**
**Goal**: Verify runtime properly tracks allocations

**Expected Output**:
```
Runtime Statistics:
  Total allocations: 3
  Total deallocations: 3
  Bytes allocated: 0
  Peak allocation: 1792 bytes
```

#### **4.3 Panic Testing**
**Test Cases**:
- Double free detection
- Invalid pointer handling
- Heap exhaustion behavior

---

## 🔧 **Implementation Checklist**

### **Phase 1: Basic Integration**
- [ ] Modify `CompilerInstance.cpp` to link runtime.o
- [ ] Update makefile to generate runtime.o
- [ ] Change runtime entry point from `_cryo_main_` to `main`
- [ ] Test basic compilation: `./bin/cryo.exe test.cryo -o test.exe`
- [ ] Verify linking order in debug output

### **Phase 2: Function Renaming**
- [ ] Implement `main()` → `_user_main_()` transformation
- [ ] Add `--no-runtime` flag support
- [ ] Test both runtime and no-runtime modes
- [ ] Update existing test cases

### **Phase 3: Memory Bridge**
- [ ] Create `stdlib/core/memory.cryo`
- [ ] Update stdlib modules to use new memory API
- [ ] Test memory allocation from user code
- [ ] Verify runtime statistics

### **Phase 4: Testing**
- [ ] Create comprehensive runtime test suite
- [ ] Test edge cases (double free, null pointers, etc.)
- [ ] Performance benchmarking
- [ ] Memory leak validation

---

## 🚨 **Potential Issues & Solutions**

### **Symbol Conflicts**
**Problem**: Multiple `main` symbols
**Solution**: Ensure runtime.o is linked first, or use --no-runtime flag

### **Startup Dependencies**
**Problem**: User code runs before runtime initialization
**Solution**: Runtime `main()` always initializes before calling `_user_main_()`

### **Cross-Platform Compatibility**
**Problem**: Different main() signatures on Windows vs Unix
**Solution**: Use C-compatible `int main(int argc, char* argv[])` in runtime

### **Debug Information**
**Problem**: Debugger can't find user's main function
**Solution**: Generate debug symbols that map `_user_main_` back to original source

---

## 📈 **Success Metrics**

1. **✅ Basic Functionality**: User can allocate/free memory
2. **✅ Automatic Runtime**: No manual initialization required
3. **✅ Clean Shutdown**: Memory leaks detected and reported
4. **✅ Error Handling**: Graceful panic on memory errors
5. **✅ Performance**: Minimal overhead compared to system malloc
6. **✅ Compatibility**: Works with existing CryoLang code

---

## 🔄 **Future Enhancements** (Post-MVP)

### **Advanced Memory Management**
- Custom allocators for different use cases
- Memory pools for specific object types
- Stack-like allocation for temporary objects

### **Runtime Configuration**
- Heap size configuration via environment variables
- Debug vs release runtime builds
- Memory usage profiling

### **Multi-threading Support**
- Thread-safe heap allocation
- Per-thread memory pools
- Atomic operations for counters

### **Integration Features**
- Foreign Function Interface (FFI)
- C library integration
- Async/await runtime support

---

## 🎯 **Next Steps**

1. **Start with Phase 1.1**: Modify `CompilerInstance.cpp` 
2. **Test immediately**: Compile simple program and check linking
3. **Iterate quickly**: Small changes, frequent testing
4. **Document issues**: Track any problems in this plan

---

*This plan will evolve as implementation progresses. Update this document with new findings and solutions.*