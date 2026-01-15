/******************************************************************************
 * @file Diag.cpp
 * @brief Implementation of the clean diagnostic system
 ******************************************************************************/

#include "Diagnostics/Diag.hpp"
#include "AST/ASTNode.hpp"
#include "Utils/File.hpp"

#include <sstream>
#include <algorithm>

namespace Cryo
{
    //=========================================================================
    // Global emitter
    //=========================================================================

    static std::unique_ptr<DiagEmitter> g_diag_emitter;

    DiagEmitter &diag_emitter()
    {
        if (!g_diag_emitter)
        {
            g_diag_emitter = std::make_unique<DiagEmitter>();
        }
        return *g_diag_emitter;
    }

    void init_diagnostics(DiagEmitter::Config config)
    {
        g_diag_emitter = std::make_unique<DiagEmitter>(std::move(config));
    }

    bool diagnostics_initialized()
    {
        return g_diag_emitter != nullptr;
    }

    //=========================================================================
    // Span implementation
    //=========================================================================

    Span Span::at(const SourceLocation &loc, std::string_view file)
    {
        Span s;
        s.file = std::string(file);
        s.start_line = loc.line();
        s.start_col = loc.column();
        s.end_line = loc.line();
        s.end_col = loc.column() + 1; // Single character span
        return s;
    }

    Span Span::range(const SourceLocation &start, const SourceLocation &end,
                     std::string_view file)
    {
        Span s;
        s.file = std::string(file);
        s.start_line = start.line();
        s.start_col = start.column();
        s.end_line = end.line();
        s.end_col = end.column();
        return s;
    }

    Span Span::from_node(const ASTNode *node)
    {
        Span s;
        if (node)
        {
            const auto &loc = node->location();
            s.file = node->source_file();
            s.start_line = loc.line();
            s.start_col = loc.column();
            s.end_line = loc.line();
            s.end_col = loc.column() + 1;
        }
        return s;
    }

    //=========================================================================
    // Suggestion implementation
    //=========================================================================

    Suggestion Suggestion::replace(Span span, std::string_view replacement,
                                   std::string_view msg)
    {
        return Suggestion(msg, std::move(span), replacement,
                          Applicability::MachineApplicable);
    }

    Suggestion Suggestion::insert_before(const SourceLocation &loc,
                                         std::string_view text,
                                         std::string_view msg,
                                         std::string_view file)
    {
        Span span = Span::at(loc, file);
        span.end_col = span.start_col; // Zero-width span
        return Suggestion(msg, std::move(span), std::string(text),
                          Applicability::MachineApplicable);
    }

    Suggestion Suggestion::insert_after(const SourceLocation &loc,
                                        std::string_view text,
                                        std::string_view msg,
                                        std::string_view file)
    {
        Span span = Span::at(loc, file);
        span.start_col = span.end_col; // Zero-width span at end
        return Suggestion(msg, std::move(span), std::string(text),
                          Applicability::MachineApplicable);
    }

    Suggestion Suggestion::remove(Span span, std::string_view msg)
    {
        return Suggestion(msg, std::move(span), "",
                          Applicability::MachineApplicable);
    }

    //=========================================================================
    // Diag convenience factory methods
    //=========================================================================

    Diag Diag::type_mismatch(std::string_view expected, std::string_view found,
                             const Span &span, std::string_view context)
    {
        std::ostringstream msg;
        msg << "mismatched types";
        if (!context.empty())
        {
            msg << " in " << context;
        }

        auto diag = Diag::error(ErrorCode::E0200_TYPE_MISMATCH, msg.str());

        // Create a labeled span
        Span labeled_span = span;
        std::ostringstream label;
        label << "expected `" << expected << "`, found `" << found << "`";
        labeled_span.label = label.str();

        return diag.at(std::move(labeled_span));
    }

    Diag Diag::undefined_symbol(std::string_view name, const Span &span)
    {
        std::ostringstream msg;
        msg << "cannot find value `" << name << "` in this scope";

        auto diag = Diag::error(ErrorCode::E0201_UNDEFINED_VARIABLE, msg.str());

        Span labeled_span = span;
        labeled_span.label = "not found in this scope";

        return diag.at(std::move(labeled_span));
    }

    Diag Diag::undefined_field(std::string_view field, std::string_view type_name,
                               const Span &span)
    {
        std::ostringstream msg;
        msg << "no field `" << field << "` on type `" << type_name << "`";

        auto diag = Diag::error(ErrorCode::E0204_UNDEFINED_FIELD, msg.str());

        Span labeled_span = span;
        labeled_span.label = "unknown field";

        return diag.at(std::move(labeled_span));
    }

    //=========================================================================
    // DiagEmitter implementation
    //=========================================================================

    void DiagEmitter::emit(Diag diagnostic)
    {
        // Update counts
        if (diagnostic.is_error())
        {
            ++_error_count;
        }
        else if (diagnostic.is_warning())
        {
            if (_config.warnings_as_errors)
            {
                ++_error_count;
            }
            else
            {
                ++_warning_count;
            }
        }

        // Render immediately
        render(diagnostic, std::cerr);

        // Store for later access
        _diagnostics.push_back(std::move(diagnostic));
    }

    void DiagEmitter::store(Diag diagnostic)
    {
        // Update counts
        if (diagnostic.is_error())
        {
            ++_error_count;
        }
        else if (diagnostic.is_warning())
        {
            if (_config.warnings_as_errors)
            {
                ++_error_count;
            }
            else
            {
                ++_warning_count;
            }
        }

        // Store without rendering
        _diagnostics.push_back(std::move(diagnostic));
    }

    void DiagEmitter::render_all(std::ostream &out) const
    {
        for (const auto &diag : _diagnostics)
        {
            render(diag, out);
        }
    }

    void DiagEmitter::render(const Diag &diagnostic, std::ostream &out) const
    {
        // Color codes
        const char *RESET = _config.colors ? "\033[0m" : "";
        const char *BOLD = _config.colors ? "\033[1m" : "";
        const char *RED = _config.colors ? "\033[31m" : "";
        const char *YELLOW = _config.colors ? "\033[33m" : "";
        const char *CYAN = _config.colors ? "\033[36m" : "";
        const char *BLUE = _config.colors ? "\033[34m" : "";
        const char *GREEN = _config.colors ? "\033[32m" : "";
        const char *MAGENTA = _config.colors ? "\033[35m" : "";

        // Get level color and text
        const char *level_color = RED;
        std::string level_text = "error";
        switch (diagnostic.level())
        {
        case Diag::Level::Note:
            level_color = CYAN;
            level_text = "note";
            break;
        case Diag::Level::Warning:
            level_color = YELLOW;
            level_text = "warning";
            break;
        case Diag::Level::Error:
            level_color = RED;
            level_text = "error";
            break;
        case Diag::Level::Fatal:
            level_color = RED;
            level_text = "fatal";
            break;
        }

        // Format error code
        std::string code_str = ErrorRegistry::format_error_code(diagnostic.code());

        // Header: error[E0200]: message
        out << BOLD << level_color << level_text << RESET;
        if (!code_str.empty() && diagnostic.code() != ErrorCode::E0000_UNKNOWN)
        {
            out << "[" << BOLD << MAGENTA << code_str << RESET << "]";
        }
        out << ": " << BOLD << diagnostic.message() << RESET << "\n";

        // Location and source snippet
        auto primary = diagnostic.primary_span();
        if (primary && primary->is_valid())
        {
            // Arrow line: --> file:line:col
            out << "  " << BLUE << "-->" << RESET << " "
                << primary->file << ":" << primary->start_line << ":"
                << primary->start_col << "\n";

            // Try to show source code
            if (has_source(primary->file))
            {
                // Calculate line number width
                size_t max_line = primary->end_line + _config.context_lines;
                size_t line_width = std::to_string(max_line).length();

                // Empty margin line
                out << std::string(line_width + 1, ' ') << BLUE << "|" << RESET << "\n";

                // Show lines
                size_t start_line = primary->start_line > _config.context_lines
                                        ? primary->start_line - _config.context_lines
                                        : 1;
                size_t end_line = primary->end_line + _config.context_lines;

                for (size_t line_no = start_line; line_no <= end_line; ++line_no)
                {
                    std::string_view line_content = get_line(primary->file, line_no);
                    if (line_content.empty() && line_no > primary->end_line)
                        break;

                    // Line number
                    std::string line_num = std::to_string(line_no);
                    out << std::string(line_width - line_num.length(), ' ')
                        << BLUE << line_num << " |" << RESET << " ";

                    // Line content
                    out << line_content << "\n";

                    // Underline for error lines
                    if (line_no >= primary->start_line && line_no <= primary->end_line)
                    {
                        out << std::string(line_width + 1, ' ') << BLUE << "|" << RESET << " ";

                        // Calculate underline position
                        size_t underline_start = (line_no == primary->start_line)
                                                     ? primary->start_col - 1
                                                     : 0;
                        size_t underline_end = (line_no == primary->end_line)
                                                   ? primary->end_col - 1
                                                   : line_content.length();

                        // Spaces before underline
                        out << std::string(underline_start, ' ');

                        // Underline
                        size_t underline_len = underline_end > underline_start
                                                   ? underline_end - underline_start
                                                   : 1;
                        out << BOLD << level_color
                            << std::string(underline_len, '^') << RESET;

                        // Label on last line
                        if (line_no == primary->end_line && !primary->label.empty())
                        {
                            out << " " << BOLD << level_color << primary->label << RESET;
                        }
                        out << "\n";
                    }
                }

                // Empty margin line after
                out << std::string(line_width + 1, ' ') << BLUE << "|" << RESET << "\n";
            }
        }

        // Secondary spans
        for (const auto &span : diagnostic.spans())
        {
            if (span.is_primary || !span.is_valid())
                continue;

            out << "  " << BLUE << "-->" << RESET << " "
                << span.file << ":" << span.start_line << ":" << span.start_col;
            if (!span.label.empty())
            {
                out << ": " << span.label;
            }
            out << "\n";
        }

        // Notes
        for (const auto &note : diagnostic.notes())
        {
            out << "  " << BOLD << CYAN << "note" << RESET << ": " << note << "\n";
        }

        // Help messages
        for (const auto &help : diagnostic.help_messages())
        {
            out << "  " << BOLD << GREEN << "help" << RESET << ": " << help << "\n";
        }

        // Suggestions with code
        for (const auto &suggestion : diagnostic.suggestions())
        {
            if (!suggestion.message.empty())
            {
                out << "  " << BOLD << GREEN << "help" << RESET << ": "
                    << suggestion.message << "\n";
            }
            if (!suggestion.replacement.empty() && suggestion.span.is_valid())
            {
                out << "  " << BLUE << "|" << RESET << "\n";
                out << "  " << BLUE << "|" << RESET << " "
                    << suggestion.replacement << "\n";
                out << "  " << BLUE << "|" << RESET << "\n";
            }
        }

        out << "\n";
    }

    void DiagEmitter::print_summary(std::ostream &out) const
    {
        const char *RESET = _config.colors ? "\033[0m" : "";
        const char *BOLD = _config.colors ? "\033[1m" : "";
        const char *RED = _config.colors ? "\033[31m" : "";
        const char *YELLOW = _config.colors ? "\033[33m" : "";

        if (_error_count > 0)
        {
            out << BOLD << RED << "error" << RESET << ": aborting due to ";
            if (_error_count == 1)
            {
                out << "previous error";
            }
            else
            {
                out << _error_count << " previous errors";
            }
            if (_warning_count > 0)
            {
                out << "; " << _warning_count << " warning"
                    << (_warning_count == 1 ? "" : "s") << " emitted";
            }
            out << "\n";
        }
        else if (_warning_count > 0)
        {
            out << BOLD << YELLOW << "warning" << RESET << ": "
                << _warning_count << " warning"
                << (_warning_count == 1 ? "" : "s") << " emitted\n";
        }
    }

    //=========================================================================
    // LSP support
    //=========================================================================

    std::vector<DiagEmitter::LspDiagnostic> DiagEmitter::to_lsp() const
    {
        std::vector<LspDiagnostic> result;
        result.reserve(_diagnostics.size());

        for (const auto &diag : _diagnostics)
        {
            LspDiagnostic lsp;

            auto primary = diag.primary_span();
            if (primary)
            {
                lsp.uri = "file://" + primary->file;
                lsp.start_line = primary->start_line;
                lsp.start_col = primary->start_col;
                lsp.end_line = primary->end_line;
                lsp.end_col = primary->end_col;
            }

            switch (diag.level())
            {
            case Diag::Level::Note:
                lsp.severity = "information";
                break;
            case Diag::Level::Warning:
                lsp.severity = "warning";
                break;
            case Diag::Level::Error:
            case Diag::Level::Fatal:
                lsp.severity = "error";
                break;
            }

            lsp.code = ErrorRegistry::format_error_code(diag.code());
            lsp.message = diag.message();

            // Add related spans
            for (const auto &span : diag.spans())
            {
                if (!span.is_primary && span.is_valid())
                {
                    lsp.related.emplace_back(span, span.label);
                }
            }

            result.push_back(std::move(lsp));
        }

        return result;
    }

    //=========================================================================
    // Source file management
    //=========================================================================

    void DiagEmitter::add_source(std::string_view filename, std::string_view content)
    {
        std::vector<std::string> lines;
        std::string current_line;

        for (char c : content)
        {
            if (c == '\n')
            {
                lines.push_back(std::move(current_line));
                current_line.clear();
            }
            else if (c != '\r')
            {
                current_line += c;
            }
        }

        // Don't forget the last line if it doesn't end with newline
        if (!current_line.empty())
        {
            lines.push_back(std::move(current_line));
        }

        _source_cache[std::string(filename)] = std::move(lines);
    }

    void DiagEmitter::add_source_file(std::string_view filename)
    {
        auto file = make_file_from_path(std::string(filename));
        if (file && file->load())
        {
            add_source(filename, file->content());
        }
    }

    std::string_view DiagEmitter::get_line(std::string_view filename, size_t line) const
    {
        auto it = _source_cache.find(std::string(filename));
        if (it == _source_cache.end())
        {
            return "";
        }

        if (line == 0 || line > it->second.size())
        {
            return "";
        }

        return it->second[line - 1]; // Lines are 1-indexed
    }

    bool DiagEmitter::has_source(std::string_view filename) const
    {
        return _source_cache.find(std::string(filename)) != _source_cache.end();
    }

} // namespace Cryo
