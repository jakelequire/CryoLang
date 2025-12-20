# CryoFormat Developer Documentation

## Architecture Overview

CryoFormat is built with a modular architecture that separates concerns and makes the codebase maintainable and extensible.

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│      CLI        │    │   Configuration │    │    Profiler     │
│   (main.rs)     │    │  (config.rs)    │    │ (profiler.rs)   │
└─────────────────┘    └─────────────────┘    └─────────────────┘
         │                       │                       │
         └───────────────────────┼───────────────────────┘
                                 │
                    ┌─────────────────┐
                    │    Formatter    │
                    │ (formatter.rs)  │
                    └─────────────────┘
                             │
                ┌────────────┴────────────┐
                │                         │
       ┌─────────────────┐      ┌─────────────────┐
       │      Lexer      │      │     Parser      │
       │   (lexer.rs)    │      │  (parser.rs)    │
       └─────────────────┘      └─────────────────┘
                │                         │
       ┌─────────────────┐      ┌─────────────────┐
       │     Tokens      │      │    AST Nodes    │
       │   (token.rs)    │      │                 │
       └─────────────────┘      └─────────────────┘
```

## Core Components

### 1. Lexer (`src/lexer.rs`)

The lexer tokenizes CryoLang source code into a stream of tokens.

**Key Features:**
- Handles all CryoLang tokens (keywords, operators, literals, etc.)
- Preserves source location information (line, column)
- Supports Unicode in string literals and comments
- Proper handling of multi-character operators
- Comment tokenization for preservation

**Example:**
```rust
let mut lexer = Lexer::new("const x: int = 42;");
let tokens = lexer.tokenize()?;
```

### 2. Parser (`src/parser.rs`)

The parser builds an Abstract Syntax Tree (AST) from the token stream.

**Key Features:**
- Recursive descent parser
- Error recovery and reporting
- Support for all major CryoLang constructs
- Precedence climbing for expressions

**Supported Constructs:**
- Variable declarations (`const`, `mut`)
- Function declarations
- Struct declarations  
- Control flow (`if`, `while`, `for`)
- Expressions (binary, unary, calls)
- Blocks and statements

### 3. Formatter (`src/formatter.rs`)

The formatter traverses the AST and generates formatted code.

**Key Features:**
- Configurable formatting rules
- Proper indentation and spacing
- Line length awareness
- Comment preservation
- Idempotent formatting (formatting twice gives same result)

**Formatting Rules:**
- Consistent spacing around operators
- Proper indentation for nested structures
- Alignment of consecutive declarations
- Standardized brace placement

### 4. Configuration (`src/config.rs`)

The configuration system supports TOML-based format customization.

**Configuration Categories:**
- **Indent**: Tab vs spaces, width
- **Spacing**: Operators, commas, colons, brackets
- **Format**: Line width, single-line preferences, sorting
- **Comments**: Documentation formatting, wrapping

### 5. CLI (`src/main.rs`)

Command-line interface with comprehensive options.

**Modes:**
- **Write mode**: Format files in-place (default)
- **Diff mode**: Show changes without writing
- **Check mode**: Validate formatting (CI-friendly)
- **List mode**: Show files that need formatting

## Data Flow

1. **Input**: Source file or stdin
2. **Lexing**: Text → Tokens
3. **Parsing**: Tokens → AST
4. **Formatting**: AST → Formatted text
5. **Output**: Formatted file or stdout

## Error Handling

CryoFormat uses comprehensive error handling with the `thiserror` crate:

```rust
pub enum FormatError {
    Io(std::io::Error),
    Parse { line: usize, column: usize, message: String },
    Lexer { line: usize, column: usize, message: String },
    Config(String),
    // ...
}
```

**Error Recovery:**
- Lexer continues on unknown characters
- Parser attempts error recovery at statement boundaries
- Detailed error messages with location information

## Performance Considerations

### Optimization Strategies

1. **Single-pass processing** where possible
2. **Minimal allocations** during hot paths
3. **Efficient string handling** with `String` and `&str`
4. **Lazy evaluation** for expensive operations

### Profiling

Use the built-in profiler for performance analysis:

```rust
let mut profiler = Profiler::new();
profiler.start();
// ... formatting operations
profiler.print_stats();
```

## Testing Strategy

### Unit Tests
- Individual component testing
- Property-based testing for formatters
- Error condition coverage

### Integration Tests
- End-to-end CLI testing
- File processing scenarios
- Configuration validation

### Benchmark Tests
- Performance regression detection
- Throughput measurements
- Memory usage profiling

## Adding New Features

### Adding a New Token Type

1. Add to `TokenType` enum in `src/token.rs`
2. Add lexing logic in `src/lexer.rs`
3. Add parsing logic in `src/parser.rs` if needed
4. Add formatting rules in `src/formatter.rs`
5. Add tests

### Adding a New AST Node

1. Add to `AstNode` enum in `src/parser.rs`
2. Add parsing logic
3. Add formatting logic in `src/formatter.rs`
4. Add visitor pattern support if needed
5. Add tests and documentation

### Adding Configuration Options

1. Add to config structs in `src/config.rs`
2. Update TOML serialization/deserialization
3. Add CLI argument if needed
4. Implement formatting behavior
5. Add tests and documentation

## Code Style Guidelines

### Rust Best Practices
- Use `rustfmt` and `clippy`
- Prefer `Result<T, E>` for error handling
- Use descriptive error messages
- Document public APIs

### CryoFormat Specific
- Keep modules focused and cohesive
- Use consistent naming conventions
- Add comprehensive tests for new features
- Update documentation and examples

## Debugging

### Enabling Debug Output

```bash
RUST_LOG=debug cryofmt file.cryo
```

### Common Issues

1. **Lexer errors**: Check character handling and token patterns
2. **Parser errors**: Verify grammar rules and precedence
3. **Formatting errors**: Check AST traversal and output generation

### Debug Tools

```bash
# Check tokens
cryofmt --debug-tokens file.cryo

# Check AST
cryofmt --debug-ast file.cryo

# Performance profiling
cryofmt --profile file.cryo
```

## Contributing

### Development Workflow

1. Fork repository
2. Create feature branch
3. Write tests first (TDD)
4. Implement feature
5. Run full test suite
6. Update documentation
7. Submit pull request

### Release Process

1. Update version in `Cargo.toml`
2. Update `CHANGELOG.md`
3. Run full test suite
4. Create release tag
5. Build release binaries
6. Update documentation

## Future Enhancements

### Planned Features

1. **Advanced formatting rules**
   - Custom operator precedence formatting
   - Complex struct field alignment
   - Import statement organization

2. **Editor integration**
   - Language Server Protocol support
   - Real-time formatting feedback
   - Incremental formatting

3. **Performance improvements**
   - Parallel file processing
   - Streaming for large files
   - Memory usage optimization

4. **Extended language support**
   - Macro formatting
   - Generic constraint formatting
   - Advanced pattern matching

### Architecture Evolution

- Plugin system for custom formatters
- WebAssembly compilation target
- Integration with CryoLang LSP
- IDE-specific formatting profiles

## Maintenance

### Regular Tasks

- Update dependencies
- Run security audits
- Performance benchmarking
- Documentation updates
- Test coverage analysis

### Monitoring

- CI/CD pipeline health
- Performance regression detection
- User feedback incorporation
- Community contribution review