#include "GDM/GDM.hpp"
#include <algorithm>
#include <iomanip>
#include <fstream>

namespace Cryo
{
    // ================================================================
    // SourceRange Implementation
    // ================================================================

    size_t SourceRange::length() const
    {
        if (start.line() == end.line())
        {
            return (end.column() > start.column()) ? (end.column() - start.column() + 1) : 1;
        }
        // Multi-line ranges are more complex, return 1 for now
        return 1;
    }

    // ================================================================
    // Diagnostic Implementation
    // ================================================================

    Diagnostic::Diagnostic(DiagnosticID id, DiagnosticSeverity severity, DiagnosticCategory category,
                           const std::string &message, const SourceRange &range,
                           const std::string &filename)
        : _id(id), _severity(severity), _category(category), _message(message),
          _range(range), _filename(filename)
    {
    }

    Diagnostic &Diagnostic::add_note(const std::string &note)
    {
        _notes.push_back(note);
        return *this;
    }

    Diagnostic &Diagnostic::add_fix_it(const SourceRange &range, const std::string &replacement)
    {
        _fix_it_hints.emplace_back(range, replacement);
        return *this;
    }

    // ================================================================
    // SourceManager Implementation
    // ================================================================

    void SourceManager::add_file(const std::string &filename, std::shared_ptr<File> file)
    {
        _files[filename] = file;
        // Clear any cached lines so they get re-read if needed
        _file_lines.erase(filename);
    }

    std::shared_ptr<File> SourceManager::get_file(const std::string &filename) const
    {
        auto it = _files.find(filename);
        return (it != _files.end()) ? it->second : nullptr;
    }

    bool SourceManager::has_file(const std::string &filename) const
    {
        return _files.find(filename) != _files.end();
    }

    std::string SourceManager::get_source_line(const std::string &filename, size_t line_number) const
    {
        ensure_file_lines_cached(filename);

        auto it = _file_lines.find(filename);
        if (it != _file_lines.end() && line_number > 0 && line_number <= it->second.size())
        {
            return it->second[line_number - 1]; // Convert to 0-based indexing
        }
        return "";
    }

    std::vector<std::string> SourceManager::get_source_context(const std::string &filename,
                                                               size_t center_line, size_t context_lines) const
    {
        ensure_file_lines_cached(filename);

        std::vector<std::string> context;
        auto it = _file_lines.find(filename);
        if (it == _file_lines.end())
        {
            return context;
        }

        const auto &lines = it->second;
        size_t start_line = (center_line > context_lines) ? (center_line - context_lines) : 1;
        size_t end_line = std::min(center_line + context_lines, lines.size());

        for (size_t i = start_line; i <= end_line; ++i)
        {
            if (i > 0 && i <= lines.size())
            {
                context.push_back(lines[i - 1]); // Convert to 0-based indexing
            }
        }

        return context;
    }

    bool SourceManager::is_valid_location(const std::string &filename, const SourceLocation &loc) const
    {
        ensure_file_lines_cached(filename);

        auto it = _file_lines.find(filename);
        if (it == _file_lines.end())
        {
            return false;
        }

        return loc.line() > 0 && loc.line() <= it->second.size() &&
               loc.column() > 0;
    }

    void SourceManager::ensure_file_lines_cached(const std::string &filename) const
    {
        if (_file_lines.find(filename) != _file_lines.end())
        {
            return; // Already cached
        }

        auto file = get_file(filename);
        if (!file)
        {
            return;
        }

        std::vector<std::string> lines;
        std::istringstream content_stream(std::string(file->content()));
        std::string line;

        while (std::getline(content_stream, line))
        {
            lines.push_back(line);
        }

        _file_lines[filename] = std::move(lines);
    }

    // ================================================================
    // DiagnosticFormatter Implementation
    // ================================================================

    DiagnosticFormatter::DiagnosticFormatter(const SourceManager &source_manager,
                                             bool use_colors, bool show_source_context, size_t context_lines)
        : _source_manager(source_manager), _use_colors(use_colors),
          _show_source_context(show_source_context), _context_lines(context_lines)
    {
    }

    std::string DiagnosticFormatter::format(const Diagnostic &diagnostic) const
    {
        std::ostringstream oss;

        // File and location info
        if (!diagnostic.filename().empty())
        {
            oss << diagnostic.filename() << ":";
        }

        if (diagnostic.range().is_valid())
        {
            oss << diagnostic.range().start.line() << ":" << diagnostic.range().start.column() << ": ";
        }

        // Severity and message
        if (_use_colors)
        {
            oss << get_color_code(diagnostic.severity());
        }

        oss << format_severity(diagnostic.severity()) << ": " << diagnostic.message();

        if (_use_colors)
        {
            oss << get_reset_color();
        }

        oss << "\n";

        // Source context
        if (_show_source_context && !diagnostic.filename().empty())
        {
            std::string context = format_source_context(diagnostic);
            if (!context.empty())
            {
                oss << context;
            }
        }

        // Notes
        for (const auto &note : diagnostic.notes())
        {
            oss << "note: " << note << "\n";
        }

        // Fix-it hints
        for (const auto &fix_it : diagnostic.fix_it_hints())
        {
            oss << "fix-it: replace with '" << fix_it.second << "'\n";
        }

        return oss.str();
    }

    void DiagnosticFormatter::print(const Diagnostic &diagnostic, std::ostream &os) const
    {
        os << format(diagnostic);
    }

    std::string DiagnosticFormatter::format_severity(DiagnosticSeverity severity) const
    {
        switch (severity)
        {
        case DiagnosticSeverity::Note:
            return "note";
        case DiagnosticSeverity::Warning:
            return "warning";
        case DiagnosticSeverity::Error:
            return "error";
        case DiagnosticSeverity::Fatal:
            return "fatal error";
        default:
            return "unknown";
        }
    }

    std::string DiagnosticFormatter::format_source_context(const Diagnostic &diagnostic) const
    {
        if (!diagnostic.range().is_valid())
        {
            return "";
        }

        std::ostringstream oss;
        size_t line_number = diagnostic.range().start.line();

        // Get the source line
        std::string source_line = _source_manager.get_source_line(diagnostic.filename(), line_number);
        if (source_line.empty())
        {
            return "";
        }

        // Show the source line with line number
        oss << std::setw(5) << line_number << " | " << source_line << "\n";

        // Show caret line pointing to the error location
        oss << format_caret_line(diagnostic, source_line) << "\n";

        return oss.str();
    }

    std::string DiagnosticFormatter::format_caret_line(const Diagnostic &diagnostic, const std::string &source_line) const
    {
        std::ostringstream oss;
        oss << "      | "; // Align with line number

        size_t column = diagnostic.range().start.column();
        size_t range_length = diagnostic.range().length();

        // Add spaces up to the error column
        for (size_t i = 1; i < column && i <= source_line.length(); ++i)
        {
            oss << (source_line[i - 1] == '\t' ? '\t' : ' ');
        }

        // Add caret(s) for the error range
        if (_use_colors)
        {
            oss << get_color_code(diagnostic.severity());
        }

        oss << "^";
        for (size_t i = 1; i < range_length; ++i)
        {
            oss << "~";
        }

        if (_use_colors)
        {
            oss << get_reset_color();
        }

        return oss.str();
    }

    std::string DiagnosticFormatter::get_color_code(DiagnosticSeverity severity) const
    {
        if (!_use_colors)
        {
            return "";
        }

        switch (severity)
        {
        case DiagnosticSeverity::Note:
            return "\033[1;36m"; // Bright cyan
        case DiagnosticSeverity::Warning:
            return "\033[1;33m"; // Bright yellow
        case DiagnosticSeverity::Error:
            return "\033[1;31m"; // Bright red
        case DiagnosticSeverity::Fatal:
            return "\033[1;35m"; // Bright magenta
        default:
            return "";
        }
    }

    std::string DiagnosticFormatter::get_reset_color() const
    {
        return _use_colors ? "\033[0m" : "";
    }

    // ================================================================
    // DiagnosticManager Implementation
    // ================================================================

    DiagnosticManager::DiagnosticManager()
        : _formatter(_source_manager), _error_count(0), _warning_count(0), _note_count(0),
          _errors_as_warnings(false), _warnings_as_errors(false), _max_errors(100)
    {
    }

    void DiagnosticManager::add_source_file(const std::string &filename, std::shared_ptr<File> file)
    {
        _source_manager.add_file(filename, file);
    }

    void DiagnosticManager::report(DiagnosticID id, DiagnosticSeverity severity, DiagnosticCategory category,
                                   const std::string &message, const SourceRange &range,
                                   const std::string &filename)
    {
        DiagnosticSeverity final_severity = adjust_severity(severity);
        Diagnostic diagnostic(id, final_severity, category, message, range, filename);
        report(diagnostic);
    }

    void DiagnosticManager::report(const Diagnostic &diagnostic)
    {
        _diagnostics.push_back(diagnostic);
        update_statistics(diagnostic.severity());

        // Print immediately in debug mode or for fatal errors
        if (diagnostic.is_fatal())
        {
            _formatter.print(diagnostic, std::cerr);
        }
    }

    void DiagnosticManager::report_error(DiagnosticID id, DiagnosticCategory category,
                                         const std::string &message, const SourceRange &range,
                                         const std::string &filename)
    {
        report(id, DiagnosticSeverity::Error, category, message, range, filename);
    }

    void DiagnosticManager::report_warning(DiagnosticID id, DiagnosticCategory category,
                                           const std::string &message, const SourceRange &range,
                                           const std::string &filename)
    {
        report(id, DiagnosticSeverity::Warning, category, message, range, filename);
    }

    void DiagnosticManager::report_note(DiagnosticID id, DiagnosticCategory category,
                                        const std::string &message, const SourceRange &range,
                                        const std::string &filename)
    {
        report(id, DiagnosticSeverity::Note, category, message, range, filename);
    }

    void DiagnosticManager::print_all(std::ostream &os) const
    {
        for (const auto &diagnostic : _diagnostics)
        {
            _formatter.print(diagnostic, os);
        }
    }

    void DiagnosticManager::print_summary(std::ostream &os) const
    {
        if (_diagnostics.empty())
        {
            os << "No diagnostics.\n";
            return;
        }

        os << "Compilation summary: ";
        if (_error_count > 0)
        {
            os << _error_count << " error" << (_error_count == 1 ? "" : "s");
        }
        if (_warning_count > 0)
        {
            if (_error_count > 0)
                os << ", ";
            os << _warning_count << " warning" << (_warning_count == 1 ? "" : "s");
        }
        if (_note_count > 0)
        {
            if (_error_count > 0 || _warning_count > 0)
                os << ", ";
            os << _note_count << " note" << (_note_count == 1 ? "" : "s");
        }
        os << ".\n";
    }

    void DiagnosticManager::set_formatter_options(bool use_colors, bool show_source_context, size_t context_lines)
    {
        _formatter.set_use_colors(use_colors);
        _formatter.set_show_source_context(show_source_context);
        _formatter.set_context_lines(context_lines);
    }

    void DiagnosticManager::clear()
    {
        _diagnostics.clear();
        _error_count = 0;
        _warning_count = 0;
        _note_count = 0;
    }

    bool DiagnosticManager::should_continue_compilation() const
    {
        return _error_count < _max_errors;
    }

    void DiagnosticManager::report_type_mismatch(const SourceRange &range, const std::string &filename,
                                                 const std::string &expected_type, const std::string &actual_type,
                                                 const std::string &context)
    {
        std::string message = "Type mismatch";
        if (!context.empty())
        {
            message += " in " + context;
        }
        message += ": expected '{}', but got '{}'";
        
        report_error(DiagnosticID::TypeMismatch, DiagnosticCategory::Semantic, range, filename, 
                     message, expected_type, actual_type);
    }

    void DiagnosticManager::report_undefined_symbol(const SourceRange &range, const std::string &filename,
                                                     const std::string &symbol_name, const std::string &context)
    {
        std::string message = "Undefined symbol '{}'";
        if (!context.empty())
        {
            message += " in " + context;
        }
        
        report_error(DiagnosticID::UndefinedVariable, DiagnosticCategory::Semantic, range, filename,
                     message, symbol_name);
    }

    void DiagnosticManager::report_redefined_symbol(const SourceRange &range, const std::string &filename,
                                                     const std::string &symbol_name, const SourceRange &previous_location)
    {
        auto diagnostic = Diagnostic(DiagnosticID::RedefinedSymbol, DiagnosticSeverity::Error, 
                                   DiagnosticCategory::Semantic,
                                   "Redefinition of symbol '" + symbol_name + "'", range, filename);
        
        diagnostic.add_note("Previous definition was here at line " + 
                           std::to_string(previous_location.start.line()) + 
                           ", column " + std::to_string(previous_location.start.column()));
        
        report(diagnostic);
    }

    void DiagnosticManager::report_invalid_operation(const SourceRange &range, const std::string &filename,
                                                      const std::string &operation, const std::string &type,
                                                      const std::string &context)
    {
        std::string message = "Invalid operation '{}' for type '{}'";
        if (!context.empty())
        {
            message += " in " + context;
        }
        
        report_error(DiagnosticID::InvalidOperator, DiagnosticCategory::Semantic, range, filename,
                     message, operation, type);
    }

    void DiagnosticManager::report_argument_mismatch(const SourceRange &range, const std::string &filename,
                                                      const std::string &function_name, size_t expected_count,
                                                      size_t actual_count)
    {
        DiagnosticID id = (actual_count > expected_count) ? DiagnosticID::TooManyArguments : DiagnosticID::TooFewArguments;
        
        std::string message = "Function '{}' expects {} argument{}, but {} {} provided";
        
        report_error(id, DiagnosticCategory::Semantic, range, filename,
                     message, function_name, expected_count, (expected_count == 1 ? "" : "s"),
                     actual_count, (actual_count == 1 ? "was" : "were"));
    }

    void DiagnosticManager::update_statistics(DiagnosticSeverity severity)
    {
        switch (severity)
        {
        case DiagnosticSeverity::Note:
            ++_note_count;
            break;
        case DiagnosticSeverity::Warning:
            ++_warning_count;
            break;
        case DiagnosticSeverity::Error:
        case DiagnosticSeverity::Fatal:
            ++_error_count;
            break;
        }
    }

    DiagnosticSeverity DiagnosticManager::adjust_severity(DiagnosticSeverity original_severity) const
    {
        if (_errors_as_warnings && (original_severity == DiagnosticSeverity::Error))
        {
            return DiagnosticSeverity::Warning;
        }

        if (_warnings_as_errors && (original_severity == DiagnosticSeverity::Warning))
        {
            return DiagnosticSeverity::Error;
        }

        return original_severity;
    }

    // ================================================================
    // Global GDM Instance
    // ================================================================

    std::unique_ptr<DiagnosticManager> g_diagnostic_manager;

    DiagnosticManager &get_diagnostic_manager()
    {
        if (!g_diagnostic_manager)
        {
            initialize_diagnostic_manager();
        }
        return *g_diagnostic_manager;
    }

    void initialize_diagnostic_manager()
    {
        g_diagnostic_manager = std::make_unique<DiagnosticManager>();
    }

    void shutdown_diagnostic_manager()
    {
        g_diagnostic_manager.reset();
    }
}