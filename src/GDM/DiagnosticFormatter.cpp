#include "GDM/DiagnosticFormatter.hpp"
#include "Utils/Logger.hpp"
#include <sstream>
#include <fstream>
#include <algorithm>
#include <iomanip>
#include <cmath>

namespace Cryo
{
    DiagnosticFormatter::DiagnosticFormatter(const SourceManager* source_manager,
                                                             bool use_colors, 
                                                             bool use_unicode,
                                                             size_t terminal_width)
        : _source_manager(source_manager), _terminal_width(terminal_width), _syntax_highlighter()
    {
        _style.use_colors = use_colors;
        _style.use_unicode = use_unicode;
        
        // Configure syntax highlighter
        _syntax_highlighter.set_color_output(use_colors);
        _syntax_highlighter.set_default_language("cryo");
        
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

    std::string DiagnosticFormatter::format_diagnostic(const Diagnostic& diagnostic)
    {
        // Use existing multi-span from diagnostic if available, otherwise create from legacy range
        MultiSpan spans = diagnostic.multi_span();
        
        // If no multi-span data, convert legacy range to multi-span
        if (spans.is_empty() && diagnostic.range().is_valid()) {
            auto start_line = diagnostic.range().start.line();
            auto start_col = diagnostic.range().start.column();
            auto end_line = diagnostic.range().end.line();
            auto end_col = diagnostic.range().end.column();
            
            // Enhanced span width calculation for better multi-character underlining
            if (start_line == end_line && start_col == end_col) {
                // Try to intelligently determine span width by analyzing the source
                if (_source_manager) {
                    std::string line = _source_manager->get_source_line(diagnostic.filename(), start_line);
                    if (!line.empty()) {
                        if (start_col > 0 && start_col <= line.length()) {
                            // Calculate token width by finding word boundaries
                            size_t col_idx = start_col - 1;  // Convert to 0-based indexing
                            size_t token_start = col_idx;
                            size_t token_end = col_idx;
                            
                            // Find start of token (scan backwards)
                            while (token_start > 0 && 
                                   (std::isalnum(line[token_start - 1]) || line[token_start - 1] == '_' || 
                                    line[token_start - 1] == '"' || line[token_start - 1] == '\'' ||
                                    line[token_start - 1] == '.')) {
                                token_start--;
                            }
                            
                            // Find end of token (scan forwards)  
                            while (token_end < line.length() - 1 &&
                                   (std::isalnum(line[token_end + 1]) || line[token_end + 1] == '_' ||
                                    line[token_end + 1] == '"' || line[token_end + 1] == '\'' ||
                                    line[token_end + 1] == '.')) {
                                token_end++;
                            }
                            
                            // For string literals, extend to closing quote
                            if (line[token_start] == '"') {
                                while (token_end < line.length() - 1 && line[token_end + 1] != '"') {
                                    token_end++;
                                }
                                if (token_end < line.length() - 1 && line[token_end + 1] == '"') {
                                    token_end++; // Include closing quote
                                }
                            }
                            
                            // Set end column to highlight entire token (convert back to 1-based)
                            end_col = token_end + 1;
                            
                            // Minimum span width of 1 character for visibility
                            if (end_col <= start_col) {
                                end_col = start_col + 1;
                            }
                        } else {
                            // Fallback for out-of-bounds positions
                            end_col = start_col + 1;
                        }
                    } else {
                        // Fallback when source not available
                        end_col = start_col + 1;
                    }
                } else {
                    // Fallback when source manager not available
                    std::string msg = diagnostic.message();
                    if (msg.find("string") != std::string::npos && msg.find("int") != std::string::npos) {
                        end_col = start_col + 5; // Type mismatch guess
                    } else if (msg.find("expression") != std::string::npos) {
                        end_col = start_col + 1; // Missing expression
                    } else {
                        end_col = start_col + 3; // Default small token
                    }
                }
            }
            
            SourceSpan span(SourceLocation(start_line, start_col),
                           SourceLocation(end_line, end_col),
                           diagnostic.filename(), true);
            
            // Generate enhanced Rust-style inline labels based on error type and message
            std::string msg = diagnostic.message();
            std::string inline_label = generate_enhanced_inline_label(diagnostic.error_code(), msg);
            
            if (!inline_label.empty()) {
                span.set_label(inline_label);
            }
            
            spans.add_primary_span(span);
        }
        
        // Use existing suggestions from diagnostic
        const auto& suggestions = diagnostic.code_suggestions();
        
        // Use the enhanced formatter for sophisticated Rust-style output
        return format_enhanced_diagnostic(diagnostic, spans, suggestions);
    }

    std::string DiagnosticFormatter::format_enhanced_diagnostic(
        const Diagnostic& diagnostic,
        const MultiSpan& spans,
        const std::vector<CodeSuggestion>& suggestions)
    {
        std::ostringstream output;

        // Header line with error code and message - ensure proper newline
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

        // Help messages from diagnostic
        for (const auto& help_msg : diagnostic.help_messages()) {
            output << "\n" << colorize("help", _style.help_color) << ": " << help_msg;
        }

        // Notes from original diagnostic
        for (const auto& note : diagnostic.notes()) {
            output << "\n" << colorize("note", _style.note_color) << ": " << note;
        }

        // Add visual separator line between error segments
        output << "\n" << colorize("+!+=========================================================================================+!+", "\x1b[90m") << "\n";

        return output.str();
    }

    DiagnosticFormatter::SourceSnippet 
    DiagnosticFormatter::extract_source_snippet(const MultiSpan& spans, size_t context_lines) const
    {
        SourceSnippet snippet;
        if (spans.is_empty() || !_source_manager) {
            return snippet;
        }

        // Use the SourceManager's sophisticated extract_snippet method
        auto source_snippet = _source_manager->extract_snippet(spans, context_lines);
        
        // Convert SourceManager::SourceSnippet to DiagnosticFormatter::SourceSnippet
        snippet.filename = source_snippet.filename;
        snippet.lines = source_snippet.lines;
        snippet.start_line_number = source_snippet.start_line_number;
        snippet.highlighted_spans = source_snippet.highlighted_spans;
        snippet.max_line_number_width = source_snippet.max_line_number_width;

        // If SourceManager couldn't extract lines, fall back to direct file reading
        if (snippet.lines.empty()) {
            auto primary = spans.primary_span();
            
            // Calculate line range manually as fallback
            size_t min_line = primary.start().line();
            size_t max_line = primary.end().line();

            for (const auto& span : spans.all_spans()) {
                min_line = std::min(min_line, span.start().line());
                max_line = std::max(max_line, span.end().line());
            }

            size_t start_line = (min_line > context_lines) ? min_line - context_lines : 1;
            size_t end_line = max_line + context_lines;

            snippet.start_line_number = start_line;
            snippet.filename = primary.filename();
            snippet.highlighted_spans = spans.all_spans();

            // Fallback: read directly from file
            for (size_t line_num = start_line; line_num <= end_line; ++line_num) {
                std::string line = read_line_from_file(snippet.filename, line_num);
                
                if (line.empty()) {
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
        }

        return snippet;
    }

    std::string DiagnosticFormatter::render_source_snippet(const SourceSnippet& snippet) const
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

            // Apply syntax highlighting to the source line
            std::string highlighted_line = _syntax_highlighter.apply_syntax_highlighting(line);

            // Render the source line with syntax highlighting
            output << format_line_number(line_number, line_width) << " " 
                   << _style.vertical_bar << " " << highlighted_line << "\n";

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

    std::string DiagnosticFormatter::create_underline_for_line(
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

        // Apply colors and add inline labels for Rust-style output
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
        
        // Skip inline labels here - they're handled by render_span_labels which properly colors them
        // This prevents duplicate white text
        
        return colored_underline;
    }

    std::string DiagnosticFormatter::render_span_labels(
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

    std::string DiagnosticFormatter::format_suggestions(
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

    std::string DiagnosticFormatter::format_single_suggestion(
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
    std::string DiagnosticFormatter::colorize(const std::string& text, const std::string& color) const
    {
        if (!_style.use_colors) {
            return text;
        }
        return color + text + _style.reset;
    }

    std::string DiagnosticFormatter::format_severity(DiagnosticSeverity severity) const
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

    std::string DiagnosticFormatter::format_line_number(size_t line_num, size_t width, bool show_line) const
    {
        if (!show_line) {
            return std::string(width, ' ');
        }
        
        std::ostringstream oss;
        oss << std::setw(width) << line_num;
        return colorize(oss.str(), _style.line_number_color);
    }

    std::vector<SourceSpan> DiagnosticFormatter::get_spans_on_line(
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

    size_t DiagnosticFormatter::calculate_line_number_width(const SourceSnippet& snippet) const
    {
        if (snippet.lines.empty()) {
            return 1;
        }
        
        size_t max_line = snippet.start_line_number + snippet.lines.size() - 1;
        return std::to_string(max_line).length();
    }

    std::string DiagnosticFormatter::repeat_char(char c, size_t count) const
    {
        return std::string(count, c);
    }

    std::string DiagnosticFormatter::read_line_from_file(const std::string& filename, size_t line_number) const
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

    size_t DiagnosticFormatter::get_file_line_count(const std::string& filename) const
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

    std::string DiagnosticFormatter::generate_enhanced_inline_label(ErrorCode error_code, const std::string& message) const
    {
        // Generate enhanced Rust-style inline labels based on error code and message
        switch (error_code) {
            case ErrorCode::E0200_TYPE_MISMATCH: // Type mismatch
                return extract_type_mismatch_label(message);
            
            case ErrorCode::E0222_INVALID_DEREF: // Cannot dereference
                return "cannot dereference non-pointer";
                
            case ErrorCode::E0204_UNDEFINED_FIELD: // No field exists
                return "field does not exist";
                
            case ErrorCode::E0209_INVALID_OPERATION: // Type mismatch in arithmetic
                return "incompatible types";
                
            case ErrorCode::E0106_EXPECTED_SEMICOLON: // Expected semicolon
                return "expected `;`";
                
            case ErrorCode::E0102_EXPECTED_EXPRESSION: // Expected expression
                return "expected expression";
                
            default:
                // Generate a descriptive label from the message
                return generate_generic_label(message);
        }
    }

    std::string DiagnosticFormatter::extract_type_mismatch_label(const std::string& message) const
    {
        // Try to extract "expected X, found Y" from type mismatch messages
        // Look for patterns like "int = string" or similar type mismatches
        
        // For now, provide a generic type mismatch message
        // TODO: Parse the actual types from the error message for more precise labels
        if (message.find("string") != std::string::npos && message.find("int") != std::string::npos) {
            return "expected `int`, found `string`";
        } else if (message.find("boolean") != std::string::npos && message.find("int") != std::string::npos) {
            return "expected `int`, found `boolean`";
        } else {
            return "type mismatch";
        }
    }

    std::string DiagnosticFormatter::generate_generic_label(const std::string& message) const
    {
        // Generate a concise label from the error message
        if (message.empty()) {
            return "";
        }
        
        // Truncate long messages and clean up formatting
        std::string label = message.substr(0, std::min(message.length(), size_t(40)));
        
        // Convert to lowercase for consistency
        std::transform(label.begin(), label.end(), label.begin(), ::tolower);
        
        return label;
    }

} // namespace Cryo