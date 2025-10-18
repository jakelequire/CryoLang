#include "GDM/DiagnosticFormatter.hpp"
#include "GDM/ErrorAnalysis.hpp"
#include "Utils/Logger.hpp"
#include <sstream>
#include <fstream>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <regex>

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
        
        // FIXED: Validate source range before processing and handle invalid ranges gracefully
        bool has_valid_range = diagnostic.range().is_valid() && 
                              is_valid_source_range(diagnostic.range(), diagnostic.filename());
        
        // If no multi-span data, convert legacy range to multi-span
        if (spans.is_empty() && has_valid_range) {
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
            
            // Generate enhanced Rust-style inline labels - NEW: Use structured data first
            std::string inline_label = generate_structured_inline_label(diagnostic);
            
            if (!inline_label.empty()) {
                span.set_label(inline_label);
            }
            
            spans.add_primary_span(span);
        } else if (spans.is_empty()) {
            // Handle invalid or missing source range by creating a minimal diagnostic
            // that doesn't reference source lines
            SourceSpan fallback_span(SourceLocation(1, 1), SourceLocation(1, 1), 
                                   diagnostic.filename().empty() ? "<unknown>" : diagnostic.filename(), true);
            fallback_span.set_label("error location unavailable");
            spans.add_primary_span(fallback_span);
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
            
            // FIXED: Don't show location line for invalid ranges (like :1:1 with empty filename)
            if (is_valid_source_range(SourceRange(primary.start(), primary.end()), primary.filename()) &&
                !primary.filename().empty()) {
                output << "  " << _style.arrow << " " << primary.filename()
                       << ":" << primary.start().line() << ":" << primary.start().column() << "\n";
            }
        }

        // Source snippet with highlighting
        if (!spans.is_empty()) {
            auto primary = spans.primary_span();
            
            // FIXED: Only show source snippet for valid ranges with actual files
            if (is_valid_source_range(SourceRange(primary.start(), primary.end()), primary.filename()) &&
                !primary.filename().empty()) {
                auto snippet = extract_source_snippet(spans);
                if (!snippet.lines.empty()) {
                    output << render_source_snippet(snippet);
                }
            }
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
        if (snippet.lines.empty() && !spans.is_empty()) {
            auto primary = spans.primary_span();
            
            // FIXED: Validate the primary span before attempting to read lines
            if (!is_valid_source_range(SourceRange(primary.start(), primary.end()), primary.filename())) {
                // Return empty snippet for invalid ranges to avoid "beyond end of file" errors
                return snippet;
            }
            
            // Calculate line range manually as fallback
            size_t min_line = primary.start().line();
            size_t max_line = primary.end().line();

            for (const auto& span : spans.all_spans()) {
                if (is_valid_source_range(SourceRange(span.start(), span.end()), span.filename())) {
                    min_line = std::min(min_line, span.start().line());
                    max_line = std::max(max_line, span.end().line());
                }
            }

            size_t start_line = (min_line > context_lines) ? min_line - context_lines : 1;
            size_t end_line = max_line + context_lines;
            
            // ADDED: Bound the end_line to the actual file length
            size_t file_line_count = get_file_line_count(primary.filename());
            if (file_line_count > 0) {
                end_line = std::min(end_line, file_line_count);
            }

            snippet.start_line_number = start_line;
            snippet.filename = primary.filename();
            snippet.highlighted_spans = spans.all_spans();

            // Fallback: read directly from file
            for (size_t line_num = start_line; line_num <= end_line; ++line_num) {
                std::string line = read_line_from_file(snippet.filename, line_num);
                
                if (line.empty()) {
                    // FIXED: Only add "beyond end of file" message if we're actually beyond the file
                    // and don't include these placeholder lines in the snippet
                    size_t total_lines = get_file_line_count(snippet.filename);
                    if (line_num > total_lines && total_lines > 0) {
                        // Skip lines beyond the file instead of showing placeholder text
                        break;
                    } else if (total_lines > 0) {
                        // Line exists but couldn't be read - show a different message
                        line = "    <source line not available>";
                    } else {
                        // File has no lines or couldn't be read
                        break;
                    }
                }
                
                if (!line.empty()) {
                    snippet.lines.push_back(line);
                }
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
            start_col = std::min(start_col, source_line.length() > 0 ? source_line.length() - 1 : 0);
            end_col = std::min(end_col, source_line.length() > 0 ? source_line.length() - 1 : 0);
            
            // Ensure we have at least one character to underline
            if (end_col < start_col) {
                end_col = start_col;
            }
            
            // Choose underline character and pattern based on span type
            if (span.is_primary()) {
                // Primary spans get ^^^^^^^ pattern
                for (size_t col = start_col; col <= end_col && col < underline.length(); ++col) {
                    underline[col] = '^';
                }
            } else {
                // Secondary spans get --- pattern  
                for (size_t col = start_col; col <= end_col && col < underline.length(); ++col) {
                    underline[col] = '-';
                }
            }
        }

        // Apply colors
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

    std::string DiagnosticFormatter::render_span_labels(
        const std::vector<SourceSpan>& spans,
        size_t line_number,
        const std::string& source_line) const
    {
        if (spans.empty()) {
            return "";
        }
        
        std::ostringstream output;
        bool has_labels = false;
        
        // ENHANCED: Create simple, clean inline labels with proper alignment
        for (const auto& span : spans) {
            if (!span.label().has_value() || span.start().line() != line_number) {
                continue;
            }
            
            has_labels = true;
            size_t span_start = span.start().column() - 1; // Convert to 0-based
            
            // Clamp to source line bounds
            span_start = std::min(span_start, source_line.length() > 0 ? source_line.length() - 1 : 0);
            
            std::string color = span.is_primary() ? _style.primary_span_color : _style.secondary_span_color;
            
            // Create spacing to align with the span position
            // No need for gutter calculation here - the labels are rendered inline after the gutter
            std::string spacing(span_start, ' ');
            
            // Add the label directly under the span
            output << spacing << colorize("|", color) << "\n";
            output << spacing << colorize(span.label().value(), color) << "\n";
        }
        
        // Remove the trailing newline if we added labels
        std::string result = output.str();
        if (has_labels && !result.empty() && result.back() == '\n') {
            result.pop_back();
        }
        
        return result;
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
        // This method is being deprecated in favor of structured data approach
        // For backward compatibility, still handle string-based approach for now
        
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

    // NEW: Generate inline label from structured diagnostic data
    std::string DiagnosticFormatter::generate_structured_inline_label(const Diagnostic& diagnostic) const
    {
        // Check if we have structured payload data
        if (diagnostic.has_payload() && diagnostic.payload()->has_type_mismatch()) {
            const auto* type_context = diagnostic.payload()->get_type_mismatch();
            if (type_context) {
                return type_context->generate_inline_label();
            }
        }
        
        // Fall back to error code + message parsing
        return generate_enhanced_inline_label(diagnostic.error_code(), diagnostic.message());
    }

    std::string DiagnosticFormatter::extract_type_mismatch_label(const std::string& message) const
    {
        // Enhanced type mismatch label extraction with sophisticated parsing
        
        // Pattern 1: "Cannot assign 'ActualType' to 'ExpectedType'"
        std::regex assign_pattern(R"(Cannot assign '([^']+)' to '([^']+)')");
        std::smatch match;
        
        if (std::regex_search(message, match, assign_pattern)) {
            std::string actual_type = match[1].str();
            std::string expected_type = match[2].str();
            return "expected `" + expected_type + "`, found `" + actual_type + "`";
        }
        
        // Pattern 2: "Type mismatch in X: ExpectedType and ActualType"
        std::regex operation_pattern(R"(Type mismatch in [^:]+: ([^\s]+) and ([^\s]+))");
        if (std::regex_search(message, match, operation_pattern)) {
            std::string expected_type = match[1].str();
            std::string actual_type = match[2].str();
            return "expected `" + expected_type + "`, found `" + actual_type + "`";
        }
        
        // Pattern 3: Look for explicit type names in various formats
        std::vector<std::string> common_types = {
            "int", "i32", "i64", "u32", "u64",
            "float", "f32", "f64", "double",
            "string", "str", "char",
            "boolean",
            "void", "null"
        };
        
        std::string found_expected, found_actual;
        
        // Try to extract types from common patterns
        for (const auto& type : common_types) {
            // Look for patterns like "expected int" or "int expected"
            std::string expected_pattern = R"(\bexpected\s+)" + type + R"(\b)";
            std::string found_pattern = R"(\bfound\s+)" + type + R"(\b)";
            std::string to_pattern = R"(\bto\s+)" + type + R"(\b)";
            std::string from_pattern = R"(\bfrom\s+)" + type + R"(\b)";
            
            std::regex expected_regex(expected_pattern, std::regex_constants::icase);
            std::regex found_regex(found_pattern, std::regex_constants::icase);
            std::regex to_regex(to_pattern, std::regex_constants::icase);
            std::regex from_regex(from_pattern, std::regex_constants::icase);
            
            if (std::regex_search(message, expected_regex) || std::regex_search(message, to_regex)) {
                found_expected = type;
            }
            if (std::regex_search(message, found_regex) || std::regex_search(message, from_regex)) {
                found_actual = type;
            }
        }
        
        if (!found_expected.empty() && !found_actual.empty()) {
            return "expected `" + found_expected + "`, found `" + found_actual + "`";
        }
        
        // Pattern 4: Simple heuristic based on common type words
        bool has_string = message.find("string") != std::string::npos;
        bool has_int = message.find("int") != std::string::npos;
        bool has_bool = message.find("bool") != std::string::npos || message.find("boolean") != std::string::npos;
        bool has_float = message.find("float") != std::string::npos || message.find("double") != std::string::npos;
        
        // Common type mismatch scenarios
        if (has_string && has_int) {
            // Determine which is expected vs found based on context
            if (message.find("assign") != std::string::npos) {
                // In assignment context, usually "cannot assign X to Y" means Y is expected
                if (message.find("string") < message.find("int")) {
                    return "expected `int`, found `string`";
                } else {
                    return "expected `string`, found `int`";
                }
            } else {
                return "expected `int`, found `string`"; // Common default
            }
        } else if (has_bool && has_int) {
            return "expected `int`, found `boolean`";
        } else if (has_float && has_int) {
            if (message.find("float") < message.find("int")) {
                return "expected `int`, found `float`";
            } else {
                return "expected `float`, found `int`";
            }
        } else if (has_string && has_bool) {
            return "expected `boolean`, found `string`";
        }
        
        // Fallback: Extract first two potential type words
        std::regex type_word_pattern(R"(\b(int|i32|i64|u32|u64|float|f32|f64|double|string|str|char|boolean|bool|void|null)\b)");
        std::vector<std::string> found_types;
        std::sregex_iterator iter(message.begin(), message.end(), type_word_pattern);
        std::sregex_iterator end;
        
        for (; iter != end && found_types.size() < 2; ++iter) {
            found_types.push_back(iter->str());
        }
        
        if (found_types.size() >= 2) {
            return "expected `" + found_types[1] + "`, found `" + found_types[0] + "`";
        } else if (found_types.size() == 1) {
            return "type mismatch involving `" + found_types[0] + "`";
        }
        
        // Ultimate fallback
        return "type mismatch";
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

    bool DiagnosticFormatter::is_valid_source_range(const SourceRange& range, const std::string& filename) const
    {
        // Check if range has valid line and column numbers
        if (range.start.line() == 0 || range.start.column() == 0 || 
            range.end.line() == 0 || range.end.column() == 0) {
            return false;
        }
        
        // If we have a source manager, validate against actual file content
        if (_source_manager && !filename.empty()) {
            size_t file_line_count = get_file_line_count(filename);
            
            // Check if the range is within the bounds of the actual file
            if (range.start.line() > file_line_count || range.end.line() > file_line_count) {
                return false;
            }
            
            // Additional validation: check if columns are reasonable
            if (range.start.line() <= file_line_count) {
                std::string line = read_line_from_file(filename, range.start.line());
                if (!line.empty() && range.start.column() > line.length() + 1) {
                    return false;
                }
            }
        }
        
        return true;
    }

} // namespace Cryo