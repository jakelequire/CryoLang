#pragma once

#include "Lexer/lexer.hpp"
#include "Utils/File.hpp"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <ostream>
#include <iostream>
#include <sstream>

namespace Cryo
{
    // Forward declarations
    class File;
    class SourceManager;
    class DiagnosticManager;

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

    // Specific diagnostic IDs for precise error identification
    enum class DiagnosticID
    {
        // Lexer diagnostics
        UnexpectedCharacter,
        UnterminatedString,
        InvalidNumber,
        InvalidEscapeSequence,

        // Parser diagnostics
        ExpectedToken,
        UnexpectedToken,
        ExpectedExpression,
        ExpectedStatement,
        ExpectedType,
        ExpectedIdentifier,

        // Semantic diagnostics
        UndefinedVariable,
        UndefinedFunction,
        TypeMismatch,
        RedefinedSymbol,
        InvalidCast,
        InvalidOperator,

        // System diagnostics
        FileNotFound,
        FileReadError,
        OutOfMemory,

        // CodeGen diagnostics
        UnimplementedIntrinsic,

        // Generic
        Unknown
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
    // Individual Diagnostic
    // ================================================================

    class Diagnostic
    {
    private:
        DiagnosticID _id;
        DiagnosticSeverity _severity;
        DiagnosticCategory _category;
        std::string _message;
        SourceRange _range;
        std::string _filename;
        std::vector<std::string> _notes;
        std::vector<std::pair<SourceRange, std::string>> _fix_it_hints;

    public:
        Diagnostic(DiagnosticID id, DiagnosticSeverity severity, DiagnosticCategory category,
                   const std::string &message, const SourceRange &range,
                   const std::string &filename = "");

        // Getters
        DiagnosticID id() const { return _id; }
        DiagnosticSeverity severity() const { return _severity; }
        DiagnosticCategory category() const { return _category; }
        const std::string &message() const { return _message; }
        const SourceRange &range() const { return _range; }
        const std::string &filename() const { return _filename; }
        const std::vector<std::string> &notes() const { return _notes; }
        const std::vector<std::pair<SourceRange, std::string>> &fix_it_hints() const { return _fix_it_hints; }

        // Builders for chaining
        Diagnostic &add_note(const std::string &note);
        Diagnostic &add_fix_it(const SourceRange &range, const std::string &replacement);

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

        // Source line access
        std::string get_source_line(const std::string &filename, size_t line_number) const;
        std::vector<std::string> get_source_context(const std::string &filename,
                                                    size_t center_line, size_t context_lines = 2) const;

        // Utility
        bool is_valid_location(const std::string &filename, const SourceLocation &loc) const;

    private:
        void ensure_file_lines_cached(const std::string &filename) const;
    };

    // ================================================================
    // Diagnostic Formatter
    // ================================================================

    class DiagnosticFormatter
    {
    private:
        const SourceManager &_source_manager;
        bool _use_colors;
        bool _show_source_context;
        size_t _context_lines;

    public:
        DiagnosticFormatter(const SourceManager &source_manager,
                            bool use_colors = true,
                            bool show_source_context = true,
                            size_t context_lines = 2);

        // Format a single diagnostic
        std::string format(const Diagnostic &diagnostic) const;
        void print(const Diagnostic &diagnostic, std::ostream &os) const;

        // Configuration
        void set_use_colors(bool use_colors) { _use_colors = use_colors; }
        void set_show_source_context(bool show_context) { _show_source_context = show_context; }
        void set_context_lines(size_t lines) { _context_lines = lines; }

    private:
        std::string format_severity(DiagnosticSeverity severity) const;
        std::string format_source_context(const Diagnostic &diagnostic) const;
        std::string format_caret_line(const Diagnostic &diagnostic, const std::string &source_line) const;
        std::string get_color_code(DiagnosticSeverity severity) const;
        std::string get_reset_color() const;
    };

    // ================================================================
    // Global Diagnostic Manager (GDM)
    // ================================================================

    class DiagnosticManager
    {
    private:
        SourceManager _source_manager;
        DiagnosticFormatter _formatter;
        std::vector<Diagnostic> _diagnostics;

        // Statistics
        size_t _error_count;
        size_t _warning_count;
        size_t _note_count;

        // Configuration
        bool _errors_as_warnings;
        bool _warnings_as_errors;
        size_t _max_errors;

    public:
        DiagnosticManager();
        ~DiagnosticManager() = default;

        // File management delegation
        void add_source_file(const std::string &filename, std::shared_ptr<File> file);
        SourceManager &source_manager() { return _source_manager; }
        const SourceManager &source_manager() const { return _source_manager; }

        // Main diagnostic interface
        void report(DiagnosticID id, DiagnosticSeverity severity, DiagnosticCategory category,
                    const std::string &message, const SourceRange &range,
                    const std::string &filename = "");

        void report(const Diagnostic &diagnostic);

        // Convenience methods for common diagnostic types
        void report_error(DiagnosticID id, DiagnosticCategory category, const std::string &message,
                          const SourceRange &range, const std::string &filename = "");
        void report_warning(DiagnosticID id, DiagnosticCategory category, const std::string &message,
                            const SourceRange &range, const std::string &filename = "");
        void report_note(DiagnosticID id, DiagnosticCategory category, const std::string &message,
                         const SourceRange &range, const std::string &filename = "");

        // Template methods for formatted messages
        template <typename... Args>
        void report_error(DiagnosticID id, DiagnosticCategory category, const SourceRange &range,
                          const std::string &filename, const std::string &format, Args &&...args);

        template <typename... Args>
        void report_warning(DiagnosticID id, DiagnosticCategory category, const SourceRange &range,
                            const std::string &filename, const std::string &format, Args &&...args);

        // Access diagnostics
        const std::vector<Diagnostic> &diagnostics() const { return _diagnostics; }
        bool has_errors() const { return _error_count > 0; }
        bool has_warnings() const { return _warning_count > 0; }

        // Statistics
        size_t error_count() const { return _error_count; }
        size_t warning_count() const { return _warning_count; }
        size_t note_count() const { return _note_count; }
        size_t total_count() const { return _diagnostics.size(); }

        // Output
        void print_all(std::ostream &os = std::cerr) const;
        void print_summary(std::ostream &os = std::cerr) const;

        // Configuration
        void set_errors_as_warnings(bool enable) { _errors_as_warnings = enable; }
        void set_warnings_as_errors(bool enable) { _warnings_as_errors = enable; }
        void set_max_errors(size_t max_errors) { _max_errors = max_errors; }
        void set_formatter_options(bool use_colors, bool show_source_context, size_t context_lines = 2);

        // State management
        void clear();
        bool should_continue_compilation() const;

    private:
        void update_statistics(DiagnosticSeverity severity);
        DiagnosticSeverity adjust_severity(DiagnosticSeverity original_severity) const;

        template <typename... Args>
        std::string format_message(const std::string &format, Args &&...args) const;
    };

    // ================================================================
    // Template implementations
    // ================================================================

    template <typename... Args>
    void DiagnosticManager::report_error(DiagnosticID id, DiagnosticCategory category,
                                         const SourceRange &range, const std::string &filename,
                                         const std::string &format, Args &&...args)
    {
        std::string message = format_message(format, std::forward<Args>(args)...);
        report_error(id, category, message, range, filename);
    }

    template <typename... Args>
    void DiagnosticManager::report_warning(DiagnosticID id, DiagnosticCategory category,
                                           const SourceRange &range, const std::string &filename,
                                           const std::string &format, Args &&...args)
    {
        std::string message = format_message(format, std::forward<Args>(args)...);
        report_warning(id, category, message, range, filename);
    }

    template <typename... Args>
    std::string DiagnosticManager::format_message(const std::string &format, Args &&...args) const
    {
        std::ostringstream oss;
        // Simple format string implementation - you might want to use a more sophisticated one
        oss << format; // This is simplified - in a real implementation you'd handle format specifiers
        return oss.str();
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