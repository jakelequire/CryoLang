#include "GDM/AdvancedDiagnosticFormatter.hpp"
#include "Utils/Logger.hpp"
#include <sstream>
#include <fstream>
#include <algorithm>
#include <iomanip>
#include <cmath>

namespace Cryo
{
    AdvancedDiagnosticFormatter::AdvancedDiagnosticFormatter(const SourceManager* source_manager,
                                                             bool use_colors, 
                                                             bool use_unicode,
                                                             size_t terminal_width)
        : _source_manager(source_manager), _terminal_width(terminal_width)
    {
        _style.use_colors = use_colors;
        _style.use_unicode = use_unicode;
        
        // Use clean ASCII by default for universal compatibility
        _style.arrow = "-->";
        _style.vertical_bar = "|";
        _style.horizontal_line = "-";
        _style.caret = "^";
        _style.tilde = "~";
        
        // Don't use Unicode at all for now to ensure clean display
        // (commented out to avoid any Unicode issues)
        /*
        if (use_unicode) {
            _style.arrow = "→";
            _style.vertical_bar = "│";
            _style.horizontal_line = "─";
        }
        */
    }

    std::string AdvancedDiagnosticFormatter::format_diagnostic(const Diagnostic& diagnostic)
    {
        // Convert diagnostic to MultiSpan for enhanced formatting
        MultiSpan spans;
        auto range = diagnostic.range();
        if (range.is_valid()) {
            SourceSpan span(SourceLocation(range.start.line(), range.start.column()),
                           SourceLocation(range.end.line(), range.end.column()),
                           diagnostic.filename(), true);
            spans.add_primary_span(span);
        }
        
        // Extract any suggestions from the diagnostic (empty for now)
        std::vector<CodeSuggestion> suggestions;
        
        // Use the enhanced formatter for sophisticated Rust-style output
        return format_enhanced_diagnostic(diagnostic, spans, suggestions);
    }

    std::string AdvancedDiagnosticFormatter::format_enhanced_diagnostic(
        const Diagnostic& diagnostic,
        const MultiSpan& spans,
        const std::vector<CodeSuggestion>& suggestions)
    {
        std::ostringstream output;

        // Header line with error code and message
        output << format_severity(diagnostic.severity()) 
               << "[E" << std::setfill('0') << std::setw(4) << static_cast<int>(diagnostic.error_code()) << "]: "
               << diagnostic.message() << "\n";

        // Location line  
        if (!spans.is_empty()) {
            auto primary = spans.primary_span();
            output << "  " << _style.arrow << " " << primary.filename()
                   << ":" << primary.start().line() << ":" << primary.start().column() << "\n";
        }

        // Source snippet with highlighting
        if (!spans.is_empty()) {
            auto snippet = extract_source_snippet(spans);
            output << render_source_snippet(snippet);
        }

        // Suggestions
        if (!suggestions.empty()) {
            output << format_suggestions(suggestions);
        }

        // Notes from original diagnostic
        for (const auto& note : diagnostic.notes()) {
            output << "\n" << colorize("note", _style.note_color) << ": " << note;
        }

        return output.str();
    }

    AdvancedDiagnosticFormatter::SourceSnippet 
    AdvancedDiagnosticFormatter::extract_source_snippet(const MultiSpan& spans, size_t context_lines) const
    {
        SourceSnippet snippet;
        if (spans.is_empty() || !_source_manager) {
            return snippet;
        }

        auto primary = spans.primary_span();
        snippet.filename = primary.filename();

        // Calculate line range
        size_t min_line = primary.start().line();
        size_t max_line = primary.end().line();

        // Extend for all spans
        for (const auto& span : spans.all_spans()) {
            min_line = std::min(min_line, span.start().line());
            max_line = std::max(max_line, span.end().line());
        }

        // Add context
        size_t start_line = (min_line > context_lines) ? min_line - context_lines : 1;
        size_t end_line = max_line + context_lines;

        snippet.start_line_number = start_line;
        snippet.highlighted_spans = spans.all_spans();

        // Extract actual source lines from the source manager
        for (size_t line_num = start_line; line_num <= end_line; ++line_num) {
            std::string line;
            if (_source_manager && _source_manager->has_file(snippet.filename)) {
                // Get the actual source line from the source manager
                line = _source_manager->get_source_line(snippet.filename, line_num);
            } else {
                // Fallback: try to read directly from file
                line = read_line_from_file(snippet.filename, line_num);
            }
            
            // If we couldn't get the line, provide better context
            if (line.empty()) {
                // Check if the line number is beyond the file's end
                size_t total_lines = get_file_line_count(snippet.filename);
                if (line_num > total_lines) {
                    line = "    <line " + std::to_string(line_num) + " beyond end of file (max: " + std::to_string(total_lines) + ")>";
                } else {
                    line = "    <source line not available>";
                }
            }
            
            snippet.lines.push_back(line);
        }

        snippet.max_line_number_width = calculate_line_number_width(snippet);
        return snippet;
    }

    std::string AdvancedDiagnosticFormatter::render_source_snippet(const SourceSnippet& snippet) const
    {
        if (snippet.lines.empty()) {
            return "";
        }

        std::ostringstream output;
        size_t line_width = snippet.max_line_number_width;

        // Empty line before snippet
        output << "   " << _style.vertical_bar << "\n";

        for (size_t i = 0; i < snippet.lines.size(); ++i) {
            size_t line_number = snippet.start_line_number + i;
            const std::string& line = snippet.lines[i];

            // Get spans that affect this line
            auto spans_on_line = get_spans_on_line(snippet.highlighted_spans, line_number);

            // Render the source line
            output << format_line_number(line_number, line_width) << " " 
                   << _style.vertical_bar << " " << line << "\n";

            // Render underlines and labels if there are spans on this line
            if (!spans_on_line.empty()) {
                std::string underline = create_underline_for_line(line, spans_on_line, line_number);
                if (!underline.empty()) {
                    output << format_line_number(0, line_width, false) << " " 
                           << _style.vertical_bar << " " << underline << "\n";
                }

                // Render span labels
                std::string labels = render_span_labels(spans_on_line, line_number, line);
                if (!labels.empty()) {
                    output << format_line_number(0, line_width, false) << " " 
                           << _style.vertical_bar << " " << labels << "\n";
                }
            }
        }

        return output.str();
    }

    std::string AdvancedDiagnosticFormatter::create_underline_for_line(
        const std::string& source_line,
        const std::vector<SourceSpan>& spans_on_line,
        size_t line_number) const
    {
        if (spans_on_line.empty()) {
            return "";
        }

        // Create a buffer for the underline with spaces initially
        std::string underline(source_line.length(), ' ');
        
        // Sort spans by start position for proper layering
        auto sorted_spans = spans_on_line;
        std::sort(sorted_spans.begin(), sorted_spans.end(), 
                  [](const SourceSpan& a, const SourceSpan& b) {
                      return a.start().column() < b.start().column();
                  });
        
        for (const auto& span : sorted_spans) {
            size_t start_col = (span.start().line() == line_number) ? span.start().column() - 1 : 0;
            size_t end_col = (span.end().line() == line_number) ? span.end().column() - 1 : source_line.length() - 1;
            
            // Clamp to line bounds
            start_col = std::min(start_col, source_line.length() - 1);
            end_col = std::min(end_col, source_line.length() - 1);
            
            // Choose underline character and pattern based on span type
            if (span.is_primary()) {
                // Primary spans get ^^^^^^^ pattern
                for (size_t col = start_col; col <= end_col; ++col) {
                    if (col < underline.length()) {
                        underline[col] = '^';
                    }
                }
            } else {
                // Secondary spans get --- pattern  
                for (size_t col = start_col; col <= end_col; ++col) {
                    if (col < underline.length()) {
                        underline[col] = '-';
                    }
                }
            }
        }

        // Apply colors to create the final underline
        std::string colored_underline;
        for (size_t i = 0; i < underline.length(); ++i) {
            if (underline[i] == '^') {
                colored_underline += colorize(std::string(1, underline[i]), _style.primary_span_color);
            } else if (underline[i] == '-') {
                colored_underline += colorize(std::string(1, underline[i]), _style.secondary_span_color);
            } else {
                colored_underline += underline[i];
            }
        }

        return colored_underline;
    }

    std::string AdvancedDiagnosticFormatter::render_span_labels(
        const std::vector<SourceSpan>& spans,
        size_t line_number,
        const std::string& source_line) const
    {
        std::ostringstream output;
        
        for (const auto& span : spans) {
            if (span.label().has_value() && span.start().line() == line_number) {
                size_t col = span.start().column() - 1;
                if (col < source_line.length()) {
                    // Create spacing to align with the span start
                    std::string spacing(col, ' ');
                    std::string color = span.is_primary() ? _style.primary_span_color : _style.secondary_span_color;
                    
                    // Add the vertical connection line
                    output << spacing << colorize(_style.vertical_bar, color) << "\n";
                    
                    // Add the label text
                    output << spacing << colorize(span.label().value(), color);
                    
                    // If this is not the last span with a label, add a newline
                    bool has_more_labels = false;
                    for (const auto& other_span : spans) {
                        if (&other_span != &span && other_span.label().has_value() && 
                            other_span.start().line() == line_number) {
                            has_more_labels = true;
                            break;
                        }
                    }
                    if (has_more_labels) {
                        output << "\n";
                    }
                }
            }
        }
        
        return output.str();
    }

    std::string AdvancedDiagnosticFormatter::format_suggestions(
        const std::vector<CodeSuggestion>& suggestions) const
    {
        if (suggestions.empty()) {
            return "";
        }

        std::ostringstream output;
        
        for (const auto& suggestion : suggestions) {
            output << "\n" << format_single_suggestion(suggestion);
        }
        
        return output.str();
    }

    std::string AdvancedDiagnosticFormatter::format_single_suggestion(
        const CodeSuggestion& suggestion) const
    {
        std::ostringstream output;
        
        // Suggestion header
        output << colorize("help", _style.help_color) << ": " << suggestion.message() << "\n";
        
        // Show the suggestion code if applicable
        if (suggestion.style() == SuggestionStyle::ShowCode || 
            suggestion.style() == SuggestionStyle::ShowAlways) {
            
            // Add empty line separator
            output << "   " << _style.vertical_bar << "\n";
            
            if (suggestion.is_simple()) {
                const auto& simple = suggestion.simple();
                
                // Show the suggested code change with line number context
                size_t line_num = simple.span.start().line();
                output << std::setw(2) << line_num << " " << _style.vertical_bar << " ";
                
                // For demonstration, show the replacement text
                // In a real implementation, this would show the actual modified line
                output << "    " << simple.replacement << "\n";
                
                // Add underline to show what changed
                output << "   " << _style.vertical_bar << " ";
                size_t col = simple.span.start().column() - 1;
                std::string spacing(col + 4, ' '); // +4 for indentation
                size_t replacement_length = simple.replacement.length();
                
                output << spacing;
                for (size_t i = 0; i < replacement_length; ++i) {
                    output << colorize("~", _style.help_color);
                }
                output << "\n";
            }
        }
        
        return output.str();
    }

    // Utility methods implementation
    std::string AdvancedDiagnosticFormatter::colorize(const std::string& text, const std::string& color) const
    {
        if (!_style.use_colors) {
            return text;
        }
        return color + text + _style.reset;
    }

    std::string AdvancedDiagnosticFormatter::format_severity(DiagnosticSeverity severity) const
    {
        switch (severity) {
            case DiagnosticSeverity::Error:
                return colorize("error", _style.error_color);
            case DiagnosticSeverity::Warning:
                return colorize("warning", _style.warning_color);
            case DiagnosticSeverity::Note:
                return colorize("note", _style.note_color);
            case DiagnosticSeverity::Fatal:
                return colorize("fatal error", _style.error_color);
            default:
                return "unknown";
        }
    }

    std::string AdvancedDiagnosticFormatter::format_line_number(size_t line_num, size_t width, bool show_line) const
    {
        if (!show_line) {
            return std::string(width, ' ');
        }
        
        std::ostringstream oss;
        oss << std::setw(width) << line_num;
        return colorize(oss.str(), _style.line_number_color);
    }

    std::vector<SourceSpan> AdvancedDiagnosticFormatter::get_spans_on_line(
        const std::vector<SourceSpan>& spans, 
        size_t line_number) const
    {
        std::vector<SourceSpan> result;
        
        for (const auto& span : spans) {
            if (span.start().line() <= line_number && span.end().line() >= line_number) {
                result.push_back(span);
            }
        }
        
        return result;
    }

    size_t AdvancedDiagnosticFormatter::calculate_line_number_width(const SourceSnippet& snippet) const
    {
        if (snippet.lines.empty()) {
            return 1;
        }
        
        size_t max_line = snippet.start_line_number + snippet.lines.size() - 1;
        return std::to_string(max_line).length();
    }

    std::string AdvancedDiagnosticFormatter::repeat_char(char c, size_t count) const
    {
        return std::string(count, c);
    }

    std::string AdvancedDiagnosticFormatter::read_line_from_file(const std::string& filename, size_t line_number) const
    {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return "";
        }

        std::string line;
        size_t current_line = 1;
        
        while (std::getline(file, line) && current_line <= line_number) {
            if (current_line == line_number) {
                return line;
            }
            current_line++;
        }
        
        return "";
    }

    size_t AdvancedDiagnosticFormatter::get_file_line_count(const std::string& filename) const
    {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return 0;
        }

        size_t line_count = 0;
        std::string line;
        while (std::getline(file, line)) {
            line_count++;
        }
        
        return line_count;
    }

} // namespace Cryo