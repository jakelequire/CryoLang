# CryoLang WebAssembly Compiler Package

A production-ready WebAssembly build of the CryoLang compiler for web applications, code playgrounds, and interactive development environments.

## 🚀 Quick Start

```bash
npm install @cryolang/wasm-compiler
```

```typescript
import { CryoCompiler, createPlayground } from '@cryolang/wasm-compiler';

// Initialize compiler
const compiler = await CryoCompiler.create();

// Compile CryoLang code
const result = await compiler.compile(`
  fn fibonacci(n: int) -> int {
    if n <= 1 { return n; }
    return fibonacci(n-1) + fibonacci(n-2);
  }
  
  fn main() -> int {
    println("Fibonacci(10) = {}", fibonacci(10));
    return 0;
  }
`);

if (result.success) {
  console.log('Generated WASM:', result.wasm);
  console.log('Generated JS:', result.javascript);
} else {
  console.error('Compilation errors:', result.errors);
}
```

## 🎯 Features

### Core Functionality
- **Full CryoLang Compilation**: Complete compiler running in WebAssembly
- **Real-time Syntax Validation**: Instant error checking and diagnostics
- **Code Intelligence**: Auto-completion, hover information, and suggestions
- **Multiple Output Formats**: WASM, JavaScript, LLVM IR, and AST
- **Source Maps**: Full debugging support with source mapping
- **Monaco Editor Integration**: Ready-to-use code playground components

### Performance
- **Optimized WASM Build**: Fast compilation with minimal memory footprint
- **Incremental Compilation**: Smart caching for improved performance
- **Web Worker Support**: Non-blocking compilation in background threads
- **Streaming Compilation**: Process large files without blocking UI

### Developer Experience
- **TypeScript Support**: Full type definitions included
- **Modern ES Modules**: Works with Vite, Webpack, Rollup, and other bundlers
- **Framework Agnostic**: Use with React, Vue, Angular, or vanilla JavaScript
- **CDN Ready**: Available via unpkg and jsDelivr for quick prototyping

## 📖 API Reference

### CryoCompiler Class

#### Static Methods

```typescript
// Create a new compiler instance
static async create(options?: CompilerOptions): Promise<CryoCompiler>
```

#### Instance Methods

```typescript
// Compile source code to multiple formats
async compile(source: string, options?: CompileOptions): Promise<CompileResult>

// Validate syntax without full compilation
async validateSyntax(source: string): Promise<ValidationResult>

// Get AST representation
async getAST(source: string): Promise<ASTResult>

// Format source code
async formatCode(source: string, options?: FormatOptions): Promise<string>

// Get code completion suggestions
async getCompletionSuggestions(source: string, position: Position): Promise<CompletionItem[]>

// Get hover information
async getHoverInfo(source: string, position: Position): Promise<HoverInfo>

// Get diagnostics (errors/warnings)
async getDiagnostics(source: string): Promise<Diagnostic[]>
```

### Configuration Options

```typescript
interface CompilerOptions {
  // Target configuration
  target?: 'wasm32' | 'wasm64';
  
  // Optimization level (0-3)
  optimizationLevel?: number;
  
  // Enable debug information
  debugInfo?: boolean;
  
  // Memory configuration
  memorySize?: number;
  
  // Include runtime in output
  includeRuntime?: boolean;
  
  // Custom module resolver
  moduleResolver?: ModuleResolver;
}

interface CompileOptions {
  // Output format selection
  outputFormats?: ('wasm' | 'javascript' | 'llvm-ir' | 'ast')[];
  
  // Enable source maps
  sourceMaps?: boolean;
  
  // Minify output
  minify?: boolean;
  
  // Custom imports/exports
  exports?: string[];
  imports?: Record<string, any>;
}
```

### Result Types

```typescript
interface CompileResult {
  success: boolean;
  wasm?: Uint8Array;
  javascript?: string;
  llvmIR?: string;
  ast?: ASTNode;
  sourceMaps?: SourceMap;
  errors?: CompileError[];
  warnings?: CompileWarning[];
  metrics?: CompileMetrics;
}

interface ValidationResult {
  isValid: boolean;
  errors: SyntaxError[];
  warnings: Warning[];
}
```

## 🎮 Playground Integration

### Quick Playground Setup

```typescript
import { createPlayground } from '@cryolang/wasm-compiler';

// Create a complete code playground
const playground = await createPlayground('#editor-container', {
  theme: 'vs-dark',
  language: 'cryo',
  features: {
    autoComplete: true,
    syntaxHighlighting: true,
    errorChecking: true,
    codeFormatting: true
  }
});

// Access compiler instance
const result = await playground.compiler.compile(playground.getValue());
```

### Monaco Editor Integration

```typescript
import * as monaco from 'monaco-editor';
import { CryoLanguageService } from '@cryolang/wasm-compiler';

// Register CryoLang language
const languageService = new CryoLanguageService();
await languageService.registerWithMonaco(monaco);

// Create editor with CryoLang support
const editor = monaco.editor.create(document.getElementById('container'), {
  value: `fn main() -> int { return 42; }`,
  language: 'cryo',
  theme: 'vs-dark'
});
```

## 🔧 Advanced Usage

### Web Worker Integration

```typescript
// main.js
import { CryoWorkerCompiler } from '@cryolang/wasm-compiler/worker';

const compiler = new CryoWorkerCompiler();
const result = await compiler.compile(sourceCode);

// worker.js (automatically handled)
import { setupCryoWorker } from '@cryolang/wasm-compiler/worker';
setupCryoWorker();
```

### Custom Module Resolution

```typescript
const compiler = await CryoCompiler.create({
  moduleResolver: {
    resolve: async (moduleName: string, fromFile: string) => {
      // Custom logic to resolve imports
      if (moduleName.startsWith('std/')) {
        return await fetchStandardLibrary(moduleName);
      }
      return await fetchUserModule(moduleName, fromFile);
    }
  }
});
```

### Runtime Integration

```typescript
// Compile and execute CryoLang code
const result = await compiler.compile(source, {
  outputFormats: ['wasm', 'javascript'],
  includeRuntime: true
});

if (result.success) {
  // Load and execute the compiled WASM
  const wasmModule = await WebAssembly.instantiate(result.wasm);
  const exitCode = wasmModule.instance.exports.main();
  console.log('Program exited with code:', exitCode);
}
```

## 🛠️ Development Setup

### Building from Source

```bash
# Clone repository
git clone https://github.com/jakelequire/CryoLang.git
cd CryoLang/web-package

# Install dependencies
npm install

# Build WASM compiler
npm run build:wasm

# Build JavaScript package
npm run build

# Run tests
npm test

# Start development server
npm run dev
```

### Project Structure

```
web-package/
├── src/                    # TypeScript source
│   ├── compiler.ts         # Main compiler interface
│   ├── playground.ts       # Playground utilities
│   ├── worker/             # Web worker support
│   └── monaco/             # Monaco editor integration
├── wasm/                   # Generated WASM files
│   ├── cryo-compiler.wasm  # Main compiler WASM
│   └── cryo-compiler.js    # Emscripten loader
├── dist/                   # Built JavaScript package
├── examples/               # Usage examples
└── docs/                   # Additional documentation
```

## 🔍 Examples

### Basic Compilation

```typescript
import { CryoCompiler } from '@cryolang/wasm-compiler';

const compiler = await CryoCompiler.create();

const cryoSource = `
  struct Point {
    x: float,
    y: float
  }
  
  fn distance(p1: Point, p2: Point) -> float {
    let dx = p1.x - p2.x;
    let dy = p1.y - p2.y;
    return sqrt(dx * dx + dy * dy);
  }
`;

const result = await compiler.compile(cryoSource);
console.log('Compilation result:', result);
```

### Real-time Error Checking

```typescript
const editor = monaco.editor.create(container, { language: 'cryo' });

editor.onDidChangeModelContent(async () => {
  const source = editor.getValue();
  const validation = await compiler.validateSyntax(source);
  
  // Update Monaco markers with errors
  monaco.editor.setModelMarkers(
    editor.getModel(),
    'cryo',
    validation.errors.map(error => ({
      startLineNumber: error.line,
      startColumn: error.column,
      endLineNumber: error.endLine || error.line,
      endColumn: error.endColumn || error.column + error.length,
      message: error.message,
      severity: monaco.MarkerSeverity.Error
    }))
  );
});
```

### Code Intelligence

```typescript
// Auto-completion provider
monaco.languages.registerCompletionItemProvider('cryo', {
  provideCompletionItems: async (model, position) => {
    const source = model.getValue();
    const suggestions = await compiler.getCompletionSuggestions(source, {
      line: position.lineNumber,
      column: position.column
    });
    
    return {
      suggestions: suggestions.map(item => ({
        label: item.label,
        kind: monaco.languages.CompletionItemKind[item.kind],
        insertText: item.insertText,
        documentation: item.documentation
      }))
    };
  }
});
```

## 📊 Performance Notes

- **Initial Load**: ~2MB WASM file, loads in <500ms on modern browsers
- **Compilation Speed**: ~1000 lines/second for typical CryoLang code
- **Memory Usage**: ~16MB baseline, scales with source complexity
- **Browser Support**: All modern browsers with WebAssembly support

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests for new functionality
5. Run `npm test` to ensure all tests pass
6. Submit a pull request

## 📄 License

Apache License 2.0 - see LICENSE file for details.

## 🆘 Support

- **Documentation**: [https://cryolang.dev/docs](https://cryolang.dev/docs)
- **Issues**: [GitHub Issues](https://github.com/jakelequire/CryoLang/issues)
- **Discord**: [CryoLang Community](https://discord.gg/cryolang)
- **Stack Overflow**: Tag questions with `cryolang`