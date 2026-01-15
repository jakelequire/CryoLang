#include "GDM/GDM.hpp"
#include "GDM/DiagnosticFormatter.hpp"
#include "GDM/ErrorAnalysis.hpp"
#include "AST/Type.hpp"
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

    bool SourceSpan::overlaps_with(const SourceSpan &other) const
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

    SourceSpan SourceSpan::merge(const std::vector<SourceSpan> &spans)
    {
        if (spans.empty())
            return SourceSpan();

        SourceSpan result = spans[0];
        for (const auto &span : spans)
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

    SourceSpan SourceSpan::from_to(const SourceSpan &from, const SourceSpan &to)
    {
        return SourceSpan(from._start, to._end, from._filename, true);
    }

    // ================================================================
    // MultiSpan Implementation
    // ================================================================

    MultiSpan::MultiSpan(const SourceSpan &primary_span)
    {
        _primary_spans.push_back(primary_span);
    }

    MultiSpan::MultiSpan(const SourceRange &range, const std::string &filename)
    {
        _primary_spans.emplace_back(range, filename);
    }

    void MultiSpan::add_primary_span(const SourceSpan &span)
    {
        _primary_spans.push_back(span);
    }

    void MultiSpan::add_primary_span(const SourceLocation &start, const SourceLocation &end,
                                     const std::string &filename)
    {
        _primary_spans.emplace_back(start, end, filename, true);
    }

    void MultiSpan::add_secondary_span(const SourceSpan &span)
    {
        SourceSpan secondary_span = span;
        secondary_span.set_primary(false);
        _secondary_spans.push_back(secondary_span);
    }

    void MultiSpan::add_secondary_span(const SourceLocation &start, const SourceLocation &end,
                                       const std::string &filename, const std::string &label)
    {
        SourceSpan span(start, end, filename, false);
        if (!label.empty())
        {
            span.set_label(label);
        }
        _secondary_spans.push_back(span);
    }

    void MultiSpan::add_span_label(const SourceSpan &span, const std::string &label)
    {
        SourceSpan labeled_span = span;
        labeled_span.set_label(label);
        labeled_span.set_primary(false);
        _secondary_spans.push_back(labeled_span);
    }

    void MultiSpan::add_span_label(const SourceLocation &start, const SourceLocation &end,
                                   const std::string &label, const std::string &filename)
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
        for (const auto &span : all_spans())
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

        if (is_simple())
        {
            spans.push_back(simple().span);
        }
        else if (is_multipart())
        {
            const auto &parts = multipart().parts;
            for (const auto &part : parts)
            {
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
        const ErrorInfo &error_info = ErrorRegistry::get_error_info(_error_code);
        _suggestion = error_info.suggestion;
        _explanation = error_info.explanation;
    }

    Diagnostic::~Diagnostic() = default;

    Diagnostic::Diagnostic(Diagnostic &&other) noexcept = default;

    Diagnostic &Diagnostic::operator=(Diagnostic &&other) noexcept = default;

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
    Diagnostic &Diagnostic::with_primary_span(const SourceSpan &span)
    {
        _multi_span.add_primary_span(span);
        return *this;
    }

    Diagnostic &Diagnostic::add_secondary_span(const SourceSpan &span, const std::string &label)
    {
        if (label.empty())
        {
            _multi_span.add_secondary_span(span);
        }
        else
        {
            _multi_span.add_secondary_span(span.start(), span.end(), span.filename(), label);
        }
        return *this;
    }

    Diagnostic &Diagnostic::add_suggestion(const CodeSuggestion &suggestion)
    {
        _code_suggestions.push_back(suggestion);
        return *this;
    }

    Diagnostic &Diagnostic::add_help(const std::string &help_message)
    {
        _help_messages.push_back(help_message);
        return *this;
    }

    Diagnostic &Diagnostic::suggest_replacement(const SourceSpan &span, const std::string &replacement,
                                                const std::string &message, SuggestionApplicability applicability)
    {
        _code_suggestions.emplace_back(message, span, replacement, applicability);
        return *this;
    }

    Diagnostic &Diagnostic::suggest_insertion(const SourceLocation &location, const std::string &insertion,
                                              const std::string &message)
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

    void SourceManager::clear()
    {
        _files.clear();
        _file_lines.clear();
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
        std::string file_content = std::string(file->content());
        std::istringstream content_stream(file_content);
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

    SourceManager::SourceSnippet SourceManager::extract_snippet(const SourceSpan &span, size_t context_lines) const
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

        const auto &file_lines = file_lines_it->second;
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

    SourceManager::SourceSnippet SourceManager::extract_snippet(const MultiSpan &multi_span, size_t context_lines) const
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

        const auto &file_lines = file_lines_it->second;
        auto all_spans = multi_span.all_spans();

        // Filter spans for this file only
        std::vector<SourceSpan> file_spans;
        for (const auto &span : all_spans)
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

    SourceManager::SourceSnippet SourceManager::extract_smart_context(const SourceSpan &span) const
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

    SourceManager::SourceSnippet SourceManager::extract_smart_context(const MultiSpan &multi_span) const
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
        const MultiSpan &multi_span, size_t context_lines) const
    {
        std::vector<SourceSnippet> snippets;
        auto affected_files = multi_span.affected_files();

        for (const auto &filename : affected_files)
        {
            // Create a temporary MultiSpan with only spans from this file
            MultiSpan file_spans;
            for (const auto &span : multi_span.all_spans())
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

    bool SourceManager::is_valid_span(const SourceSpan &span) const
    {
        return !span.filename().empty() &&
               is_valid_location(span.filename(), span.start()) &&
               is_valid_location(span.filename(), span.end());
    }

    size_t SourceManager::get_line_count(const std::string &filename) const
    {
        ensure_file_lines_cached(filename);
        auto it = _file_lines.find(filename);
        return (it != _file_lines.end()) ? it->second.size() : 0;
    }

    std::string SourceManager::get_line_text(const std::string &filename, size_t line_number) const
    {
        return get_source_line(filename, line_number);
    }

    size_t SourceManager::calculate_line_number_width(size_t max_line_number) const
    {
        if (max_line_number == 0)
            return 1;

        size_t width = 0;
        while (max_line_number > 0)
        {
            width++;
            max_line_number /= 10;
        }
        return width;
    }

    std::vector<SourceSpan> SourceManager::get_spans_on_line(const std::vector<SourceSpan> &spans,
                                                             size_t line_number) const
    {
        std::vector<SourceSpan> spans_on_line;
        for (const auto &span : spans)
        {
            auto affected_lines = span.affected_lines();
            if (std::find(affected_lines.begin(), affected_lines.end(), line_number) != affected_lines.end())
            {
                spans_on_line.push_back(span);
            }
        }
        return spans_on_line;
    }

    std::pair<size_t, size_t> SourceManager::calculate_context_range(const SourceSpan &span,
                                                                     size_t context_lines,
                                                                     size_t file_line_count) const
    {
        size_t start_line = (span.start().line() > context_lines) ? span.start().line() - context_lines : 1;
        size_t end_line = std::min(span.end().line() + context_lines, file_line_count);

        return {start_line, end_line};
    }

    std::pair<size_t, size_t> SourceManager::calculate_multi_span_range(const std::vector<SourceSpan> &spans,
                                                                        size_t context_lines,
                                                                        size_t file_line_count) const
    {
        if (spans.empty())
        {
            return {1, 1};
        }

        size_t min_line = spans[0].start().line();
        size_t max_line = spans[0].end().line();

        for (const auto &span : spans)
        {
            min_line = std::min(min_line, span.start().line());
            max_line = std::max(max_line, span.end().line());
        }

        size_t start_line = (min_line > context_lines) ? min_line - context_lines : 1;
        size_t end_line = std::min(max_line + context_lines, file_line_count);

        return {start_line, end_line};
    }

    // ================================================================
    // DiagnosticManager Implementation
    // ================================================================

    // ================================================================
    // BasicTypeMismatchContext Implementation
    // ================================================================

    std::string BasicTypeMismatchContext::generate_inline_label() const
    {
        if (!expected_type || !actual_type)
        {
            return "type mismatch";
        }

        std::string expected_str = expected_type->display_name();
        std::string actual_str = actual_type->display_name();

        return "expected `" + expected_str + "`, found `" + actual_str + "`";
    }

    std::vector<std::string> BasicTypeMismatchContext::generate_help_messages() const
    {
        std::vector<std::string> help_messages;

        if (!expected_type || !actual_type)
        {
            help_messages.push_back("ensure the types are compatible or add an explicit cast if appropriate");
            return help_messages;
        }

        std::string expected_str = expected_type->display_name();
        std::string actual_str = actual_type->display_name();

        // Context-specific suggestions based on type combination
        if (context_description.find("assignment") != std::string::npos ||
            context_description.find("initialization") != std::string::npos)
        {

            if (expected_str == "int" && actual_str == "string")
            {
                help_messages.push_back("if you want to convert string to integer, try parsing");
                help_messages.push_back("if you meant to create a string variable, remove the type annotation");
                help_messages.push_back("string literals cannot be implicitly converted to integers");
            }
            else if (expected_str == "string" && actual_str == "int")
            {
                help_messages.push_back("if you want to convert integer to string, use toString()");
                help_messages.push_back("if you meant to create an integer variable, change the type annotation");
            }
            else if (expected_str == "boolean" && actual_str == "int")
            {
                help_messages.push_back("boolean and integer types are not compatible");
                help_messages.push_back("use explicit comparison like `value != 0` to convert to boolean");
            }
            else if (expected_str == "float" && actual_str == "int")
            {
                help_messages.push_back("consider using explicit cast or change variable type to int");
            }
            else if (expected_str.find("*") != std::string::npos || actual_str.find("*") != std::string::npos)
            {
                help_messages.push_back("pointer and value types are not directly compatible");
                help_messages.push_back("use address-of (&) or dereference (*) operators as appropriate");
            }
            else
            {
                help_messages.push_back("ensure the types are compatible or add an explicit cast if appropriate");
            }
        }
        else if (context_description.find("arithmetic") != std::string::npos)
        {
            help_messages.push_back("arithmetic operations require compatible types");
            help_messages.push_back("consider converting one operand to match the other's type");
        }
        else if (context_description.find("function") != std::string::npos)
        {
            help_messages.push_back("function argument type must match parameter type");
            help_messages.push_back("check the function signature or convert the argument");
        }
        else
        {
            help_messages.push_back("ensure the types are compatible or add an explicit cast if appropriate");
        }

        return help_messages;
    }

    bool BasicTypeMismatchContext::can_suggest_parsing() const
    {
        if (!expected_type || !actual_type)
            return false;
        return (expected_type->display_name() == "int" && actual_type->display_name() == "string");
    }

    bool BasicTypeMismatchContext::can_suggest_cast() const
    {
        if (!expected_type || !actual_type)
            return false;
        // Most type mismatches can potentially be resolved with casts
        return true;
    }

    bool BasicTypeMismatchContext::are_related_types() const
    {
        if (!expected_type || !actual_type)
            return false;

        std::string expected_str = expected_type->display_name();
        std::string actual_str = actual_type->display_name();

        // Check for related numeric types
        std::vector<std::string> numeric_types = {"int", "i32", "i64", "u32", "u64", "float", "f32", "f64", "double"};
        bool expected_numeric = std::find(numeric_types.begin(), numeric_types.end(), expected_str) != numeric_types.end();
        bool actual_numeric = std::find(numeric_types.begin(), numeric_types.end(), actual_str) != numeric_types.end();

        return expected_numeric && actual_numeric;
    }

    std::string BasicTypeMismatchContext::generate_primary_message() const
    {
        if (!expected_type || !actual_type)
        {
            return "type mismatch in " + context_description;
        }

        return "type mismatch in " + context_description + ": expected `" +
               expected_type->display_name() + "`, found `" + actual_type->display_name() + "`";
    }

    std::vector<std::string> BasicTypeMismatchContext::generate_suggestions() const
    {
        std::vector<std::string> suggestions;

        if (!expected_type || !actual_type)
        {
            return suggestions;
        }

        std::string expected_name = expected_type->display_name();
        std::string actual_name = actual_type->display_name();

        // Basic conversion suggestions
        if (expected_name == "int" && actual_name == "string")
        {
            suggestions.push_back("try parsing with `int.parse(value)`");
            suggestions.push_back("if you meant to create a string variable, remove the type annotation");
        }
        else if (expected_name == "string" && actual_name == "int")
        {
            suggestions.push_back("try `value.to_string()` to convert the integer");
            suggestions.push_back("use string interpolation: `\"${value}\"`");
        }
        else if (expected_name == "boolean" && (actual_name == "int" || actual_name == "float"))
        {
            suggestions.push_back("use comparison operators like `value != 0` or `value > 0`");
        }
        else if (expected_name.find("*") != std::string::npos && actual_name.find("*") == std::string::npos)
        {
            suggestions.push_back("use address-of operator `&` to get a pointer");
        }
        else if (expected_name.find("*") == std::string::npos && actual_name.find("*") != std::string::npos)
        {
            suggestions.push_back("use dereference operator `*` to get the value");
        }

        return suggestions;
    }

    std::vector<CodeSuggestion> BasicTypeMismatchContext::generate_code_suggestions(const SourceSpan &span) const
    {
        std::vector<CodeSuggestion> suggestions;

        if (!expected_type || !actual_type)
        {
            return suggestions;
        }

        std::string expected_name = expected_type->display_name();
        std::string actual_name = actual_type->display_name();

        if (expected_name == "int" && actual_name == "string")
        {
            suggestions.emplace_back(
                "parse the string to integer",
                span,
                "int.parse(/* value */)",
                SuggestionApplicability::MaybeIncorrect);
        }

        return suggestions;
    }

    // ================================================================
    // Diagnostic Implementation Extensions
    // ================================================================

    Diagnostic &Diagnostic::with_payload(std::unique_ptr<DiagnosticPayload> payload)
    {
        _payload = std::move(payload);
        return *this;
    }

    // ================================================================
    // Enhanced diagnostic reporting with error codes
    // ================================================================
    // ================================================================
    // DiagnosticManager Implementation
    // ================================================================

    DiagnosticManager::DiagnosticManager()
        : _error_count(0), _warning_count(0), _note_count(0),
          _errors_as_warnings(false), _warnings_as_errors(false), _max_errors(100),
          _show_stdlib_diagnostics(false)
    {
        // Initialize advanced formatter with clean ASCII by default for universal compatibility
        _formatter = std::make_unique<DiagnosticFormatter>(&_source_manager, true, false, 80);
    }

    DiagnosticManager::~DiagnosticManager() = default;

    void DiagnosticManager::add_source_file(const std::string &filename, std::shared_ptr<File> file)
    {
        _source_manager.add_file(filename, file);
    }

    void DiagnosticManager::report(Diagnostic diagnostic)
    {
        _diagnostics.emplace_back(std::move(diagnostic));
        update_statistics(diagnostic.severity());

        // Print immediately in debug mode or for fatal errors
        if (diagnostic.is_fatal())
        {
            std::string formatted = _formatter->format_diagnostic(diagnostic);
            std::cerr << formatted;
        }
    }

    // ================================================================
    // UNIFIED ERROR REPORTING IMPLEMENTATION
    // ================================================================

    /**
     * @brief Determine category from ErrorCode automatically
     */
    DiagnosticCategory DiagnosticManager::determine_category_from_error_code(ErrorCode error_code) const
    {
        uint32_t code_num = static_cast<uint32_t>(error_code);

        if (code_num < 100)
            return DiagnosticCategory::Lexer;
        else if (code_num < 200)
            return DiagnosticCategory::Parser;
        else if (code_num < 600)
            return DiagnosticCategory::Semantic;
        else if (code_num < 700)
            return DiagnosticCategory::CodeGen;
        else
            return DiagnosticCategory::System;
    }

    /**
     * @brief Determine severity from ErrorCode automatically
     */
    DiagnosticSeverity DiagnosticManager::determine_severity_from_error_code(ErrorCode error_code) const
    {
        ErrorRegistry::initialize();
        const ErrorInfo &error_info = ErrorRegistry::get_error_info(error_code);
        return error_info.is_warning ? DiagnosticSeverity::Warning : DiagnosticSeverity::Error;
    }

    Diagnostic &DiagnosticManager::create_diagnostic(ErrorCode error_code,
                                                     const SourceRange &range,
                                                     const std::string &filename,
                                                     const std::string &message,
                                                     const std::any &context)
    {
        // Suppress errors from problematic external modules (like runtime dependencies)
        // that are being re-parsed without proper symbol context
        if (filename.find("std::Runtime") != std::string::npos ||
            filename.find("runtime") != std::string::npos)
        {
            // Return a dummy diagnostic that won't be displayed
            static Diagnostic dummy_diagnostic(ErrorCode::E0101_UNEXPECTED_TOKEN, DiagnosticSeverity::Error,
                                               DiagnosticCategory::CodeGen, "runtime error suppressed",
                                               SourceRange{}, "");
            return dummy_diagnostic;
        }

        ErrorRegistry::initialize();
        const ErrorInfo &error_info = ErrorRegistry::get_error_info(error_code);

        // Determine category and severity automatically from ErrorCode
        DiagnosticCategory category = determine_category_from_error_code(error_code);
        DiagnosticSeverity severity = determine_severity_from_error_code(error_code);

        // Use custom message or default from ErrorInfo
        std::string final_message = message.empty() ? error_info.short_description : message;

        // Create diagnostic
        Diagnostic diagnostic(error_code, severity, category, final_message, range, filename);

        // Auto-add explanations and suggestions from ErrorInfo - TEMPORARILY DISABLED
        /*
        if (!error_info.explanation.empty())
        {
            diagnostic.add_note(error_info.explanation);
        }
        if (!error_info.suggestion.empty())
        {
            diagnostic.add_note("Help: " + error_info.suggestion);
        }
        */

        // Create appropriate payload based on ErrorCode
        if (DiagnosticPayload::is_type_mismatch_error(error_code) ||
            DiagnosticPayload::is_undefined_symbol_error(error_code) ||
            DiagnosticPayload::is_invalid_operation_error(error_code) ||
            DiagnosticPayload::is_argument_mismatch_error(error_code))
        {
            auto payload = DiagnosticPayload::create_from_error_code(error_code, context);
            diagnostic.with_payload(std::make_unique<DiagnosticPayload>(std::move(payload)));
        }

        _diagnostics.push_back(std::move(diagnostic));
        update_statistics(severity);

        return _diagnostics.back();
    }

    Diagnostic &DiagnosticManager::create_error(ErrorCode error_code,
                                                const SourceRange &range,
                                                const std::string &filename)
    {
        return create_diagnostic(error_code, range, filename);
    }

    Diagnostic &DiagnosticManager::create_error(ErrorCode error_code,
                                                const SourceRange &range,
                                                const std::string &filename,
                                                const std::string &custom_message)
    {
        return create_diagnostic(error_code, range, filename, custom_message);
    }

    void DiagnosticManager::print_all(std::ostream &os) const
    {
        size_t error_number = 0;
        for (const auto &diagnostic : _diagnostics)
        {
            // Filter out diagnostics from stdlib files unless explicitly enabled
            if (!_show_stdlib_diagnostics && is_stdlib_diagnostic(diagnostic))
            {
                continue; // Skip stdlib diagnostics - users don't need to see these
            }

            // Always use the sophisticated Rust-style formatter
            if (_formatter)
            {
                // Only count actual errors (not warnings or notes) for numbering
                if (diagnostic.severity() == DiagnosticSeverity::Error ||
                    diagnostic.severity() == DiagnosticSeverity::Fatal)
                {
                    error_number++;
                }

                std::string formatted = _formatter->format_diagnostic(diagnostic, error_number);
                os << formatted;
            }
        }
    }

    void DiagnosticManager::print_summary(std::ostream &os) const
    {
        // Use user-only counts (unless showing stdlib diagnostics)
        size_t user_errors = _show_stdlib_diagnostics ? _error_count : user_error_count();
        size_t user_warnings = _show_stdlib_diagnostics ? _warning_count : user_warning_count();
        size_t user_total = _show_stdlib_diagnostics ? _diagnostics.size() : user_total_count();

        if (user_total == 0)
        {
            os << "No diagnostics.\n";
            return;
        }

        os << "Compilation summary: ";
        if (user_errors > 0)
        {
            os << user_errors << " error" << (user_errors == 1 ? "" : "s");
        }
        if (user_warnings > 0)
        {
            if (user_errors > 0)
                os << ", ";
            os << user_warnings << " warning" << (user_warnings == 1 ? "" : "s");
        }
        if (_note_count > 0) // Notes are generally less critical, keep original count
        {
            if (user_errors > 0 || user_warnings > 0)
                os << ", ";
            os << _note_count << " note" << (_note_count == 1 ? "" : "s");
        }
        os << ".\n";
    }

    void DiagnosticManager::set_formatter_options(bool use_colors, bool use_unicode, size_t terminal_width)
    {
        // Recreate the formatter with new options
        _formatter = std::make_unique<DiagnosticFormatter>(&_source_manager, use_colors, use_unicode, terminal_width);
    }

    void DiagnosticManager::clear()
    {
        _diagnostics.clear();
        _error_count = 0;
        _warning_count = 0;
        _note_count = 0;

        // Clear source manager to prevent stale file references
        _source_manager.clear();
    }

    bool DiagnosticManager::should_continue_compilation() const
    {
        return _error_count < _max_errors;
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

    std::vector<DiagnosticManager::LSPDiagnostic> DiagnosticManager::get_lsp_diagnostics() const
    {
        std::vector<LSPDiagnostic> lsp_diagnostics;

        for (const auto &diagnostic : _diagnostics)
        {
            LSPDiagnostic lsp_diag;
            lsp_diag.message = diagnostic.message();

            // Convert severity to LSP format
            switch (diagnostic.severity())
            {
            case DiagnosticSeverity::Error:
            case DiagnosticSeverity::Fatal:
                lsp_diag.severity = "error";
                break;
            case DiagnosticSeverity::Warning:
                lsp_diag.severity = "warning";
                break;
            case DiagnosticSeverity::Note:
                lsp_diag.severity = "info";
                break;
            }

            // Extract position information
            if (diagnostic.range().is_valid())
            {
                lsp_diag.line = diagnostic.range().start.line();
                lsp_diag.column = diagnostic.range().start.column();
                lsp_diag.end_line = diagnostic.range().end.line();
                lsp_diag.end_column = diagnostic.range().end.column();
            }
            else
            {
                lsp_diag.line = 1;
                lsp_diag.column = 1;
                lsp_diag.end_line = 1;
                lsp_diag.end_column = 1;
            }

            lsp_diag.filename = diagnostic.filename();

            // Extract suggestions from primary spans
            const auto &primary_spans = diagnostic.multi_span().primary_spans();
            for (const auto &span : primary_spans)
            {
                const auto &label = span.label();
                if (label.has_value() && !label.value().empty() && label.value() != diagnostic.message())
                {
                    lsp_diag.suggestions.push_back(label.value());
                }
            }

            // Add error code if available
            lsp_diag.code = "E" + std::to_string(static_cast<uint32_t>(diagnostic.error_code()));

            lsp_diagnostics.push_back(std::move(lsp_diag));
        }

        return lsp_diagnostics;
    }

    void DiagnosticManager::clear_lsp_diagnostics()
    {
        clear();
    }

    bool DiagnosticManager::has_lsp_diagnostics() const
    {
        return !_diagnostics.empty();
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

    size_t DiagnosticManager::user_error_count() const
    {
        size_t count = 0;
        for (const auto &diagnostic : _diagnostics)
        {
            if (!is_stdlib_diagnostic(diagnostic) &&
                (diagnostic.severity() == DiagnosticSeverity::Error ||
                 diagnostic.severity() == DiagnosticSeverity::Fatal))
            {
                count++;
            }
        }
        return count;
    }

    size_t DiagnosticManager::user_warning_count() const
    {
        size_t count = 0;
        for (const auto &diagnostic : _diagnostics)
        {
            if (!is_stdlib_diagnostic(diagnostic) && diagnostic.severity() == DiagnosticSeverity::Warning)
            {
                count++;
            }
        }
        return count;
    }

    size_t DiagnosticManager::user_total_count() const
    {
        size_t count = 0;
        for (const auto &diagnostic : _diagnostics)
        {
            if (!is_stdlib_diagnostic(diagnostic))
            {
                count++;
            }
        }
        return count;
    }

    bool DiagnosticManager::is_stdlib_diagnostic(const Diagnostic &diagnostic) const
    {
        const std::string &filename = diagnostic.filename();

        // Check for stdlib file paths
        bool is_stdlib_file = (filename.find("stdlib") != std::string::npos ||
                               filename.find("core/") != std::string::npos ||
                               filename.find("/core/") != std::string::npos ||
                               filename.find("\\core\\") != std::string::npos ||
                               filename.find("/stdlib/") != std::string::npos ||
                               filename.find("\\stdlib\\") != std::string::npos);

        if (is_stdlib_file)
        {
            return true;
        }

        // Additional heuristic: check error message for stdlib-related content
        // This helps catch auto-imported stdlib errors that get misattributed to user files
        const std::string &message = diagnostic.message();
        bool is_stdlib_error = (message.find("Option<T>") != std::string::npos ||
                                message.find("Result<T,E>") != std::string::npos ||
                                message.find("is not declared in string") != std::string::npos ||
                                message.find("is not declared in Array") != std::string::npos ||
                                message.find("is not declared in Vec") != std::string::npos);

        return is_stdlib_error;
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