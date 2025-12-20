# CryoFormat

A production-ready code formatter for the CryoLang programming language, built in Rust.

## Overview

CryoFormat is an automatic code formatter for CryoLang, similar to `gofmt` for Go or `rustfmt` for Rust. It enforces a consistent code style across CryoLang projects while being highly configurable to meet different team preferences.

## Features

- **Fast & Reliable**: Built in Rust for performance and memory safety
- **Configurable**: Extensive configuration options via TOML files  
- **Editor Integration**: Designed to work with IDEs and text editors
- **Batch Processing**: Format entire directories recursively
- **Diff Mode**: Preview changes before applying them
- **Check Mode**: Validate formatting without making changes
- **Line-width Aware**: Intelligent line breaking and wrapping

## Installation

### From Source

```bash
cd tools/CryoFormat
cargo build --release
```

The binary will be available at `target/release/cryofmt`

### Development Build

```bash
cd tools/CryoFormat
cargo build
```

## Usage

### Basic Usage

```bash
# Format a single file
cryofmt file.cryo

# Format multiple files
cryofmt file1.cryo file2.cryo

# Format all .cryo files in current directory
cryofmt *.cryo

# Format recursively
cryofmt -r src/

# Read from stdin, write to stdout
cat file.cryo | cryofmt
```

### Command Line Options

```bash
cryofmt [OPTIONS] [FILES]...

OPTIONS:
    -w, --write              Write result back to files (default)
    -d, --diff               Show diff instead of writing files
    -l, --list               List files that would be reformatted
        --check              Check if files are formatted (exit 1 if not)
    -r, --recursive          Process directories recursively
    -c, --config <FILE>      Configuration file path
    -v, --verbose            Verbose output
        --no-color           Disable colored output
        --tab-width <N>      Tab width for indentation (default: 4)
        --use-tabs           Use tabs instead of spaces
        --max-width <N>      Maximum line width (default: 100)
    -h, --help               Print help information
    -V, --version            Print version information
```

### Examples

```bash
# Check if files are properly formatted (CI/CD)
cryofmt --check src/

# Show what would change without modifying files
cryofmt --diff src/

# List files that need formatting
cryofmt --list -r .

# Format with custom line width
cryofmt --max-width 120 src/

# Format using tabs instead of spaces
cryofmt --use-tabs src/
```

## Configuration

CryoFormat looks for configuration files in the following order:

1. `cryofmt.toml`
2. `.cryofmt.toml`
3. `cryolang.toml`
4. `.cryolang.toml`

It searches the current directory and parent directories.

### Configuration File Example

```toml
[indent]
use_tabs = false
tab_width = 4

[spacing]
binary_operators = true
after_comma = true
after_colon = true
assignment_operators = true
inside_parentheses = false
inside_brackets = false
inside_braces = true

[format]
max_width = 100
short_function_single_line = true
short_struct_single_line = true
align_consecutive_declarations = false
sort_imports = true
insert_final_newline = true
trim_trailing_whitespace = true

[comment]
format_doc_comments = true
wrap_comments = true
normalize_comment_spacing = true
```

### Configuration Options

#### `[indent]`
- `use_tabs`: Use tabs instead of spaces for indentation
- `tab_width`: Width of tab characters in spaces

#### `[spacing]`
- `binary_operators`: Add spaces around binary operators (`a + b`)
- `after_comma`: Add space after commas (`a, b, c`)
- `after_colon`: Add space after colons in type annotations (`x: int`)
- `assignment_operators`: Add spaces around assignment (`x = 5`)
- `inside_parentheses`: Add spaces inside parentheses (`( expr )`)
- `inside_brackets`: Add spaces inside brackets (`[ expr ]`)
- `inside_braces`: Add spaces inside braces (`{ field: value }`)

#### `[format]`
- `max_width`: Maximum line length before wrapping
- `short_function_single_line`: Keep short functions on single line
- `short_struct_single_line`: Keep short structs on single line
- `align_consecutive_declarations`: Align consecutive variable declarations
- `sort_imports`: Sort import statements alphabetically
- `insert_final_newline`: Ensure files end with newline
- `trim_trailing_whitespace`: Remove trailing whitespace

#### `[comment]`
- `format_doc_comments`: Format documentation comments
- `wrap_comments`: Wrap long comments at max_width
- `normalize_comment_spacing`: Normalize spacing in comments

## Formatting Rules

### Variable Declarations

```cryo
// Before
const   x:int=42;
mut     y:string="hello";

// After
const x: int = 42;
mut y: string = "hello";
```

### Functions

```cryo
// Before
function add(a:int,b:int)->int{return a+b;}

// After
function add(a: int, b: int) -> int {
    return a + b;
}
```

### Structs

```cryo
// Before
type struct Point{x:int,y:int}

// After
type struct Point {
    x: int,
    y: int
}
```

### Control Flow

```cryo
// Before
if(x>0){print("positive");}else{print("not positive");}

// After
if (x > 0) {
    print("positive");
} else {
    print("not positive");
}
```

## Editor Integration

### VS Code

Add to your `settings.json`:

```json
{
    "[cryo]": {
        "editor.formatOnSave": true,
        "editor.defaultFormatter": "cryolang.cryofmt"
    }
}
```

### Vim/Neovim

```vim
autocmd BufWritePre *.cryo !cryofmt %
```

### Emacs

```elisp
(add-hook 'cryo-mode-hook
  (lambda ()
    (add-hook 'before-save-hook 'cryofmt-buffer nil 'local)))
```

## Development

### Building

```bash
cargo build
```

### Testing

```bash
cargo test
```

### Running Tests with Coverage

```bash
cargo tarpaulin --out html
```

### Linting

```bash
cargo clippy -- -D warnings
```

## Contributing

1. Fork the repository
2. Create a feature branch
3. Add tests for new functionality
4. Ensure all tests pass
5. Run `cargo fmt` and `cargo clippy`
6. Submit a pull request

## Architecture

CryoFormat consists of several key components:

- **Lexer**: Tokenizes CryoLang source code
- **Parser**: Builds an AST from tokens
- **Formatter**: Traverses AST and generates formatted output
- **Config**: Handles configuration file parsing and options
- **CLI**: Command-line interface and file processing

The formatter is designed to preserve the semantic meaning of code while applying consistent styling rules.

## License

Licensed under the MIT License. See the main CryoLang repository for details.