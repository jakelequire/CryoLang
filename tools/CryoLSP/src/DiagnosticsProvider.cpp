#include "DiagnosticsProvider.hpp"
#include <fstream>
#include <sstream>

namespace CryoLSP
{

    DiagnosticsProvider::DiagnosticsProvider(Cryo::CompilerInstance *compiler) : _compiler(compiler)
    {
    }

    std::vector<Diagnostic> DiagnosticsProvider::get_diagnostics(const std::string &uri)
    {
        std::vector<Diagnostic> diagnostics;

        if (!_compiler)
        {
            return diagnostics;
        }

        // Get diagnostics from compiler
        auto diag_emitter = _compiler->diagnostics();
        if (!diag_emitter)
        {
            return diagnostics;
        }

        auto lsp_diagnostics = diag_emitter->to_lsp();
        diagnostics = convert_compiler_diagnostics(lsp_diagnostics);

        return diagnostics;
    }

    void DiagnosticsProvider::clear_diagnostics(const std::string &uri)
    {
        if (!_compiler)
        {
            return;
        }

        auto diag_emitter = _compiler->diagnostics();
        if (diag_emitter)
        {
            diag_emitter->clear();
        }
    }

    bool DiagnosticsProvider::compile_and_analyze(const std::string &uri, const std::string &content)
    {
        if (!_compiler)
        {
            return false;
        }

        try
        {
            // Write content to temporary file if needed, or use in-memory compilation
            // For now, use the compiler's parse_source method for in-memory compilation
            bool success = _compiler->parse_source(content);

            if (success)
            {
                // Run analysis phase
                success = _compiler->analyze();
            }

            return success;
        }
        catch (const std::exception &e)
        {
            return false;
        }
    }

    std::vector<Diagnostic> DiagnosticsProvider::convert_compiler_diagnostics(
        const std::vector<Cryo::DiagEmitter::LspDiagnostic> &compiler_diagnostics)
    {

        std::vector<Diagnostic> diagnostics;

        for (const auto &compiler_diag : compiler_diagnostics)
        {
            diagnostics.push_back(convert_single_diagnostic(compiler_diag));
        }

        return diagnostics;
    }

    Diagnostic DiagnosticsProvider::convert_single_diagnostic(const Cryo::DiagEmitter::LspDiagnostic &compiler_diag)
    {
        Diagnostic diagnostic;

        // Convert position information
        diagnostic.range.start.line = static_cast<int>(compiler_diag.start_line) - 1; // Convert to 0-based
        diagnostic.range.start.character = static_cast<int>(compiler_diag.start_col) - 1;
        diagnostic.range.end.line = static_cast<int>(compiler_diag.end_line) - 1;
        diagnostic.range.end.character = static_cast<int>(compiler_diag.end_col) - 1;

        // Convert severity
        diagnostic.severity = map_compiler_severity(compiler_diag.severity);

        // Set message and code
        diagnostic.message = compiler_diag.message;
        diagnostic.source = "CryoLang";

        if (!compiler_diag.code.empty())
        {
            diagnostic.code = compiler_diag.code;
        }

        return diagnostic;
    }

    DiagnosticSeverity DiagnosticsProvider::map_compiler_severity(const std::string &severity)
    {
        if (severity == "error")
        {
            return DiagnosticSeverity::Error;
        }
        else if (severity == "warning")
        {
            return DiagnosticSeverity::Warning;
        }
        else if (severity == "info")
        {
            return DiagnosticSeverity::Information;
        }
        else
        {
            return DiagnosticSeverity::Hint;
        }
    }

    Range DiagnosticsProvider::calculate_diagnostic_range(const Cryo::DiagEmitter::LspDiagnostic &diag, const std::string &content)
    {
        Range range;

        // Convert line/column to 0-based positions
        range.start.line = static_cast<int>(diag.start_line) - 1;
        range.start.character = static_cast<int>(diag.start_col) - 1;
        range.end.line = static_cast<int>(diag.end_line) - 1;
        range.end.character = static_cast<int>(diag.end_col) - 1;

        // Validate ranges
        if (range.start.line < 0)
            range.start.line = 0;
        if (range.start.character < 0)
            range.start.character = 0;
        if (range.end.line < range.start.line)
            range.end.line = range.start.line;
        if (range.end.line == range.start.line && range.end.character < range.start.character)
        {
            range.end.character = range.start.character;
        }

        return range;
    }

    Position DiagnosticsProvider::calculate_position_from_line_column(size_t line, size_t column)
    {
        Position position;
        position.line = static_cast<int>(line) - 1; // Convert to 0-based
        position.character = static_cast<int>(column) - 1;
        return position;
    }

    std::vector<Diagnostic> DiagnosticsProvider::generate_additional_diagnostics(const std::string &uri, const std::string &content)
    {
        std::vector<Diagnostic> additional_diagnostics;

        // Add style checking
        auto style_diagnostics = check_style_issues(content);
        additional_diagnostics.insert(additional_diagnostics.end(), style_diagnostics.begin(), style_diagnostics.end());

        // Add potential issue detection
        auto potential_issues = check_potential_issues(content);
        additional_diagnostics.insert(additional_diagnostics.end(), potential_issues.begin(), potential_issues.end());

        return additional_diagnostics;
    }

    std::vector<Diagnostic> DiagnosticsProvider::check_style_issues(const std::string &content)
    {
        std::vector<Diagnostic> diagnostics;

        // Example: Check for trailing whitespace
        std::istringstream stream(content);
        std::string line;
        int line_number = 0;

        while (std::getline(stream, line))
        {
            if (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
            {
                Diagnostic diagnostic;
                diagnostic.range.start.line = line_number;
                diagnostic.range.start.character = static_cast<int>(line.find_last_not_of(" \t") + 1);
                diagnostic.range.end.line = line_number;
                diagnostic.range.end.character = static_cast<int>(line.length());
                diagnostic.severity = DiagnosticSeverity::Information;
                diagnostic.message = "Trailing whitespace";
                diagnostic.source = "CryoLang Style";

                diagnostics.push_back(diagnostic);
            }

            line_number++;
        }

        return diagnostics;
    }

    std::vector<Diagnostic> DiagnosticsProvider::check_potential_issues(const std::string &content)
    {
        std::vector<Diagnostic> diagnostics;

        // Example: Check for TODO comments
        std::istringstream stream(content);
        std::string line;
        int line_number = 0;

        while (std::getline(stream, line))
        {
            size_t todo_pos = line.find("TODO");
            if (todo_pos != std::string::npos)
            {
                Diagnostic diagnostic;
                diagnostic.range.start.line = line_number;
                diagnostic.range.start.character = static_cast<int>(todo_pos);
                diagnostic.range.end.line = line_number;
                diagnostic.range.end.character = static_cast<int>(todo_pos + 4);
                diagnostic.severity = DiagnosticSeverity::Information;
                diagnostic.message = "TODO comment found";
                diagnostic.source = "CryoLang";

                diagnostics.push_back(diagnostic);
            }

            line_number++;
        }

        return diagnostics;
    }

} // namespace CryoLSP
