# CryoLang WebAssembly Build Guide

This document explains how to build CryoLang for WebAssembly to use in web applications, code playgrounds, and browser-based IDEs.

## Prerequisites

- **Git** (for cloning dependencies)
- **Make** (for building)
- **Emscripten** (for WebAssembly compilation)

## Quick Start

### 1. Install Emscripten (Choose One Method)

#### Method A: System Installation (Recommended)
Follow the official Emscripten installation guide:
https://emscripten.org/docs/getting_started/downloads.html

#### Method B: Automatic Setup Script
```bash
# Linux/macOS
./scripts/setup-emscripten.sh

# Windows
scripts\setup-emscripten.bat
```

#### Method C: Manual Local Installation
```bash
# Clone emsdk
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk

# Install and activate latest Emscripten
./emsdk install latest
./emsdk activate latest

# Set up environment (run this in each session)
source ./emsdk_env.sh  # Linux/macOS
# OR
emsdk_env.bat         # Windows
```

### 2. Build CryoLang for WebAssembly

```bash
# Build WASM module
make -f wasm.makefile.config wasm

# Build with debug info (for development)
make -f wasm.makefile.config wasm-debug

# Build and generate test page
make -f wasm.makefile.config wasm-test
```

### 3. Generated Files

After building, you'll find:
- `bin/cryo-compiler.wasm` - The WebAssembly module
- `bin/cryo-compiler.js` - JavaScript loader/glue code
- `bin/cryo-compiler.html` - Test page (if using wasm-test)

## Using in Web Applications

### Next.js/React Integration

#### 1. Add CryoLang as Submodule
```bash
# In your Next.js project root
git submodule add https://github.com/jakelequire/CryoLang.git cryo-compiler
cd cryo-compiler

# Build WASM
make -f wasm.makefile.config wasm

# Copy files to public directory
cp bin/cryo-compiler.* ../public/wasm/
```

#### 2. Package.json Integration
```json
{
  "scripts": {
    "build:cryo": "cd cryo-compiler && make -f wasm.makefile.config wasm",
    "copy:cryo": "cp cryo-compiler/bin/cryo-compiler.* public/wasm/",
    "update:cryo": "npm run build:cryo && npm run copy:cryo",
    "dev": "npm run update:cryo && next dev",
    "build": "npm run update:cryo && next build"
  }
}
```

#### 3. TypeScript Integration
```typescript
// lib/cryo-compiler.ts
export class CryoCompiler {
  private module: any;

  static async create(): Promise<CryoCompiler> {
    const compiler = new CryoCompiler();
    await compiler.initialize();
    return compiler;
  }

  private async initialize() {
    const CryoWasm = await import('/wasm/cryo-compiler.js');
    this.module = await CryoWasm.default();
  }

  compile(source: string): any {
    return this.module.ccall(
      'cryo_compile_source', 
      'string', 
      ['string'], 
      [source]
    );
  }
}
```

#### 4. React Component Example
```tsx
// components/CryoEditor.tsx
import { useEffect, useState } from 'react';
import { CryoCompiler } from '../lib/cryo-compiler';

export default function CryoEditor() {
  const [compiler, setCompiler] = useState<CryoCompiler | null>(null);
  const [code, setCode] = useState('fn main() -> int { return 0; }');

  useEffect(() => {
    CryoCompiler.create().then(setCompiler);
  }, []);

  const handleCompile = async () => {
    if (!compiler) return;
    
    try {
      const result = compiler.compile(code);
      console.log('Compilation result:', result);
    } catch (error) {
      console.error('Compilation error:', error);
    }
  };

  return (
    <div>
      <textarea 
        value={code} 
        onChange={(e) => setCode(e.target.value)}
        rows={10}
        cols={80}
      />
      <button onClick={handleCompile} disabled={!compiler}>
        Compile
      </button>
    </div>
  );
}
```

## Build Configuration

The WASM build system automatically detects Emscripten in this order:
1. **System PATH** - If `emcc` is available globally
2. **EMSDK Environment** - If `$EMSDK` variable is set
3. **Local emsdk** - If `./emsdk/` directory exists

### Makefile Targets

- `wasm` - Build release version
- `wasm-debug` - Build with debug information
- `wasm-release` - Explicitly build release version
- `wasm-test` - Build and generate test HTML
- `wasm-clean` - Clean build artifacts
- `wasm-analyze` - Show build size information

### Build Flags

Key WebAssembly compilation flags:
- `-s WASM=1` - Generate WebAssembly
- `-s MODULARIZE=1` - Create ES6 module
- `-s ALLOW_MEMORY_GROWTH=1` - Dynamic memory allocation
- `-s EXPORTED_FUNCTIONS=[...]` - C functions visible to JavaScript
- `--js-library` - Custom JavaScript library integration

## Troubleshooting

### Emscripten Not Found
```
Error: Emscripten not found! Please install...
```
**Solution**: Run the setup script or install Emscripten manually.

### Missing Source Files
```
error: src/wasm/WASMRuntimeAdapter.cpp: No such file
```
**Solution**: Ensure you're building from the correct CryoLang directory.

### Memory Issues
```
RuntimeError: memory access out of bounds
```
**Solution**: Increase memory limits in `wasm.makefile.config`:
```makefile
-s INITIAL_MEMORY=134217728 \
-s MAXIMUM_MEMORY=268435456 \
```

### Browser CORS Issues
```
Cross-Origin Request Blocked
```
**Solution**: Serve files from a local server:
```bash
# In the directory containing WASM files
python3 -m http.server 8000
# Open http://localhost:8000/
```

## Advanced Usage

### Custom Build Configuration

Create a `wasm.config.local` file to override settings:
```makefile
# Custom memory settings
WASM_MEMORY_SIZE := 134217728

# Additional exports
WASM_CUSTOM_EXPORTS := "_custom_function"

# Debug mode
WASM_DEBUG := 1
```

### Optimization Settings

For production builds:
```bash
make -f wasm.makefile.config wasm-release OPTIMIZE=3
```

For development/debugging:
```bash
make -f wasm.makefile.config wasm-debug
```

## Performance Considerations

- **Initial Load**: ~2-5MB WASM file (varies by features included)
- **Compilation Speed**: ~1000-5000 lines/second (depends on complexity)
- **Memory Usage**: 16-64MB baseline (configurable)
- **Browser Compatibility**: All modern browsers with WebAssembly support

## Contributing

When adding WASM functionality:

1. **C++ Code**: Add new functions to `src/wasm/CryoWASMAPI.cpp`
2. **Exports**: Update `EXPORTED_FUNCTIONS` in `wasm.makefile.config`
3. **JavaScript**: Add corresponding functions to `src/wasm/cryo_js_library.js`
4. **TypeScript**: Update type definitions in `web-package/src/types.ts`

## Resources

- [Emscripten Documentation](https://emscripten.org/docs/)
- [WebAssembly MDN Guide](https://developer.mozilla.org/en-US/docs/WebAssembly)
- [CryoLang Language Reference](../docs/cryo.md)