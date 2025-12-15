#pragma once

#include "LSPTypes.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "GDM/GDM.hpp"
#include <vector>
#include <string>
#include <memory>

namespace CryoLSP
{

    /**
     * @brief Provides real-time diagnostics and error reporting
     *
     * Integrates with the CryoLang compiler's diagnostic system to provide:
     * - Real-time syntax and semantic error reporting
     * - Warning and hint diagnostics
     * - Code action suggestions
     * - Diagnostic severity mapping
     * - Rich error context and suggestions
     */
    class DiagnosticsProvider
    {
    public:
        DiagnosticsProvider(Cryo::CompilerInstance *compiler);
        ~DiagnosticsProvider() = default;

        // Diagnostic generation
        std::vector<Diagnostic> get_diagnostics(const std::string &uri);
        void clear_diagnostics(const std::string &uri);

        // Compilation integration
        bool compile_and_analyze(const std::string &uri, const std::string &content);

    private:
        Cryo::CompilerInstance *_compiler;

        // Diagnostic conversion
        std::vector<Diagnostic> convert_compiler_diagnostics(const std::vector<Cryo::DiagnosticManager::LSPDiagnostic> &compiler_diagnostics);
        Diagnostic convert_single_diagnostic(const Cryo::DiagnosticManager::LSPDiagnostic &compiler_diag);

        // Severity mapping
        DiagnosticSeverity map_compiler_severity(const std::string &severity);

        // Range calculation
        Range calculate_diagnostic_range(const Cryo::DiagnosticManager::LSPDiagnostic &diag, const std::string &content);
        Position calculate_position_from_line_column(size_t line, size_t column);

        // Additional diagnostics
        std::vector<Diagnostic> generate_additional_diagnostics(const std::string &uri, const std::string &content);
        std::vector<Diagnostic> check_style_issues(const std::string &content);
        std::vector<Diagnostic> check_potential_issues(const std::string &content);
    };

} // namespace CryoLSP
