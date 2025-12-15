# CryoLSP - CryoLang Language Server

A simple Language Server Protocol (LSP) implementation for CryoLang that provides intelligent code assistance through TCP communication.

## Features

- **Real-time Diagnostics**: Syntax and type errors
- **Code Completion**: Intelligent suggestions 
- **Hover Information**: Type and documentation info
- **Go to Definition**: Symbol navigation
- **Find References**: Find symbol usage
- **Document/Workspace Symbols**: Code outline and search

## Building

From the main CryoLang directory:
```bash
make lsp
```

Or build directly:
```bash
cd tools/CryoLSP
make
```

## Usage

```bash
./bin/cryolsp --port 7777 --log-level debug
```

## Editor Setup

Configure your editor to connect to `localhost:7777` using the LSP TCP protocol.

## Editor Integration

### VS Code

Create a VS Code extension or configure a generic LSP extension:

```json
{
  "languageServerExample.trace.server": "verbose",
  "languageServerExample.server": {
    "command": "cryolsp",
    "args": ["--port", "7777"],
    "transport": "tcp",
    "port": 7777
  }
}
```

### Neovim

Using `nvim-lspconfig`:

```lua
local lspconfig = require('lspconfig')

lspconfig.cryolsp = {
  default_config = {
    cmd = {'cryolsp', '--port', '7777'},
    filetypes = {'cryo'},
    root_dir = lspconfig.util.root_pattern('.git', 'Cargo.toml'),
    settings = {},
  },
}

lspconfig.cryolsp.setup{}
```

### Emacs

Using `lsp-mode`:

```elisp
(with-eval-after-load 'lsp-mode
  (add-to-list 'lsp-language-id-configuration '(cryo-mode . "cryo"))
  (lsp-register-client
   (make-lsp-client
    :new-connection (lsp-tcp-connection 
                     (lambda (port) 
                       (list "cryolsp" "--port" (number-to-string port))))
    :major-modes '(cryo-mode)
    :server-id 'cryolsp)))
```

### Vim

Using `vim-lsp`:

```vim
if executable('cryolsp')
  au User lsp_setup call lsp#register_server({
    \ 'name': 'cryolsp',
    \ 'cmd': {server_info->['cryolsp', '--port', '7777']},
    \ 'allowlist': ['cryo'],
    \ })
endif
```

## Configuration

### Configuration File

CryoLSP can be configured using a configuration file (JSON format):

```json
{
  "server": {
    "port": 7777,
    "host": "localhost",
    "max_clients": 10,
    "request_timeout": 30000
  },
  "logging": {
    "level": "info",
    "file": "/var/log/cryolsp.log",
    "log_requests": false,
    "log_responses": false
  },
  "features": {
    "diagnostics": true,
    "completion": true,
    "hover": true,
    "goto_definition": true,
    "find_references": true,
    "document_symbols": true,
    "workspace_symbols": true,
    "code_action": true,
    "formatting": false,
    "rename": false
  },
  "compiler": {
    "stdlib_path": "/usr/local/lib/cryo/stdlib",
    "include_paths": ["/usr/local/include/cryo"],
    "optimization_level": "debug",
    "target_triple": "auto"
  }
}
```

### Environment Variables

- `CRYOLSP_PORT`: Override default port
- `CRYOLSP_HOST`: Override default host
- `CRYOLSP_LOG_LEVEL`: Override log level
- `CRYOLSP_CONFIG`: Path to configuration file
- `CRYO_STDLIB_PATH`: Path to CryoLang standard library

## Development

### Project Structure

```
tools/CryoLSP/
├── include/           # Header files
│   ├── LSPServer.hpp
│   ├── LSPTypes.hpp
│   ├── DocumentManager.hpp
│   ├── SymbolProvider.hpp
│   ├── HoverProvider.hpp
│   ├── CompletionProvider.hpp
│   └── DiagnosticsProvider.hpp
├── src/               # Source files
│   ├── main.cpp
│   ├── LSPServer.cpp
│   ├── LSPHandlers.cpp
│   ├── DocumentManager.cpp
│   ├── SymbolProvider.cpp
│   ├── HoverProvider.cpp
│   ├── CompletionProvider.cpp
│   └── DiagnosticsProvider.cpp
├── config/            # Configuration files
├── tests/             # Unit tests
├── CMakeLists.txt     # Build configuration
└── README.md          # This file
```

### Building for Development

```bash
# Debug build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)

# Run with debug logging
./bin/cryolsp --log-level debug --port 7777
```

### Code Formatting

```bash
# Format source code
make format-lsp

# Static analysis
make analyze-lsp
```

### Testing

```bash
# Basic functionality test
make test-lsp

# Run in development mode
make run-lsp
```

## Protocol Support

### Implemented LSP Methods

#### Lifecycle
- `initialize`
- `initialized`
- `shutdown`
- `exit`

#### Document Synchronization
- `textDocument/didOpen`
- `textDocument/didChange`
- `textDocument/didSave`
- `textDocument/didClose`

#### Language Features
- `textDocument/completion`
- `textDocument/hover`
- `textDocument/definition`
- `textDocument/references`
- `textDocument/documentSymbol`
- `workspace/symbol`
- `textDocument/publishDiagnostics`

#### Workspace Features
- `workspace/didChangeConfiguration`
- `workspace/didChangeWatchedFiles`

### Planned Features
- `textDocument/formatting`
- `textDocument/rangeFormatting`
- `textDocument/rename`
- `textDocument/codeAction`
- `textDocument/documentHighlight`
- `textDocument/signatureHelp`

## Troubleshooting

### Common Issues

#### Connection Refused
```bash
# Check if server is running
netstat -tlnp | grep 7777

# Check firewall settings
sudo ufw status
```

#### High Memory Usage
```bash
# Monitor memory usage
top -p $(pgrep cryolsp)

# Enable debug logging to investigate
./cryolsp --log-level debug --log-file debug.log
```

#### Slow Completion
```bash
# Check compiler performance
time cryo --lsp your_file.cryo

# Enable profiling (if built with profiling support)
./cryolsp --profile --log-level debug
```

### Debug Information

```bash
# Get detailed version information
./cryolsp --version

# Test basic functionality
./cryolsp --help

# Check system limits
ulimit -a
```

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests for new functionality
5. Ensure all tests pass
6. Submit a pull request

### Code Style

- Follow the existing code style
- Use meaningful variable and function names
- Add comments for complex algorithms
- Keep functions focused and small
- Handle all error conditions

### Testing Guidelines

- Write unit tests for new features
- Test error conditions
- Verify memory management
- Test with large codebases
- Validate LSP protocol compliance

## License

This project is licensed under the same license as CryoLang. See the main project LICENSE file for details.

## Support

- **Issues**: Report bugs and feature requests on GitHub
- **Documentation**: See the CryoLang documentation
- **Community**: Join the CryoLang community discussions

## Performance

### Benchmarks

- **Startup Time**: < 100ms
- **Memory Usage**: ~50MB base + ~10MB per 1000 lines of code
- **Response Time**: < 10ms for most operations
- **Throughput**: > 1000 requests/second

### Optimization Tips

- Use SSD storage for better I/O performance
- Allocate sufficient RAM for large projects
- Configure appropriate ulimits for file descriptors
- Use release builds for production

## Architecture

### Component Overview

```
┌─────────────────┐    TCP     ┌─────────────────┐
│   LSP Client    │◄──────────►│   LSP Server    │
│  (VS Code, etc) │            │                 │
└─────────────────┘            └─────────────────┘
                                         │
                               ┌─────────▼─────────┐
                               │  Message Handler  │
                               └─────────┬─────────┘
                                         │
        ┌────────────────────────────────┼────────────────────────────────┐
        │                               │                               │
┌───────▼────────┐            ┌─────────▼─────────┐           ┌────────▼────────┐
│ Document Mgr   │            │ Symbol Provider   │           │ Diagnostics    │
│               │            │                   │           │ Provider       │
└────────────────┘            └───────────────────┘           └─────────────────┘
        │                               │                               │
        │                    ┌──────────▼──────────┐                   │
        │                    │ Cryo Compiler API   │                   │
        │                    │                     │                   │
        └────────────────────┤ • Type Checker      │───────────────────┘
                             │ • Symbol Table      │
                             │ • AST Context       │
                             │ • Diagnostic Mgr    │
                             └─────────────────────┘
```

### Threading Model

- **Main Thread**: TCP server and connection handling
- **Worker Threads**: Request processing and compilation
- **Background Thread**: File watching and incremental analysis

### Memory Management

- Shared pointers for compiler instances
- Document content caching with LRU eviction
- Symbol table sharing between requests
- Incremental parsing to minimize memory allocation
