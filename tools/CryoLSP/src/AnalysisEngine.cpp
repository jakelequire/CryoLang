#include "LSP/AnalysisEngine.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "Compiler/ModuleLoader.hpp"
#include "AST/ASTNode.hpp"
#include "CLI/ConfigParser.hpp"
#include "LSP/Transport.hpp"

#include <algorithm>
#include <fstream>
#include <filesystem>

namespace CryoLSP
{

    AnalysisEngine::AnalysisEngine()
        : _intrinsics_loaded(false) {}
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

            // Set workspace include path if available
            if (!_workspace_root.empty())
            {
                instance->add_include_path(_workspace_root);
            }

            // Detect whether this file belongs to a Cryo project
            auto project = detectProject(file_path);

            if (project.valid)
            {
                // Project mode: parse + module resolution
                Transport::log("[Project] Detected project '" + project.config.project_name +
                               "' at " + project.project_root + " for file " + file_path);

                bool is_stdlib_project = project.config.stdlib_mode ||
                                        project.config.target_type == "stdlib";
                bool use_stdlib = !project.config.no_std && !is_stdlib_project;

                instance->set_raw_mode(false);
                instance->set_auto_imports_enabled(use_stdlib); // Only auto-import prelude when stdlib is used
                instance->set_stdlib_linking(false);             // No codegen/linking in LSP

                if (is_stdlib_project)
                {
                    instance->set_stdlib_compilation_mode(true);
                    Transport::log("[Project] Stdlib project detected — using project root as stdlib root");
                }

                configureModuleLoader(instance.get(), project, file_path, is_stdlib_project);
                instance->compile_for_lsp_from_content(file_path, content);

                // After parse, run auto-imports (if stdlib enabled) and process explicit imports
                if (instance->ast_root())
                {
                    // Always run auto-import phase — this injects parent module
                    // imports for submodules even in stdlib projects. The prelude/core
                    // auto-imports are already guarded internally.
                    instance->run_auto_import_phase();
                    processImportDeclarations(instance.get(), project, file_path);

                    // Run semantic analysis for real diagnostics (type checking, symbol resolution).
                    // This runs pipeline stages 3-5 (declaration collection, type resolution,
                    // semantic analysis) which catch type mismatches, unknown types, etc.
                    // Wrapped in try/catch because incomplete code can crash analysis passes.
                    try
                    {
                        size_t errors_before = instance->diagnostics() ? instance->diagnostics()->error_count() : 0;
                        instance->run_lsp_semantic_analysis();
                        size_t errors_after = instance->diagnostics() ? instance->diagnostics()->error_count() : 0;
                        Transport::log("[Project] Semantic analysis complete: " +
                                       std::to_string(errors_after - errors_before) + " new errors (total: " +
                                       std::to_string(errors_after) + ")");
                    }
                    catch (const std::exception &e)
                    {
                        Transport::log("[Project] Analysis exception (non-fatal): " + std::string(e.what()));
                    }
                    catch (...)
                    {
                        Transport::log("[Project] Analysis exception (non-fatal, unknown)");
                    }
                }
            }
            else
            {
                // Raw mode (standalone file, no project)
                instance->set_raw_mode(true);
                instance->compile_for_lsp_from_content(file_path, content);

                // Even in raw mode, try to run analysis for basic type checking
                try
                {
                    if (instance->ast_root())
                        instance->run_lsp_semantic_analysis();
                }
                catch (...)
                {
                }
            }

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

    Cryo::CompilerInstance *AnalysisEngine::getIntrinsicsInstance()
    {
        if (!_intrinsics_loaded)
        {
            loadIntrinsics();
        }

        if (_intrinsics_instance)
            return _intrinsics_instance.get();

        return nullptr;
    }

    void AnalysisEngine::loadIntrinsics()
    {
        _intrinsics_loaded = true;

        if (_workspace_root.empty())
            return;

        std::string intrinsics_path = _workspace_root + "/stdlib/core/intrinsics.cryo";

        if (!std::filesystem::exists(intrinsics_path))
        {
            Transport::log("[Intrinsics] File not found: " + intrinsics_path);
            return;
        }

        _intrinsics_file_path = intrinsics_path;

        // Always create a dedicated instance for intrinsics — never reuse from _instances.
        // The _instances map stores instances created by analyzeDocument (via didOpen),
        // which go through parse_source_from_file and may run analysis passes that
        // corrupt the AST. We need a clean, parse-only AST for safe traversal.
        std::ifstream file(intrinsics_path);
        if (!file.is_open())
        {
            Transport::log("[Intrinsics] Failed to open file");
            return;
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        file.close();

        try
        {
            Transport::log("[Intrinsics] Creating dedicated CompilerInstance...");
            auto instance = Cryo::create_compiler_instance();
            instance->set_raw_mode(true);
            Transport::log("[Intrinsics] Calling compile_for_lsp_from_content...");
            instance->compile_for_lsp_from_content(intrinsics_path, content);
            Transport::log("[Intrinsics] compile_for_lsp_from_content returned");

            if (instance->ast_root())
            {
                _intrinsics_instance = std::move(instance);
                Transport::log("[Intrinsics] Loaded " + std::to_string(
                                   _intrinsics_instance->ast_root()->statements().size()) +
                               " declarations from intrinsics.cryo");
            }
            else
            {
                Transport::log("[Intrinsics] Parse produced no AST");
            }
        }
        catch (const std::exception &e)
        {
            Transport::log(std::string("[Intrinsics] Failed to load: ") + e.what());
        }
        catch (...)
        {
            Transport::log("[Intrinsics] Failed to load: unknown error");
        }
    }

    Cryo::CompilerInstance *AnalysisEngine::findModuleInstance(const std::string &module_name)
    {
        for (auto &[path, instance] : _instances)
        {
            if (!instance || !instance->ast_root())
                continue;

            for (const auto &stmt : instance->ast_root()->statements())
            {
                if (!stmt)
                    continue;

                Cryo::ASTNode *check = stmt.get();
                if (auto *decl_stmt = dynamic_cast<Cryo::DeclarationStatementNode *>(check))
                    check = decl_stmt->declaration();

                if (auto *mod = dynamic_cast<Cryo::ModuleDeclarationNode *>(check))
                {
                    if (mod->module_path() == module_name)
                        return instance.get();
                }
            }
        }
        return nullptr;
    }

    Cryo::CompilerInstance *AnalysisEngine::findInstanceByNamespace(const std::string &namespace_name)
    {
        for (auto &[path, instance] : _instances)
        {
            if (!instance)
                continue;
            if (instance->get_namespace_context() == namespace_name)
                return instance.get();
        }
        return nullptr;
    }

    std::string AnalysisEngine::findModuleFilePath(const std::string &module_name)
    {
        for (auto &[path, instance] : _instances)
        {
            if (!instance || !instance->ast_root())
                continue;

            for (const auto &stmt : instance->ast_root()->statements())
            {
                if (!stmt)
                    continue;

                Cryo::ASTNode *check = stmt.get();
                if (auto *decl_stmt = dynamic_cast<Cryo::DeclarationStatementNode *>(check))
                    check = decl_stmt->declaration();

                if (auto *mod = dynamic_cast<Cryo::ModuleDeclarationNode *>(check))
                {
                    if (mod->module_path() == module_name)
                        return path;
                }
            }
        }
        return {};
    }

    ProjectInfo AnalysisEngine::detectProject(const std::string &file_path)
    {
        namespace fs = std::filesystem;

        // Walk up from the file's parent directory looking for cryoconfig
        fs::path dir;
        try
        {
            dir = fs::path(file_path).parent_path();
        }
        catch (...)
        {
            return {};
        }

        // Collect directories we walk through for caching
        std::vector<std::string> walked_dirs;

        while (!dir.empty())
        {
            std::string dir_str = dir.string();

            // Check positive cache
            auto cache_it = _project_cache.find(dir_str);
            if (cache_it != _project_cache.end())
            {
                // Cache all intermediate dirs too
                for (const auto &d : walked_dirs)
                    _project_cache[d] = cache_it->second;
                return cache_it->second;
            }

            // Check negative cache
            if (_non_project_dirs.count(dir_str))
            {
                // This dir and everything below it has no project — but we still
                // need to keep walking up since an ancestor might have one.
                // Only skip if we've already walked through here.
            }

            walked_dirs.push_back(dir_str);

            // Check for cryoconfig in this directory
            fs::path config_path = dir / "cryoconfig";
            if (fs::exists(config_path))
            {
                ProjectInfo info;
                info.config_path = config_path.string();
                info.project_root = dir_str;

                if (Cryo::CLI::ConfigParser::parse_config(info.config_path, info.config))
                {
                    info.valid = true;
                    Transport::log("[Project] Found cryoconfig at " + info.config_path);
                }
                else
                {
                    Transport::log("[Project] Failed to parse cryoconfig at " + info.config_path);
                }

                // Cache for all walked directories
                for (const auto &d : walked_dirs)
                    _project_cache[d] = info;

                return info;
            }

            // Move to parent
            fs::path parent = dir.parent_path();
            if (parent == dir)
                break; // Reached filesystem root
            dir = parent;
        }

        // No project found — add all walked dirs to negative cache
        for (const auto &d : walked_dirs)
            _non_project_dirs.insert(d);

        return {};
    }

    void AnalysisEngine::configureModuleLoader(Cryo::CompilerInstance *instance, const ProjectInfo &project,
                                               const std::string &file_path, bool is_stdlib_project)
    {
        auto *loader = instance->module_loader();
        if (!loader)
            return;

        // Set the current file so relative imports resolve correctly
        loader->set_current_file(file_path);

        if (is_stdlib_project)
        {
            // The project root IS the stdlib — don't auto-detect from executable path
            loader->set_stdlib_root(project.project_root);
            Transport::log("[Project] Set stdlib root to project root: " + project.project_root);
        }
        else
        {
            // Normal project: auto-detect stdlib from the compiler executable path
            if (!loader->auto_detect_stdlib_root())
            {
                Transport::log("[Project] Warning: could not auto-detect stdlib root");
            }
        }

        // Add the project source directory as an include path
        if (!project.config.source_dir.empty())
        {
            std::string source_path = project.project_root + "/" + project.config.source_dir;
            instance->add_include_path(source_path);
        }

        // Also add the project root itself
        instance->add_include_path(project.project_root);
    }

    void AnalysisEngine::processImportDeclarations(Cryo::CompilerInstance *instance,
                                                   const ProjectInfo &project,
                                                   const std::string &file_path)
    {
        if (!instance || !instance->ast_root() || !instance->module_loader())
            return;

        auto *symbol_table = instance->symbol_table();
        if (!symbol_table)
            return;

        auto *loader = instance->module_loader();

        // Defense-in-depth: resolve the current file's path and mark it as "loading"
        // so that if any transitive import chain leads back here, it hits the circular
        // dependency check instead of re-parsing from disk.
        std::string current_resolved;
        try
        {
            auto canonical = std::filesystem::canonical(file_path);
            current_resolved = canonical.string();
        }
        catch (...)
        {
            current_resolved = file_path;
        }

        // Insert current file into loaded modules as a no-op entry so it won't be re-loaded
        auto *loading_set = loader->loading_modules_set();
        bool inserted_guard = false;
        if (loading_set)
        {
            inserted_guard = loading_set->insert(current_resolved).second;
        }

        for (const auto &stmt : instance->ast_root()->statements())
        {
            if (!stmt)
                continue;

            // Import declarations can appear directly or wrapped in DeclarationStatementNode
            Cryo::ASTNode *node = stmt.get();
            if (auto *decl_stmt = dynamic_cast<Cryo::DeclarationStatementNode *>(node))
                node = decl_stmt->declaration();

            auto *import_decl = dynamic_cast<Cryo::ImportDeclarationNode *>(node);
            if (!import_decl)
                continue;

            try
            {
                auto result = loader->load_import(*import_decl);
                if (!result.success)
                {
                    Transport::log("[Project] Import failed for '" + import_decl->module_path() +
                                   "': " + result.error_message);
                    continue;
                }

                if (result.symbol_map.empty())
                    continue;

                if (import_decl->is_specific_import())
                {
                    if (!result.namespace_alias.empty())
                    {
                        // Single specific import treated as namespace alias
                        symbol_table->register_namespace(result.namespace_alias, result.symbol_map);
                    }
                    else
                    {
                        // Multiple specific imports — register symbols directly
                        for (const auto &[name, symbol] : result.symbol_map)
                        {
                            symbol_table->declare_symbol(name, symbol.kind, symbol.location, symbol.type, "");
                        }
                    }
                }
                else
                {
                    // Wildcard import — register as namespace
                    std::string ns_name = import_decl->has_alias()
                                              ? import_decl->alias()
                                              : import_decl->path();
                    symbol_table->register_namespace(ns_name, result.symbol_map);
                }

                Transport::log("[Project] Loaded import '" + import_decl->module_path() +
                               "' with " + std::to_string(result.symbol_map.size()) + " symbols");
            }
            catch (const std::exception &e)
            {
                Transport::log("[Project] Exception processing import '" +
                               import_decl->module_path() + "': " + e.what());
            }
            catch (...)
            {
                Transport::log("[Project] Unknown exception processing import '" +
                               import_decl->module_path() + "'");
            }
        }

        // Remove the guard so we don't leak state into the persistent loader
        if (inserted_guard && loading_set)
        {
            loading_set->erase(current_resolved);
        }
    }

    std::vector<Diagnostic> AnalysisEngine::convertDiagnostics(Cryo::CompilerInstance *instance, const std::string &file_path)
    {
        std::vector<Diagnostic> result;

        if (!instance || !instance->diagnostics())
            return result;

        auto lsp_diags = instance->diagnostics()->to_lsp();

        Transport::log("[Diagnostics] Converting " + std::to_string(lsp_diags.size()) +
                       " diagnostics for " + file_path +
                       " (error_count=" + std::to_string(instance->diagnostics()->error_count()) + ")");

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
