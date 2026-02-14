#pragma once

#include "LSP/Protocol.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

// Forward declare CompilerInstance to avoid pulling all compiler headers into LSP headers
namespace Cryo
{
    class CompilerInstance;
}

namespace CryoLSP
{

    /**
     * @brief Result of analyzing a document
     */
    struct AnalysisResult
    {
        std::vector<Diagnostic> diagnostics;
        bool success = false;
    };

    /**
     * @brief Wraps CompilerInstance for LSP analysis
     *
     * Manages per-file CompilerInstance objects and converts
     * compiler diagnostics to LSP protocol types.
     */
    class AnalysisEngine
    {
    public:
        AnalysisEngine();
        ~AnalysisEngine();

        // Set workspace root for module resolution
        void setWorkspaceRoot(const std::string &root);

        // Analyze a document and return diagnostics
        AnalysisResult analyzeDocument(const std::string &file_path, const std::string &content);

        // Get the CompilerInstance for a file (for provider queries)
        // Returns nullptr if not analyzed yet
        Cryo::CompilerInstance *getCompilerInstance(const std::string &file_path);

        // Get the intrinsics CompilerInstance (parsed from stdlib/core/intrinsics.cryo)
        // Lazily loaded on first call. Returns nullptr if intrinsics file not found.
        Cryo::CompilerInstance *getIntrinsicsInstance();

        // Get the absolute path to the intrinsics.cryo file (empty if not found)
        const std::string &getIntrinsicsFilePath() const { return _intrinsics_file_path; }

    private:
        std::string _workspace_root;
        std::unordered_map<std::string, std::unique_ptr<Cryo::CompilerInstance>> _instances;

        // Intrinsics support
        std::unique_ptr<Cryo::CompilerInstance> _intrinsics_instance;
        std::string _intrinsics_file_path;
        bool _intrinsics_loaded = false;

        void loadIntrinsics();

        // Convert compiler's LspDiagnostic to our Diagnostic
        std::vector<Diagnostic> convertDiagnostics(Cryo::CompilerInstance *instance, const std::string &file_path);
    };

} // namespace CryoLSP
