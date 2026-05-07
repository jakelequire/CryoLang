# NewCryoLSP

A language server for the Cryo programming language, written in Cryo. Speaks LSP 3.17 over JSON-RPC 2.0 / stdio. Eats its own dog food: the compiler analyzes itself.

## Capabilities (v1)

- **Diagnostics** — push (`textDocument/publishDiagnostics`) on every `didOpen` / `didChange`
- **Hover** — type of the expression under the cursor
- **Go to definition** — jump to the declaring span of a symbol
- **Completion** — every in-scope symbol, mapped to LSP `CompletionItemKind`
- **Lifecycle** — `initialize` / `initialized` / `shutdown` / `exit`
- **Document sync** — full-text replacement on `didChange` (incremental sync deferred to v1.1)

## Build

From the repo root:

```sh
make lsp
```

This depends on `make cryo`, which depends on `make stdlib`. The output binary is `bin/cryolsp`.

## Run

The server reads framed JSON-RPC from stdin and writes responses to stdout. **Stderr is reserved for log output** — every editor LSP client captures it for you.

Manual smoke test:

```sh
printf 'Content-Length: 56\r\n\r\n{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' | bin/cryolsp
```

You should see a JSON response containing `serverInfo.name = "cryolsp"` and the capability advertisement.

## Editor integration

### VS Code

The `tools/CryoAnalyzer` extension auto-detects `<repo>/bin/cryolsp`. Reload the window after building.

To override the binary path explicitly, set in `settings.json`:

```jsonc
{
  "cryo.languageServer.path": "/abs/path/to/CryoLang/bin/cryolsp"
}
```

### Neovim (lspconfig)

```lua
require("lspconfig.configs").cryo = {
  default_config = {
    cmd        = { "/abs/path/to/CryoLang/bin/cryolsp" },
    filetypes  = { "cryo" },
    root_dir   = require("lspconfig.util").root_pattern("cryoconfig", ".git"),
    settings   = {},
  },
}
require("lspconfig").cryo.setup({})
vim.filetype.add({ extension = { cryo = "cryo" } })
```

### Helix (`languages.toml`)

```toml
[language-server.cryolsp]
command = "/abs/path/to/CryoLang/bin/cryolsp"

[[language]]
name             = "cryo"
scope            = "source.cryo"
file-types       = ["cryo"]
roots            = ["cryoconfig", ".git"]
language-servers = ["cryolsp"]
```

### Sublime LSP

`LSP-cryo` doesn't exist on Package Control; use the generic LSP package with this in `LSP.sublime-settings → clients`:

```jsonc
"cryolsp": {
  "command":   ["/abs/path/to/CryoLang/bin/cryolsp"],
  "selector":  "source.cryo",
  "enabled":   true
}
```

## Architecture

```
src/
├── main.cryo                  Entry point — initialize logger, run Server::run.
├── server/
│   ├── server.cryo            Event loop + dispatcher + lifecycle gating.
│   ├── document.cryo          DocumentStore, Document.
│   ├── line_index.cryo        Byte-offset-of-line-start cache.
│   └── compiler_session.cryo  CompilerInstance + per-URI DocumentSession.
├── protocol/
│   ├── framing.cryo           Content-Length read/write.
│   ├── jsonrpc.cryo           Request/Response/Notification + parse/encode.
│   ├── lsp_types.cryo         Position, Range, Location, Hover, ...
│   ├── capabilities.cryo      build_initialize_result.
│   └── conv.cryo              SourceSpan ↔ Range, severity-string ↔ int.
├── handlers/
│   ├── lifecycle.cryo
│   ├── text_sync.cryo         didOpen / didChange / didSave / didClose.
│   ├── diagnostics.cryo       publishDiagnostics push.
│   ├── hover.cryo
│   ├── definition.cryo
│   └── completion.cryo
├── ast_query/
│   ├── position_finder.cryo   NodeAtPositionVisitor.
│   └── scope_index.cryo       Span → ScopeID (linear-search v1).
└── util/
    ├── log.cryo               Stderr logger.
    ├── uri.cryo               file path ↔ URI.
    └── string_ops.cryo        Local string helpers.
```

The server holds **one** `CompilerInstance` and constructs a fresh `CompilationContext*` per recompile (one per open document). The frontend pipeline runs in milliseconds for typical files; cross-document context reuse is a v1.x optimization.

## Known limitations (v1)

- **Full-text sync only** — incremental edits land in v1.1.
- **ASCII assumption for `Position.character`** — LSP defines it as a UTF-16 code-unit offset; v1 treats it as a byte offset. Cryo source is overwhelmingly ASCII so the discrepancy doesn't bite in practice. The LineIndex struct is wide enough to add UTF-16 boundaries in v1.1 without breaking handlers.
- **Single-threaded** — Cryo has no concurrency primitives. Each request is processed serially. VS Code, neovim, and Helix all serialize per-connection so this is rarely visible.
- **Linear scope search** — completion's enclosing-scope lookup is O(n) over `resolver.scopes[]`. Fast enough for v1; v1.1 introduces a tree-built ScopeIndex.
- **No trigger-character context** — completion always returns the full in-scope set; the editor's fuzzy filter narrows it. Trigger-aware filtering (`obj.foo` only showing methods of `obj`'s type) lands in v1.1.

## Verification

After building, exercise each capability in your editor:

1. Open a `.cryo` file with a deliberate type error → red squiggle within ~1s.
2. Hover a typed identifier → tooltip shows ``` `name: <type>` ``` rendered as a Cryo code block.
3. F12 (or `gd` in vim) on a function call → cursor jumps to the declaration.
4. Inside a function body, Ctrl-Space → completion list appears with in-scope symbols.
5. Close the file → squiggles clear.

If something doesn't work, check the editor's LSP output channel — the server logs every request and its outcome.
