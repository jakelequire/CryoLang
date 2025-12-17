#pragma once

#include "GDM/GDM.hpp"
#include "Utils/File.hpp"
#include "Utils/SyntaxHighlighter.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <iomanip>

namespace Cryo
{
    // ================================================================
    // Advanced Terminal Formatting for Rust-like Error Output
    // ================================================================

    class DiagnosticFormatter
    {
    public:
        struct FormatStyle
        {
            // Colors
            std::string error_color = "\033[1;31m";      // Bold red
            std::string warning_color = "\033[1;33m";    // Bold yellow
            std::string note_color = "\033[1;36m";       // Bold cyan
            std::string help_color = "\033[1;32m";       // Bold green
            std::string line_number_color = "\033[34m";  // Blue
            std::string primary_span_color = "\033[1;31m";   // Bold red
            std::string secondary_span_color = "\033[1;36m"; // Bold cyan
            std::string error_code_color = "\033[1;38;5;208m"; // Bold orange (256-color mode)
            std::string reset = "\033[0m";               // Reset
            std::string bold = "\033[1m";                // Bold

            // Clean ASCII characters for universal compatibility
            std::string arrow = "-->";
            std::string vertical_bar = "|";
            std::string horizontal_line = "-";
            std::string caret = "^";
            std::string tilde = "~";

            // Fallback ASCII characters
            bool use_unicode = false;  // Default to ASCII for compatibility
            bool use_colors = true;
        };

        struct SourceSnippet
        {
            std::vector<std::string> lines;
            size_t start_line_number;
            std::vector<SourceSpan> highlighted_spans;
            size_t max_line_number_width;
            std::string filename;
        };

    private:
        FormatStyle _style;
        size_t _terminal_width;
        const SourceManager* _source_manager;
        SyntaxHighlighter _syntax_highlighter;

    public:
        DiagnosticFormatter(const SourceManager* source_manager,
                                    bool use_colors = true, 
                                    bool use_unicode = true,
                                    size_t terminal_width = 80);

        // Main formatting method
        std::string format_diagnostic(const Diagnostic& diagnostic, size_t error_number = 0);

        // Enhanced diagnostic formatting (when we upgrade Diagnostic)
        std::string format_enhanced_diagnostic(const Diagnostic& diagnostic,
                                               const MultiSpan& spans,
                                               const std::vector<CodeSuggestion>& suggestions,
                                               size_t error_number = 0);

    private:
        // Source snippet extraction and formatting
        SourceSnippet extract_source_snippet(const MultiSpan& spans, 
                                              size_t context_lines = 2) const;
        
        std::string render_source_snippet(const SourceSnippet& snippet) const;
        
        std::string create_underline_for_line(const std::string& source_line,
                                              const std::vector<SourceSpan>& spans_on_line,
                                              size_t line_number) const;

        // Label and annotation rendering
        std::string render_span_labels(const std::vector<SourceSpan>& spans,
                                       size_t line_number,
                                       const std::string& source_line) const;

        std::string render_vertical_connections(const std::vector<SourceSpan>& spans,
                                                size_t line_number,
                                                size_t line_number_width) const;

        // Suggestion formatting
        std::string format_suggestions(const std::vector<CodeSuggestion>& suggestions) const;
        std::string format_single_suggestion(const CodeSuggestion& suggestion) const;
        
        // Phase information formatting
        std::string format_error_code_with_phase(ErrorCode error_code, DiagnosticCategory category) const;

        // Utility methods
        std::string colorize(const std::string& text, const std::string& color) const;
        std::string format_severity(DiagnosticSeverity severity) const;
        std::string format_line_number(size_t line_num, size_t width, bool show_line = true) const;
        
        // Helper methods for source management
        std::vector<SourceSpan> get_spans_on_line(const std::vector<SourceSpan>& spans, 
                                                  size_t line_number) const;
        size_t calculate_line_number_width(const SourceSnippet& snippet) const;
        std::string repeat_char(char c, size_t count) const;
        std::string create_margin(size_t line_number_width) const;
        std::string read_line_from_file(const std::string& filename, size_t line_number) const;
        size_t get_file_line_count(const std::string& filename) const;

        // Enhanced inline label generation
        std::string generate_enhanced_inline_label(ErrorCode error_code, const std::string& message) const;
        std::string generate_structured_inline_label(const Diagnostic& diagnostic) const; // NEW: Use structured data
        std::string extract_type_mismatch_label(const std::string& message) const;
        std::string extract_codegen_label(const std::string& message) const;
        std::string generate_generic_label(const std::string& message) const;

        // Smart context determination
        std::pair<size_t, size_t> calculate_optimal_context(const MultiSpan& spans) const;
        
        // ADDED: Source range validation for preventing "beyond end of file" errors
        bool is_valid_source_range(const SourceRange& range, const std::string& filename) const;
        
        // Multi-span relationship rendering
        struct SpanLayout
        {
            size_t column_start;
            size_t column_end;
            bool is_primary;
            std::string label;
            size_t depth; // For nested highlighting
        };

        std::vector<SpanLayout> calculate_span_layout(const std::vector<SourceSpan>& spans,
                                                      const std::string& source_line) const;
    };

    // ================================================================
    // Enhanced Source Manager for Rich Context
    // ================================================================

    class EnhancedSourceManager
    {
    private:
        const SourceManager* _base_manager;

    public:
        explicit EnhancedSourceManager(const SourceManager* base_manager)
            : _base_manager(base_manager) {}

        // Enhanced context extraction methods
        std::vector<std::string> get_source_lines(const std::string& filename,
                                                   size_t start_line,
                                                   size_t end_line) const;

        std::string get_source_line(const std::string& filename, size_t line_number) const;
        
        // Smart context window calculation
        std::pair<size_t, size_t> calculate_context_window(const MultiSpan& spans,
                                                           size_t max_context_lines = 3) const;

        // Get source text for a specific span
        std::string get_span_text(const SourceSpan& span) const;

        // Check if a file exists and is accessible
        bool can_access_file(const std::string& filename) const;
    };

    // ================================================================
    // Helper Classes for Complex Error Formatting
    // ================================================================

    class DiagnosticRenderer
    {
    public:
        // Render complex multi-span diagnostics with relationships
        static std::string render_type_mismatch_diagnostic(
            const SourceSpan& error_span,
            const SourceSpan& type_annotation_span,
            const std::string& expected_type,
            const std::string& actual_type,
            const std::vector<CodeSuggestion>& suggestions,
            const DiagnosticFormatter& formatter
        );

        // Render undefined variable diagnostics with "did you mean" suggestions
        static std::string render_undefined_variable_diagnostic(
            const SourceSpan& error_span,
            const std::string& variable_name,
            const std::vector<std::string>& similar_variables,
            const DiagnosticFormatter& formatter
        );

        // Render syntax error diagnostics with context-aware suggestions
        static std::string render_syntax_error_diagnostic(
            const SourceSpan& error_span,
            const std::string& expected_token,
            const std::string& actual_token,
            const std::vector<CodeSuggestion>& suggestions,
            const DiagnosticFormatter& formatter
        );
    };

} // namespace Cryo
