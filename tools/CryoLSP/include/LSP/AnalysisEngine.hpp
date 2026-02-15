#pragma once

#include "LSP/Protocol.hpp"
#include "CLI/ConfigParser.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
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
     * @brief Cached information about a detected Cryo project
     */
    struct ProjectInfo
    {
        std::string project_root;  // Directory containing cryoconfig
        std::string config_path;   // Full path to cryoconfig file
        Cryo::CLI::CryoConfig config;
        bool valid = false;
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

        // Find a compiler instance for a module by namespace name
        // Searches all loaded instances for one with a matching ModuleDeclarationNode
        Cryo::CompilerInstance *findModuleInstance(const std::string &module_name);

        // Find the file path for a module by namespace name
        // Same search as findModuleInstance but returns the file path string
        std::string findModuleFilePath(const std::string &module_name);

    private:
        std::string _workspace_root;
        std::unordered_map<std::string, std::unique_ptr<Cryo::CompilerInstance>> _instances;

        // Intrinsics support
        std::unique_ptr<Cryo::CompilerInstance> _intrinsics_instance;
        std::string _intrinsics_file_path;
        bool _intrinsics_loaded = false;

        // Project detection cache
        std::unordered_map<std::string, ProjectInfo> _project_cache; // dir -> ProjectInfo (positive cache)
        std::unordered_set<std::string> _non_project_dirs;           // dirs confirmed to have no project

        void loadIntrinsics();

        // Detect whether a file belongs to a Cryo project (has cryoconfig in parent dirs)
        ProjectInfo detectProject(const std::string &file_path);

        // Configure the ModuleLoader for project-aware compilation
        void configureModuleLoader(Cryo::CompilerInstance *instance, const ProjectInfo &project, const std::string &file_path);

        // Walk AST to find and resolve import declarations
        void processImportDeclarations(Cryo::CompilerInstance *instance, const std::string &file_path);

        // Convert compiler's LspDiagnostic to our Diagnostic
        std::vector<Diagnostic> convertDiagnostics(Cryo::CompilerInstance *instance, const std::string &file_path);
    };

} // namespace CryoLSP
