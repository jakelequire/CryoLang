#include "LSP/AnalysisEngine.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "LSP/Transport.hpp"

namespace CryoLSP
{

    AnalysisEngine::AnalysisEngine() = default;
    AnalysisEngine::~AnalysisEngine() = default;

    void AnalysisEngine::setWorkspaceRoot(const std::string &root)
    {
        _workspace_root = root;
    }

    AnalysisResult AnalysisEngine::analyzeDocument(const std::string &file_path, const std::string &content)
    {
        AnalysisResult result;

        try
        {
            // Create a FRESH CompilerInstance each time to avoid stale state.
            // Reusing + clear() caused double reset_state() calls that left
            // internal compiler state inconsistent, leading to segfaults.
            auto instance = Cryo::create_compiler_instance();

            // Set raw mode: no stdlib linking, no auto-imports, no main transform
            instance->set_raw_mode(true);

            // Set workspace include path if available
            if (!_workspace_root.empty())
            {
                instance->add_include_path(_workspace_root);
            }

            // Run frontend-only compilation from content (parse only)
            instance->compile_for_lsp_from_content(file_path, content);

            // Convert diagnostics
            result.diagnostics = convertDiagnostics(instance.get(), file_path);
            result.success = true;

            // Store instance for provider queries (hover, semantic tokens, etc.)
            _instances[file_path] = std::move(instance);
        }
        catch (const std::exception &e)
        {
            Transport::log(std::string("Analysis exception for ") + file_path + ": " + e.what());
            result.success = false;
        }
        catch (...)
        {
            Transport::log(std::string("Unknown exception for ") + file_path);
            result.success = false;
        }

        return result;
    }

    Cryo::CompilerInstance *AnalysisEngine::getCompilerInstance(const std::string &file_path)
    {
        auto it = _instances.find(file_path);
        if (it != _instances.end())
            return it->second.get();
        return nullptr;
    }

    std::vector<Diagnostic> AnalysisEngine::convertDiagnostics(Cryo::CompilerInstance *instance, const std::string &file_path)
    {
        std::vector<Diagnostic> result;

        if (!instance || !instance->diagnostics())
            return result;

        auto lsp_diags = instance->diagnostics()->to_lsp();

        for (const auto &ld : lsp_diags)
        {
            Diagnostic diag;

            // Convert 1-based (compiler) to 0-based (LSP) positions
            diag.range.start.line = static_cast<int>(ld.start_line > 0 ? ld.start_line - 1 : 0);
            diag.range.start.character = static_cast<int>(ld.start_col > 0 ? ld.start_col - 1 : 0);
            diag.range.end.line = static_cast<int>(ld.end_line > 0 ? ld.end_line - 1 : 0);
            diag.range.end.character = static_cast<int>(ld.end_col > 0 ? ld.end_col - 1 : 0);

            // If end equals start, extend to end of identifier/token (at least 1 char)
            if (diag.range.start.line == diag.range.end.line &&
                diag.range.start.character == diag.range.end.character)
            {
                diag.range.end.character += 1;
            }

            // Convert severity
            if (ld.severity == "error")
                diag.severity = DiagnosticSeverity::Error;
            else if (ld.severity == "warning")
                diag.severity = DiagnosticSeverity::Warning;
            else if (ld.severity == "information")
                diag.severity = DiagnosticSeverity::Information;
            else if (ld.severity == "hint")
                diag.severity = DiagnosticSeverity::Hint;
            else
                diag.severity = DiagnosticSeverity::Error;

            diag.code = ld.code;
            diag.message = ld.message;
            diag.source = "cryo";

            result.push_back(std::move(diag));
        }

        return result;
    }

} // namespace CryoLSP
