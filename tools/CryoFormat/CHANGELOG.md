# Changelog

All notable changes to CryoFormat will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Initial release of CryoFormat
- Complete lexer and parser for CryoLang syntax
- Configurable formatting options via TOML files
- Command-line interface with multiple modes (format, check, diff, list)
- Support for formatting variables, functions, structs, and control flow
- Recursive directory processing
- Editor integration support
- Comprehensive test suite
- Cross-platform build scripts

### Features
- **Formatting Rules**:
  - Variable declarations with proper spacing
  - Function declarations with parameter formatting
  - Struct declarations with field alignment
  - Binary and unary expression spacing
  - Control flow statement formatting
  - Comment preservation and normalization

- **Configuration Options**:
  - Indentation settings (tabs vs spaces, width)
  - Spacing rules for operators, commas, colons
  - Line width and wrapping behavior
  - Import sorting and organization
  - Trailing whitespace and newline handling

- **CLI Features**:
  - Multiple output modes (write, diff, check, list)
  - Recursive directory processing
  - Stdin/stdout support
  - Verbose and colored output
  - Custom configuration file paths

- **Developer Experience**:
  - Fast Rust-based implementation
  - Comprehensive error handling
  - Integration-ready architecture
  - Extensive test coverage

## [0.1.0] - 2025-12-20

### Added
- Initial implementation of CryoFormat
- Core formatting engine
- CLI interface
- Configuration system
- Test suite and documentation