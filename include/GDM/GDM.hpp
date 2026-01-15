#pragma once

#include "Lexer/lexer.hpp"
#include "Utils/File.hpp"
#include "GDM/ErrorCodes.hpp"
#include "Types2/TypeID.hpp"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <ostream>
#include <iostream>
#include <sstream>
#include <variant>
#include <any>

namespace Cryo
{
    // Forward declarations
    class File;
    class SourceManager;
    class DiagnosticManager;
    class DiagnosticFormatter;
    class DiagnosticPayload;
    class ErrorAnalysis;

    // ================================================================
    // Diagnostic Severity Levels
    // ================================================================

    enum class DiagnosticSeverity
    {
        Note,    // Informational notes
        Warning, // Warnings that don't prevent compilation
        Error,   // Errors that prevent successful compilation
        Fatal    // Fatal errors that stop compilation immediately
    };

    // ================================================================
    // Diagnostic Categories and IDs
    // ================================================================

    enum class DiagnosticCategory
    {
        Lexer,    // Tokenization errors
        Parser,   // Syntax errors
        Semantic, // Type checking, undefined variables, etc.
        CodeGen,  // Code generation errors
        System    // System/IO errors
    };

    // ================================================================
    // Suggestion System for Advanced Diagnostics
    // ================================================================

    enum class SuggestionApplicability
    {
        MachineApplicable, // Can be applied automatically by tools
        MaybeIncorrect,    // Probably correct, but may need user review
        HasPlaceholders,   // Contains placeholder text that needs user input
        Unspecified        // Unknown applicability
    };

    enum class SuggestionStyle
    {
        ShowCode,       // Show the suggestion with code
        HideCodeInline, // Show as inline help text
        HideCodeAlways, // Never show code, only description
        ShowAlways,     // Always show, even if verbose
        ToolOnly        // Only for tools, not human-readable output
    };

    // ================================================================
    // Source Range for highlighting spans of code
    // ================================================================

    struct SourceRange
    {
        SourceLocation start;
        SourceLocation end;

        SourceRange() = default;
        SourceRange(const SourceLocation &start, const SourceLocation &end)
            : start(start), end(end) {}

        // Single character range
        explicit SourceRange(const SourceLocation &loc)
            : start(loc), end(loc) {}

        bool is_valid() const { return start.line() > 0 && start.column() > 0; }
        bool is_single_location() const { return start == end; }
        size_t length() const;
    };

    // ================================================================
    // Enhanced Source Span for sophisticated error reporting
    // ================================================================

    class SourceSpan
    {
    private:
        SourceLocation _start;
        SourceLocation _end;
        std::string _filename;
        bool _is_primary;
        std::optional<std::string> _label;

    public:
        SourceSpan() = default;
        SourceSpan(const SourceLocation &start, const SourceLocation &end,
                   const std::string &filename = "", bool is_primary = true)
            : _start(start), _end(end), _filename(filename), _is_primary(is_primary) {}

        // Single character span
        explicit SourceSpan(const SourceLocation &loc, const std::string &filename = "",
                            bool is_primary = true)
            : _start(loc), _end(loc), _filename(filename), _is_primary(is_primary) {}

        // Convert from existing SourceRange
        SourceSpan(const SourceRange &range, const std::string &filename = "",
                   bool is_primary = true)
            : _start(range.start), _end(range.end), _filename(filename), _is_primary(is_primary) {}

        // Getters
        const SourceLocation &start() const { return _start; }
        const SourceLocation &end() const { return _end; }
        const std::string &filename() const { return _filename; }
        bool is_primary() const { return _is_primary; }
        const std::optional<std::string> &label() const { return _label; }

        // Setters
        void set_primary(bool primary) { _is_primary = primary; }
        void set_label(const std::string &label) { _label = label; }
        void set_filename(const std::string &filename) { _filename = filename; }

        // Utility methods
        bool is_valid() const { return _start.line() > 0 && _start.column() > 0; }
        bool is_single_location() const { return _start == _end; }
        bool spans_multiple_lines() const { return _start.line() != _end.line(); }
        size_t length() const;

        std::vector<size_t> affected_lines() const;
        bool overlaps_with(const SourceSpan &other) const;

        // Convert to legacy SourceRange for compatibility
        SourceRange to_source_range() const { return SourceRange(_start, _end); }

        // Static utility methods
        static SourceSpan merge(const std::vector<SourceSpan> &spans);
        static SourceSpan from_to(const SourceSpan &from, const SourceSpan &to);
    };

    // ================================================================
    // Multi-Span support for complex diagnostics
    // ================================================================

    class MultiSpan
    {
    private:
        std::vector<SourceSpan> _primary_spans;
        std::vector<SourceSpan> _secondary_spans;

    public:
        MultiSpan() = default;
        explicit MultiSpan(const SourceSpan &primary_span);
        explicit MultiSpan(const SourceRange &range, const std::string &filename = "");

        // Primary span management
        void add_primary_span(const SourceSpan &span);
        void add_primary_span(const SourceLocation &start, const SourceLocation &end,
                              const std::string &filename = "");
        const std::vector<SourceSpan> &primary_spans() const { return _primary_spans; }

        // Secondary span management
        void add_secondary_span(const SourceSpan &span);
        void add_secondary_span(const SourceLocation &start, const SourceLocation &end,
                                const std::string &filename = "", const std::string &label = "");
        const std::vector<SourceSpan> &secondary_spans() const { return _secondary_spans; }

        // Convenience methods
        void add_span_label(const SourceSpan &span, const std::string &label);
        void add_span_label(const SourceLocation &start, const SourceLocation &end,
                            const std::string &label, const std::string &filename = "");

        // Utility methods
        bool is_empty() const { return _primary_spans.empty() && _secondary_spans.empty(); }
        bool has_primary_spans() const { return !_primary_spans.empty(); }
        SourceSpan primary_span() const;           // Returns first primary span
        std::vector<SourceSpan> all_spans() const; // All spans combined
        std::vector<std::string> affected_files() const;

        // Legacy compatibility
        SourceRange to_source_range() const;
    };

    // ================================================================
    // Advanced Code Suggestion System
    // ================================================================

    class CodeSuggestion
    {
    public:
        // Single-span suggestion for simple replacements
        struct SimpleSuggestion
        {
            SourceSpan span;
            std::string replacement;

            SimpleSuggestion(const SourceSpan &span, const std::string &replacement)
                : span(span), replacement(replacement) {}
        };

        // Multi-part suggestion for complex edits
        struct MultipartSuggestion
        {
            std::vector<std::pair<SourceSpan, std::string>> parts;

            void add_part(const SourceSpan &span, const std::string &replacement)
            {
                parts.emplace_back(span, replacement);
            }
        };

    private:
        std::string _message;
        SuggestionApplicability _applicability;
        SuggestionStyle _style;
        std::variant<SimpleSuggestion, MultipartSuggestion> _suggestion_data;

    public:
        // Constructors
        CodeSuggestion(const std::string &message,
                       const SourceSpan &span,
                       const std::string &replacement,
                       SuggestionApplicability applicability = SuggestionApplicability::MaybeIncorrect,
                       SuggestionStyle style = SuggestionStyle::ShowCode)
            : _message(message), _applicability(applicability), _style(style),
              _suggestion_data(SimpleSuggestion(span, replacement)) {}

        CodeSuggestion(const std::string &message,
                       const std::vector<std::pair<SourceSpan, std::string>> &parts,
                       SuggestionApplicability applicability = SuggestionApplicability::MaybeIncorrect,
                       SuggestionStyle style = SuggestionStyle::ShowCode)
            : _message(message), _applicability(applicability), _style(style),
              _suggestion_data(MultipartSuggestion{})
        {
            MultipartSuggestion &multipart = std::get<MultipartSuggestion>(_suggestion_data);
            for (const auto &part : parts)
            {
                multipart.add_part(part.first, part.second);
            }
        }

        // Getters
        const std::string &message() const { return _message; }
        SuggestionApplicability applicability() const { return _applicability; }
        SuggestionStyle style() const { return _style; }

        bool is_simple() const
        {
            return std::holds_alternative<SimpleSuggestion>(_suggestion_data);
        }

        bool is_multipart() const
        {
            return std::holds_alternative<MultipartSuggestion>(_suggestion_data);
        }

        const SimpleSuggestion &simple() const
        {
            return std::get<SimpleSuggestion>(_suggestion_data);
        }

        const MultipartSuggestion &multipart() const
        {
            return std::get<MultipartSuggestion>(_suggestion_data);
        }

        // Utility methods
        std::vector<SourceSpan> affected_spans() const;
        bool should_show_code() const;
        bool is_machine_applicable() const
        {
            return _applicability == SuggestionApplicability::MachineApplicable;
        }

        // Builder pattern methods
        CodeSuggestion &with_applicability(SuggestionApplicability applicability)
        {
            _applicability = applicability;
            return *this;
        }

        CodeSuggestion &with_style(SuggestionStyle style)
        {
            _style = style;
            return *this;
        }
    };

    // ================================================================
    // Structured Type Information for Dynamic Error Reporting
    // ================================================================

    // Forward declaration for Type
    class Type;

    // Structured type mismatch context - eliminates need for string parsing
    struct BasicTypeMismatchContext
    {
        TypeRef expected_type;
        TypeRef actual_type;
        std::string context_description; // "variable initialization", "assignment", etc.

        BasicTypeMismatchContext(TypeRef expected, TypeRef actual, const std::string &context = "")
            : expected_type(expected), actual_type(actual), context_description(context) {}

        // Generate the "expected X, found Y" label dynamically
        std::string generate_inline_label() const;

        // Generate context-appropriate help messages
        std::vector<std::string> generate_help_messages() const;

        // Check if types are compatible for specific conversions
        bool can_suggest_parsing() const;
        bool can_suggest_cast() const;
        bool are_related_types() const;

        // Advanced methods that delegate to ErrorAnalysis when available
        std::string generate_primary_message() const;
        std::vector<std::string> generate_suggestions() const;
        std::vector<CodeSuggestion> generate_code_suggestions(const SourceSpan &span) const;
    };

    // ================================================================
    // Individual Diagnostic
    // ================================================================

    class Diagnostic
    {
    private:
        ErrorCode _error_code;
        DiagnosticSeverity _severity;
        DiagnosticCategory _category;
        std::string _message;
        SourceRange _range;
        std::string _filename;
        std::vector<std::string> _notes;
        std::vector<std::pair<SourceRange, std::string>> _fix_it_hints;
        std::string _suggestion;  // Enhanced suggestion based on error code
        std::string _explanation; // Detailed explanation of the error

        // Enhanced span and suggestion system
        MultiSpan _multi_span;
        std::vector<CodeSuggestion> _code_suggestions;
        std::vector<std::string> _help_messages;

        // Structured diagnostic payload for dynamic error generation
        std::unique_ptr<DiagnosticPayload> _payload;

    public:
        // Primary constructor with error codes
        Diagnostic(ErrorCode error_code, DiagnosticSeverity severity, DiagnosticCategory category,
                   const std::string &message, const SourceRange &range,
                   const std::string &filename = "");

        // Destructor
        ~Diagnostic();

        // Move constructor
        Diagnostic(Diagnostic &&other) noexcept;

        // Move assignment
        Diagnostic &operator=(Diagnostic &&other) noexcept;

        // Delete copy constructor and assignment for now (we can add them later if needed)
        Diagnostic(const Diagnostic &) = delete;
        Diagnostic &operator=(const Diagnostic &) = delete;

        // Getters
        ErrorCode error_code() const { return _error_code; }
        DiagnosticSeverity severity() const { return _severity; }
        DiagnosticCategory category() const { return _category; }
        const std::string &message() const { return _message; }
        const SourceRange &range() const { return _range; }
        const std::string &filename() const { return _filename; }
        const std::vector<std::string> &notes() const { return _notes; }
        const std::vector<std::pair<SourceRange, std::string>> &fix_it_hints() const { return _fix_it_hints; }
        const std::string &suggestion() const { return _suggestion; }
        const std::string &explanation() const { return _explanation; }

        // Enhanced span and suggestion system accessors
        const MultiSpan &multi_span() const { return _multi_span; }
        const std::vector<CodeSuggestion> &code_suggestions() const { return _code_suggestions; }
        const std::vector<std::string> &help_messages() const { return _help_messages; }

        // Structured payload accessor
        const DiagnosticPayload *payload() const { return _payload.get(); }
        bool has_payload() const { return _payload != nullptr; }

        // Builders for chaining
        Diagnostic &add_note(const std::string &note);
        Diagnostic &add_fix_it(const SourceRange &range, const std::string &replacement);

        // Enhanced suggestion builders
        Diagnostic &with_primary_span(const SourceSpan &span);
        Diagnostic &add_secondary_span(const SourceSpan &span, const std::string &label = "");
        Diagnostic &add_suggestion(const CodeSuggestion &suggestion);
        Diagnostic &add_help(const std::string &help_message);

        // Convenience suggestion builders
        Diagnostic &suggest_replacement(const SourceSpan &span, const std::string &replacement,
                                        const std::string &message = "try this",
                                        SuggestionApplicability applicability = SuggestionApplicability::MaybeIncorrect);

        Diagnostic &suggest_insertion(const SourceLocation &location, const std::string &insertion,
                                      const std::string &message = "try adding this");

        // Structured payload builder
        Diagnostic &with_payload(std::unique_ptr<DiagnosticPayload> payload);

        // Severity checks
        bool is_note() const { return _severity == DiagnosticSeverity::Note; }
        bool is_warning() const { return _severity == DiagnosticSeverity::Warning; }
        bool is_error() const { return _severity == DiagnosticSeverity::Error; }
        bool is_fatal() const { return _severity == DiagnosticSeverity::Fatal; }
    };

    // ================================================================
    // Source Manager - handles file content and source locations
    // ================================================================

    class SourceManager
    {
    public:
        // Enhanced context structure for sophisticated error display
        struct SourceSnippet
        {
            std::vector<std::string> lines;
            size_t start_line_number;
            std::vector<SourceSpan> highlighted_spans;
            size_t max_line_number_width;
            std::string filename;

            SourceSnippet() : start_line_number(1), max_line_number_width(0) {}
        };

    private:
        std::unordered_map<std::string, std::shared_ptr<File>> _files;
        mutable std::unordered_map<std::string, std::vector<std::string>> _file_lines;

    public:
        SourceManager() = default;
        ~SourceManager() = default;

        // File management
        void add_file(const std::string &filename, std::shared_ptr<File> file);
        std::shared_ptr<File> get_file(const std::string &filename) const;
        bool has_file(const std::string &filename) const;
        void clear(); // Clear all cached files and lines

        // Basic source line access (legacy compatibility)
        std::string get_source_line(const std::string &filename, size_t line_number) const;
        std::vector<std::string> get_source_context(const std::string &filename,
                                                    size_t center_line, size_t context_lines = 2) const;

        // Enhanced context extraction for sophisticated diagnostics
        SourceSnippet extract_snippet(const SourceSpan &span, size_t context_lines = 2) const;
        SourceSnippet extract_snippet(const MultiSpan &multi_span, size_t context_lines = 2) const;

        // Smart context - automatically determine optimal context based on span complexity
        SourceSnippet extract_smart_context(const SourceSpan &span) const;
        SourceSnippet extract_smart_context(const MultiSpan &multi_span) const;

        // Multi-file snippet extraction for spans across multiple files
        std::vector<SourceSnippet> extract_multi_file_snippets(const MultiSpan &multi_span,
                                                               size_t context_lines = 2) const;

        // Utility methods for enhanced source management
        bool is_valid_location(const std::string &filename, const SourceLocation &loc) const;
        bool is_valid_span(const SourceSpan &span) const;
        size_t get_line_count(const std::string &filename) const;
        std::string get_line_text(const std::string &filename, size_t line_number) const;

        // Calculate optimal line number width for formatting
        size_t calculate_line_number_width(size_t max_line_number) const;

        // Span utilities
        std::vector<SourceSpan> get_spans_on_line(const std::vector<SourceSpan> &spans,
                                                  size_t line_number) const;

    private:
        void ensure_file_lines_cached(const std::string &filename) const;

        // Helper methods for enhanced context extraction
        std::pair<size_t, size_t> calculate_context_range(const SourceSpan &span,
                                                          size_t context_lines,
                                                          size_t file_line_count) const;

        std::pair<size_t, size_t> calculate_multi_span_range(const std::vector<SourceSpan> &spans,
                                                             size_t context_lines,
                                                             size_t file_line_count) const;
    };

    // ================================================================
    // Global Diagnostic Manager (GDM)
    // ================================================================

    class DiagnosticManager
    {
    private:
        SourceManager _source_manager;
        std::unique_ptr<DiagnosticFormatter> _formatter;
        std::vector<Diagnostic> _diagnostics;

        // Statistics
        size_t _error_count;
        size_t _warning_count;
        size_t _note_count;

        // Configuration
        bool _errors_as_warnings;
        bool _warnings_as_errors;
        size_t _max_errors;
        bool _show_stdlib_diagnostics; // When true, show diagnostics from stdlib files

    public:
        DiagnosticManager();
        ~DiagnosticManager(); // Custom destructor needed for unique_ptr with incomplete type

        // Delete copy constructor and assignment operator
        DiagnosticManager(const DiagnosticManager &) = delete;
        DiagnosticManager &operator=(const DiagnosticManager &) = delete;

        // Allow move constructor and assignment
        DiagnosticManager(DiagnosticManager &&) = default;
        DiagnosticManager &operator=(DiagnosticManager &&) = default;

        // File management delegation
        void add_source_file(const std::string &filename, std::shared_ptr<File> file);
        SourceManager &source_manager() { return _source_manager; }
        const SourceManager &source_manager() const { return _source_manager; }

        // ================================================================
        // UNIFIED ERROR REPORTING API - ErrorCode-based only
        // ================================================================

        /**
         * @brief Main diagnostic reporting interface - ErrorCode determines everything
         * @param error_code The specific error code that determines category, severity, and payload type
         * @param range Source location of the error
         * @param filename Source file containing the error
         * @param message Optional custom message (uses ErrorCode default if empty)
         * @param context Optional error-specific context data
         */
        Diagnostic &create_diagnostic(ErrorCode error_code,
                                      const SourceRange &range,
                                      const std::string &filename = "",
                                      const std::string &message = "",
                                      const std::any &context = {});

        /**
         * @brief Simplified diagnostic creation with automatic context
         * @param error_code The error code
         * @param range Source range
         * @param filename Source filename
         */
        Diagnostic &create_error(ErrorCode error_code,
                                 const SourceRange &range,
                                 const std::string &filename = "");

        /**
         * @brief Create diagnostic with custom message override
         */
        Diagnostic &create_error(ErrorCode error_code,
                                 const SourceRange &range,
                                 const std::string &filename,
                                 const std::string &custom_message);

        /**
         * @brief Legacy support - will be deprecated
         */
        void report(Diagnostic diagnostic);

        // Access diagnostics
        const std::vector<Diagnostic> &diagnostics() const { return _diagnostics; }
        bool has_errors() const { return _show_stdlib_diagnostics ? _error_count > 0 : user_error_count() > 0; }
        bool has_warnings() const { return _warning_count > 0; }

        // Statistics
        size_t error_count() const { return _error_count; }
        size_t warning_count() const { return _warning_count; }
        size_t note_count() const { return _note_count; }
        size_t total_count() const { return _diagnostics.size(); }

        // User code only statistics (excluding stdlib)
        size_t user_error_count() const;
        size_t user_warning_count() const;
        size_t user_total_count() const;

        // Utility method to detect if a diagnostic is from stdlib code
        bool is_stdlib_diagnostic(const Diagnostic &diagnostic) const;

        // Output
        void print_all(std::ostream &os = std::cerr) const;
        void print_summary(std::ostream &os = std::cerr) const;

        // Configuration
        void set_errors_as_warnings(bool enable) { _errors_as_warnings = enable; }
        void set_warnings_as_errors(bool enable) { _warnings_as_errors = enable; }
        void set_max_errors(size_t max_errors) { _max_errors = max_errors; }
        void set_show_stdlib_diagnostics(bool enable) { _show_stdlib_diagnostics = enable; }
        bool show_stdlib_diagnostics() const { return _show_stdlib_diagnostics; }
        void set_formatter_options(bool use_colors, bool use_unicode, size_t terminal_width = 80);

        // State management
        void clear();
        bool should_continue_compilation() const;

        // LSP-specific API
        struct LSPDiagnostic
        {
            std::string message;
            std::string severity; // "error", "warning", "info", "hint"
            size_t line;
            size_t column;
            size_t end_line;
            size_t end_column;
            std::string filename;
            std::vector<std::string> suggestions;
            std::string code; // Error code like "E0001"
        };

        std::vector<LSPDiagnostic> get_lsp_diagnostics() const;
        void clear_lsp_diagnostics();
        bool has_lsp_diagnostics() const;

    private:
        void update_statistics(DiagnosticSeverity severity);
        DiagnosticSeverity adjust_severity(DiagnosticSeverity original_severity) const;

        // Helper methods for unified error reporting
        DiagnosticCategory determine_category_from_error_code(ErrorCode error_code) const;
        DiagnosticSeverity determine_severity_from_error_code(ErrorCode error_code) const;

        template <typename... Args>
        std::string format_message(const std::string &format, Args &&...args) const;
    };

    // ================================================================
    // Template implementations
    // ================================================================

    // Helper for variadic template message formatting
    template <typename T>
    void format_impl(std::ostream &os, const std::string &format, T &&value)
    {
        auto pos = format.find("{}");
        if (pos != std::string::npos)
        {
            os << format.substr(0, pos) << value << format.substr(pos + 2);
        }
        else
        {
            os << format;
        }
    }

    template <typename T, typename... Args>
    void format_impl(std::ostream &os, const std::string &format, T &&value, Args &&...args)
    {
        auto pos = format.find("{}");
        if (pos != std::string::npos)
        {
            os << format.substr(0, pos) << value;
            format_impl(os, format.substr(pos + 2), std::forward<Args>(args)...);
        }
        else
        {
            os << format;
        }
    }

    template <typename... Args>
    std::string DiagnosticManager::format_message(const std::string &format, Args &&...args) const
    {
        if constexpr (sizeof...(args) == 0)
        {
            return format;
        }
        else
        {
            std::ostringstream oss;
            format_impl(oss, format, std::forward<Args>(args)...);
            return oss.str();
        }
    }

    // ================================================================
    // Global GDM Instance
    // ================================================================

    // Global diagnostic manager instance
    extern std::unique_ptr<DiagnosticManager> g_diagnostic_manager;

    // Convenience functions for global access
    DiagnosticManager &get_diagnostic_manager();
    void initialize_diagnostic_manager();
    void shutdown_diagnostic_manager();
}