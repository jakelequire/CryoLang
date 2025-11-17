import type { 
  PlaygroundOptions, 
  PlaygroundInstance, 
  CompletionItem,
  CompletionItemKind
} from './types';
import { DiagnosticSeverity } from './types';
import { CryoCompiler } from './compiler';

// Monaco editor type definitions
declare const monaco: any;

export class CryoLanguageService {
  private compiler: CryoCompiler | null = null;

  async initialize(): Promise<void> {
    this.compiler = await CryoCompiler.create();
  }

  /**
   * Register CryoLang language support with Monaco Editor
   */
  async registerWithMonaco(monacoInstance: any): Promise<void> {
    if (!this.compiler) {
      await this.initialize();
    }

    const monaco = monacoInstance;

    // Register the language
    monaco.languages.register({ id: 'cryo' });

    // Set up syntax highlighting
    monaco.languages.setMonarchTokensProvider('cryo', this.getCryoSyntax());

    // Set up language configuration
    monaco.languages.setLanguageConfiguration('cryo', this.getCryoLanguageConfig());

    // Register completion provider
    monaco.languages.registerCompletionItemProvider('cryo', {
      provideCompletionItems: async (model: any, position: any) => {
        if (!this.compiler) return { suggestions: [] };
        
        const source = model.getValue();
        const suggestions = await this.compiler.getCompletionSuggestions(source, {
          line: position.lineNumber,
          column: position.column
        });

        return {
          suggestions: suggestions.map((item: CompletionItem) => ({
            label: item.label,
            kind: monaco.languages.CompletionItemKind[item.kind] || monaco.languages.CompletionItemKind.Text,
            insertText: item.insertText,
            documentation: item.documentation,
            detail: item.detail,
            sortText: item.sortText,
            filterText: item.filterText,
            range: item.range ? {
              startLineNumber: item.range.start.line,
              startColumn: item.range.start.column,
              endLineNumber: item.range.end.line,
              endColumn: item.range.end.column
            } : undefined
          }))
        };
      }
    });

    // Register hover provider
    monaco.languages.registerHoverProvider('cryo', {
      provideHover: async (model: any, position: any) => {
        if (!this.compiler) return null;
        
        const source = model.getValue();
        const hoverInfo = await this.compiler.getHoverInfo(source, {
          line: position.lineNumber,
          column: position.column
        });

        if (!hoverInfo) return null;

        return {
          range: hoverInfo.range ? {
            startLineNumber: hoverInfo.range.start.line,
            startColumn: hoverInfo.range.start.column,
            endLineNumber: hoverInfo.range.end.line,
            endColumn: hoverInfo.range.end.column
          } : undefined,
          contents: hoverInfo.contents.map(content => ({ value: content }))
        };
      }
    });

    // Register diagnostic provider (manual triggering)
    this.setupDiagnostics(monaco);
  }

  /**
   * Set up real-time diagnostics for CryoLang
   */
  private setupDiagnostics(monaco: any): void {
    const updateDiagnostics = async (model: any) => {
      if (!this.compiler) return;
      
      const source = model.getValue();
      const diagnostics = await this.compiler.getDiagnostics(source);

      const markers = diagnostics.map(diagnostic => ({
        startLineNumber: diagnostic.range.start.line,
        startColumn: diagnostic.range.start.column,
        endLineNumber: diagnostic.range.end.line,
        endColumn: diagnostic.range.end.column,
        message: diagnostic.message,
        severity: this.convertSeverity(diagnostic.severity, monaco),
        source: diagnostic.source,
        code: diagnostic.code
      }));

      monaco.editor.setModelMarkers(model, 'cryo', markers);
    };

    // Set up content change listener
    monaco.editor.onDidCreateModel((model: any) => {
      if (model.getLanguageId() === 'cryo') {
        model.onDidChangeContent(() => {
          // Debounce diagnostics updates
          clearTimeout((model as any)._cryoDiagnosticsTimeout);
          (model as any)._cryoDiagnosticsTimeout = setTimeout(() => {
            updateDiagnostics(model);
          }, 500);
        });
        
        // Initial diagnostics
        updateDiagnostics(model);
      }
    });
  }

  /**
   * Convert diagnostic severity to Monaco severity
   */
  private convertSeverity(severity: DiagnosticSeverity, monaco: any): number {
    switch (severity) {
      case DiagnosticSeverity.Error:
        return monaco.MarkerSeverity.Error;
      case DiagnosticSeverity.Warning:
        return monaco.MarkerSeverity.Warning;
      case DiagnosticSeverity.Information:
        return monaco.MarkerSeverity.Info;
      case DiagnosticSeverity.Hint:
        return monaco.MarkerSeverity.Hint;
      default:
        return monaco.MarkerSeverity.Info;
    }
  }

  /**
   * Get CryoLang syntax highlighting rules
   */
  private getCryoSyntax() {
    return {
      tokenizer: {
        root: [
          // Keywords
          [/\b(fn|struct|enum|trait|impl|if|else|while|for|match|return|let|const|mut|pub|mod|use|as|break|continue|loop|in|where|self|Self|true|false|null)\b/, 'keyword'],
          
          // Types
          [/\b(int|float|bool|string|char|void|u8|u16|u32|u64|i8|i16|i32|i64|f32|f64)\b/, 'type'],
          
          // Numbers
          [/\d*\.\d+([eE][-+]?\d+)?[fFdD]?/, 'number.float'],
          [/0[xX][\da-fA-F]+/, 'number.hex'],
          [/0[bB][01]+/, 'number.binary'],
          [/\d+/, 'number'],
          
          // Strings
          [/"([^"\\]|\\.)*"/, 'string'],
          [/'([^'\\]|\\.)*'/, 'string'],
          
          // Comments
          [/\/\/.*$/, 'comment'],
          [/\/\*/, 'comment', '@comment'],
          
          // Operators
          [/[=><!~?:&|+\-*\/\^%]+/, 'operator'],
          
          // Delimiters
          [/[{}()\[\]]/, '@brackets'],
          [/[;,.]/, 'delimiter'],
          
          // Identifiers
          [/[a-zA-Z_]\w*/, 'identifier']
        ],
        
        comment: [
          [/[^\/*]+/, 'comment'],
          [/\*\//, 'comment', '@pop'],
          [/[\/*]/, 'comment']
        ]
      }
    };
  }

  /**
   * Get CryoLang language configuration
   */
  private getCryoLanguageConfig() {
    return {
      comments: {
        lineComment: '//',
        blockComment: ['/*', '*/']
      },
      brackets: [
        ['{', '}'],
        ['[', ']'],
        ['(', ')']
      ],
      autoClosingPairs: [
        { open: '{', close: '}' },
        { open: '[', close: ']' },
        { open: '(', close: ')' },
        { open: '"', close: '"', notIn: ['string'] },
        { open: "'", close: "'", notIn: ['string', 'comment'] }
      ],
      surroundingPairs: [
        { open: '{', close: '}' },
        { open: '[', close: ']' },
        { open: '(', close: ')' },
        { open: '"', close: '"' },
        { open: "'", close: "'" }
      ],
      indentationRules: {
        increaseIndentPattern: /^(.*\{[^}]*|\s*\([^)]*|\s*\[[^\]]*)$/,
        decreaseIndentPattern: /^(.*\}.*|\s*\)|\s*\])$/
      }
    };
  }
}

/**
 * Create a complete CryoLang code playground
 */
export async function createPlayground(
  container: string | HTMLElement,
  options: PlaygroundOptions = {}
): Promise<PlaygroundInstance> {
  // Ensure Monaco is available
  if (typeof monaco === 'undefined') {
    throw new Error('Monaco Editor is required. Please load Monaco Editor before creating playground.');
  }

  // Initialize compiler and language service
  const compiler = await CryoCompiler.create();
  const languageService = new CryoLanguageService();
  await languageService.registerWithMonaco(monaco);

  // Get container element
  const containerElement = typeof container === 'string' 
    ? document.querySelector(container) as HTMLElement
    : container;

  if (!containerElement) {
    throw new Error(`Container element not found: ${container}`);
  }

  // Create Monaco editor with CryoLang support
  const editor = monaco.editor.create(containerElement, {
    value: options.initialCode || `fn main() -> int {\n    println("Hello, CryoLang!");\n    return 0;\n}`,
    language: 'cryo',
    theme: options.theme || 'vs-dark',
    automaticLayout: true,
    minimap: { enabled: options.minimap !== false },
    wordWrap: options.wordWrap || 'on',
    readOnly: options.readOnly || false,
    fontSize: 14,
    lineHeight: 21,
    fontFamily: '"Fira Code", "Consolas", "Monaco", monospace',
    fontLigatures: true,
    cursorBlinking: 'smooth',
    cursorSmoothCaretAnimation: true,
    smoothScrolling: true,
    mouseWheelScrollSensitivity: 1,
    fastScrollSensitivity: 5,
    scrollBeyondLastLine: false,
    renderLineHighlight: 'line',
    selectionHighlight: true,
    occurrencesHighlight: true,
    codeLens: false,
    folding: true,
    foldingHighlight: true,
    showFoldingControls: 'mouseover',
    matchBrackets: 'always',
    renderWhitespace: 'selection',
    renderControlCharacters: false,
    renderIndentGuides: true,
    highlightActiveIndentGuide: true,
    bracketPairColorization: { enabled: true },
    guides: {
      bracketPairs: true,
      bracketPairsHorizontal: 'active',
      highlightActiveBracketPair: true,
      indentation: true,
      highlightActiveIndentation: true
    }
  });

  // Create playground instance
  const playground: PlaygroundInstance = {
    compiler,
    editor,

    getValue(): string {
      return editor.getValue();
    },

    setValue(value: string): void {
      editor.setValue(value);
    },

    async format(): Promise<void> {
      const currentValue = editor.getValue();
      const formatted = await compiler.formatCode(currentValue);
      if (formatted !== currentValue) {
        editor.setValue(formatted);
      }
    },

    async compile() {
      const source = editor.getValue();
      return compiler.compile(source);
    },

    dispose(): void {
      editor.dispose();
      compiler.dispose();
    }
  };

  // Add format command
  editor.addAction({
    id: 'cryo-format',
    label: 'Format Document',
    keybindings: [monaco.KeyMod.Shift | monaco.KeyMod.Alt | monaco.KeyCode.KeyF],
    run: () => playground.format()
  });

  // Add compile command  
  editor.addAction({
    id: 'cryo-compile',
    label: 'Compile CryoLang',
    keybindings: [monaco.KeyMod.CtrlCmd | monaco.KeyCode.Enter],
    run: async () => {
      const result = await playground.compile();
      console.log('Compilation result:', result);
      return result;
    }
  });

  return playground;
}

// Export types and utilities
export * from './types';
export { CryoCompiler } from './compiler';