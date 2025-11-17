export interface Position {
  line: number;
  column: number;
}

export interface Range {
  start: Position;
  end: Position;
}

export interface CompilerOptions {
  target?: 'wasm32' | 'wasm64';
  optimizationLevel?: number;
  debugInfo?: boolean;
  memorySize?: number;
  includeRuntime?: boolean;
  moduleResolver?: ModuleResolver;
}

export interface CompileOptions {
  outputFormats?: ('wasm' | 'javascript' | 'llvm-ir' | 'ast')[];
  sourceMaps?: boolean;
  minify?: boolean;
  exports?: string[];
  imports?: Record<string, any>;
}

export interface CompileResult {
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

export interface ValidationResult {
  isValid: boolean;
  errors: SyntaxError[];
  warnings: CompileWarning[];
}

export interface ASTResult {
  success: boolean;
  ast?: ASTNode;
  errors?: CompileError[];
}

export interface CompileError {
  message: string;
  line: number;
  column: number;
  endLine?: number;
  endColumn?: number;
  length: number;
  severity: 'error' | 'warning' | 'info';
  code?: string;
}

export interface CompileWarning extends CompileError {
  severity: 'warning';
}

export interface CompileMetrics {
  compilationTime: number;
  memoryUsage: number;
  linesOfCode: number;
  astNodeCount: number;
  outputSize: number;
}

export interface ASTNode {
  type: string;
  range: Range;
  children?: ASTNode[];
  properties?: Record<string, any>;
}

export interface SourceMap {
  version: number;
  file: string;
  sources: string[];
  names: string[];
  mappings: string;
}

export interface ModuleResolver {
  resolve(moduleName: string, fromFile: string): Promise<string>;
}

export interface CompletionItem {
  label: string;
  kind: CompletionItemKind;
  insertText: string;
  documentation?: string;
  detail?: string;
  sortText?: string;
  filterText?: string;
  range?: Range;
}

export enum CompletionItemKind {
  Text = 1,
  Method = 2,
  Function = 3,
  Constructor = 4,
  Field = 5,
  Variable = 6,
  Class = 7,
  Interface = 8,
  Module = 9,
  Property = 10,
  Unit = 11,
  Value = 12,
  Enum = 13,
  Keyword = 14,
  Snippet = 15,
  Color = 16,
  File = 17,
  Reference = 18,
  Folder = 19,
  EnumMember = 20,
  Constant = 21,
  Struct = 22,
  Event = 23,
  Operator = 24,
  TypeParameter = 25
}

export interface HoverInfo {
  contents: string[];
  range?: Range;
}

export interface Diagnostic {
  message: string;
  range: Range;
  severity: DiagnosticSeverity;
  source: string;
  code?: string | number;
}

export enum DiagnosticSeverity {
  Error = 1,
  Warning = 2,
  Information = 3,
  Hint = 4
}

export interface FormatOptions {
  tabSize?: number;
  insertSpaces?: boolean;
  insertFinalNewline?: boolean;
  trimTrailingWhitespace?: boolean;
}

export interface PlaygroundOptions {
  theme?: 'vs' | 'vs-dark' | 'hc-black';
  language?: string;
  features?: {
    autoComplete?: boolean;
    syntaxHighlighting?: boolean;
    errorChecking?: boolean;
    codeFormatting?: boolean;
    hover?: boolean;
    gotoDefinition?: boolean;
  };
  initialCode?: string;
  readOnly?: boolean;
  minimap?: boolean;
  wordWrap?: 'off' | 'on' | 'wordWrapColumn' | 'bounded';
}

export interface PlaygroundInstance {
  compiler: any; // CryoCompiler instance - will be defined in compiler.ts
  editor: any; // Monaco editor instance
  getValue(): string;
  setValue(value: string): void;
  format(): Promise<void>;
  compile(): Promise<CompileResult>;
  dispose(): void;
}