# Enhanced Diagnostic System Plan for CryoLang

*Rust-Inspired Sophisticated Error Reporting System*

---

## Table of Contents

1. [Current State Analysis](#current-state-analysis)
2. [Enhanced Diagnostic System Design](#enhanced-diagnostic-system-design)
3. [Phase 1: Enhanced Span System](#phase-1-enhanced-span-system)
4. [Phase 2: Sophisticated Suggestion System](#phase-2-sophisticated-suggestion-system)
5. [Phase 3: Advanced Diagnostic Features](#phase-3-advanced-diagnostic-features)
6. [Phase 4: Advanced Formatting and Display](#phase-4-advanced-formatting-and-display)
7. [Phase 5: JSON Output for Tool Integration](#phase-5-json-output-for-tool-integration)
8. [Phase 6: Error Code Explanations](#phase-6-error-code-explanations)
9. [Implementation Strategy](#implementation-strategy)
10. [Integration Points](#integration-points)
11. [Key Insights from Rust](#key-insights-from-rust)

---

## Current State Analysis

### Strengths of Existing System

- ✅ **Well-structured error codes** with systematic categorization (E0000-E9999)
- ✅ **Source location tracking** via `SourceLocation` class in tokens and AST nodes  
- ✅ **Good foundation** with `Diagnostic` class supporting severity levels, notes, and fix-it hints
- ✅ **File management** through `SourceManager` for accessing source content
- ✅ **Detailed error information** with explanations and suggestions per error code

### Areas for Enhancement

- 🔶 **Limited span support** - only single locations, not ranges
- 🔶 **Basic formatting** - text-only output without visual highlighting
- 🔶 **Simple suggestions** - no applicability levels or multiple alternatives  
- 🔶 **No multi-span diagnostics** - can't highlight related code sections
- 🔶 **Limited context** - no automatic code snippet extraction and display

---

## Enhanced Diagnostic System Design

The goal is to transform the current system to match Rust's sophistication while building on the existing solid foundation.

---

## Phase 1: Enhanced Span System

### 1.1 Multi-Span Support

```cpp
// Enhanced SourceRange with better functionality
class SourceSpan {
    SourceLocation start;
    SourceLocation end;
    std::string filename;
    bool is_primary;
    std::optional<std::string> label;
    
    // Multi-span support
    static SourceSpan merge(const std::vector<SourceSpan>& spans);
    std::vector<size_t> affected_lines() const;
    bool spans_multiple_lines() const;
    bool overlaps_with(const SourceSpan& other) const;
};

// Support for multiple related spans
class MultiSpan {
    std::vector<SourceSpan> primary_spans;
    std::vector<SourceSpan> secondary_spans;
    
    void add_primary_span(SourceSpan span);
    void add_secondary_span(SourceSpan span, std::string label);
    void add_span_label(SourceSpan span, std::string label);
};
```

### 1.2 Enhanced Source Context

```cpp
class SourceManager {
    // Enhanced context extraction
    struct SourceSnippet {
        std::vector<std::string> lines;
        size_t start_line_number;
        std::vector<SourceSpan> highlighted_spans;
        size_t max_line_number_width;
    };
    
    SourceSnippet extract_snippet(
        const MultiSpan& span, 
        size_t context_lines = 2
    ) const;
    
    // Smart context - automatically determine optimal context
    SourceSnippet extract_smart_context(const MultiSpan& span) const;
};
```

---

## Phase 2: Sophisticated Suggestion System

### 2.1 Applicability Levels (like Rust)

```cpp
enum class SuggestionApplicability {
    MachineApplicable,    // Can be applied automatically by tools
    MaybeIncorrect,       // Probably correct, but may need user review
    HasPlaceholders,      // Contains placeholder text that needs user input
    Unspecified           // Unknown applicability
};

enum class SuggestionStyle {
    ShowCode,            // Show the suggestion with code
    HideCodeInline,      // Show as inline help text
    HideCodeAlways,      // Never show code, only description
    ShowAlways,          // Always show, even if verbose
    ToolOnly             // Only for tools, not human-readable output
};
```

### 2.2 Advanced Suggestion Types

```cpp
class CodeSuggestion {
    std::string message;
    SuggestionApplicability applicability;
    SuggestionStyle style;
    
    // Single-span suggestions
    struct SimpleSuggestion {
        SourceSpan span;
        std::string replacement;
    };
    
    // Multi-part suggestions for complex edits
    struct MultipartSuggestion {
        std::vector<std::pair<SourceSpan, std::string>> parts;
    };
    
    std::variant<SimpleSuggestion, MultipartSuggestion> suggestion_data;
};
```

---

## Phase 3: Advanced Diagnostic Features

### 3.1 Enhanced Diagnostic Class

```cpp
class EnhancedDiagnostic {
    ErrorCode error_code;
    DiagnosticSeverity severity;
    std::string primary_message;
    MultiSpan span;
    std::string filename;
    
    // Enhanced features
    std::vector<std::string> notes;
    std::vector<std::pair<SourceSpan, std::string>> span_labels;
    std::vector<CodeSuggestion> suggestions;
    std::vector<EnhancedDiagnostic> child_diagnostics;  // Related errors
    
    // Contextual help
    std::optional<std::string> explanation;  // Detailed explanation
    std::optional<std::string> help_url;     // Link to documentation
    std::vector<std::string> related_errors; // "See also" error codes
    
    // Builder pattern methods
    EnhancedDiagnostic& span_label(SourceSpan span, std::string label);
    EnhancedDiagnostic& span_suggestion(
        SourceSpan span, 
        std::string message, 
        std::string replacement,
        SuggestionApplicability applicability = SuggestionApplicability::MaybeIncorrect
    );
    EnhancedDiagnostic& multipart_suggestion(
        std::string message,
        std::vector<std::pair<SourceSpan, std::string>> parts,
        SuggestionApplicability applicability
    );
    EnhancedDiagnostic& note(std::string note);
    EnhancedDiagnostic& help(std::string help);
    EnhancedDiagnostic& with_explanation(std::string explanation);
};
```

### 3.2 Smart Error Context Detection

```cpp
class ContextAnalyzer {
    // Automatically detect common error patterns and suggest fixes
    static std::vector<CodeSuggestion> analyze_type_mismatch(
        const SourceSpan& error_span,
        const std::string& expected_type,
        const std::string& actual_type,
        const SourceManager& source_mgr
    );
    
    static std::vector<CodeSuggestion> analyze_undefined_symbol(
        const SourceSpan& error_span,
        const std::string& symbol_name,
        const std::vector<std::string>& available_symbols
    );
    
    // Fuzzy matching for "did you mean" suggestions
    static std::vector<std::string> find_similar_symbols(
        const std::string& target,
        const std::vector<std::string>& candidates,
        double threshold = 0.6
    );
};
```

---

## Phase 4: Advanced Formatting and Display

### 4.1 Rich Terminal Output

```cpp
class AdvancedDiagnosticFormatter {
    bool use_colors;
    bool use_unicode;
    size_t terminal_width;
    
    struct FormatStyle {
        std::string error_color = "\033[1;31m";     // Bold red
        std::string warning_color = "\033[1;33m";   // Bold yellow
        std::string note_color = "\033[1;36m";      // Bold cyan
        std::string line_number_color = "\033[34m"; // Blue
        std::string primary_span_color = "\033[1;31m"; // Bold red underline
        std::string secondary_span_color = "\033[1;36m"; // Bold cyan underline
    };
    
    std::string format_diagnostic(
        const EnhancedDiagnostic& diagnostic,
        const SourceManager& source_mgr
    );
    
private:
    std::string render_code_snippet(
        const SourceManager::SourceSnippet& snippet,
        const std::vector<SourceSpan>& spans
    );
    
    std::string create_underline(
        const std::string& source_line,
        const std::vector<SourceSpan>& spans_on_line,
        bool is_primary
    );
};
```

### 4.2 Example Output Format

The enhanced system would produce output like this:

```
error[E0200]: type mismatch in assignment
  --> src/main.cryo:15:13
   |
15 |     let x: int = "hello";
   |            ---   ^^^^^^^ expected `int`, found `string`
   |            |
   |            expected due to this type annotation
   |
help: if you meant to create a string variable, remove the type annotation
   |
15 |     let x = "hello";
   |         --
help: if you want to convert string to integer, try parsing
   |
15 |     let x: int = "hello".parse_int();
   |                         ~~~~~~~~~~~

note: string literals cannot be implicitly converted to integers
  --> src/main.cryo:15:18
   |
15 |     let x: int = "hello";
   |                  ^^^^^^^

error: aborting due to previous error

For more information about this error, try `cryo --explain E0200`.
```

---

## Phase 5: JSON Output for Tool Integration

### 5.1 Structured Output

```cpp
class JsonDiagnosticEmitter {
    nlohmann::json emit_diagnostic(const EnhancedDiagnostic& diagnostic);
    
private:
    nlohmann::json format_span(const SourceSpan& span);
    nlohmann::json format_suggestion(const CodeSuggestion& suggestion);
};
```

**Example JSON Output:**

```json
{
  "message": "type mismatch in assignment",
  "code": {
    "code": "E0200",
    "explanation": "..."
  },
  "level": "error",
  "spans": [
    {
      "file_name": "src/main.cryo",
      "byte_start": 234,
      "byte_end": 241,
      "line_start": 15,
      "line_end": 15,
      "column_start": 18,
      "column_end": 25,
      "is_primary": true,
      "text": [
        {
          "text": "    let x: int = \"hello\";",
          "highlight_start": 17,
          "highlight_end": 24
        }
      ],
      "label": "expected `int`, found `string`",
      "suggested_replacement": null,
      "suggestion_applicability": null
    }
  ],
  "children": [
    {
      "message": "if you meant to create a string variable, remove the type annotation",
      "code": null,
      "level": "help",
      "spans": [
        {
          "file_name": "src/main.cryo",
          "byte_start": 220,
          "byte_end": 227,
          "line_start": 15,
          "line_end": 15,
          "column_start": 8,
          "column_end": 15,
          "is_primary": true,
          "suggested_replacement": "let x",
          "suggestion_applicability": "MaybeIncorrect"
        }
      ]
    }
  ],
  "rendered": "error[E0200]: type mismatch in assignment\n..."
}
```

---

## Phase 6: Error Code Explanations

### 6.1 Enhanced Error Registry

```cpp
class EnhancedErrorRegistry {
    struct DetailedErrorInfo {
        ErrorCode code;
        std::string title;
        std::string description;
        std::string explanation;  // Detailed explanation
        std::vector<std::string> examples;  // Good and bad examples
        std::vector<std::string> common_fixes;
        std::string documentation_url;
        std::vector<ErrorCode> related_errors;
    };
    
    static const DetailedErrorInfo& get_detailed_info(ErrorCode code);
    static std::string format_explanation(ErrorCode code);
};
```

### 6.2 Example Detailed Error Explanation

```
Error E0200: Type mismatch in assignment

This error occurs when you try to assign a value of one type to a variable 
that expects a different type.

Example of erroneous code:
```cryo
const x: int = "hello";  // Error: string cannot be assigned to int
```

Correct examples:
```cryo
const x: int = 42;           // Correct: int assigned to int
const x: string = "hello";   // Correct: string assigned to string
const x = "hello";           // Correct: type inferred as string
```

Common fixes:
1. Remove the type annotation to let the compiler infer the type
2. Change the value to match the expected type
3. Use explicit type conversion if available
4. Change the variable's type annotation

Related errors:
- E0201: Undefined variable
- E0210: Invalid type conversion

See also: https://cryolang.dev/docs/types/type-system
```

---

## Implementation Strategy

### Phase 1: Foundation (Week 1-2)
1. **Enhance SourceSpan and MultiSpan classes**
   - Extend current `SourceRange` to `SourceSpan` with labels and primary/secondary distinction
   - Create `MultiSpan` class for handling multiple related spans
   - Update `SourceManager` for better context extraction

2. **Update existing diagnostic creation sites**
   - Modify parser error reporting to use enhanced spans
   - Update type checker to create multi-span diagnostics
   - Ensure AST nodes provide accurate span information

### Phase 2: Suggestions (Week 3-4)
1. **Implement SuggestionApplicability and SuggestionStyle enums**
   - Define applicability levels for automated tooling
   - Create style options for different display scenarios

2. **Add advanced suggestion types to CodeSuggestion**
   - Support for simple and multipart suggestions
   - Integration with existing fix-it hint system

3. **Update error sites to provide better suggestions**
   - Parser: better syntax error recovery suggestions
   - Type checker: type conversion and correction suggestions

### Phase 3: Smart Analysis (Week 5-6)
1. **Implement ContextAnalyzer for common error patterns**
   - Type mismatch analysis with conversion suggestions
   - Undefined symbol analysis with fuzzy matching
   - Common syntax error pattern recognition

2. **Add fuzzy matching for symbol suggestions**
   - Levenshtein distance algorithm for "did you mean" suggestions
   - Context-aware symbol filtering

3. **Integrate smart suggestions into type checker and parser**
   - Automatic suggestion generation for common errors
   - Context-aware help messages

### Phase 4: Formatting (Week 7-8)
1. **Implement AdvancedDiagnosticFormatter**
   - Rich terminal output with colors and Unicode
   - Adaptive formatting based on terminal capabilities
   - Code snippet rendering with precise highlighting

2. **Add color and Unicode support**
   - ANSI color codes for different diagnostic elements
   - Fallback to ASCII for limited terminals
   - User preference for color/no-color output

3. **Create rich code snippet rendering**
   - Multi-line span highlighting
   - Precise underlining and annotation
   - Smart context window selection

### Phase 5: Polish (Week 9-10)
1. **Add JSON output support**
   - Machine-readable diagnostic format
   - Tool integration support (LSP, IDEs, etc.)
   - Structured suggestion format for automated fixes

2. **Implement detailed error explanations**
   - Enhanced error registry with detailed information
   - `cryo --explain E####` command implementation
   - Examples and common fixes for each error

3. **Comprehensive testing and refinement**
   - Test all error scenarios with new formatting
   - Validate JSON output format
   - Performance optimization for large files

---

## Integration Points

The beauty of this plan is that it builds on the existing solid foundation:

### 1. **Current `ErrorCode` System**
- Can be extended with the detailed error information
- Existing systematic categorization (E0000-E9999) remains intact
- Enhanced with detailed explanations and examples

### 2. **Existing `SourceLocation` Tracking**
- Already present in AST nodes provides foundation for enhanced spans
- Tokens already have location information
- Can be extended to span ranges rather than single points

### 3. **Current `Diagnostic` Class**
- Can be evolved into `EnhancedDiagnostic`
- Existing severity levels, notes, and fix-it hints provide base functionality
- Builder pattern can be added for fluent API

### 4. **Current `DiagnosticManager`**
- Can be updated to use the new formatting system
- Existing error reporting infrastructure remains
- Enhanced with new output formats (JSON, rich terminal)

### 5. **Parser and Type Checker Integration**
- Integration points are already established
- Can be enhanced to provide better suggestions and multi-span errors
- Error recovery can be improved with better context analysis

---

## Key Insights from Rust

This approach will give Cryo error messages that rival Rust's sophistication while building naturally on the existing architecture. The key insights from Rust are:

### 1. **Multi-span Highlighting**
- Show relationships between different parts of code
- Primary spans (the main error location) vs secondary spans (related context)
- Span labels that explain the relationship

### 2. **Sophisticated Suggestion System**
- Applicability levels for automated tooling integration
- Multiple suggestion styles for different contexts
- Multipart suggestions for complex code transformations

### 3. **Rich Visual Formatting**
- Colors and precise underlining for better readability
- Smart code snippet extraction with optimal context
- Unicode characters for beautiful terminal output

### 4. **Contextual Help**
- Understands common error patterns and provides specific help
- Fuzzy matching for "did you mean" suggestions
- Related error cross-references

### 5. **Machine-readable Output**
- JSON format for tool integration
- Structured suggestion format for automated fixes
- Language Server Protocol compatibility

### 6. **Comprehensive Error Explanations**
- Detailed explanations available on demand
- Examples of correct and incorrect code
- Links to documentation and related concepts

---

## Example Migration Path

Here's how a current error would be enhanced:

### Current Error Output:
```
Parse Error: Expected semicolon after expression
```

### Enhanced Error Output:
```
error[E0106]: expected semicolon after expression
  --> src/main.cryo:12:23
   |
12 |     mut result: int = calculate()
   |                                  ^ help: try adding a semicolon: `;`
13 |     print(result);
   |     ----- expected before this statement

error: aborting due to previous error

For more information about this error, try `cryo --explain E0106`.
```

This transformation demonstrates how the enhanced system provides:
- Precise location highlighting
- Clear visual indication of the problem
- Actionable suggestions with exact code fixes
- Context showing why the fix is needed
- Reference to detailed explanations

---

## Conclusion

This comprehensive plan transforms the Cryo compiler's error reporting from basic text messages to a sophisticated, Rust-inspired diagnostic system that:

- **Helps developers understand** exactly what went wrong and where
- **Provides actionable suggestions** for fixing problems
- **Integrates with development tools** through structured output
- **Maintains backward compatibility** while dramatically improving user experience
- **Scales with project complexity** through smart context analysis

The phased implementation approach ensures that each enhancement builds on the previous work while maintaining a working compiler throughout the development process.


Goal Error output:
```
error[E0200]: type mismatch in assignment
  --> src/main.cryo:15:13
   |
15 |     const x: int = "hello";
   |              ---   ^^^^^^^ expected `int`, found `string`
   |              |
   |              expected due to this type annotation
   |
help: if you meant to create a string variable, remove the type annotation
   |
15 |     const x = "hello";
   |           --
help: if you want to convert string to integer, try parsing
   |
15 |     const x: int = "hello".parse_int();
   |                            ~~~~~~~~~~~

note: string literals cannot be implicitly converted to integers
  --> src/main.cryo:15:18
   |
15 |     const x: int = "hello";
   |                    ^^^^^^^

error: aborting due to previous error

For more information about this error, try `cryo --explain E0200`.
```