#include "GDM/GDM.hpp"
#include "GDM/AdvancedDiagnosticFormatter.hpp"
#include <algorithm>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <cctype>

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
    // SourceSpan Implementation
    // ================================================================

    size_t SourceSpan::length() const
    {
        if (_start.line() == _end.line())
        {
            return (_end.column() > _start.column()) ? (_end.column() - _start.column() + 1) : 1;
        }
        // For multi-line spans, calculate total character length
        // This is a simplified calculation - in practice you'd need access to source text
        return (_end.line() - _start.line() + 1) * 20; // Rough estimate
    }

    std::vector<size_t> SourceSpan::affected_lines() const
    {
        std::vector<size_t> lines;
        for (size_t line = _start.line(); line <= _end.line(); ++line)
        {
            lines.push_back(line);
        }
        return lines;
    }

    bool SourceSpan::overlaps_with(const SourceSpan& other) const
    {
        // Different files don't overlap
        if (_filename != other._filename)
            return false;

        // Check if spans overlap
        return !(_end.line() < other._start.line() || 
                 _start.line() > other._end.line() ||
                 (_start.line() == other._end.line() && _start.column() > other._end.column()) ||
                 (_end.line() == other._start.line() && _end.column() < other._start.column()));
    }

    SourceSpan SourceSpan::merge(const std::vector<SourceSpan>& spans)
    {
        if (spans.empty())
            return SourceSpan();

        SourceSpan result = spans[0];
        for (const auto& span : spans)
        {
            if (span._start.line() < result._start.line() ||
                (span._start.line() == result._start.line() && span._start.column() < result._start.column()))
            {
                result._start = span._start;
            }
            if (span._end.line() > result._end.line() ||
                (span._end.line() == result._end.line() && span._end.column() > result._end.column()))
            {
                result._end = span._end;
            }
        }
        return result;
    }

    SourceSpan SourceSpan::from_to(const SourceSpan& from, const SourceSpan& to)
    {
        return SourceSpan(from._start, to._end, from._filename, true);
    }

    // ================================================================
    // MultiSpan Implementation
    // ================================================================

    MultiSpan::MultiSpan(const SourceSpan& primary_span)
    {
        _primary_spans.push_back(primary_span);
    }

    MultiSpan::MultiSpan(const SourceRange& range, const std::string& filename)
    {
        _primary_spans.emplace_back(range, filename);
    }

    void MultiSpan::add_primary_span(const SourceSpan& span)
    {
        _primary_spans.push_back(span);
    }

    void MultiSpan::add_primary_span(const SourceLocation& start, const SourceLocation& end, 
                                     const std::string& filename)
    {
        _primary_spans.emplace_back(start, end, filename, true);
    }

    void MultiSpan::add_secondary_span(const SourceSpan& span)
    {
        SourceSpan secondary_span = span;
        secondary_span.set_primary(false);
        _secondary_spans.push_back(secondary_span);
    }

    void MultiSpan::add_secondary_span(const SourceLocation& start, const SourceLocation& end,
                                       const std::string& filename, const std::string& label)
    {
        SourceSpan span(start, end, filename, false);
        if (!label.empty())
        {
            span.set_label(label);
        }
        _secondary_spans.push_back(span);
    }

    void MultiSpan::add_span_label(const SourceSpan& span, const std::string& label)
    {
        SourceSpan labeled_span = span;
        labeled_span.set_label(label);
        labeled_span.set_primary(false);
        _secondary_spans.push_back(labeled_span);
    }

    void MultiSpan::add_span_label(const SourceLocation& start, const SourceLocation& end,
                                   const std::string& label, const std::string& filename)
    {
        SourceSpan span(start, end, filename, false);
        span.set_label(label);
        _secondary_spans.push_back(span);
    }

    SourceSpan MultiSpan::primary_span() const
    {
        return _primary_spans.empty() ? SourceSpan() : _primary_spans[0];
    }

    std::vector<SourceSpan> MultiSpan::all_spans() const
    {
        std::vector<SourceSpan> all;
        all.insert(all.end(), _primary_spans.begin(), _primary_spans.end());
        all.insert(all.end(), _secondary_spans.begin(), _secondary_spans.end());
        return all;
    }

    std::vector<std::string> MultiSpan::affected_files() const
    {
        std::vector<std::string> files;
        for (const auto& span : all_spans())
        {
            if (!span.filename().empty() && 
                std::find(files.begin(), files.end(), span.filename()) == files.end())
            {
                files.push_back(span.filename());
            }
        }
        return files;
    }

    SourceRange MultiSpan::to_source_range() const
    {
        if (_primary_spans.empty())
            return SourceRange();
        return _primary_spans[0].to_source_range();
    }

    // ================================================================
    // CodeSuggestion Implementation
    // ================================================================

    std::vector<SourceSpan> CodeSuggestion::affected_spans() const
    {
        std::vector<SourceSpan> spans;
        
        if (is_simple()) {
            spans.push_back(simple().span);
        } else if (is_multipart()) {
            const auto& parts = multipart().parts;
            for (const auto& part : parts) {
                spans.push_back(part.first);
            }
        }
        
        return spans;
    }

    bool CodeSuggestion::should_show_code() const
    {
        return _style == SuggestionStyle::ShowCode || 
               _style == SuggestionStyle::ShowAlways;
    }

    // ================================================================
    // Diagnostic Implementation
    // ================================================================

    Diagnostic::Diagnostic(ErrorCode error_code, DiagnosticSeverity severity, DiagnosticCategory category,
                           const std::string &message, const SourceRange &range,
                           const std::string &filename)
        : _error_code(error_code), _severity(severity), _category(category), 
          _message(message), _range(range), _filename(filename)
    {
        // Initialize enhanced error information from error code
        const ErrorInfo& error_info = ErrorRegistry::get_error_info(_error_code);
        _suggestion = error_info.suggestion;
        _explanation = error_info.explanation;
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

    // Enhanced suggestion system implementations
    Diagnostic& Diagnostic::with_primary_span(const SourceSpan& span)
    {
        _multi_span.add_primary_span(span);
        return *this;
    }

    Diagnostic& Diagnostic::add_secondary_span(const SourceSpan& span, const std::string& label)
    {
        if (label.empty()) {
            _multi_span.add_secondary_span(span);
        } else {
            _multi_span.add_secondary_span(span.start(), span.end(), span.filename(), label);
        }
        return *this;
    }

    Diagnostic& Diagnostic::add_suggestion(const CodeSuggestion& suggestion)
    {
        _code_suggestions.push_back(suggestion);
        return *this;
    }

    Diagnostic& Diagnostic::add_help(const std::string& help_message)
    {
        _help_messages.push_back(help_message);
        return *this;
    }

    Diagnostic& Diagnostic::suggest_replacement(const SourceSpan& span, const std::string& replacement,
                                              const std::string& message, SuggestionApplicability applicability)
    {
        _code_suggestions.emplace_back(message, span, replacement, applicability);
        return *this;
    }

    Diagnostic& Diagnostic::suggest_insertion(const SourceLocation& location, const std::string& insertion,
                                            const std::string& message)
    {
        // Create a zero-length span at the insertion point
        SourceSpan span(location, location);
        _code_suggestions.emplace_back(message, span, insertion, SuggestionApplicability::MachineApplicable);
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
    // Enhanced SourceManager Implementation
    // ================================================================

    SourceManager::SourceSnippet SourceManager::extract_snippet(const SourceSpan& span, size_t context_lines) const
    {
        SourceSnippet snippet;
        snippet.filename = span.filename();
        
        if (!is_valid_span(span))
        {
            return snippet;
        }

        ensure_file_lines_cached(span.filename());
        auto file_lines_it = _file_lines.find(span.filename());
        if (file_lines_it == _file_lines.end())
        {
            return snippet;
        }

        const auto& file_lines = file_lines_it->second;
        auto [start_line, end_line] = calculate_context_range(span, context_lines, file_lines.size());

        snippet.start_line_number = start_line;
        snippet.max_line_number_width = calculate_line_number_width(end_line);
        snippet.highlighted_spans.push_back(span);

        for (size_t line_num = start_line; line_num <= end_line && line_num <= file_lines.size(); ++line_num)
        {
            snippet.lines.push_back(file_lines[line_num - 1]); // Convert to 0-based indexing
        }

        return snippet;
    }

    SourceManager::SourceSnippet SourceManager::extract_snippet(const MultiSpan& multi_span, size_t context_lines) const
    {
        SourceSnippet snippet;
        
        if (multi_span.is_empty())
        {
            return snippet;
        }

        // For now, handle single-file multi-spans
        // TODO: Extend for multi-file spans
        auto primary_span = multi_span.primary_span();
        snippet.filename = primary_span.filename();
        
        if (!is_valid_span(primary_span))
        {
            return snippet;
        }

        ensure_file_lines_cached(primary_span.filename());
        auto file_lines_it = _file_lines.find(primary_span.filename());
        if (file_lines_it == _file_lines.end())
        {
            return snippet;
        }

        const auto& file_lines = file_lines_it->second;
        auto all_spans = multi_span.all_spans();
        
        // Filter spans for this file only
        std::vector<SourceSpan> file_spans;
        for (const auto& span : all_spans)
        {
            if (span.filename() == primary_span.filename())
            {
                file_spans.push_back(span);
            }
        }

        auto [start_line, end_line] = calculate_multi_span_range(file_spans, context_lines, file_lines.size());

        snippet.start_line_number = start_line;
        snippet.max_line_number_width = calculate_line_number_width(end_line);
        snippet.highlighted_spans = file_spans;

        for (size_t line_num = start_line; line_num <= end_line && line_num <= file_lines.size(); ++line_num)
        {
            snippet.lines.push_back(file_lines[line_num - 1]);
        }

        return snippet;
    }

    SourceManager::SourceSnippet SourceManager::extract_smart_context(const SourceSpan& span) const
    {
        // Smart context: adjust context lines based on span characteristics
        size_t context_lines = 2;
        
        if (span.spans_multiple_lines())
        {
            context_lines = 1; // Less context for multi-line spans
        }
        else if (span.length() > 80)
        {
            context_lines = 3; // More context for long single-line spans
        }

        return extract_snippet(span, context_lines);
    }

    SourceManager::SourceSnippet SourceManager::extract_smart_context(const MultiSpan& multi_span) const
    {
        // Smart context for multi-spans
        size_t context_lines = 2;
        
        auto all_spans = multi_span.all_spans();
        if (all_spans.size() > 3)
        {
            context_lines = 1; // Less context for complex multi-spans
        }

        return extract_snippet(multi_span, context_lines);
    }

    std::vector<SourceManager::SourceSnippet> SourceManager::extract_multi_file_snippets(
        const MultiSpan& multi_span, size_t context_lines) const
    {
        std::vector<SourceSnippet> snippets;
        auto affected_files = multi_span.affected_files();

        for (const auto& filename : affected_files)
        {
            // Create a temporary MultiSpan with only spans from this file
            MultiSpan file_spans;
            for (const auto& span : multi_span.all_spans())
            {
                if (span.filename() == filename)
                {
                    if (span.is_primary())
                    {
                        file_spans.add_primary_span(span);
                    }
                    else
                    {
                        file_spans.add_secondary_span(span);
                    }
                }
            }

            if (!file_spans.is_empty())
            {
                snippets.push_back(extract_snippet(file_spans, context_lines));
            }
        }

        return snippets;
    }

    bool SourceManager::is_valid_span(const SourceSpan& span) const
    {
        return !span.filename().empty() && 
               is_valid_location(span.filename(), span.start()) &&
               is_valid_location(span.filename(), span.end());
    }

    size_t SourceManager::get_line_count(const std::string& filename) const
    {
        ensure_file_lines_cached(filename);
        auto it = _file_lines.find(filename);
        return (it != _file_lines.end()) ? it->second.size() : 0;
    }

    std::string SourceManager::get_line_text(const std::string& filename, size_t line_number) const
    {
        return get_source_line(filename, line_number);
    }

    size_t SourceManager::calculate_line_number_width(size_t max_line_number) const
    {
        if (max_line_number == 0) return 1;
        
        size_t width = 0;
        while (max_line_number > 0)
        {
            width++;
            max_line_number /= 10;
        }
        return width;
    }

    std::vector<SourceSpan> SourceManager::get_spans_on_line(const std::vector<SourceSpan>& spans, 
                                                             size_t line_number) const
    {
        std::vector<SourceSpan> spans_on_line;
        for (const auto& span : spans)
        {
            auto affected_lines = span.affected_lines();
            if (std::find(affected_lines.begin(), affected_lines.end(), line_number) != affected_lines.end())
            {
                spans_on_line.push_back(span);
            }
        }
        return spans_on_line;
    }

    std::pair<size_t, size_t> SourceManager::calculate_context_range(const SourceSpan& span, 
                                                                     size_t context_lines,
                                                                     size_t file_line_count) const
    {
        size_t start_line = (span.start().line() > context_lines) ? 
                           span.start().line() - context_lines : 1;
        size_t end_line = std::min(span.end().line() + context_lines, file_line_count);
        
        return {start_line, end_line};
    }

    std::pair<size_t, size_t> SourceManager::calculate_multi_span_range(const std::vector<SourceSpan>& spans,
                                                                        size_t context_lines,
                                                                        size_t file_line_count) const
    {
        if (spans.empty())
        {
            return {1, 1};
        }

        size_t min_line = spans[0].start().line();
        size_t max_line = spans[0].end().line();

        for (const auto& span : spans)
        {
            min_line = std::min(min_line, span.start().line());
            max_line = std::max(max_line, span.end().line());
        }

        size_t start_line = (min_line > context_lines) ? min_line - context_lines : 1;
        size_t end_line = std::min(max_line + context_lines, file_line_count);

        return {start_line, end_line};
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

        // Severity and message (enhanced format: "error[E0308]: type mismatch")
        if (_use_colors)
        {
            oss << get_color_code(diagnostic.severity());
        }

        oss << format_severity(diagnostic.severity());

        // Enhanced error code display
        ErrorCode error_code = diagnostic.error_code();
        if (error_code != ErrorCode::E0805_INTERNAL_ERROR)
        {
            ErrorRegistry::initialize();
            oss << "[" << ErrorRegistry::format_error_code(error_code) << "]";
        }

        oss << ": " << diagnostic.message();

        if (_use_colors)
        {
            oss << get_reset_color();
        }

        oss << "\n";

        // Location info (Rust format: " --> filename:line:column")
        if (!diagnostic.filename().empty() && diagnostic.range().is_valid())
        {
            if (_use_colors)
            {
                oss << "\033[1;34m"; // Bright blue
            }
            oss << " --> " << diagnostic.filename() << ":"
                << diagnostic.range().start.line() << ":"
                << diagnostic.range().start.column();
            if (_use_colors)
            {
                oss << get_reset_color();
            }
            oss << "\n";
        }

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
            if (_use_colors)
            {
                oss << "\033[1;36m"; // Bright cyan for notes
            }
            oss << "note: " << note;
            if (_use_colors)
            {
                oss << get_reset_color();
            }
            oss << "\n";
        }

        // Fix-it hints
        for (const auto &fix_it : diagnostic.fix_it_hints())
        {
            if (_use_colors)
            {
                oss << "\033[1;32m"; // Bright green for suggestions
            }
            oss << "help: try `" << fix_it.second << "`";
            if (_use_colors)
            {
                oss << get_reset_color();
            }
            oss << "\n";
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

        // Get source context around the error line
        std::vector<std::string> context_lines = _source_manager.get_source_context(
            diagnostic.filename(), line_number, _context_lines);

        if (context_lines.empty())
        {
            // Fallback to single line
            std::string source_line = _source_manager.get_source_line(diagnostic.filename(), line_number);
            if (source_line.empty())
            {
                return "";
            }

            // Show just the error line with better formatting
            oss << "   |\n";
            oss << std::setw(3) << line_number << " | " << source_line << "\n";
            oss << format_caret_line(diagnostic, source_line) << "\n";
            return oss.str();
        }

        // Calculate the starting line number for context
        size_t start_line = (line_number > _context_lines) ? (line_number - _context_lines) : 1;

        // Add a blank separator line
        oss << "   |\n";

        // Show context lines
        for (size_t i = 0; i < context_lines.size(); ++i)
        {
            size_t current_line = start_line + i;

            if (current_line == line_number)
            {
                // This is the error line - highlight it
                oss << std::setw(3) << current_line << " | " << context_lines[i] << "\n";
                oss << format_caret_line(diagnostic, context_lines[i]) << "\n";
            }
            else
            {
                // Context line
                oss << std::setw(3) << current_line << " | " << context_lines[i] << "\n";
            }
        }

        // Add closing separator line
        oss << "   |\n";

        return oss.str();
    }

    std::string DiagnosticFormatter::format_caret_line(const Diagnostic &diagnostic, const std::string &source_line) const
    {
        std::ostringstream oss;

        // Match the line number prefix format exactly: "   | "
        oss << "   | ";

        size_t column = diagnostic.range().start.column();

        // Since the lexer now properly tracks visual column positions (including tab expansion),
        // we can trust the column position directly. Convert to 0-based for positioning.
        size_t visual_position = (column > 0) ? column - 1 : 0;

        // For specific error types, try to find the actual token and adjust position
        size_t adjusted_position = visual_position;
        
        // Handle string literal errors
        if (diagnostic.message().find("string") != std::string::npos || 
            diagnostic.message().find("String") != std::string::npos)
        {
            // Search forward from the error position to find the string literal
            // This handles cases where the error location points to the assignment operator
            // but we want to highlight the actual string being assigned
            for (size_t i = visual_position; i < source_line.length(); ++i)
            {
                if (source_line[i] == '"' || source_line[i] == '\'')
                {
                    adjusted_position = i;
                    break;
                }
                // Stop searching after reasonable distance or if we hit end of statement
                if (i - visual_position > 10 || source_line[i] == ';')
                {
                    break;
                }
            }
        }
        // Handle namespace-specific errors - just highlight the word "namespace"
        else if (diagnostic.message().find("namespace") != std::string::npos)
        {
            // For namespace errors, we usually want to highlight just the "namespace" keyword
            // The position should already be correct, no adjustment needed
        }

        // Add spaces up to the caret position
        for (size_t i = 0; i < adjusted_position; ++i)
        {
            oss << ' ';
        }

        // Determine the appropriate length to highlight based on the token at this position
        size_t highlight_length = determine_token_length(source_line, adjusted_position, diagnostic);

        // Add highlighting for the error range
        if (_use_colors)
        {
            oss << get_color_code(diagnostic.severity());
        }

        // For single character errors, use ^
        // For multi-character tokens, use ^ followed by ~~~~~
        if (highlight_length <= 1)
        {
            oss << "^";
        }
        else
        {
            oss << "^";
            // Add tildes for the rest of the token, but limit to reasonable length
            size_t tilde_count = std::min(highlight_length - 1, static_cast<size_t>(20)); // Max 20 tildes
            for (size_t i = 0; i < tilde_count; ++i)
            {
                oss << "~";
            }
        }
        if (_use_colors)
        {
            oss << get_reset_color();
        }

        // Add helpful context message for certain error types
        std::string context_message = get_error_context_message(diagnostic);
        if (!context_message.empty())
        {
            oss << " " << context_message;
        }

        return oss.str();
    }

    size_t DiagnosticFormatter::determine_token_length(const std::string &source_line, size_t position, const Diagnostic &diagnostic) const
    {
        // If we already have a multi-character range from the diagnostic system, use it
        if (!diagnostic.range().is_single_location())
        {
            return diagnostic.range().length();
        }

        // Check if position is valid
        if (position >= source_line.length())
        {
            return 1;
        }

        // Note: The position parameter here should already be the adjusted position from format_caret_line

        // Get the character at the error position
        char ch = source_line[position];

        // For string literals at the exact position, find the matching quote
        if (ch == '"' || ch == '\'')
        {
            char quote_char = ch;
            size_t length = 1; // Start with opening quote

            for (size_t i = position + 1; i < source_line.length(); ++i)
            {
                length++;
                if (source_line[i] == quote_char && (i == 0 || source_line[i - 1] != '\\'))
                {
                    break; // Found closing quote
                }
            }
            return length;
        }

        // For numbers, find the complete number
        if (std::isdigit(ch) || ch == '.' || ch == '-')
        {
            size_t start = position;
            size_t end = position;

            // Find start of number (handle negative numbers)
            while (start > 0 && (std::isdigit(source_line[start - 1]) || source_line[start - 1] == '.' ||
                                 (start == position && source_line[start - 1] == '-')))
            {
                start--;
            }

            // Find end of number
            while (end < source_line.length() && (std::isdigit(source_line[end]) || source_line[end] == '.'))
            {
                end++;
            }

            return end - start;
        }

        // For identifiers/keywords, find the complete word
        if (std::isalpha(ch) || ch == '_')
        {
            size_t start = position;
            size_t end = position;

            // Find start of identifier
            while (start > 0 && (std::isalnum(source_line[start - 1]) || source_line[start - 1] == '_'))
            {
                start--;
            }

            // Find end of identifier
            while (end < source_line.length() && (std::isalnum(source_line[end]) || source_line[end] == '_'))
            {
                end++;
            }

            size_t length = end - start;
            // Limit extremely long identifiers to reasonable length for display
            return std::min(length, static_cast<size_t>(15));
        }

        // For operators and single characters, use appropriate length
        if (position + 1 < source_line.length())
        {
            // Check for multi-character operators
            std::string two_char = source_line.substr(position, 2);
            if (two_char == "==" || two_char == "!=" || two_char == "<=" || two_char == ">=" ||
                two_char == "&&" || two_char == "||" || two_char == "++" || two_char == "--" ||
                two_char == "+=" || two_char == "-=" || two_char == "*=" || two_char == "/=")
            {
                return 2;
            }
        }

        // Default to single character
        return 1;
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

    std::string DiagnosticFormatter::get_error_context_message(const Diagnostic &diagnostic) const
    {
        // The new ErrorCode system provides rich error information through the ErrorRegistry
        // Context is now provided via the diagnostic's suggestion and explanation
        if (!diagnostic.suggestion().empty()) {
            return "help: " + diagnostic.suggestion();
        }
        return "";
    }



    // ================================================================
    // DiagnosticManager Implementation
    // ================================================================

    DiagnosticManager::DiagnosticManager()
        : _formatter(_source_manager), _error_count(0), _warning_count(0), _note_count(0),
          _errors_as_warnings(false), _warnings_as_errors(false), _max_errors(100),
          _use_advanced_formatting(true)
    {
        // Initialize advanced formatter with clean ASCII by default for universal compatibility
        _advanced_formatter = std::make_unique<AdvancedDiagnosticFormatter>(&_source_manager, true, false, 80);
    }

    DiagnosticManager::~DiagnosticManager() = default;

    void DiagnosticManager::add_source_file(const std::string &filename, std::shared_ptr<File> file)
    {
        _source_manager.add_file(filename, file);
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



    // ================================================================
    // Enhanced diagnostic reporting with error codes
    // ================================================================

    void DiagnosticManager::report(ErrorCode error_code, const std::string &message, const SourceRange &range,
                                   const std::string &filename)
    {
        // Determine severity and category from error code
        ErrorRegistry::initialize();
        const ErrorInfo& error_info = ErrorRegistry::get_error_info(error_code);
        
        DiagnosticSeverity severity = error_info.is_warning ? DiagnosticSeverity::Warning : DiagnosticSeverity::Error;
        
        // Determine category based on error code range
        DiagnosticCategory category = DiagnosticCategory::System;
        uint32_t code_num = static_cast<uint32_t>(error_code);
        
        if (code_num < 100) category = DiagnosticCategory::Lexer;
        else if (code_num < 200) category = DiagnosticCategory::Parser;
        else if (code_num < 600) category = DiagnosticCategory::Semantic;
        else if (code_num < 700) category = DiagnosticCategory::CodeGen;
        else category = DiagnosticCategory::System;
        
        report(error_code, severity, category, message, range, filename);
    }
    
    void DiagnosticManager::report(ErrorCode error_code, DiagnosticSeverity severity, DiagnosticCategory category,
                                   const std::string &message, const SourceRange &range,
                                   const std::string &filename)
    {
        ErrorRegistry::initialize();
        
        // Create enhanced diagnostic with error code
        Diagnostic diagnostic(error_code, severity, category, message, range, filename);
        
        // Auto-add helpful suggestions and explanations based on error code
        const ErrorInfo& error_info = ErrorRegistry::get_error_info(error_code);
        if (!error_info.explanation.empty()) {
            diagnostic.add_note(error_info.explanation);
        }
        if (!error_info.suggestion.empty()) {
            diagnostic.add_note("Help: " + error_info.suggestion);
        }
        
        report(diagnostic);
    }

    void DiagnosticManager::report_error(ErrorCode error_code, const std::string &message,
                                         const SourceRange &range, const std::string &filename)
    {
        ErrorRegistry::initialize();
        
        // Determine category based on error code
        DiagnosticCategory category = DiagnosticCategory::System;
        uint32_t code_num = static_cast<uint32_t>(error_code);
        
        if (code_num < 100) category = DiagnosticCategory::Lexer;
        else if (code_num < 200) category = DiagnosticCategory::Parser;
        else if (code_num < 600) category = DiagnosticCategory::Semantic;
        else if (code_num < 700) category = DiagnosticCategory::CodeGen;
        
        report(error_code, DiagnosticSeverity::Error, category, message, range, filename);
    }

    void DiagnosticManager::report_warning(ErrorCode error_code, const std::string &message,
                                           const SourceRange &range, const std::string &filename)
    {
        ErrorRegistry::initialize();
        
        // Determine category based on error code
        DiagnosticCategory category = DiagnosticCategory::System;
        uint32_t code_num = static_cast<uint32_t>(error_code);
        
        if (code_num >= 10000) { // Warning code
            code_num -= 10000;
            if (code_num < 100) category = DiagnosticCategory::Lexer;
            else if (code_num < 200) category = DiagnosticCategory::Parser;
            else if (code_num < 600) category = DiagnosticCategory::Semantic;
            else if (code_num < 700) category = DiagnosticCategory::CodeGen;
        }
        
        report(error_code, DiagnosticSeverity::Warning, category, message, range, filename);
    }

    void DiagnosticManager::report_note(ErrorCode error_code, const std::string &message,
                                        const SourceRange &range, const std::string &filename)
    {
        ErrorRegistry::initialize();
        
        // Determine category based on error code
        DiagnosticCategory category = DiagnosticCategory::System;
        uint32_t code_num = static_cast<uint32_t>(error_code);
        
        if (code_num < 100) category = DiagnosticCategory::Lexer;
        else if (code_num < 200) category = DiagnosticCategory::Parser;
        else if (code_num < 600) category = DiagnosticCategory::Semantic;
        else if (code_num < 700) category = DiagnosticCategory::CodeGen;
        
        report(error_code, DiagnosticSeverity::Note, category, message, range, filename);
    }

    void DiagnosticManager::print_all(std::ostream &os) const
    {
        for (const auto &diagnostic : _diagnostics)
        {
            if (_use_advanced_formatting && _advanced_formatter)
            {
                // Use the sophisticated Rust-style formatter
                std::string formatted = _advanced_formatter->format_diagnostic(diagnostic);
                os << formatted;
            }
            else
            {
                // Fall back to basic formatter
                _formatter.print(diagnostic, os);
            }
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
        
        // Also apply to advanced formatter if available
        if (_advanced_formatter) {
            set_advanced_formatter_options(use_colors, true, 80);
        }
    }

    void DiagnosticManager::enable_advanced_formatting(bool enable)
    {
        _use_advanced_formatting = enable;
        if (enable && !_advanced_formatter) {
            _advanced_formatter = std::make_unique<AdvancedDiagnosticFormatter>(&_source_manager, true, true, 80);
        }
    }

    void DiagnosticManager::set_advanced_formatter_options(bool use_colors, bool use_unicode, size_t terminal_width)
    {
        if (_advanced_formatter) {
            // For now, we'll recreate the formatter with new options
            // In a more sophisticated implementation, we'd add setter methods
            _advanced_formatter = std::make_unique<AdvancedDiagnosticFormatter>(&_source_manager, use_colors, use_unicode, terminal_width);
        }
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

        report_error(ErrorCode::E0200_TYPE_MISMATCH,
                     message + ": expected '" + expected_type + "', but got '" + actual_type + "'",
                     range, filename);
    }

    void DiagnosticManager::report_undefined_symbol(const SourceRange &range, const std::string &filename,
                                                    const std::string &symbol_name, const std::string &context)
    {
        std::string message = "Undefined symbol '" + symbol_name + "'";
        if (!context.empty())
        {
            message += " in " + context;
        }

        report_error(ErrorCode::E0201_UNDEFINED_VARIABLE, message, range, filename);
    }

    void DiagnosticManager::report_redefined_symbol(const SourceRange &range, const std::string &filename,
                                                    const std::string &symbol_name, const SourceRange &previous_location)
    {
        auto diagnostic = Diagnostic(ErrorCode::E0205_REDEFINED_SYMBOL, DiagnosticSeverity::Error,
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
        std::string message = "Invalid operation '" + operation + "' for type '" + type + "'";
        if (!context.empty())
        {
            message += " in " + context;
        }

        report_error(ErrorCode::E0209_INVALID_OPERATION, message, range, filename);
    }

    void DiagnosticManager::report_argument_mismatch(const SourceRange &range, const std::string &filename,
                                                     const std::string &function_name, size_t expected_count,
                                                     size_t actual_count)
    {
        ErrorCode error_code = (actual_count > expected_count) ? ErrorCode::E0215_TOO_MANY_ARGS : ErrorCode::E0216_TOO_FEW_ARGS;

        std::string message = "Function '" + function_name + "' expects " + std::to_string(expected_count) + 
                              " argument" + (expected_count == 1 ? "" : "s") + ", but " + std::to_string(actual_count) + 
                              " " + (actual_count == 1 ? "was" : "were") + " provided";

        report_error(error_code, message, range, filename);
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
    // Enhanced specific error reporting methods
    // ================================================================

    void DiagnosticManager::report_invalid_dereference(const SourceRange &range, const std::string &filename,
                                                       const std::string &type_name)
    {
        std::string message = "Cannot dereference value of type '" + type_name + "'";
        report_error(ErrorCode::E0222_INVALID_DEREF, message, range, filename);
    }

    void DiagnosticManager::report_invalid_address_of(const SourceRange &range, const std::string &filename,
                                                      const std::string &expression)
    {
        std::string message = "Cannot take address of " + expression;
        report_error(ErrorCode::E0223_INVALID_ADDRESS_OF, message, range, filename);
    }

    void DiagnosticManager::report_undefined_field(const SourceRange &range, const std::string &filename,
                                                   const std::string &field_name, const std::string &type_name)
    {
        std::string message = "No field '" + field_name + "' on type '" + type_name + "'";
        report_error(ErrorCode::E0204_UNDEFINED_FIELD, message, range, filename);
    }

    void DiagnosticManager::report_non_callable_type(const SourceRange &range, const std::string &filename,
                                                     const std::string &type_name)
    {
        std::string message = "Cannot call value of type '" + type_name + "'";
        report_error(ErrorCode::E0213_NON_CALLABLE, message, range, filename);
    }

    // Enhanced diagnostic builder methods
    Diagnostic& DiagnosticManager::create_error(ErrorCode error_code, const SourceRange& range, const std::string& filename)
    {
        const ErrorInfo& error_info = ErrorRegistry::get_error_info(error_code);
        
        Diagnostic diagnostic(error_code, DiagnosticSeverity::Error, DiagnosticCategory::Semantic,
                             error_info.short_description, range, filename);
        
        _diagnostics.push_back(std::move(diagnostic));
        update_statistics(DiagnosticSeverity::Error);
        
        return _diagnostics.back();
    }

    Diagnostic& DiagnosticManager::create_warning(ErrorCode error_code, const SourceRange& range, const std::string& filename)
    {
        const ErrorInfo& error_info = ErrorRegistry::get_error_info(error_code);
        
        Diagnostic diagnostic(error_code, DiagnosticSeverity::Warning, DiagnosticCategory::Semantic,
                             error_info.short_description, range, filename);
        
        _diagnostics.push_back(std::move(diagnostic));
        update_statistics(DiagnosticSeverity::Warning);
        
        return _diagnostics.back();
    }

    Diagnostic& DiagnosticManager::create_note(ErrorCode error_code, const SourceRange& range, const std::string& filename)
    {
        const ErrorInfo& error_info = ErrorRegistry::get_error_info(error_code);
        
        Diagnostic diagnostic(error_code, DiagnosticSeverity::Note, DiagnosticCategory::Semantic,
                             error_info.short_description, range, filename);
        
        _diagnostics.push_back(std::move(diagnostic));
        update_statistics(DiagnosticSeverity::Note);
        
        return _diagnostics.back();
    }

    void DiagnosticManager::emit(const Diagnostic& diagnostic)
    {
        // This method is for when diagnostics are built externally and need to be added
        // For now, we just add it to our list since the create_* methods already add them
        // In a more sophisticated implementation, this could handle deferred emission
    }

    // Enhanced diagnostic builders for message-first approach
    Diagnostic& DiagnosticManager::create_error(ErrorCode error_code, const std::string& message)
    {
        const ErrorInfo& error_info = ErrorRegistry::get_error_info(error_code);
        
        Diagnostic diagnostic(error_code, DiagnosticSeverity::Error, DiagnosticCategory::Semantic,
                             message.empty() ? error_info.short_description : message, 
                             SourceRange(), "");
        
        _diagnostics.push_back(std::move(diagnostic));
        update_statistics(DiagnosticSeverity::Error);
        
        return _diagnostics.back();
    }

    Diagnostic& DiagnosticManager::create_warning(ErrorCode error_code, const std::string& message)
    {
        const ErrorInfo& error_info = ErrorRegistry::get_error_info(error_code);
        
        Diagnostic diagnostic(error_code, DiagnosticSeverity::Warning, DiagnosticCategory::Semantic,
                             message.empty() ? error_info.short_description : message, 
                             SourceRange(), "");
        
        _diagnostics.push_back(std::move(diagnostic));
        update_statistics(DiagnosticSeverity::Warning);
        
        return _diagnostics.back();
    }

    Diagnostic& DiagnosticManager::create_note(ErrorCode error_code, const std::string& message)
    {
        const ErrorInfo& error_info = ErrorRegistry::get_error_info(error_code);
        
        Diagnostic diagnostic(error_code, DiagnosticSeverity::Note, DiagnosticCategory::Semantic,
                             message.empty() ? error_info.short_description : message, 
                             SourceRange(), "");
        
        _diagnostics.push_back(std::move(diagnostic));
        update_statistics(DiagnosticSeverity::Note);
        
        return _diagnostics.back();
    }

    // Enhanced type mismatch with sophisticated error reporting
    void DiagnosticManager::report_enhanced_type_mismatch(const SourceSpan& value_span,
                                                          const SourceSpan& type_annotation_span,
                                                          const std::string& expected_type,
                                                          const std::string& actual_type,
                                                          const std::string& filename)
    {
        // Create sophisticated type mismatch diagnostic
        auto& diagnostic = create_error(ErrorCode::E0200_TYPE_MISMATCH, 
                                       "type mismatch in assignment");
        
        // Create primary span with label
        SourceSpan primary_value_span = value_span;
        primary_value_span.set_label("expected `" + expected_type + "`, found `" + actual_type + "`");
        diagnostic.with_primary_span(primary_value_span);
        
        // Add secondary span for type annotation if different from value location
        if (!type_annotation_span.is_single_location() && 
            !value_span.overlaps_with(type_annotation_span)) {
            diagnostic.add_secondary_span(type_annotation_span, "expected due to this type annotation");
        }
        
        // Add contextual suggestions based on the types
        if (expected_type == "string" && actual_type == "int") {
            diagnostic.suggest_replacement(
                type_annotation_span,
                "const x = \"hello\";", // Remove type annotation
                "if you meant to create a string variable, remove the type annotation"
            );
        } else if (expected_type == "int" && actual_type == "string") {
            // Suggest string parsing
            diagnostic.suggest_replacement(
                value_span,
                "\"hello\".parse_int()",
                "if you want to convert string to integer, try parsing"
            );
            diagnostic.suggest_replacement(
                type_annotation_span,
                "const x = \"hello\";",
                "if you meant to create a string variable, remove the type annotation"
            );
        }
        
        // Add helpful note
        diagnostic.add_help("string literals cannot be implicitly converted to integers");
        
        emit(diagnostic);
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