# CryoLang Language Support

VS Code extension providing language support for the CryoLang programming language.

## Features

- **Syntax Highlighting**: Full syntax highlighting for `.cryo` files
- **Language Server Protocol (LSP)**: Real-time diagnostics, autocomplete, hover information, and more
- **Go to Definition**: Navigate to symbol definitions
- **Find References**: Find all references to symbols
- **Error Squiggles**: Real-time error reporting and validation
- **Code Completion**: Intelligent code completion suggestions

## Requirements

This extension requires the CryoLSP language server to be available. The extension will automatically search for:

1. Path configured in settings (`cryo.languageServer.path`)
2. `bin/cryolsp.exe` in the current workspace
3. `cryolsp` or `cryolsp.exe` in system PATH

## Extension Settings

This extension contributes the following settings:

- `cryo.languageServer.path`: Path to the CryoLSP language server executable
- `cryo.languageServer.debug`: Enable debug logging for the language server
- `cryo.languageServer.logFile`: Path to log file for language server output

## Commands

- `CryoLang: Restart Language Server`: Restart the CryoLSP language server
- `CryoLang: Shutdown Language Server`: Stop the CryoLSP language server

## Development

### Building

1. Install dependencies: `npm install`
2. Compile TypeScript: `npm run compile`
3. Package extension: `npm run package`

### Installing

Install the packaged extension with:
```bash
code --install-extension cryo-language-support-0.1.0.vsix
```

## License

MIT License - see LICENSE file for details.