import type { 
  CompilerOptions, 
  CompileOptions, 
  CompileResult, 
  ValidationResult, 
  ASTResult,
  Position,
  CompletionItem,
  HoverInfo,
  Diagnostic,
  FormatOptions
} from './types';

// Emscripten module interface
interface EmscriptenModule {
  _cryo_compile_source: (sourcePtr: number, sourceLen: number, optionsPtr: number) => number;
  _cryo_validate_syntax: (sourcePtr: number, sourceLen: number) => number;
  _cryo_get_ast: (sourcePtr: number, sourceLen: number) => number;
  _cryo_format_code: (sourcePtr: number, sourceLen: number, optionsPtr: number) => number;
  _cryo_get_completions: (sourcePtr: number, sourceLen: number, line: number, col: number) => number;
  _cryo_get_hover: (sourcePtr: number, sourceLen: number, line: number, col: number) => number;
  _cryo_get_diagnostics: (sourcePtr: number, sourceLen: number) => number;
  _cryo_free_result: (resultPtr: number) => void;
  _malloc: (size: number) => number;
  _free: (ptr: number) => void;
  HEAP8: Int8Array;
  HEAPU8: Uint8Array;
  UTF8ToString: (ptr: number) => string;
  stringToUTF8: (str: string, ptr: number, maxBytes: number) => void;
  lengthBytesUTF8: (str: string) => number;
  ready: Promise<EmscriptenModule>;
}

declare const Module: () => Promise<EmscriptenModule>;

export class CryoCompiler {
  private module: EmscriptenModule | null = null;
  private isReady = false;

  private constructor() {}

  /**
   * Create a new CryoCompiler instance
   */
  static async create(options: CompilerOptions = {}): Promise<CryoCompiler> {
    const compiler = new CryoCompiler();
    await compiler.initialize(options);
    return compiler;
  }

  /**
   * Initialize the compiler with WASM module
   */
  private async initialize(options: CompilerOptions): Promise<void> {
    try {
      // Load the Emscripten module
      const moduleFactory = await import('../wasm/cryo-compiler.js');
      this.module = await moduleFactory.default();
      
      // Set default options
      this.setCompilerOptions(options);
      
      this.isReady = true;
    } catch (error) {
      throw new Error(`Failed to initialize CryoLang compiler: ${error}`);
    }
  }

  /**
   * Check if compiler is ready
   */
  get ready(): boolean {
    return this.isReady && this.module !== null;
  }

  /**
   * Set compiler configuration options
   */
  private setCompilerOptions(options: CompilerOptions): void {
    if (!this.module) throw new Error('Compiler not initialized');
    
    // Configure compiler with options
    // This would call into WASM to set global compiler state
    const optionsJson = JSON.stringify({
      target: options.target || 'wasm32',
      optimizationLevel: options.optimizationLevel || 2,
      debugInfo: options.debugInfo || false,
      memorySize: options.memorySize || 16 * 1024 * 1024, // 16MB default
      includeRuntime: options.includeRuntime || true
    });
    
    this.callWasmFunction('_cryo_set_options', [optionsJson]);
  }

  /**
   * Compile CryoLang source code
   */
  async compile(source: string, options: CompileOptions = {}): Promise<CompileResult> {
    if (!this.ready) throw new Error('Compiler not ready');
    
    try {
      const optionsJson = JSON.stringify({
        outputFormats: options.outputFormats || ['wasm', 'javascript'],
        sourceMaps: options.sourceMaps || true,
        minify: options.minify || false,
        exports: options.exports || [],
        imports: options.imports || {}
      });

      const result = this.callWasmFunction('_cryo_compile_source', [source, optionsJson]);
      return this.parseCompileResult(result);
    } catch (error) {
      return {
        success: false,
        errors: [{
          message: `Compilation failed: ${error}`,
          line: 1,
          column: 1,
          length: 0,
          severity: 'error' as const
        }]
      };
    }
  }

  /**
   * Validate syntax without full compilation
   */
  async validateSyntax(source: string): Promise<ValidationResult> {
    if (!this.ready) throw new Error('Compiler not ready');
    
    try {
      const result = this.callWasmFunction('_cryo_validate_syntax', [source]);
      return this.parseValidationResult(result);
    } catch (error) {
      return {
        isValid: false,
        errors: [{
          message: `Validation failed: ${error}`,
          name: 'SyntaxError',
          line: 1,
          column: 1,
          length: 0,
          severity: 'error' as const
        }],
        warnings: []
      };
    }
  }

  /**
   * Get AST representation of source code
   */
  async getAST(source: string): Promise<ASTResult> {
    if (!this.ready) throw new Error('Compiler not ready');
    
    try {
      const result = this.callWasmFunction('_cryo_get_ast', [source]);
      return this.parseASTResult(result);
    } catch (error) {
      return {
        success: false,
        errors: [{
          message: `AST generation failed: ${error}`,
          line: 1,
          column: 1,
          length: 0,
          severity: 'error' as const
        }]
      };
    }
  }

  /**
   * Format source code
   */
  async formatCode(source: string, options: FormatOptions = {}): Promise<string> {
    if (!this.ready) throw new Error('Compiler not ready');
    
    try {
      const optionsJson = JSON.stringify({
        tabSize: options.tabSize || 2,
        insertSpaces: options.insertSpaces !== false,
        insertFinalNewline: options.insertFinalNewline !== false,
        trimTrailingWhitespace: options.trimTrailingWhitespace !== false
      });

      const result = this.callWasmFunction('_cryo_format_code', [source, optionsJson]);
      const parsed = this.parseStringResult(result);
      return parsed || source; // Return original if formatting fails
    } catch (error) {
      console.warn('Code formatting failed:', error);
      return source;
    }
  }

  /**
   * Get code completion suggestions
   */
  async getCompletionSuggestions(source: string, position: Position): Promise<CompletionItem[]> {
    if (!this.ready) throw new Error('Compiler not ready');
    
    try {
      const result = this.callWasmFunction('_cryo_get_completions', [
        source, 
        position.line, 
        position.column
      ]);
      return this.parseCompletionResult(result);
    } catch (error) {
      console.warn('Completion suggestions failed:', error);
      return [];
    }
  }

  /**
   * Get hover information for position
   */
  async getHoverInfo(source: string, position: Position): Promise<HoverInfo | null> {
    if (!this.ready) throw new Error('Compiler not ready');
    
    try {
      const result = this.callWasmFunction('_cryo_get_hover', [
        source, 
        position.line, 
        position.column
      ]);
      return this.parseHoverResult(result);
    } catch (error) {
      console.warn('Hover info failed:', error);
      return null;
    }
  }

  /**
   * Get diagnostics (errors and warnings)
   */
  async getDiagnostics(source: string): Promise<Diagnostic[]> {
    if (!this.ready) throw new Error('Compiler not ready');
    
    try {
      const result = this.callWasmFunction('_cryo_get_diagnostics', [source]);
      return this.parseDiagnosticsResult(result);
    } catch (error) {
      console.warn('Diagnostics failed:', error);
      return [];
    }
  }

  /**
   * Call a WASM function with proper memory management
   */
  private callWasmFunction(funcName: string, args: any[]): any {
    if (!this.module) throw new Error('WASM module not loaded');
    
    const func = (this.module as any)[funcName];
    if (!func) throw new Error(`WASM function ${funcName} not found`);

    // Convert arguments to appropriate types
    const wasmArgs: number[] = [];
    const allocatedPtrs: number[] = [];

    try {
      for (const arg of args) {
        if (typeof arg === 'string') {
          // Allocate memory for string and copy data
          const len = this.module.lengthBytesUTF8(arg) + 1;
          const ptr = this.module._malloc(len);
          allocatedPtrs.push(ptr);
          this.module.stringToUTF8(arg, ptr, len);
          wasmArgs.push(ptr);
          wasmArgs.push(len - 1); // Length without null terminator
        } else if (typeof arg === 'number') {
          wasmArgs.push(arg);
        } else {
          // For objects, stringify and treat as string
          const str = JSON.stringify(arg);
          const len = this.module.lengthBytesUTF8(str) + 1;
          const ptr = this.module._malloc(len);
          allocatedPtrs.push(ptr);
          this.module.stringToUTF8(str, ptr, len);
          wasmArgs.push(ptr);
        }
      }

      // Call the function
      const resultPtr = func(...wasmArgs);
      
      // Read result if it's a pointer
      if (resultPtr !== 0) {
        const resultStr = this.module.UTF8ToString(resultPtr);
        this.module._cryo_free_result(resultPtr);
        return resultStr;
      }
      
      return null;
    } finally {
      // Clean up allocated memory
      allocatedPtrs.forEach(ptr => this.module!._free(ptr));
    }
  }

  /**
   * Parse compilation result from JSON string
   */
  private parseCompileResult(jsonResult: string | null): CompileResult {
    if (!jsonResult) {
      return { success: false, errors: [{ message: 'No result from compiler', line: 1, column: 1, length: 0, severity: 'error' }] };
    }

    try {
      const parsed = JSON.parse(jsonResult);
      
      // Convert base64 WASM to Uint8Array if present
      if (parsed.wasm && typeof parsed.wasm === 'string') {
        parsed.wasm = Uint8Array.from(atob(parsed.wasm), c => c.charCodeAt(0));
      }
      
      return parsed;
    } catch (error) {
      return {
        success: false,
        errors: [{ message: `Failed to parse compile result: ${error}`, line: 1, column: 1, length: 0, severity: 'error' }]
      };
    }
  }

  /**
   * Parse validation result from JSON string
   */
  private parseValidationResult(jsonResult: string | null): ValidationResult {
    if (!jsonResult) {
      return { isValid: false, errors: [], warnings: [] };
    }

    try {
      return JSON.parse(jsonResult);
    } catch (error) {
      return {
        isValid: false,
        errors: [{ message: `Failed to parse validation result: ${error}`, name: 'ParseError', line: 1, column: 1, length: 0, severity: 'error' }],
        warnings: []
      };
    }
  }

  /**
   * Parse AST result from JSON string
   */
  private parseASTResult(jsonResult: string | null): ASTResult {
    if (!jsonResult) {
      return { success: false, errors: [] };
    }

    try {
      return JSON.parse(jsonResult);
    } catch (error) {
      return {
        success: false,
        errors: [{ message: `Failed to parse AST result: ${error}`, line: 1, column: 1, length: 0, severity: 'error' }]
      };
    }
  }

  /**
   * Parse string result
   */
  private parseStringResult(jsonResult: string | null): string | null {
    if (!jsonResult) return null;
    
    try {
      const parsed = JSON.parse(jsonResult);
      return parsed.result || parsed;
    } catch (error) {
      return jsonResult; // Return as-is if not JSON
    }
  }

  /**
   * Parse completion result
   */
  private parseCompletionResult(jsonResult: string | null): CompletionItem[] {
    if (!jsonResult) return [];
    
    try {
      const parsed = JSON.parse(jsonResult);
      return Array.isArray(parsed) ? parsed : parsed.completions || [];
    } catch (error) {
      return [];
    }
  }

  /**
   * Parse hover result
   */
  private parseHoverResult(jsonResult: string | null): HoverInfo | null {
    if (!jsonResult) return null;
    
    try {
      const parsed = JSON.parse(jsonResult);
      return parsed.hover || parsed;
    } catch (error) {
      return null;
    }
  }

  /**
   * Parse diagnostics result
   */
  private parseDiagnosticsResult(jsonResult: string | null): Diagnostic[] {
    if (!jsonResult) return [];
    
    try {
      const parsed = JSON.parse(jsonResult);
      return Array.isArray(parsed) ? parsed : parsed.diagnostics || [];
    } catch (error) {
      return [];
    }
  }

  /**
   * Clean up resources
   */
  dispose(): void {
    this.isReady = false;
    this.module = null;
  }
}