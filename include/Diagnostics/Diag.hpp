#pragma once
/******************************************************************************
 * @file Diag.hpp
 * @brief Clean diagnostic system for Cryo compiler - Rust-inspired design
 *
 * Provides a simple, fluent API for creating and emitting diagnostics:
 *
 *   diag_emitter().emit(
 *       Diag::error(E0200_TYPE_MISMATCH, "expected `int`, found `string`")
 *           .at(Span::from_node(node))
 *           .note("types must match for assignment")
 *           .help("consider using `.parse::<int>()`")
 *   );
 *
 ******************************************************************************/

#include "Diagnostics/ErrorCodes.hpp"
#include "Lexer/lexer.hpp" // SourceLocation

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <unordered_map>
#include <memory>
#include <ostream>
#include <iostream>

namespace Cryo
{
    // Forward declarations
    class ASTNode;
    class Type;
    class TypeRef;
    class SyntaxHighlighter;

    //=========================================================================
    // Span - A labeled region of source code
    //=========================================================================

    struct Span
    {
        std::string file;
        size_t start_line = 0;
        size_t start_col = 0;
        size_t end_line = 0;
        size_t end_col = 0;
        std::string label;
        bool is_primary = true;

        Span() = default;

        Span(std::string_view f, size_t sl, size_t sc, size_t el, size_t ec)
            : file(f), start_line(sl), start_col(sc), end_line(el), end_col(ec) {}

        // Factory methods
        static Span at(const SourceLocation &loc, std::string_view file = "");
        static Span range(const SourceLocation &start, const SourceLocation &end,
                          std::string_view file = "");
        static Span from_node(const ASTNode *node);

        // Builder methods for fluent API
        Span &with_label(std::string_view lbl)
        {
            label = std::string(lbl);
            return *this;
        }

        Span &as_secondary()
        {
            is_primary = false;
            return *this;
        }

        bool is_valid() const { return start_line > 0; }
        bool is_multiline() const { return end_line > start_line; }
    };

    //=========================================================================
    // Suggestion - A code fix suggestion
    //=========================================================================

    struct Suggestion
    {
        enum class Applicability
        {
            MachineApplicable, // Safe to auto-apply
            MaybeIncorrect,    // Needs human review
            HasPlaceholders    // Contains <placeholders>
        };

        std::string message;
        Span span;
        std::string replacement;
        Applicability applicability = Applicability::MaybeIncorrect;

        Suggestion() = default;

        Suggestion(std::string_view msg, Span s, std::string_view repl,
                   Applicability app = Applicability::MaybeIncorrect)
            : message(msg), span(std::move(s)), replacement(repl), applicability(app) {}

        // Factory methods
        static Suggestion replace(Span span, std::string_view replacement,
                                  std::string_view msg = "");
        static Suggestion insert_before(const SourceLocation &loc,
                                        std::string_view text,
                                        std::string_view msg = "",
                                        std::string_view file = "");
        static Suggestion insert_after(const SourceLocation &loc,
                                       std::string_view text,
                                       std::string_view msg = "",
                                       std::string_view file = "");
        static Suggestion remove(Span span, std::string_view msg = "");
    };

    //=========================================================================
    // Diag - The main diagnostic type
    //=========================================================================

    class Diag
    {
    public:
        enum class Level
        {
            Note,
            Warning,
            Error,
            Fatal
        };

    private:
        ErrorCode _code;
        Level _level;
        std::string _message;
        std::vector<Span> _spans;
        std::vector<std::string> _notes;
        std::vector<std::string> _help;
        std::vector<Suggestion> _suggestions;

        // Private constructor - use factory methods
        Diag(ErrorCode code, Level level, std::string_view message)
            : _code(code), _level(level), _message(message) {}

    public:
        //=====================================================================
        // Factory Methods - THE SINGLE ENTRY POINT
        //=====================================================================

        static Diag error(ErrorCode code, std::string_view message)
        {
            return Diag(code, Level::Error, message);
        }

        static Diag warning(ErrorCode code, std::string_view message)
        {
            return Diag(code, Level::Warning, message);
        }

        static Diag note(std::string_view message)
        {
            return Diag(ErrorCode::E0000_UNKNOWN, Level::Note, message);
        }

        static Diag fatal(ErrorCode code, std::string_view message)
        {
            return Diag(code, Level::Fatal, message);
        }

        //=====================================================================
        // Convenience Factory Methods for Common Patterns
        //=====================================================================

        // Type mismatch error with automatic message
        static Diag type_mismatch(std::string_view expected, std::string_view found,
                                  const Span &span, std::string_view context = "");

        // Undefined symbol error
        static Diag undefined_symbol(std::string_view name, const Span &span);

        // Undefined field/member error
        static Diag undefined_field(std::string_view field, std::string_view type_name,
                                    const Span &span);

        //=====================================================================
        // Builder Methods - Fluent API for adding details
        //=====================================================================

        // Add primary span
        Diag &at(Span span)
        {
            span.is_primary = true;
            _spans.insert(_spans.begin(), std::move(span));
            return *this;
        }

        // Add span from source location
        Diag &at(const SourceLocation &loc, std::string_view file = "")
        {
            return at(Span::at(loc, file));
        }

        // Add span from AST node
        Diag &at(const ASTNode *node)
        {
            if (node)
            {
                return at(Span::from_node(node));
            }
            return *this;
        }

        // Add secondary span
        Diag &also_at(Span span)
        {
            span.is_primary = false;
            _spans.push_back(std::move(span));
            return *this;
        }

        // Add a note (informational message)
        Diag &with_note(std::string_view text)
        {
            _notes.emplace_back(text);
            return *this;
        }

        // Add help message
        Diag &help(std::string_view text)
        {
            _help.emplace_back(text);
            return *this;
        }

        // Add code suggestion
        Diag &suggest(Suggestion suggestion)
        {
            _suggestions.push_back(std::move(suggestion));
            return *this;
        }

        //=====================================================================
        // Accessors
        //=====================================================================

        ErrorCode code() const { return _code; }
        Level level() const { return _level; }
        const std::string &message() const { return _message; }
        const std::vector<Span> &spans() const { return _spans; }
        const std::vector<std::string> &notes() const { return _notes; }
        const std::vector<std::string> &help_messages() const { return _help; }
        const std::vector<Suggestion> &suggestions() const { return _suggestions; }

        // Get the primary span (first one, or first primary)
        std::optional<Span> primary_span() const
        {
            for (const auto &span : _spans)
            {
                if (span.is_primary)
                    return span;
            }
            return _spans.empty() ? std::nullopt : std::optional<Span>(_spans[0]);
        }

        bool has_spans() const { return !_spans.empty(); }
        bool is_error() const { return _level == Level::Error || _level == Level::Fatal; }
        bool is_warning() const { return _level == Level::Warning; }
        bool is_note() const { return _level == Level::Note; }
    };

    //=========================================================================
    // DiagEmitter - Collects and emits diagnostics
    //=========================================================================

    class DiagEmitter
    {
    public:
        struct Config
        {
            bool colors = true;
            bool unicode = false; // ASCII by default for compatibility
            size_t terminal_width = 120;
            size_t max_errors = 50;
            bool warnings_as_errors = false;
            bool show_stdlib_errors = false;
            size_t context_lines = 2; // Lines before/after error to show
        };

    private:
        std::vector<Diag> _diagnostics;
        Config _config;
        size_t _error_count = 0;
        size_t _warning_count = 0;

        // Source file cache for rendering (filename -> lines)
        std::unordered_map<std::string, std::vector<std::string>> _source_cache;

    public:
        DiagEmitter() = default;
        explicit DiagEmitter(Config config) : _config(std::move(config)) {}

        //=====================================================================
        // Emit diagnostics
        //=====================================================================

        // Emit a diagnostic (renders immediately)
        void emit(Diag diagnostic);

        // Emit without rendering (store for batch output)
        void store(Diag diagnostic);

        //=====================================================================
        // Statistics
        //=====================================================================

        size_t error_count() const { return _error_count; }
        size_t warning_count() const { return _warning_count; }
        size_t diagnostic_count() const { return _diagnostics.size(); }
        bool has_errors() const { return _error_count > 0; }
        bool should_stop() const { return _error_count >= _config.max_errors; }

        //=====================================================================
        // Output
        //=====================================================================

        // Render all stored diagnostics
        void render_all(std::ostream &out = std::cerr) const;

        // Render a single diagnostic
        void render(const Diag &diagnostic, std::ostream &out = std::cerr) const;

        // Print summary (e.g., "error: aborting due to 3 previous errors")
        void print_summary(std::ostream &out = std::cerr) const;

        //=====================================================================
        // LSP Support
        //=====================================================================

        struct LspDiagnostic
        {
            std::string uri;
            size_t start_line = 0;
            size_t start_col = 0;
            size_t end_line = 0;
            size_t end_col = 0;
            std::string severity; // "error", "warning", "information", "hint"
            std::string code;
            std::string message;
            std::vector<std::pair<Span, std::string>> related;
        };

        std::vector<LspDiagnostic> to_lsp() const;

        //=====================================================================
        // Source file management
        //=====================================================================

        // Add source file content for rendering
        void add_source(std::string_view filename, std::string_view content);

        // Add source file by reading from disk
        void add_source_file(std::string_view filename);

        // Get a specific line from cached source
        std::string_view get_line(std::string_view filename, size_t line) const;

        // Check if source is cached
        bool has_source(std::string_view filename) const;

        //=====================================================================
        // Configuration
        //=====================================================================

        void configure(Config config) { _config = std::move(config); }
        const Config &config() const { return _config; }

        //=====================================================================
        // Reset
        //=====================================================================

        void clear()
        {
            _diagnostics.clear();
            _error_count = 0;
            _warning_count = 0;
        }

        void clear_source_cache()
        {
            _source_cache.clear();
        }

        //=====================================================================
        // Access stored diagnostics
        //=====================================================================

        const std::vector<Diag> &diagnostics() const { return _diagnostics; }

    private:
        // Internal rendering helper with syntax highlighter
        void render_diagnostic(const Diag &diagnostic, SyntaxHighlighter &highlighter,
                               size_t error_num, std::ostream &out) const;
    };

    //=========================================================================
    // Global emitter (optional convenience)
    //=========================================================================

    // Get the global diagnostic emitter
    DiagEmitter &diag_emitter();

    // Initialize global emitter with config
    void init_diagnostics(DiagEmitter::Config config = {});

    // Check if global emitter is initialized
    bool diagnostics_initialized();

} // namespace Cryo
