#include "Compiler/ModuleLoader.hpp"
#include "Parser/Parser.hpp"
#include "Lexer/lexer.hpp"
#include "Utils/File.hpp"
#include "Utils/Logger.hpp"
#include "Utils/OS.hpp"
#include "AST/ASTContext.hpp"
#include "Types/Types.hpp"
#include "Types/Type.hpp"
#include "Types/UserDefinedTypes.hpp"
#include "Types/GenericRegistry.hpp"
#include "Types/GenericTypes.hpp"
#include "Types/TypeResolver.hpp"
#include "Diagnostics/Diag.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace Cryo
{
    // Static variable definition
    std::string ModuleLoader::_global_executable_path;

    ModuleLoader::ModuleLoader(SymbolTable &symbol_table, TemplateRegistry &template_registry, ASTContext &ast_context)
        : _symbol_table(symbol_table), _template_registry(template_registry), _ast_context(ast_context)
    {
        // Default stdlib location - can be overridden
        _stdlib_root = "./stdlib";
    }

    void ModuleLoader::set_global_executable_path(const std::string &executable_path)
    {
        _global_executable_path = executable_path;
    }

    void ModuleLoader::set_auto_import_callback(std::function<void(SymbolTable *, const std::string &, const std::string &)> callback)
    {
        _auto_import_callback = callback;
    }

    void ModuleLoader::set_stdlib_root(const std::string &stdlib_root)
    {
        _stdlib_root = stdlib_root;
    }

    bool ModuleLoader::auto_detect_stdlib_root(const std::string &executable_path)
    {
        // Use provided path or global path
        std::string exe_path = executable_path.empty() ? _global_executable_path : executable_path;
        std::string stdlib_path = find_stdlib_directory(exe_path);
        if (!stdlib_path.empty())
        {
            set_stdlib_root(stdlib_path);
            return true;
        }
        return false;
    }

    std::string ModuleLoader::find_stdlib_directory(const std::string &executable_path)
    {
        // Use provided path or fall back to global path (set by set_global_executable_path)
        const std::string &exe_path = executable_path.empty() ? _global_executable_path : executable_path;

        std::vector<std::string> search_paths;

        // If executable path is available, search relative to it
        if (!exe_path.empty())
        {
            try
            {
                // Get the directory containing the executable using OS utility
                auto &os = Cryo::Utils::OS::instance();
                std::string exe_dir = std::filesystem::path(os.absolute_path(exe_path)).parent_path().string();

                // Common relative locations from binary directory:
                // 1. ../stdlib (development setup: bin/cryo, stdlib/)
                // 2. ./stdlib (alternative setup: cryo and stdlib/ in same dir)
                // 3. ../lib/cryo/stdlib (system install: /usr/bin/cryo, /usr/lib/cryo/stdlib)
                // 4. ../../stdlib (nested build setup: build/debug/bin/cryo, stdlib/)
                search_paths.push_back(os.join_path(exe_dir, "../stdlib"));
                search_paths.push_back(os.join_path(exe_dir, "./stdlib"));
                search_paths.push_back(os.join_path(exe_dir, "../lib/cryo/stdlib"));
                search_paths.push_back(os.join_path(exe_dir, "../../stdlib"));
            }
            catch (const std::exception &)
            {
                // If path operations fail, continue with fallback paths
            }
        }

        // Get current working directory for relative fallbacks
        try
        {
            auto &os = Cryo::Utils::OS::instance();
            std::string cwd = os.get_working_directory();

            // Common development and project patterns - try new_stdlib first, then stdlib:
            search_paths.push_back(os.join_path(cwd, "new_stdlib"));       // ./new_stdlib (new structure)
            search_paths.push_back(os.join_path(cwd, "stdlib"));           // ./stdlib (legacy)
            search_paths.push_back(os.join_path(cwd, "../new_stdlib"));    // ../new_stdlib (for project dirs)
            search_paths.push_back(os.join_path(cwd, "../stdlib"));        // ../stdlib (legacy)
            search_paths.push_back(os.join_path(cwd, "../../new_stdlib")); // ../../new_stdlib (for nested dirs)
            search_paths.push_back(os.join_path(cwd, "../../stdlib"));     // ../../stdlib (legacy)
        }
        catch (const std::exception &)
        {
            // If current_path fails, add basic fallbacks
            search_paths.push_back("./new_stdlib");
            search_paths.push_back("./stdlib");
            search_paths.push_back("../new_stdlib");
            search_paths.push_back("../stdlib");
        }

        // System-wide installation paths
        search_paths.push_back("/usr/local/lib/cryo/stdlib"); // Unix/Linux standard
        search_paths.push_back("/usr/lib/cryo/stdlib");       // Unix/Linux alternative
        search_paths.push_back("/opt/cryo/stdlib");           // Unix/Linux /opt

        // Windows system paths (if we're on Windows)
        // Platform-specific search paths
        auto &os = Cryo::Utils::OS::instance();
        if (os.is_windows())
        {
            search_paths.push_back("C:/Program Files/Cryo/stdlib");
            search_paths.push_back("C:/Program Files (x86)/Cryo/stdlib");

            // Also check Windows environment variables
            std::string programfiles = os.get_env_or_default("PROGRAMFILES", "");
            std::string programfiles_x86 = os.get_env_or_default("PROGRAMFILES(X86)", "");
            if (!programfiles.empty())
            {
                search_paths.push_back(os.join_path(programfiles, "Cryo/stdlib"));
            }
            if (!programfiles_x86.empty())
            {
                search_paths.push_back(os.join_path(programfiles_x86, "Cryo/stdlib"));
            }
        }

        // Search for a stdlib directory with core files
        for (const auto &path : search_paths)
        {
            try
            {
                std::filesystem::path stdlib_path = std::filesystem::absolute(path);

                // Check for new stdlib structure first (prelude.cryo + core/_module.cryo)
                std::filesystem::path prelude_file = stdlib_path / "prelude.cryo";
                std::filesystem::path core_module = stdlib_path / "core" / "_module.cryo";

                if (std::filesystem::exists(prelude_file) && std::filesystem::exists(core_module))
                {
                    LOG_DEBUG(LogComponent::GENERAL, "Found new stdlib structure at: {}", stdlib_path.string());
                    return stdlib_path.string();
                }

                // Fallback to old stdlib structure (core/types.cryo + io/stdio.cryo)
                std::filesystem::path core_types = stdlib_path / "core" / "types.cryo";
                std::filesystem::path io_stdio = stdlib_path / "io" / "stdio.cryo";

                if (std::filesystem::exists(core_types) && std::filesystem::exists(io_stdio))
                {
                    LOG_DEBUG(LogComponent::GENERAL, "Found legacy stdlib structure at: {}", stdlib_path.string());
                    return stdlib_path.string();
                }
            }
            catch (const std::exception &)
            {
                // Skip invalid paths
                continue;
            }
        }

        // If nothing found, return empty string
        return "";
    }

    void ModuleLoader::set_current_file(const std::string &current_file_path)
    {
        _current_file_dir = get_parent_directory(current_file_path);
    }

    void ModuleLoader::set_project_root(const std::string &project_root)
    {
        _project_root = project_root;
    }

    ModuleLoader::ImportResult ModuleLoader::load_import(const ImportDeclarationNode &import_node)
    {
        std::string import_path = import_node.path();
        std::string resolved_path = resolve_import_path(import_path);

        // Fallback: treat last path segment as a specific symbol name
        // e.g., "Baz::Qix::Buz" → load "Baz::Qix" module, filter to "Buz"
        // Note: resolve_import_path may return a non-existent path as fallback,
        // so we check file existence rather than just empty string.
        if (!std::filesystem::exists(resolved_path) && import_path.find("::") != std::string::npos)
        {
            size_t last_sep = import_path.rfind("::");
            std::string parent_path = import_path.substr(0, last_sep);
            std::string symbol_name = import_path.substr(last_sep + 2);

            std::string parent_resolved = resolve_import_path(parent_path);
            if (std::filesystem::exists(parent_resolved))
            {
                LOG_DEBUG(LogComponent::GENERAL,
                          "ModuleLoader: Fallback - treating '{}' as specific symbol '{}' from module '{}'",
                          import_path, symbol_name, parent_path);

                // Check cache for parent module
                auto cached = _loaded_modules.find(parent_resolved);
                ImportResult result;
                if (cached != _loaded_modules.end())
                {
                    result = cached->second;
                }
                else if (has_circular_dependency(parent_resolved))
                {
                    // The parent module is already being loaded (we are inside it).
                    // Do a shallow load to get the parent's direct symbols without
                    // recursively expanding its `public module` submodules.
                    LOG_DEBUG(LogComponent::GENERAL,
                              "ModuleLoader: Fallback circular dep on '{}'. Doing shallow load.",
                              parent_resolved);
                    result = load_module_shallow(parent_resolved, parent_path);
                }
                else
                {
                    _loading_modules.insert(parent_resolved);
                    result = load_module_from_file(parent_resolved, parent_path);
                    _loading_modules.erase(parent_resolved);

                    if (result.success)
                    {
                        // Cache the full parent module
                        _loaded_modules[parent_resolved] = result;
                    }
                }

                if (result.success)
                {
                    result.is_local_import = _last_resolve_was_local;
                    if (result.is_local_import && !result.module_name.empty())
                        _all_local_import_names.insert(result.module_name);
                    result = filter_specific_imports(std::move(result), {symbol_name});
                    result.resolved_as_specific = true;
                }
                return result;
            }
        }

        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Loading import '{}' -> '{}'", import_path, resolved_path);
        LOG_DEBUG(LogComponent::GENERAL, "=== IMPORT: Loading '{}' (resolved: '{}') ===", import_path, resolved_path);

        // Check for specific vs wildcard import
        if (import_node.is_specific_import())
        {
            std::string symbols_list;
            for (const auto &symbol : import_node.specific_imports())
            {
                if (!symbols_list.empty())
                    symbols_list += " ";
                symbols_list += symbol;
            }
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Processing specific import with symbols: {}", symbols_list);
        }
        else
        {
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Processing wildcard import");
        }

        // Check if already loaded
        auto cached = _loaded_modules.find(resolved_path);
        if (cached != _loaded_modules.end())
        {
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Using cached module");
            LOG_DEBUG(LogComponent::GENERAL, "=== IMPORT_CACHE: Module '{}' already cached, skipping registration ===", resolved_path);

            // Track cached local imports for transitive dependency IR generation
            if (cached->second.is_local_import && cached->second.success && !cached->second.module_name.empty())
                _all_local_import_names.insert(cached->second.module_name);

            // Create a copy of the cached result to return
            ImportResult cached_result;
            cached_result.success = cached->second.success;
            cached_result.error_message = cached->second.error_message;
            cached_result.module_name = cached->second.module_name;
            cached_result.namespace_alias = cached->second.namespace_alias;
            cached_result.symbol_map = cached->second.symbol_map;
            cached_result.exported_symbols = cached->second.exported_symbols;
            cached_result.is_local_import = cached->second.is_local_import;

            // Apply filtering for specific imports if needed
            if (import_node.is_specific_import() && cached_result.success)
            {
                if (import_node.specific_imports().size() == 1)
                {
                    cached_result.namespace_alias = import_node.specific_imports()[0];
                }
                else
                {
                    cached_result = filter_specific_imports(std::move(cached_result), import_node.specific_imports());
                }
            }

            return cached_result;
        }

        // Check for circular dependency
        if (has_circular_dependency(resolved_path))
        {
            LOG_DEBUG(LogComponent::GENERAL,
                      "ModuleLoader: Circular import detected for '{}'. Performing shallow load (direct symbols only, no submodule expansion).",
                      resolved_path);

            // Instead of failing, do a shallow load: re-parse the _module.cryo file and
            // extract only its directly-defined symbols (enums, structs, functions, etc.)
            // WITHOUT recursively loading `public module` submodules.
            // This breaks the cycle while still making the module's own types available.
            ImportResult result = load_module_shallow(resolved_path, import_path);
            if (result.success)
            {
                // Apply filtering for specific imports if needed
                if (import_node.is_specific_import() && result.success)
                {
                    if (import_node.specific_imports().size() == 1)
                    {
                        result.namespace_alias = import_node.specific_imports()[0];
                    }
                    else
                    {
                        result = filter_specific_imports(std::move(result), import_node.specific_imports());
                    }
                }
                return result;
            }

            // If shallow load also fails, report the circular dependency error
            auto get_import_stack = [this]()
            {
                std::string stack = "\n";
                size_t count = 0;
                for (const auto &mod : _loading_modules)
                {
                    if (count >= 0)
                        stack += "[" + std::to_string(count) + "] -> ";
                    stack += mod;
                    stack += "\n";
                    ++count;
                }
                return stack.empty() ? "(empty)" : stack;
            };
            result.success = false;
            std::string err_msg = "Circular import detected: " + resolved_path + " is already being loaded" + " (import stack: " + get_import_stack() + ")";
            result.error_message = err_msg;
            if (_diagnostics)
            {
                _diagnostics->emit(Diag::error(ErrorCode::E0501_CIRCULAR_IMPORT, result.error_message));
            }
            return result;
        }

        // Load the module
        _loading_modules.insert(resolved_path);
        ImportResult result = load_module_from_file(resolved_path, import_path);
        _loading_modules.erase(resolved_path);

        // Mark whether this is a local project import (not from stdlib)
        result.is_local_import = _last_resolve_was_local;
        if (result.is_local_import && result.success && !result.module_name.empty())
            _all_local_import_names.insert(result.module_name);

        // Handle specific imports - distinguish between namespace aliases and symbol imports
        if (import_node.is_specific_import() && result.success)
        {
            // If we have exactly one specific import, treat it as a namespace alias
            // Otherwise, treat it as symbol imports
            if (import_node.specific_imports().size() == 1)
            {
                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Treating single specific import as namespace alias: {}", import_node.specific_imports()[0]);
                // For namespace alias, we keep all symbols but set the alias
                result.namespace_alias = import_node.specific_imports()[0];
            }
            else
            {
                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Treating multiple specific imports as symbol imports");
                result = filter_specific_imports(std::move(result), import_node.specific_imports());
            }
        }

        // Create a copy for caching before returning
        ImportResult cache_copy;
        cache_copy.success = result.success;
        cache_copy.is_local_import = result.is_local_import;
        cache_copy.error_message = result.error_message;
        cache_copy.module_name = result.module_name;
        cache_copy.namespace_alias = result.namespace_alias;
        cache_copy.symbol_map = result.symbol_map; // This should copy the map
        cache_copy.exported_symbols = result.exported_symbols;

        // Cache the copy
        _loaded_modules[resolved_path] = std::move(cache_copy);

        return result;
    }

    // Case-insensitive path resolution: walks directory segments of `relative_path`
    // against actual filesystem entries under `root`, matching case-insensitively.
    // Resolves all segments EXCEPT the last one (which is the module/file name handled
    // by resolve_module_file_path). Returns the resolved base path with the original
    // last segment appended, or empty string if any directory segment can't be found.
    // For single-segment paths, returns root + segment unchanged.
    static std::string resolve_path_case_insensitive(const std::string &root, const std::string &relative_path)
    {
        namespace fs = std::filesystem;

        // Split relative_path by '/'
        std::vector<std::string> segments;
        size_t start = 0;
        while (start < relative_path.size())
        {
            size_t slash = relative_path.find('/', start);
            if (slash == std::string::npos)
            {
                segments.push_back(relative_path.substr(start));
                break;
            }
            segments.push_back(relative_path.substr(start, slash - start));
            start = slash + 1;
        }
        if (segments.empty())
            return "";

        // Single segment — nothing to resolve case-insensitively
        if (segments.size() == 1)
            return "";

        // Walk directory segments (all except the last), matching case-insensitively
        fs::path current = root;
        for (size_t i = 0; i < segments.size() - 1; ++i)
        {
            if (!fs::is_directory(current))
                return "";

            std::string target_lower = segments[i];
            std::transform(target_lower.begin(), target_lower.end(), target_lower.begin(), ::tolower);

            bool found = false;
            for (const auto &entry : fs::directory_iterator(current))
            {
                if (!entry.is_directory())
                    continue;
                std::string entry_name = entry.path().filename().string();
                std::string entry_lower = entry_name;
                std::transform(entry_lower.begin(), entry_lower.end(), entry_lower.begin(), ::tolower);

                if (entry_lower == target_lower)
                {
                    current = entry.path();
                    found = true;
                    break;
                }
            }
            if (!found)
                return "";
        }

        // Resolve the last segment case-insensitively too — it could be a directory
        // (e.g., AST/) or a file prefix (e.g., node → node.cryo).
        // resolve_module_file_path will add .cryo or /_module.cryo as needed.
        std::string last_seg = segments.back();
        std::string last_lower = last_seg;
        std::transform(last_lower.begin(), last_lower.end(), last_lower.begin(), ::tolower);

        if (fs::is_directory(current))
        {
            for (const auto &entry : fs::directory_iterator(current))
            {
                std::string entry_name = entry.path().filename().string();
                std::string entry_lower = entry_name;
                std::transform(entry_lower.begin(), entry_lower.end(), entry_lower.begin(), ::tolower);

                if (entry_lower == last_lower)
                {
                    return (current / entry_name).string();
                }
            }
        }

        // No case-insensitive match found; fall back to lowercased last segment
        return (current / last_lower).string();
    }

    std::string ModuleLoader::resolve_import_path(const std::string &import_path)
    {
        // Convert module path (core::option) to file path (core/option)
        std::string file_path = import_path;
        // Replace :: with /
        size_t pos = 0;
        while ((pos = file_path.find("::", pos)) != std::string::npos)
        {
            file_path.replace(pos, 2, "/");
            pos += 1; // Move past the inserted '/'
        }

        auto &os = Cryo::Utils::OS::instance();
        _last_resolve_was_local = false;

        // Precompute case-insensitive variants for fallback on case-sensitive filesystems.
        // We try these variants in order:
        //   1. Exact case (e.g., Compiler/CompileMode)
        //   2. Last segment lowercased (e.g., Compiler/compilemode)
        //   3. Last segment as snake_case (e.g., Compiler/compile_mode)  ← NEW
        //   4. Fully lowercased path (e.g., compiler/compilemode)
        //   5. Fully snake_cased path (e.g., compiler/compile_mode)      ← NEW
        std::string lower_file_path = file_path;
        std::transform(lower_file_path.begin(), lower_file_path.end(), lower_file_path.begin(), ::tolower);
        bool has_lower_variant = (lower_file_path != file_path);

        // Helper: convert a PascalCase or camelCase string to snake_case
        auto to_snake_case = [](const std::string &s) -> std::string
        {
            std::string result;
            for (size_t i = 0; i < s.size(); ++i)
            {
                char c = s[i];
                if (std::isupper(c))
                {
                    // Insert underscore before uppercase if:
                    //   - not at start
                    //   - previous char is lowercase, OR
                    //   - next char is lowercase (handles "XMLParser" → "xml_parser")
                    if (i > 0 && (std::islower(s[i - 1]) ||
                                  (i + 1 < s.size() && std::islower(s[i + 1]))))
                    {
                        result += '_';
                    }
                    result += static_cast<char>(std::tolower(c));
                }
                else
                {
                    result += c;
                }
            }
            return result;
        };

        // Build a variant with each path segment converted to snake_case
        auto snake_case_path = [&](const std::string &path) -> std::string
        {
            std::string result;
            size_t start = 0;
            while (start < path.size())
            {
                size_t slash = path.find('/', start);
                if (slash == std::string::npos)
                {
                    result += to_snake_case(path.substr(start));
                    break;
                }
                result += to_snake_case(path.substr(start, slash - start));
                result += '/';
                start = slash + 1;
            }
            return result;
        };

        std::string snake_file_path = snake_case_path(file_path);
        bool has_snake_variant = (snake_file_path != file_path && snake_file_path != lower_file_path);

        // Build a variant with only the last path segment lowercased
        std::string last_seg_lower_path;
        bool has_last_seg_lower = false;
        std::string last_seg_snake_path;
        bool has_last_seg_snake = false;
        {
            size_t last_slash = file_path.rfind('/');
            if (last_slash != std::string::npos)
            {
                std::string dir_part = file_path.substr(0, last_slash + 1);
                std::string file_part = file_path.substr(last_slash + 1);
                std::string lower_file_part = file_part;
                std::transform(lower_file_part.begin(), lower_file_part.end(), lower_file_part.begin(), ::tolower);
                if (lower_file_part != file_part)
                {
                    last_seg_lower_path = dir_part + lower_file_part;
                    has_last_seg_lower = (last_seg_lower_path != file_path && last_seg_lower_path != lower_file_path);
                }

                std::string snake_file_part = to_snake_case(file_part);
                if (snake_file_part != file_part && snake_file_part != lower_file_part)
                {
                    last_seg_snake_path = dir_part + snake_file_part;
                    has_last_seg_snake = (last_seg_snake_path != file_path &&
                                         last_seg_snake_path != lower_file_path &&
                                         last_seg_snake_path != last_seg_lower_path);
                }
            }
        }

        // 1. Try project root first (project-local imports shadow stdlib)
        if (!_project_root.empty())
        {
            // Exact case (import Compiler → Compiler/_module.cryo)
            std::string project_resolved = os.join_path(_project_root, file_path);
            std::string result = resolve_module_file_path(project_resolved, ImportDeclarationNode::ImportStyle::WildcardImport);
            if (std::filesystem::exists(result))
            {
                _last_resolve_was_local = true;
                return result;
            }

            // Last-segment lowercase (import CLI::Commands → CLI/commands.cryo)
            if (has_last_seg_lower)
            {
                project_resolved = os.join_path(_project_root, last_seg_lower_path);
                result = resolve_module_file_path(project_resolved, ImportDeclarationNode::ImportStyle::WildcardImport);
                if (std::filesystem::exists(result))
                {
                    _last_resolve_was_local = true;
                    return result;
                }
            }

            // Last-segment snake_case (import Compiler::CompileMode → Compiler/compile_mode.cryo)
            if (has_last_seg_snake)
            {
                project_resolved = os.join_path(_project_root, last_seg_snake_path);
                result = resolve_module_file_path(project_resolved, ImportDeclarationNode::ImportStyle::WildcardImport);
                if (std::filesystem::exists(result))
                {
                    _last_resolve_was_local = true;
                    return result;
                }
            }

            // Fully lowercase (import Compiler → compiler/_module.cryo)
            if (has_lower_variant)
            {
                project_resolved = os.join_path(_project_root, lower_file_path);
                result = resolve_module_file_path(project_resolved, ImportDeclarationNode::ImportStyle::WildcardImport);
                if (std::filesystem::exists(result))
                {
                    _last_resolve_was_local = true;
                    return result;
                }
            }

            // Fully snake_cased (import Compiler::CompileMode → compiler/compile_mode.cryo)
            if (has_snake_variant)
            {
                project_resolved = os.join_path(_project_root, snake_file_path);
                result = resolve_module_file_path(project_resolved, ImportDeclarationNode::ImportStyle::WildcardImport);
                if (std::filesystem::exists(result))
                {
                    _last_resolve_was_local = true;
                    return result;
                }
            }

            // Case-insensitive filesystem walk: handles mixed-case directories
            // (e.g., import Compiler::AST::Node → compiler/AST/node.cryo)
            {
                std::string ci_path = resolve_path_case_insensitive(_project_root, file_path);
                if (!ci_path.empty())
                {
                    result = resolve_module_file_path(ci_path, ImportDeclarationNode::ImportStyle::WildcardImport);
                    if (std::filesystem::exists(result))
                    {
                        _last_resolve_was_local = true;
                        return result;
                    }
                }
            }
        }

        // 2. Try current file's directory (relative imports within a project)
        if (!_current_file_dir.empty())
        {
            // Exact case
            std::string local_resolved = os.join_path(_current_file_dir, file_path);
            std::string result = resolve_module_file_path(local_resolved, ImportDeclarationNode::ImportStyle::WildcardImport);
            if (std::filesystem::exists(result))
            {
                _last_resolve_was_local = true;
                return result;
            }

            // Last-segment lowercase
            if (has_last_seg_lower)
            {
                local_resolved = os.join_path(_current_file_dir, last_seg_lower_path);
                result = resolve_module_file_path(local_resolved, ImportDeclarationNode::ImportStyle::WildcardImport);
                if (std::filesystem::exists(result))
                {
                    _last_resolve_was_local = true;
                    return result;
                }
            }

            // Last-segment snake_case
            if (has_last_seg_snake)
            {
                local_resolved = os.join_path(_current_file_dir, last_seg_snake_path);
                result = resolve_module_file_path(local_resolved, ImportDeclarationNode::ImportStyle::WildcardImport);
                if (std::filesystem::exists(result))
                {
                    _last_resolve_was_local = true;
                    return result;
                }
            }

            // Fully lowercase
            if (has_lower_variant)
            {
                local_resolved = os.join_path(_current_file_dir, lower_file_path);
                result = resolve_module_file_path(local_resolved, ImportDeclarationNode::ImportStyle::WildcardImport);
                if (std::filesystem::exists(result))
                {
                    _last_resolve_was_local = true;
                    return result;
                }
            }

            // Fully snake_cased
            if (has_snake_variant)
            {
                local_resolved = os.join_path(_current_file_dir, snake_file_path);
                result = resolve_module_file_path(local_resolved, ImportDeclarationNode::ImportStyle::WildcardImport);
                if (std::filesystem::exists(result))
                {
                    _last_resolve_was_local = true;
                    return result;
                }
            }
        }

        // 3. Try stdlib root (standard library imports)
        std::string stdlib_resolved = os.join_path(_stdlib_root, file_path);
        std::string result = resolve_module_file_path(stdlib_resolved, ImportDeclarationNode::ImportStyle::WildcardImport);
        if (std::filesystem::exists(result))
        {
            _last_resolve_was_local = false;
            return result;
        }

        // Last-segment lowercase for stdlib
        if (has_last_seg_lower)
        {
            stdlib_resolved = os.join_path(_stdlib_root, last_seg_lower_path);
            result = resolve_module_file_path(stdlib_resolved, ImportDeclarationNode::ImportStyle::WildcardImport);
            if (std::filesystem::exists(result))
            {
                _last_resolve_was_local = false;
                return result;
            }
        }

        // Last-segment snake_case for stdlib
        if (has_last_seg_snake)
        {
            stdlib_resolved = os.join_path(_stdlib_root, last_seg_snake_path);
            result = resolve_module_file_path(stdlib_resolved, ImportDeclarationNode::ImportStyle::WildcardImport);
            if (std::filesystem::exists(result))
            {
                _last_resolve_was_local = false;
                return result;
            }
        }

        // Fully lowercase for stdlib
        if (has_lower_variant)
        {
            stdlib_resolved = os.join_path(_stdlib_root, lower_file_path);
            result = resolve_module_file_path(stdlib_resolved, ImportDeclarationNode::ImportStyle::WildcardImport);
            if (std::filesystem::exists(result))
            {
                _last_resolve_was_local = false;
                return result;
            }
        }

        // Fully snake_cased for stdlib
        if (has_snake_variant)
        {
            stdlib_resolved = os.join_path(_stdlib_root, snake_file_path);
            result = resolve_module_file_path(stdlib_resolved, ImportDeclarationNode::ImportStyle::WildcardImport);
            if (std::filesystem::exists(result))
            {
                _last_resolve_was_local = false;
                return result;
            }
        }

        // 4. Fallback to stdlib path (will fail at load time with proper error)
        _last_resolve_was_local = false;
        return resolve_module_file_path(stdlib_resolved, ImportDeclarationNode::ImportStyle::WildcardImport);
    }

    std::string ModuleLoader::resolve_module_file_path(const std::string &module_path, ImportDeclarationNode::ImportStyle import_style)
    {
        auto &os = Cryo::Utils::OS::instance();
        std::string abs_path = os.absolute_path(module_path);

        // First, check if the path points to a direct .cryo file
        std::string direct_file = abs_path + ".cryo";
        if (std::filesystem::exists(direct_file))
        {
            return direct_file;
        }

        // Next, check if it's a module directory with _module.cryo
        std::string module_file = os.join_path(abs_path, "_module.cryo");
        if (std::filesystem::exists(module_file))
        {
            return module_file;
        }

        // For backwards compatibility, try the old direct .cryo approach
        return direct_file;
    }

    bool ModuleLoader::is_module_directory(const std::string &path)
    {
        auto &os = Cryo::Utils::OS::instance();
        std::string module_file = os.join_path(path, "_module.cryo");
        return std::filesystem::exists(module_file);
    }

    void ModuleLoader::clear_cache()
    {
        _loaded_modules.clear();
        _loading_modules.clear();
    }

    ModuleLoader::ImportResult ModuleLoader::load_module_from_file(const std::string &file_path, const std::string &import_path)
    {
        ImportResult result;

        // Check if file exists
        if (!std::filesystem::exists(file_path))
        {
            result.success = false;
            result.error_message = "Import file not found: " + import_path + " (resolved to: " + file_path + ")";
            if (_diagnostics)
            {
                _diagnostics->emit(Diag::error(ErrorCode::E0500_MODULE_NOT_FOUND, result.error_message));
            }
            return result;
        }

        // CRITICAL: Save the current module context before parsing the import.
        // The Parser modifies the SymbolTable's current module when it encounters
        // a namespace declaration. Without saving/restoring, the importing module's
        // context gets corrupted, causing types to be registered with wrong module IDs.
        // This must be declared before try block so it's accessible in catch block.
        ModuleID saved_module = _symbol_table.current_module();

        // Save and update _current_file_dir so that recursive imports (e.g., public module
        // declarations in _module.cryo files) resolve relative to the imported file's directory.
        std::string saved_file_dir = _current_file_dir;
        _current_file_dir = get_parent_directory(file_path);

        try
        {
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Reading file {}", file_path);

            // Create a File object from the path
            auto file = make_file_from_path(file_path);
            if (!file)
            {
                _current_file_dir = saved_file_dir;
                result.success = false;
                result.error_message = "Failed to create File object for: " + file_path;
                if (_diagnostics)
                {
                    _diagnostics->emit(Diag::error(ErrorCode::E0502_INVALID_IMPORT, result.error_message));
                }
                return result;
            }

            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Created File object for {}", file_path);

            // Create lexer with the file
            auto lexer = std::make_unique<Lexer>(std::move(file));
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Created Lexer");

            // Use the main ASTContext instead of creating a new one
            // This ensures all types use the same TypeContext and prevents corruption
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Using main ASTContext for type consistency");
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Saved current module context (id={})", saved_module.id);

            // Create parser with the lexer and main context
            Parser parser(std::move(lexer), _ast_context);
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Created Parser");

            // Parse the program
            auto ast = parser.parse_program();

            if (!ast)
            {
                // Restore module context before early return since Parser modified it
                _symbol_table.set_current_module(saved_module);
                _current_file_dir = saved_file_dir;
                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Restored current module context after parse failure (id={})", saved_module.id);

                result.success = false;
                result.error_message = "Failed to parse import file: " + import_path;
                if (_diagnostics)
                {
                    _diagnostics->emit(Diag::error(ErrorCode::E0502_INVALID_IMPORT, result.error_message));
                }
                return result;
            }

            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Successfully parsed {}", import_path);

            // Check if this is a runtime module that needs auto-imports
            if (_auto_import_callback &&
                (import_path.find("runtime/runtime") != std::string::npos ||
                 import_path.find("runtime\\runtime") != std::string::npos))
            {
                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Triggering auto-imports for runtime dependency: {}", import_path);
                _auto_import_callback(&_symbol_table, "Global", import_path);
            }

            // Extract module name from parser's namespace information
            result.module_name = parser.current_namespace();
            if (result.module_name.empty() || result.module_name == "Global")
            {
                // Fall back to filename
                std::filesystem::path path(import_path);
                result.module_name = path.stem().string();
            }

            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Detected module namespace: {}", result.module_name);

            // Mark all declarations in the imported AST with their source module
            // This prevents duplicate IR generation when these declarations are encountered during codegen
            mark_declarations_as_imported(*ast, result.module_name);

            // Step 0: Resolve this module's imports FIRST
            // This ensures types from imported modules (e.g., generic templates like Map)
            // are available in the TemplateRegistry when field types are being registered
            for (const auto &stmt : ast->statements())
            {
                auto *import_decl = dynamic_cast<ImportDeclarationNode *>(stmt.get());
                if (!import_decl)
                    continue;

                LOG_DEBUG(LogComponent::GENERAL,
                          "ModuleLoader: Pre-resolving import '{}' from module '{}'",
                          import_decl->path(), result.module_name);

                auto sub_result = load_import(*import_decl);
                if (!sub_result.success)
                {
                    _symbol_table.set_current_module(saved_module);
                    _current_file_dir = saved_file_dir;
                    result.success = false;
                    result.error_message = sub_result.error_message;
                    return result;
                }
            }

            // Do ALL processing on the original AST and keep it alive
            // 1. Create symbol map from the original AST FIRST
            //    This creates the StructType/ClassType/EnumType with field/variant info
            result.symbol_map = create_symbol_map(*ast, result.module_name);
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Created symbol map with {} symbols", result.symbol_map.size());

            // 2. Register generic templates from the original AST
            //    This must happen AFTER create_symbol_map so it can reuse the types with fields
            register_templates_from_ast(*ast, result.module_name);

            // 2.5. Resolve 'this' parameters that have error types
            //      This must happen AFTER create_symbol_map registers types
            resolve_this_parameters_in_ast(*ast);

            // 2.6. Resolve struct/class method return types that are "unresolved generic" errors
            //      The parser creates ErrorType placeholders for generic return types like
            //      "Types::Outcome<boolean,string>" because it can't resolve generics at parse time.
            //      Since imported modules don't go through the TypeResolutionPass, we must resolve
            //      these here using the TypeResolver after all types and templates are registered.
            resolve_method_return_types_in_ast(*ast, result.module_name);

            // 3. Extract symbols from the original AST
            result.exported_symbols = extract_exported_symbols(*ast);
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Found {} exported symbols", result.exported_symbols.size());

            // Keep the AST alive for template access by storing it
            // Note: Template registry has raw pointers to nodes in this AST
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Storing AST to keep template nodes alive");

            // Handle namespace collision between a parent _module.cryo and its submodule files.
            // When _module.cryo declares "namespace Compiler::Lex;" and "public module Lexer;",
            // the submodule lexer.cryo (also "namespace Compiler::Lex;") gets loaded first during
            // extract_exported_symbols. Both would map to the same AST key "Compiler::Lex".
            //
            // The submodule's AST was stored first (no collision at that time). Now the parent
            // _module.cryo is trying to store under the same key. The parent should keep the
            // namespace key since it defines the core types (e.g., TokenType, Token, SourceLocation)
            // that the submodule depends on. Move the existing submodule AST to a disambiguated key.
            std::string ast_key = result.module_name;
            auto existing_ast_it = _imported_asts.find(ast_key);
            if (existing_ast_it != _imported_asts.end())
            {
                // The existing AST is from a submodule that was loaded during extract_exported_symbols.
                // Derive a disambiguated key from the source file path of the existing AST.
                std::string submodule_key;
                auto *existing_program = existing_ast_it->second.get();
                if (existing_program && !existing_program->statements().empty())
                {
                    // Try to find a unique type name from the submodule AST to build a key.
                    // Look for struct/class declarations to identify it.
                    for (const auto &stmt : existing_program->statements())
                    {
                        if (auto *struct_decl = dynamic_cast<StructDeclarationNode *>(stmt.get()))
                        {
                            submodule_key = ast_key + "::" + struct_decl->name();
                            break;
                        }
                        else if (auto *class_decl = dynamic_cast<ClassDeclarationNode *>(stmt.get()))
                        {
                            submodule_key = ast_key + "::" + class_decl->name();
                            break;
                        }
                    }
                }

                if (submodule_key.empty())
                {
                    // Fallback: use a counter-based suffix
                    submodule_key = ast_key + "::_submodule_" + std::to_string(_imported_asts.size());
                }

                LOG_DEBUG(LogComponent::GENERAL,
                          "ModuleLoader: Namespace collision for '{}'. Moving existing submodule AST to '{}', "
                          "parent _module.cryo keeps '{}'",
                          ast_key, submodule_key, ast_key);

                // Move the existing submodule AST to the disambiguated key
                _imported_asts[submodule_key] = std::move(existing_ast_it->second);
                _imported_asts.erase(existing_ast_it);

                // Track the submodule under its new key for IR generation
                _all_local_import_names.insert(submodule_key);
            }

            _imported_asts[ast_key] = std::move(ast); // Move is necessary to transfer ownership
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Stored AST under key '{}'. Map now has {} entries", ast_key, _imported_asts.size());

            // CRITICAL: Restore the saved module context after import processing.
            // This ensures the importing module's compilation continues with its own module ID,
            // not the imported module's ID.
            _symbol_table.set_current_module(saved_module);
            _current_file_dir = saved_file_dir;
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Restored current module context (id={})", saved_module.id);

            result.success = true;
            return result;
        }
        catch (const std::exception &e)
        {
            // Restore module context even on exception to maintain consistency
            _symbol_table.set_current_module(saved_module);
            _current_file_dir = saved_file_dir;
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Restored current module context after exception (id={})", saved_module.id);

            result.success = false;
            result.error_message = "Exception while loading import '" + import_path + "': " + e.what();
            if (_diagnostics)
            {
                _diagnostics->emit(Diag::error(ErrorCode::E0502_INVALID_IMPORT, result.error_message));
            }
            return result;
        }
    }

    std::vector<std::string> ModuleLoader::extract_exported_symbols(const ProgramNode &ast)
    {
        std::vector<std::string> exported_symbols;

        // For now, we'll consider all top-level declarations as exported
        // In the future, this could be refined with visibility modifiers

        for (const auto &statement : ast.statements())
        {
            if (auto decl = dynamic_cast<DeclarationNode *>(statement.get()))
            {
                if (auto func_decl = dynamic_cast<FunctionDeclarationNode *>(decl))
                {
                    exported_symbols.push_back(func_decl->name());
                }
                else if (auto var_decl = dynamic_cast<VariableDeclarationNode *>(decl))
                {
                    exported_symbols.push_back(var_decl->name());
                }
                else if (auto struct_decl = dynamic_cast<StructDeclarationNode *>(decl))
                {
                    exported_symbols.push_back(struct_decl->name());
                }
                else if (auto class_decl = dynamic_cast<ClassDeclarationNode *>(decl))
                {
                    exported_symbols.push_back(class_decl->name());
                }
                else if (auto enum_decl = dynamic_cast<EnumDeclarationNode *>(decl))
                {
                    exported_symbols.push_back(enum_decl->name());
                }
                else if (auto intrinsic_decl = dynamic_cast<IntrinsicDeclarationNode *>(decl))
                {
                    exported_symbols.push_back(intrinsic_decl->name());
                }
                else if (auto module_decl = dynamic_cast<ModuleDeclarationNode *>(decl))
                {
                    // Handle public module declarations - add re-exported symbol names
                    if (module_decl->is_public())
                    {
                        const std::string &submodule_path = module_decl->module_path();
                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Extracting symbols from public module: {}", submodule_path);

                        // Create a synthetic ImportDeclarationNode for the submodule
                        auto submodule_import = std::make_unique<ImportDeclarationNode>(
                            module_decl->location(),
                            submodule_path);

                        // Load the submodule (will use cache if already loaded)
                        ImportResult submodule_result = load_import(*submodule_import);

                        if (submodule_result.success)
                        {
                            // Add all exported symbols from the submodule
                            for (const auto &sym_name : submodule_result.exported_symbols)
                            {
                                exported_symbols.push_back(sym_name);
                            }
                            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Added {} exported symbols from submodule '{}'",
                                      submodule_result.exported_symbols.size(), submodule_path);
                        }
                    }
                }
            }
        }

        return exported_symbols;
    }

    std::unordered_map<std::string, Symbol> ModuleLoader::create_symbol_map(const ProgramNode &ast, const std::string &module_name)
    {
        std::unordered_map<std::string, Symbol> symbol_map;

        // Get TypeArena for creating proper type objects
        TypeArena &type_arena = _symbol_table.arena();

        // Get or create a proper ModuleID for this module
        ModuleID module_id = _ast_context.modules().get_or_create_module(module_name);

        // Process all top-level declarations
        for (const auto &statement : ast.statements())
        {
            if (auto decl = dynamic_cast<DeclarationNode *>(statement.get()))
            {
                if (auto func_decl = dynamic_cast<FunctionDeclarationNode *>(decl))
                {
                    // Create function symbol with proper type information
                    TypeRef func_type = create_function_type_from_declaration(func_decl, &type_arena);

                    // If the function type was successfully created and the AST node's return type
                    // is still an error (unresolved generic), propagate the resolved return type
                    // back to the AST node so that codegen sees it.
                    if (func_type.is_valid() && !func_type.is_error() &&
                        func_decl->get_resolved_return_type().is_error())
                    {
                        auto *func_type_obj = static_cast<const FunctionType *>(func_type.get());
                        TypeRef resolved_ret = func_type_obj->return_type();
                        if (resolved_ret.is_valid() && !resolved_ret.is_error())
                        {
                            func_decl->set_resolved_return_type(resolved_ret);
                            LOG_DEBUG(LogComponent::GENERAL,
                                      "ModuleLoader: Updated AST return type for '{}' -> {}",
                                      func_decl->name(), resolved_ret->display_name());
                        }
                    }

                    Symbol symbol(func_decl->name(), SymbolKind::Function, func_type, module_id, func_decl->location());
                    symbol.scope = module_name;
                    symbol_map[func_decl->name()] = symbol;
                }
                else if (auto var_decl = dynamic_cast<VariableDeclarationNode *>(decl))
                {
                    // Create variable symbol
                    Symbol symbol(var_decl->name(), SymbolKind::Variable, TypeRef{}, module_id, var_decl->location());
                    symbol.scope = module_name;
                    symbol_map[var_decl->name()] = symbol;

                    // Register module-level constants in TemplateRegistry for cross-module
                    // generic instantiation. Constants with simple literal initializers
                    // are registered so that generic methods can reference them when instantiated
                    // in other modules (before the defining module's IR is generated).
                    if (!var_decl->is_mutable() && var_decl->initializer())
                    {
                        std::string type_ann;
                        if (var_decl->type_annotation())
                            type_ann = var_decl->type_annotation()->to_string();

                        bool registered = false;
                        auto *literal = dynamic_cast<LiteralNode *>(var_decl->initializer());
                        if (literal && (literal->literal_kind() == TokenKind::TK_NUMERIC_CONSTANT ||
                                        literal->literal_kind() == TokenKind::TK_BOOLEAN_LITERAL))
                        {
                            // Check if this is a float constant based on type annotation
                            bool is_float = (type_ann == "f32" || type_ann == "f64");
                            if (is_float)
                            {
                                double float_value = std::stod(literal->value());
                                _template_registry.register_module_constant_float(
                                    module_name, var_decl->name(), type_ann, float_value);
                            }
                            else
                            {
                                uint64_t int_value = 0;
                                if (literal->literal_kind() == TokenKind::TK_BOOLEAN_LITERAL)
                                    int_value = (literal->value() == "true") ? 1 : 0;
                                else
                                    int_value = std::stoull(literal->value());

                                _template_registry.register_module_constant(
                                    module_name, var_decl->name(), type_ann, int_value);
                            }
                            registered = true;
                        }

                        // Handle computed float constants like 1.0 / 0.0 (INFINITY) or 0.0 / 0.0 (NAN)
                        if (!registered && (type_ann == "f32" || type_ann == "f64"))
                        {
                            auto *binary = dynamic_cast<BinaryExpressionNode *>(var_decl->initializer());
                            if (binary && binary->operator_token().kind() == TokenKind::TK_SLASH)
                            {
                                // Try to extract literal values from both sides
                                // Handle both plain literals and unary negation (e.g., -1.0)
                                auto extract_float = [](ExpressionNode *expr, double &out) -> bool
                                {
                                    if (auto *lit = dynamic_cast<LiteralNode *>(expr))
                                    {
                                        if (lit->literal_kind() == TokenKind::TK_NUMERIC_CONSTANT)
                                        {
                                            out = std::stod(lit->value());
                                            return true;
                                        }
                                    }
                                    if (auto *unary = dynamic_cast<UnaryExpressionNode *>(expr))
                                    {
                                        if (unary->operator_token().kind() == TokenKind::TK_MINUS)
                                        {
                                            if (auto *lit = dynamic_cast<LiteralNode *>(unary->operand()))
                                            {
                                                if (lit->literal_kind() == TokenKind::TK_NUMERIC_CONSTANT)
                                                {
                                                    out = -std::stod(lit->value());
                                                    return true;
                                                }
                                            }
                                        }
                                    }
                                    return false;
                                };

                                double lhs_val, rhs_val;
                                if (extract_float(binary->left(), lhs_val) &&
                                    extract_float(binary->right(), rhs_val))
                                {
                                    double result = lhs_val / rhs_val;
                                    _template_registry.register_module_constant_float(
                                        module_name, var_decl->name(), type_ann, result);
                                    registered = true;
                                }
                            }
                        }

                        if (registered)
                        {
                            LOG_DEBUG(LogComponent::GENERAL,
                                      "ModuleLoader: Registered constant '{}::{}' (type: {}) in TemplateRegistry",
                                      module_name, var_decl->name(), type_ann);
                        }
                    }
                }
                else if (auto struct_decl = dynamic_cast<StructDeclarationNode *>(decl))
                {
                    TypeRef struct_type{};
                    bool is_generic = !struct_decl->generic_parameters().empty();

                    // For generic structs, we still need to create the StructType with field info
                    // so that map_instantiated_struct can substitute type parameters later
                    if (is_generic)
                    {
                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Processing generic struct template '{}' with {} type parameters",
                                  struct_decl->name(), struct_decl->generic_parameters().size());

                        // Create a StructType for the generic template
                        QualifiedTypeName struct_qname{module_id, struct_decl->name()};
                        struct_type = type_arena.create_struct(struct_qname);

                        // Build a mapping from type parameter names to GenericParam types
                        std::unordered_map<std::string, TypeRef> param_types;
                        for (size_t i = 0; i < struct_decl->generic_parameters().size(); ++i)
                        {
                            const auto &param = struct_decl->generic_parameters()[i];
                            TypeRef param_type = type_arena.create_generic_param(param->name(), i);
                            param_types[param->name()] = param_type;
                            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Created GenericParam '{}' at index {} for struct '{}'",
                                      param->name(), i, struct_decl->name());
                        }

                        // Build field information with GenericParam types for generic fields
                        if (struct_type.is_valid() && !struct_decl->fields().empty())
                        {
                            std::vector<FieldInfo> fields;
                            for (const auto &field : struct_decl->fields())
                            {
                                if (field)
                                {
                                    TypeRef field_type = field->get_resolved_type();

                                    // If field type is a generic parameter, use the GenericParam type
                                    if (!field_type.is_valid() && field->has_type_annotation())
                                    {
                                        std::string type_str = field->type_annotation()->to_string();

                                        // Check if it's a direct generic parameter
                                        auto it = param_types.find(type_str);
                                        if (it != param_types.end())
                                        {
                                            field_type = it->second;
                                            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Resolved field '{}' to GenericParam '{}' in struct '{}'",
                                                      field->name(), type_str, struct_decl->name());
                                        }
                                        // Check if it's a pointer to a generic parameter (e.g., *T, &T)
                                        else if (type_str.size() > 1 && (type_str[0] == '*' || type_str[0] == '&'))
                                        {
                                            std::string inner = type_str.substr(1);
                                            auto inner_it = param_types.find(inner);
                                            if (inner_it != param_types.end())
                                            {
                                                // Create a pointer type to the generic param
                                                bool is_mut = type_str[0] == '*';
                                                field_type = type_arena.get_pointer_to(inner_it->second);
                                                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Resolved field '{}' to {}GenericParam '{}' in struct '{}'",
                                                          field->name(), is_mut ? "*" : "&", inner, struct_decl->name());
                                            }
                                            else
                                            {
                                                // Try to resolve as regular type
                                                field_type = resolve_primitive_type(type_str, type_arena);
                                            }
                                        }
                                        else
                                        {
                                            // Try to resolve as regular type
                                            field_type = resolve_primitive_type(type_str, type_arena);
                                        }
                                    }

                                    if (field_type.is_valid())
                                    {
                                        fields.push_back(FieldInfo(field->name(), field_type, 0, true, field->is_mutable()));
                                    }
                                    else
                                    {
                                        // For complex generic field types like Array<T>, create a placeholder
                                        // that will be substituted during instantiation
                                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Field '{}' in generic struct '{}' has complex/unresolved type",
                                                  field->name(), struct_decl->name());
                                    }
                                }
                            }

                            // Set fields on the struct type
                            if (!fields.empty())
                            {
                                auto *struct_ptr = const_cast<StructType *>(dynamic_cast<const StructType *>(struct_type.get()));
                                if (struct_ptr)
                                {
                                    struct_ptr->set_fields(std::move(fields));
                                    LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Created generic StructType for '{}' with {} fields",
                                              struct_decl->name(), struct_decl->fields().size());
                                }
                            }
                        }
                    }
                    else
                    {
                        // For non-generic structs, create a proper StructType with field information
                        QualifiedTypeName struct_qname{module_id, struct_decl->name()};
                        struct_type = type_arena.create_struct(struct_qname);

                        // Build field information from the struct declaration
                        if (struct_type.is_valid() && !struct_decl->fields().empty())
                        {
                            std::vector<FieldInfo> fields;
                            for (const auto &field : struct_decl->fields())
                            {
                                if (field)
                                {
                                    TypeRef field_type = field->get_resolved_type();

                                    // If resolved type is not available, try to resolve from type annotation
                                    if (!field_type.is_valid() && field->has_type_annotation())
                                    {
                                        std::string type_str = field->type_annotation()->to_string();
                                        field_type = resolve_primitive_type(type_str, type_arena);
                                        if (field_type.is_valid())
                                        {
                                            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Resolved field '{}' type '{}' from annotation in struct '{}'",
                                                      field->name(), type_str, struct_decl->name());
                                        }
                                    }

                                    if (field_type.is_valid())
                                    {
                                        fields.push_back(FieldInfo(field->name(), field_type, 0, true, field->is_mutable()));
                                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Added field '{}' of type '{}' to struct '{}'",
                                                  field->name(), field_type->display_name(), struct_decl->name());
                                    }
                                    else
                                    {
                                        LOG_WARN(LogComponent::GENERAL, "ModuleLoader: Field '{}' in struct '{}' has no resolved type (annotation: '{}')",
                                                 field->name(), struct_decl->name(),
                                                 field->has_type_annotation() ? field->type_annotation()->to_string() : "none");
                                    }
                                }
                            }

                            // Set fields on the struct type (completes it)
                            if (!fields.empty())
                            {
                                auto *struct_ptr = const_cast<StructType *>(dynamic_cast<const StructType *>(struct_type.get()));
                                if (struct_ptr)
                                {
                                    struct_ptr->set_fields(std::move(fields));
                                    LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Created complete StructType for '{}' with {} fields",
                                              struct_decl->name(), struct_decl->fields().size());
                                }
                            }
                        }
                    }

                    // Create type symbol for struct (with proper type if non-generic)
                    Symbol symbol(struct_decl->name(), SymbolKind::Type, struct_type, module_id, struct_decl->location());
                    symbol.scope = module_name;
                    symbol_map[struct_decl->name()] = symbol;

                    // Also register struct methods with qualified names (TypeName::methodName)
                    // Use type-disambiguated keys to support overloads in the flat symbol_map.
                    // Skip overload disambiguation for generic struct templates — their param
                    // TypeRefs may contain dangling arena pointers for unresolved generic types.
                    // Generic methods are monomorphized at use-site and don't need LLVM forward
                    // declarations via pre_register_functions.
                    bool is_generic_struct = !struct_decl->generic_parameters().empty();
                    for (const auto &method : struct_decl->methods())
                    {
                        if (method)
                        {
                            std::string qualified_method_name = struct_decl->name() + "::" + method->name();
                            TypeRef method_type = create_function_type_from_declaration(method.get(), &type_arena);
                            if (method_type.is_valid())
                            {
                                Symbol method_symbol(qualified_method_name, SymbolKind::Function, method_type, module_id, method->location());
                                method_symbol.scope = module_name;

                                std::string map_key = qualified_method_name;

                                if (!is_generic_struct)
                                {
                                    auto existing_it = symbol_map.find(map_key);
                                    if (existing_it != symbol_map.end())
                                    {
                                        auto *new_func = static_cast<const FunctionType *>(method_type.get());
                                        bool needs_disambig = true;
                                        if (existing_it->second.type.is_valid() && existing_it->second.type->kind() == TypeKind::Function)
                                        {
                                            auto *old_func = static_cast<const FunctionType *>(existing_it->second.type.get());
                                            if (old_func->param_count() == new_func->param_count())
                                            {
                                                // Same arity: check if parameter types actually differ
                                                bool types_match = true;
                                                for (size_t pi = 0; pi < old_func->param_count(); ++pi)
                                                {
                                                    auto &old_p = old_func->param_types()[pi];
                                                    auto &new_p = new_func->param_types()[pi];
                                                    if (old_p.is_valid() && new_p.is_valid() &&
                                                        old_p->display_name() != new_p->display_name())
                                                    {
                                                        types_match = false;
                                                        break;
                                                    }
                                                }
                                                if (types_match)
                                                    needs_disambig = false; // Identical signature, safe to overwrite
                                            }
                                        }
                                        if (needs_disambig)
                                        {
                                            // Build overload suffix from param types for the NEW entry
                                            std::string new_suffix;
                                            if (new_func->param_count() > 0)
                                            {
                                                new_suffix = "(";
                                                for (size_t pi = 0; pi < new_func->param_count(); ++pi)
                                                {
                                                    if (pi > 0) new_suffix += ",";
                                                    auto &p = new_func->param_types()[pi];
                                                    new_suffix += p.is_valid() ? p->display_name() : "?";
                                                }
                                                new_suffix += ")";
                                            }

                                            map_key += "#" + (new_suffix.empty() ? "()" : new_suffix);

                                            // Set qualified_name on the NEW entry so pre_register_functions
                                            // uses the correct LLVM name with overload suffix.
                                            if (!new_suffix.empty())
                                            {
                                                method_symbol.qualified_name = module_name + "::" +
                                                    qualified_method_name + new_suffix;
                                            }

                                            // Also fix the EXISTING entry: give it a qualified_name
                                            // so pre_register_functions creates a correctly-named
                                            // LLVM declaration for it too. For 0-param overloads,
                                            // use the base name (no suffix) so it stays distinct
                                            // from the suffixed overload.
                                            if (existing_it->second.qualified_name.empty())
                                            {
                                                auto *old_func_type = static_cast<const FunctionType *>(
                                                    existing_it->second.type.get());
                                                if (old_func_type && old_func_type->param_count() > 0)
                                                {
                                                    std::string old_suffix = "(";
                                                    for (size_t pi = 0; pi < old_func_type->param_count(); ++pi)
                                                    {
                                                        if (pi > 0) old_suffix += ",";
                                                        auto &p = old_func_type->param_types()[pi];
                                                        old_suffix += p.is_valid() ? p->display_name() : "?";
                                                    }
                                                    old_suffix += ")";
                                                    existing_it->second.qualified_name = module_name + "::" +
                                                        qualified_method_name + old_suffix;
                                                }
                                                else
                                                {
                                                    // 0-param overload: set qualified_name without suffix
                                                    existing_it->second.qualified_name = module_name + "::" +
                                                        qualified_method_name;
                                                }
                                            }
                                        }
                                    }
                                }

                                symbol_map[map_key] = method_symbol;
                                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Added struct method '{}' to symbol map (key='{}')", qualified_method_name, map_key);
                            }
                        }
                    }
                }
                else if (auto class_decl = dynamic_cast<ClassDeclarationNode *>(decl))
                {
                    TypeRef class_type{};
                    bool is_generic = !class_decl->generic_parameters().empty();

                    // For generic classes, we still need to create the ClassType with field info
                    // so that map_instantiated_struct can substitute type parameters later
                    if (is_generic)
                    {
                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Processing generic class template '{}' with {} type parameters",
                                  class_decl->name(), class_decl->generic_parameters().size());

                        // Create a ClassType for the generic template
                        QualifiedTypeName class_qname{module_id, class_decl->name()};
                        class_type = type_arena.create_class(class_qname);

                        // Build a mapping from type parameter names to GenericParam types
                        std::unordered_map<std::string, TypeRef> param_types;
                        for (size_t i = 0; i < class_decl->generic_parameters().size(); ++i)
                        {
                            const auto &param = class_decl->generic_parameters()[i];
                            TypeRef param_type = type_arena.create_generic_param(param->name(), i);
                            param_types[param->name()] = param_type;
                            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Created GenericParam '{}' at index {} for class '{}'",
                                      param->name(), i, class_decl->name());
                        }

                        // Build field information with GenericParam types for generic fields
                        if (class_type.is_valid() && !class_decl->fields().empty())
                        {
                            std::vector<FieldInfo> fields;
                            for (const auto &field : class_decl->fields())
                            {
                                if (field)
                                {
                                    TypeRef field_type = field->get_resolved_type();

                                    // If field type is a generic parameter, use the GenericParam type
                                    if (!field_type.is_valid() && field->has_type_annotation())
                                    {
                                        std::string type_str = field->type_annotation()->to_string();

                                        // Check if it's a direct generic parameter
                                        auto it = param_types.find(type_str);
                                        if (it != param_types.end())
                                        {
                                            field_type = it->second;
                                            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Resolved field '{}' to GenericParam '{}' in class '{}'",
                                                      field->name(), type_str, class_decl->name());
                                        }
                                        // Check if it's a pointer to a generic parameter (e.g., *T, &T)
                                        else if (type_str.size() > 1 && (type_str[0] == '*' || type_str[0] == '&'))
                                        {
                                            std::string inner = type_str.substr(1);
                                            auto inner_it = param_types.find(inner);
                                            if (inner_it != param_types.end())
                                            {
                                                // Create a pointer type to the generic param
                                                bool is_mut = type_str[0] == '*';
                                                field_type = type_arena.get_pointer_to(inner_it->second);
                                                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Resolved field '{}' to {}GenericParam '{}' in class '{}'",
                                                          field->name(), is_mut ? "*" : "&", inner, class_decl->name());
                                            }
                                            else
                                            {
                                                // Try to resolve as regular type
                                                field_type = resolve_primitive_type(type_str, type_arena);
                                            }
                                        }
                                        else
                                        {
                                            // Try to resolve as regular type
                                            field_type = resolve_primitive_type(type_str, type_arena);
                                        }
                                    }

                                    if (field_type.is_valid())
                                    {
                                        fields.push_back(FieldInfo(field->name(), field_type, 0, true, field->is_mutable()));
                                    }
                                    else
                                    {
                                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Field '{}' in generic class '{}' has complex/unresolved type",
                                                  field->name(), class_decl->name());
                                    }
                                }
                            }

                            // Set fields on the class type
                            if (!fields.empty())
                            {
                                auto *class_ptr = const_cast<ClassType *>(dynamic_cast<const ClassType *>(class_type.get()));
                                if (class_ptr)
                                {
                                    class_ptr->set_fields(std::move(fields));
                                    LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Created generic ClassType for '{}' with {} fields",
                                              class_decl->name(), class_decl->fields().size());
                                }
                            }
                        }
                    }
                    else
                    {
                        // For non-generic classes, create a proper ClassType with field information
                        QualifiedTypeName class_qname{module_id, class_decl->name()};
                        class_type = type_arena.create_class(class_qname);

                        // Resolve base class if specified
                        if (class_type.is_valid() && !class_decl->base_class().empty())
                        {
                            auto *class_ptr = const_cast<ClassType *>(dynamic_cast<const ClassType *>(class_type.get()));
                            if (class_ptr && !class_ptr->has_base_class())
                            {
                                TypeRef base_type = _symbol_table.lookup_class_type(class_decl->base_class());
                                if (!base_type.is_valid())
                                {
                                    base_type = type_arena.lookup_type_by_name(class_decl->base_class());
                                }
                                if (base_type.is_valid())
                                {
                                    class_ptr->set_base_class(base_type);
                                    LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Set base class '{}' for class '{}'",
                                              class_decl->base_class(), class_decl->name());
                                }
                                else
                                {
                                    LOG_WARN(LogComponent::GENERAL, "ModuleLoader: Base class '{}' not found for class '{}'",
                                             class_decl->base_class(), class_decl->name());
                                }
                            }
                        }

                        // Build field information from the class declaration
                        if (class_type.is_valid() && !class_decl->fields().empty())
                        {
                            std::vector<FieldInfo> fields;

                            // Prepend inherited base class fields
                            auto *class_ptr = const_cast<ClassType *>(dynamic_cast<const ClassType *>(class_type.get()));
                            if (class_ptr && class_ptr->has_base_class())
                            {
                                auto *base_class = dynamic_cast<const ClassType *>(class_ptr->base_class().get());
                                if (base_class)
                                {
                                    for (const auto &base_field : base_class->fields())
                                    {
                                        fields.push_back(base_field);
                                    }
                                    LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Prepended {} inherited fields from '{}' to class '{}'",
                                              base_class->fields().size(), class_decl->base_class(), class_decl->name());
                                }
                            }

                            for (const auto &field : class_decl->fields())
                            {
                                if (field)
                                {
                                    TypeRef field_type = field->get_resolved_type();

                                    // If resolved type is not available, try to resolve from type annotation
                                    if (!field_type.is_valid() && field->has_type_annotation())
                                    {
                                        std::string type_str = field->type_annotation()->to_string();
                                        field_type = resolve_primitive_type(type_str, type_arena);
                                        if (field_type.is_valid())
                                        {
                                            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Resolved field '{}' type '{}' from annotation in class '{}'",
                                                      field->name(), type_str, class_decl->name());
                                        }
                                    }

                                    if (field_type.is_valid())
                                    {
                                        fields.push_back(FieldInfo(field->name(), field_type, 0, true, field->is_mutable()));
                                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Added field '{}' of type '{}' to class '{}'",
                                                  field->name(), field_type->display_name(), class_decl->name());
                                    }
                                    else
                                    {
                                        LOG_WARN(LogComponent::GENERAL, "ModuleLoader: Field '{}' in class '{}' has no resolved type (annotation: '{}')",
                                                 field->name(), class_decl->name(),
                                                 field->has_type_annotation() ? field->type_annotation()->to_string() : "none");
                                    }
                                }
                            }

                            // Set fields on the class type (completes it)
                            if (!fields.empty())
                            {
                                auto *class_ptr = const_cast<ClassType *>(dynamic_cast<const ClassType *>(class_type.get()));
                                if (class_ptr)
                                {
                                    class_ptr->set_fields(std::move(fields));
                                    LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Created complete ClassType for '{}' with {} fields",
                                              class_decl->name(), class_decl->fields().size());
                                }
                            }

                            // Also register class field types in TemplateRegistry for cross-module
                            // method resolution (same as non-generic structs).
                            {
                                std::string qualified_name = module_name.empty()
                                    ? class_decl->name()
                                    : module_name + "::" + class_decl->name();
                                std::vector<std::string> field_names;
                                std::vector<TypeRef> field_types;
                                std::vector<std::string> field_annotations;

                                for (const auto &field : class_decl->fields())
                                {
                                    if (field)
                                    {
                                        field_names.push_back(field->name());
                                        std::string annotation = field->has_type_annotation()
                                            ? field->type_annotation()->to_string() : "";
                                        field_annotations.push_back(annotation);
                                        TypeRef field_type = field->get_resolved_type();
                                        if (!field_type.is_valid() && field->has_type_annotation())
                                        {
                                            field_type = resolve_primitive_type(
                                                field->type_annotation()->to_string(), type_arena);
                                        }
                                        field_types.push_back(field_type); // May be invalid for complex types
                                    }
                                }

                                if (!field_names.empty())
                                {
                                    _template_registry.register_struct_field_types(
                                        qualified_name, field_names, field_types, module_name, field_annotations);
                                    if (qualified_name != class_decl->name())
                                    {
                                        _template_registry.register_struct_field_types(
                                            class_decl->name(), field_names, field_types, module_name, field_annotations);
                                    }
                                    LOG_DEBUG(LogComponent::GENERAL,
                                              "ModuleLoader: Registered class field types in TemplateRegistry: {} with {} fields",
                                              qualified_name, field_names.size());
                                }
                            }
                        }
                    }

                    // Register virtual/override methods on the ClassType so that
                    // needs_vtable_pointer() returns true and codegen emits vtables.
                    // This mirrors CompilerInstance::populate_type_fields_pass logic.
                    if (class_type.is_valid())
                    {
                        auto *class_ptr = const_cast<ClassType *>(dynamic_cast<const ClassType *>(class_type.get()));
                        if (class_ptr)
                        {
                            for (const auto &method : class_decl->methods())
                            {
                                if (method && (method->is_virtual() || method->is_override()))
                                {
                                    MethodInfo mi(
                                        method->name(),
                                        method->get_resolved_return_type(),
                                        (method->visibility() == Visibility::Public),
                                        method->is_static());
                                    mi.is_virtual = method->is_virtual();
                                    mi.is_override = method->is_override();

                                    // Build overload suffix from parameter types for
                                    // overloaded virtual methods (e.g., visit(ProgramNode*) vs visit(ExpressionNode*)).
                                    {
                                        std::string suffix = "(";
                                        bool first = true;
                                        for (const auto &param : method->parameters())
                                        {
                                            if (!param) continue;
                                            if (param->name() == "this") continue;

                                            if (!first) suffix += ",";
                                            first = false;

                                            TypeRef param_type = param->get_resolved_type();
                                            if (param_type.is_valid() && !param_type.is_error())
                                            {
                                                suffix += param_type->display_name();
                                            }
                                            else if (param->has_type_annotation())
                                            {
                                                suffix += param->type_annotation()->to_string();
                                            }
                                            else
                                            {
                                                suffix += "?";
                                            }
                                        }
                                        suffix += ")";
                                        if (suffix != "()")
                                        {
                                            mi.overload_suffix = suffix;
                                        }
                                    }

                                    class_ptr->add_method(mi);
                                    LOG_DEBUG(LogComponent::GENERAL,
                                              "ModuleLoader: Registered {} method '{}{}' on class '{}'",
                                              method->is_virtual() ? "virtual" : "override",
                                              method->name(), mi.overload_suffix, class_decl->name());
                                }
                            }

                            // Detect abstract class
                            bool is_abstract = false;
                            for (const auto &method : class_decl->methods())
                            {
                                if (method && method->is_virtual() && !method->body())
                                {
                                    is_abstract = true;
                                }
                            }
                            class_ptr->set_abstract(is_abstract);
                        }
                    }

                    // Create type symbol for class (with proper type if non-generic)
                    Symbol symbol(class_decl->name(), SymbolKind::Type, class_type, module_id, class_decl->location());
                    symbol.scope = module_name;
                    symbol_map[class_decl->name()] = symbol;

                    // Also register class methods with qualified names (TypeName::methodName)
                    // Use type-disambiguated keys to support overloads in the flat symbol_map.
                    // Skip overload disambiguation for generic class templates (same as structs).
                    bool is_generic_class = !class_decl->generic_parameters().empty();
                    for (const auto &method : class_decl->methods())
                    {
                        if (method)
                        {
                            std::string qualified_method_name = class_decl->name() + "::" + method->name();
                            TypeRef method_type = create_function_type_from_declaration(method.get(), &type_arena);
                            if (method_type.is_valid())
                            {
                                Symbol method_symbol(qualified_method_name, SymbolKind::Function, method_type, module_id, method->location());
                                method_symbol.scope = module_name;
                                std::string map_key = qualified_method_name;

                                if (!is_generic_class)
                                {
                                    auto existing_it = symbol_map.find(map_key);
                                    if (existing_it != symbol_map.end())
                                    {
                                        auto *new_func = static_cast<const FunctionType *>(method_type.get());
                                        bool same_arity = false;
                                        if (existing_it->second.type.is_valid() && existing_it->second.type->kind() == TypeKind::Function)
                                        {
                                            auto *old_func = static_cast<const FunctionType *>(existing_it->second.type.get());
                                            same_arity = (old_func->param_count() == new_func->param_count());
                                        }
                                        if (!same_arity)
                                            map_key += "#" + std::to_string(new_func->param_count());
                                    }
                                }

                                symbol_map[map_key] = method_symbol;
                                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Added class method '{}' to symbol map (key='{}')", qualified_method_name, map_key);
                            }
                        }
                    }
                }
                else if (auto enum_decl = dynamic_cast<EnumDeclarationNode *>(decl))
                {
                    TypeRef enum_type{};
                    if (!enum_decl->generic_parameters().empty())
                    {
                        // For generic enums, try to look up in symbol table first
                        Symbol *enum_sym = _symbol_table.lookup_symbol(enum_decl->name());
                        if (enum_sym && enum_sym->type.is_valid() && enum_sym->type->kind() == TypeKind::Enum)
                        {
                            enum_type = enum_sym->type;
                            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Found parameterized enum template for {}", enum_decl->name());
                        }
                        else
                        {
                            // Create enum type for generic enums - this is needed for is_enum_type() checks
                            // The type serves as a marker that this is an enum, even if generic
                            QualifiedTypeName enum_qname{module_id, enum_decl->name()};
                            enum_type = type_arena.create_enum(enum_qname);
                            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Created enum type for generic enum '{}'", enum_decl->name());
                        }

                        // Build variants for generic enums too, using GenericParamType for type parameters.
                        // This allows TypeMapper to compute correct payload sizes via substitute_generic_param().
                        if (enum_type.is_valid())
                        {
                            auto *enum_ptr = const_cast<EnumType *>(dynamic_cast<const EnumType *>(enum_type.get()));
                            if (enum_ptr && enum_ptr->variant_count() == 0)
                            {
                                // Collect generic parameter names for identification
                                std::vector<std::string> generic_param_names;
                                for (const auto &gp : enum_decl->generic_parameters())
                                    generic_param_names.push_back(gp->name());

                                std::vector<EnumVariant> variants;
                                size_t tag = 0;
                                for (const auto &variant : enum_decl->variants())
                                {
                                    std::vector<TypeRef> payload_types;
                                    for (const std::string &type_str : variant->associated_types())
                                    {
                                        TypeRef payload_type;

                                        // Check if this is a generic type parameter (e.g., "T", "E")
                                        for (size_t i = 0; i < generic_param_names.size(); ++i)
                                        {
                                            if (type_str == generic_param_names[i])
                                            {
                                                payload_type = type_arena.create_generic_param(type_str, i);
                                                break;
                                            }
                                        }

                                        // If not a type param, resolve as concrete type
                                        if (!payload_type.is_valid())
                                            payload_type = resolve_primitive_type(type_str, type_arena);
                                        if (!payload_type.is_valid())
                                            payload_type = type_arena.lookup_type_by_name(type_str);
                                        if (!payload_type.is_valid())
                                        {
                                            auto it = symbol_map.find(type_str);
                                            if (it != symbol_map.end() && it->second.type.is_valid())
                                                payload_type = it->second.type;
                                        }
                                        // Handle pointer types to module-local types (e.g., "SomeStruct*")
                                        if (!payload_type.is_valid() && type_str.size() > 1 && type_str.back() == '*')
                                        {
                                            std::string base_name = type_str.substr(0, type_str.size() - 1);
                                            auto it = symbol_map.find(base_name);
                                            if (it != symbol_map.end() && it->second.type.is_valid())
                                                payload_type = type_arena.get_pointer_to(it->second.type);
                                        }

                                        payload_types.push_back(payload_type.is_valid() ? payload_type : TypeRef{});
                                    }

                                    variants.push_back(EnumVariant(variant->name(), std::move(payload_types), tag++));
                                }

                                enum_ptr->set_variants(std::move(variants));
                                LOG_DEBUG(LogComponent::GENERAL,
                                          "ModuleLoader: Built {} variants for generic enum '{}'",
                                          enum_ptr->variant_count(), enum_decl->name());
                            }
                        }
                    }
                    else
                    {
                        // For non-generic enums, create a regular enum type
                        QualifiedTypeName enum_qname{module_id, enum_decl->name()};
                        enum_type = type_arena.create_enum(enum_qname);

                        // Build variants and set them on the enum type
                        std::vector<EnumVariant> variants;
                        size_t tag = 0;
                        for (const auto &variant : enum_decl->variants())
                        {
                            // Resolve payload types for this variant
                            std::vector<TypeRef> payload_types;
                            for (const std::string &type_str : variant->associated_types())
                            {
                                TypeRef payload_type;

                                // Try to resolve as primitive first
                                payload_type = resolve_primitive_type(type_str, type_arena);

                                // If not primitive, try to look up user-defined type
                                if (!payload_type.is_valid())
                                {
                                    payload_type = type_arena.lookup_type_by_name(type_str);
                                }

                                // If still not found, check symbol_map (for types in this module)
                                if (!payload_type.is_valid())
                                {
                                    auto it = symbol_map.find(type_str);
                                    if (it != symbol_map.end() && it->second.type.is_valid())
                                    {
                                        payload_type = it->second.type;
                                    }
                                }

                                // Handle pointer types to user-defined types (e.g., "PrimitiveAnnotation*")
                                // resolve_primitive_type handles primitive pointers and arena-registered types,
                                // but we also need to check symbol_map for types defined in this module
                                if (!payload_type.is_valid() && type_str.size() > 1 && type_str.back() == '*')
                                {
                                    std::string base_name = type_str.substr(0, type_str.size() - 1);
                                    auto it = symbol_map.find(base_name);
                                    if (it != symbol_map.end() && it->second.type.is_valid())
                                    {
                                        payload_type = type_arena.get_pointer_to(it->second.type);
                                    }
                                }

                                if (payload_type.is_valid())
                                {
                                    payload_types.push_back(payload_type);
                                    LOG_DEBUG(LogComponent::GENERAL,
                                              "ModuleLoader: Resolved enum variant payload type '{}' for {}::{}",
                                              type_str, enum_decl->name(), variant->name());
                                }
                                else
                                {
                                    // Type not yet resolved - will need to be fixed up later
                                    // For now, push an invalid ref as placeholder
                                    payload_types.push_back(TypeRef{});
                                    LOG_DEBUG(LogComponent::GENERAL,
                                              "ModuleLoader: Deferred enum variant payload type '{}' for {}::{}",
                                              type_str, enum_decl->name(), variant->name());
                                }
                            }

                            variants.push_back(EnumVariant(variant->name(), std::move(payload_types), tag++));
                        }

                        // Set variants on the enum type (completes it)
                        if (enum_type.is_valid())
                        {
                            auto *enum_ptr = const_cast<EnumType *>(dynamic_cast<const EnumType *>(enum_type.get()));
                            if (enum_ptr)
                            {
                                enum_ptr->set_variants(std::move(variants));
                            }
                        }
                    }

                    // Create type symbol for enum
                    Symbol symbol(enum_decl->name(), SymbolKind::Type, enum_type, module_id, enum_decl->location());
                    symbol.scope = module_name;
                    symbol_map[enum_decl->name()] = symbol;
                }
                else if (auto alias_decl = dynamic_cast<TypeAliasDeclarationNode *>(decl))
                {
                    // Handle type aliases - the alias name refers to the target type
                    // Type aliases are transparent: AllocResult is just another name for Result<void*, AllocError>
                    TypeRef target_type{};

                    if (alias_decl->has_resolved_target_type())
                    {
                        // If the target is already resolved, use it directly
                        target_type = alias_decl->get_resolved_target_type();
                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Type alias '{}' -> '{}'",
                                  alias_decl->alias_name(), target_type->display_name());
                    }
                    else if (alias_decl->has_target_type_annotation())
                    {
                        // Target is deferred - will be resolved in TypeResolutionPass
                        // For now, create a placeholder struct with the alias name
                        // This will be replaced when the alias is resolved
                        QualifiedTypeName placeholder_qname{module_id, alias_decl->alias_name()};
                        target_type = type_arena.create_struct(placeholder_qname);
                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Type alias '{}' has deferred target '{}', creating placeholder",
                                  alias_decl->alias_name(), alias_decl->target_type_annotation()->to_string());
                    }
                    else
                    {
                        // Forward declaration - no target type
                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Type alias '{}' is a forward declaration",
                                  alias_decl->alias_name());
                    }

                    // Create type symbol for the alias
                    Symbol symbol(alias_decl->alias_name(), SymbolKind::Type, target_type, module_id, alias_decl->location());
                    symbol.scope = module_name;
                    symbol_map[alias_decl->alias_name()] = symbol;
                    LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Added type alias '{}' to symbol map",
                              alias_decl->alias_name());

                    // Register type alias base name for codegen (::default(), type resolution)
                    // This ensures cross-module aliases are visible to generate_default_value()
                    if (alias_decl->has_target_type_annotation())
                    {
                        std::string target_str = alias_decl->target_type_annotation()->to_string();
                        _ast_context.modules().register_type_alias_base(alias_decl->alias_name(), target_str);
                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Registered type alias base '{}' -> '{}' (from annotation)",
                                  alias_decl->alias_name(), target_str);
                    }
                    else if (alias_decl->has_resolved_target_type())
                    {
                        std::string target_str = alias_decl->get_resolved_target_type()->display_name();
                        _ast_context.modules().register_type_alias_base(alias_decl->alias_name(), target_str);
                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Registered type alias base '{}' -> '{}' (from resolved type)",
                                  alias_decl->alias_name(), target_str);
                    }
                }
                else if (auto intrinsic_decl = dynamic_cast<IntrinsicDeclarationNode *>(decl))
                {
                    // Create intrinsic symbol with proper type information (same as regular functions)
                    TypeRef intrinsic_type = create_function_type_from_declaration(intrinsic_decl, &type_arena);
                    if (intrinsic_type.is_valid())
                    {
                        Symbol symbol(intrinsic_decl->name(), SymbolKind::Intrinsic, intrinsic_type, module_id, intrinsic_decl->location());
                        symbol.scope = module_name;
                        symbol_map[intrinsic_decl->name()] = symbol;
                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Added intrinsic '{}' to symbol map with type '{}'",
                                  intrinsic_decl->name(), intrinsic_type->display_name());
                    }
                    else
                    {
                        LOG_WARN(LogComponent::GENERAL, "ModuleLoader: Skipping intrinsic '{}' - failed to create function type",
                                 intrinsic_decl->name());
                    }
                }
                else if (auto module_decl = dynamic_cast<ModuleDeclarationNode *>(decl))
                {
                    // Handle public module declarations (e.g., "public module core::option;")
                    // These re-export symbols from submodules
                    if (module_decl->is_public())
                    {
                        const std::string &submodule_path = module_decl->module_path();
                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Processing public module declaration: {}", submodule_path);

                        // Create a synthetic ImportDeclarationNode for the submodule
                        auto submodule_import = std::make_unique<ImportDeclarationNode>(
                            module_decl->location(),
                            submodule_path);

                        // Recursively load the submodule
                        ImportResult submodule_result = load_import(*submodule_import);

                        if (submodule_result.success)
                        {
                            // Merge submodule symbols into our symbol map
                            for (const auto &[sym_name, sym] : submodule_result.symbol_map)
                            {
                                // Don't overwrite existing symbols
                                if (symbol_map.find(sym_name) == symbol_map.end())
                                {
                                    symbol_map[sym_name] = sym;
                                    LOG_TRACE(LogComponent::GENERAL, "ModuleLoader: Re-exported symbol '{}' from submodule '{}'",
                                              sym_name, submodule_path);
                                }
                            }
                            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Merged {} symbols from submodule '{}'",
                                      submodule_result.symbol_map.size(), submodule_path);
                        }
                        else
                        {
                            LOG_WARN(LogComponent::GENERAL, "ModuleLoader: Failed to load submodule '{}': {}",
                                     submodule_path, submodule_result.error_message);
                        }
                    }
                }
                else if (auto impl_block = dynamic_cast<ImplementationBlockNode *>(decl))
                {
                    // Process implementation blocks to register methods with qualified names
                    std::string type_name = impl_block->target_type();

                    // Extract base type name (remove generics like "Option<T>" -> "Option")
                    std::string base_type_name = type_name;
                    size_t generic_start = type_name.find('<');
                    if (generic_start != std::string::npos)
                    {
                        base_type_name = type_name.substr(0, generic_start);
                    }

                    LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Processing impl block for '{}' (base: {}) with {} methods",
                              type_name, base_type_name, impl_block->method_implementations().size());

                    // Register each method with qualified name (TypeName::methodName)
                    // Use arity-disambiguated keys to support overloads in the flat symbol_map
                    for (const auto &method : impl_block->method_implementations())
                    {
                        if (method)
                        {
                            std::string qualified_method_name = base_type_name + "::" + method->name();
                            TypeRef method_type = create_function_type_from_declaration(method.get(), &type_arena);
                            if (method_type.is_valid())
                            {
                                Symbol method_symbol(qualified_method_name, SymbolKind::Function, method_type, module_id, method->location());
                                method_symbol.scope = module_name;
                                std::string map_key = qualified_method_name;
                                auto existing_it = symbol_map.find(map_key);
                                if (existing_it != symbol_map.end())
                                {
                                    auto *new_func = static_cast<const FunctionType *>(method_type.get());
                                    bool needs_disambig = true;
                                    if (existing_it->second.type.is_valid() && existing_it->second.type->kind() == TypeKind::Function)
                                    {
                                        auto *old_func = static_cast<const FunctionType *>(existing_it->second.type.get());
                                        if (old_func->param_count() == new_func->param_count())
                                        {
                                            // Same arity: check if parameter types actually differ
                                            bool types_match = true;
                                            for (size_t pi = 0; pi < old_func->param_count(); ++pi)
                                            {
                                                auto &old_p = old_func->param_types()[pi];
                                                auto &new_p = new_func->param_types()[pi];
                                                if (old_p.is_valid() && new_p.is_valid() &&
                                                    old_p->display_name() != new_p->display_name())
                                                {
                                                    types_match = false;
                                                    break;
                                                }
                                            }
                                            if (types_match)
                                                needs_disambig = false; // Identical signature, safe to overwrite
                                        }
                                    }
                                    if (needs_disambig)
                                    {
                                        // Build type-based suffix for disambiguation
                                        std::string suffix;
                                        for (size_t pi = 0; pi < new_func->param_count(); ++pi)
                                        {
                                            if (pi > 0) suffix += ",";
                                            auto &p = new_func->param_types()[pi];
                                            suffix += p.is_valid() ? p->display_name() : "?";
                                        }
                                        map_key += "#(" + suffix + ")";
                                        // Set qualified_name with LLVM-style overload suffix so that
                                        // pre_register_functions creates the correct LLVM function name
                                        // matching the stdlib definition (e.g., String::new(u64)).
                                        if (new_func->param_count() > 0)
                                        {
                                            method_symbol.qualified_name = module_name + "::" +
                                                qualified_method_name + "(" + suffix + ")";
                                        }

                                        // Also fix the EXISTING entry's qualified_name
                                        if (existing_it->second.qualified_name.empty())
                                        {
                                            auto *old_func = static_cast<const FunctionType *>(
                                                existing_it->second.type.get());
                                            if (old_func && old_func->param_count() > 0)
                                            {
                                                std::string old_suffix = "(";
                                                for (size_t pi = 0; pi < old_func->param_count(); ++pi)
                                                {
                                                    if (pi > 0) old_suffix += ",";
                                                    auto &p = old_func->param_types()[pi];
                                                    old_suffix += p.is_valid() ? p->display_name() : "?";
                                                }
                                                old_suffix += ")";
                                                existing_it->second.qualified_name = module_name + "::" +
                                                    qualified_method_name + old_suffix;
                                            }
                                            else
                                            {
                                                // 0-param overload: set qualified_name without suffix
                                                existing_it->second.qualified_name = module_name + "::" +
                                                    qualified_method_name;
                                            }
                                        }
                                    }
                                }
                                symbol_map[map_key] = method_symbol;
                                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Added impl method '{}' to symbol map (key='{}')", qualified_method_name, map_key);
                            }
                            else
                            {
                                // Method type creation failed, possibly due to unresolved generic types
                                // Still add a placeholder symbol so the method is discoverable
                                Symbol method_symbol(qualified_method_name, SymbolKind::Function, TypeRef{}, module_id, method->location());
                                method_symbol.scope = module_name;
                                std::string map_key = qualified_method_name;
                                auto existing_it = symbol_map.find(map_key);
                                if (existing_it != symbol_map.end())
                                {
                                    // For placeholder types, compare arity from AST parameter count
                                    bool same_arity = false;
                                    if (existing_it->second.type.is_valid() && existing_it->second.type->kind() == TypeKind::Function)
                                    {
                                        auto *old_func = static_cast<const FunctionType *>(existing_it->second.type.get());
                                        same_arity = (old_func->param_count() == method->parameters().size());
                                    }
                                    if (!same_arity)
                                        map_key += "#" + std::to_string(method->parameters().size());
                                }
                                symbol_map[map_key] = method_symbol;
                                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Added impl method '{}' to symbol map (key='{}', with placeholder type)", qualified_method_name, map_key);
                            }
                        }
                    }
                }
            }
        }

        // Register all type symbols in the ModuleTypeRegistry under their module ID.
        // This ensures resolve_with_imports can find types from imported modules.
        for (const auto &[name, sym] : symbol_map)
        {
            if (sym.kind == SymbolKind::Type && sym.type.is_valid() && !sym.type.is_error())
            {
                _ast_context.modules().register_type(module_id, name, sym.type);
            }
        }
        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Registered types from '{}' in ModuleTypeRegistry (module_id={})",
                  module_name, module_id.id);

        return symbol_map;
    }

    ModuleLoader::ImportResult ModuleLoader::load_module_shallow(const std::string &file_path, const std::string &import_path)
    {
        ImportResult result;

        if (!std::filesystem::exists(file_path))
        {
            result.success = false;
            result.error_message = "Shallow load: file not found: " + file_path;
            return result;
        }

        // Save context
        ModuleID saved_module = _symbol_table.current_module();
        std::string saved_file_dir = _current_file_dir;
        _current_file_dir = get_parent_directory(file_path);

        try
        {
            auto file = make_file_from_path(file_path);
            if (!file)
            {
                _current_file_dir = saved_file_dir;
                result.success = false;
                result.error_message = "Shallow load: failed to read file: " + file_path;
                return result;
            }

            auto lexer = std::make_unique<Lexer>(std::move(file));
            Parser parser(std::move(lexer), _ast_context);
            auto ast = parser.parse_program();

            if (!ast)
            {
                _symbol_table.set_current_module(saved_module);
                _current_file_dir = saved_file_dir;
                result.success = false;
                result.error_message = "Shallow load: failed to parse: " + file_path;
                return result;
            }

            result.module_name = parser.current_namespace();
            if (result.module_name.empty() || result.module_name == "Global")
            {
                std::filesystem::path path(import_path);
                result.module_name = path.stem().string();
            }

            LOG_DEBUG(LogComponent::GENERAL,
                      "ModuleLoader: Shallow load of '{}' (namespace '{}'). Extracting direct symbols only.",
                      file_path, result.module_name);

            // Extract ONLY directly-defined symbols — skip `public module` declarations.
            // This is a simplified version of create_symbol_map that avoids recursive loading.
            TypeArena &type_arena = _symbol_table.arena();
            ModuleID module_id = _ast_context.modules().get_or_create_module(result.module_name);

            for (const auto &stmt : ast->statements())
            {
                auto *decl = dynamic_cast<DeclarationNode *>(stmt.get());
                if (!decl)
                    continue;

                if (auto *func_decl = dynamic_cast<FunctionDeclarationNode *>(decl))
                {
                    TypeRef func_type = create_function_type_from_declaration(func_decl, &type_arena);
                    if (func_type.is_valid())
                    {
                        Symbol symbol(func_decl->name(), SymbolKind::Function, func_type, module_id, func_decl->location());
                        symbol.scope = result.module_name;
                        result.symbol_map[func_decl->name()] = symbol;
                    }
                    result.exported_symbols.push_back(func_decl->name());
                }
                else if (auto *var_decl = dynamic_cast<VariableDeclarationNode *>(decl))
                {
                    Symbol symbol(var_decl->name(), SymbolKind::Variable, TypeRef(), module_id, var_decl->location());
                    symbol.scope = result.module_name;
                    result.symbol_map[var_decl->name()] = symbol;
                    result.exported_symbols.push_back(var_decl->name());
                }
                if (auto *struct_decl = dynamic_cast<StructDeclarationNode *>(decl))
                {
                    QualifiedTypeName qname{module_id, struct_decl->name()};
                    TypeRef existing = type_arena.lookup_type_by_name(struct_decl->name());
                    if (!existing.is_valid())
                        existing = type_arena.create_struct(qname);
                    Symbol symbol(struct_decl->name(), SymbolKind::Type, existing, module_id, struct_decl->location());
                    symbol.scope = result.module_name;
                    result.symbol_map[struct_decl->name()] = symbol;
                    result.exported_symbols.push_back(struct_decl->name());
                }
                else if (auto *class_decl = dynamic_cast<ClassDeclarationNode *>(decl))
                {
                    QualifiedTypeName qname{module_id, class_decl->name()};
                    TypeRef existing = type_arena.lookup_type_by_name(class_decl->name());
                    if (!existing.is_valid())
                        existing = type_arena.create_class(qname);
                    Symbol symbol(class_decl->name(), SymbolKind::Type, existing, module_id, class_decl->location());
                    symbol.scope = result.module_name;
                    result.symbol_map[class_decl->name()] = symbol;
                    result.exported_symbols.push_back(class_decl->name());
                }
                else if (auto *enum_decl = dynamic_cast<EnumDeclarationNode *>(decl))
                {
                    QualifiedTypeName qname{module_id, enum_decl->name()};
                    TypeRef existing = type_arena.lookup_type_by_name(enum_decl->name());
                    if (!existing.is_valid())
                        existing = type_arena.create_enum(qname);
                    Symbol symbol(enum_decl->name(), SymbolKind::Type, existing, module_id, enum_decl->location());
                    symbol.scope = result.module_name;
                    result.symbol_map[enum_decl->name()] = symbol;
                    result.exported_symbols.push_back(enum_decl->name());

                    // Also register enum variants
                    for (const auto &variant : enum_decl->variants())
                    {
                        if (auto *ev = dynamic_cast<EnumVariantNode *>(variant.get()))
                        {
                            Symbol variant_sym(ev->name(), SymbolKind::EnumVariant, existing, module_id, ev->location());
                            variant_sym.scope = result.module_name;
                            result.symbol_map[ev->name()] = variant_sym;
                        }
                    }
                }
                else if (auto *trait_decl = dynamic_cast<TraitDeclarationNode *>(decl))
                {
                    QualifiedTypeName qname{module_id, trait_decl->name()};
                    TypeRef existing = type_arena.lookup_type_by_name(trait_decl->name());
                    if (!existing.is_valid())
                        existing = type_arena.create_trait(qname);
                    Symbol symbol(trait_decl->name(), SymbolKind::Type, existing, module_id, trait_decl->location());
                    symbol.scope = result.module_name;
                    result.symbol_map[trait_decl->name()] = symbol;
                    result.exported_symbols.push_back(trait_decl->name());
                }
                else if (auto *alias_decl = dynamic_cast<TypeAliasDeclarationNode *>(decl))
                {
                    Symbol symbol(alias_decl->alias_name(), SymbolKind::Type, TypeRef(), module_id, alias_decl->location());
                    symbol.scope = result.module_name;
                    result.symbol_map[alias_decl->alias_name()] = symbol;
                    result.exported_symbols.push_back(alias_decl->alias_name());
                }
                else if (auto *intrinsic_decl = dynamic_cast<IntrinsicDeclarationNode *>(decl))
                {
                    TypeRef intrinsic_type = create_function_type_from_declaration(intrinsic_decl, &type_arena);
                    if (intrinsic_type.is_valid())
                    {
                        Symbol symbol(intrinsic_decl->name(), SymbolKind::Intrinsic, intrinsic_type, module_id, intrinsic_decl->location());
                        symbol.scope = result.module_name;
                        result.symbol_map[intrinsic_decl->name()] = symbol;
                    }
                    result.exported_symbols.push_back(intrinsic_decl->name());
                }
                // NOTE: ModuleDeclarationNode (public module X;) is intentionally skipped
                // to avoid recursive loading that would cause the circular dependency.
            }

            LOG_DEBUG(LogComponent::GENERAL,
                      "ModuleLoader: Shallow load complete for '{}'. Got {} direct symbols.",
                      file_path, result.symbol_map.size());

            // Restore context
            _symbol_table.set_current_module(saved_module);
            _current_file_dir = saved_file_dir;

            result.success = true;
            return result;
        }
        catch (const std::exception &e)
        {
            _symbol_table.set_current_module(saved_module);
            _current_file_dir = saved_file_dir;
            result.success = false;
            result.error_message = std::string("Shallow load exception: ") + e.what();
            return result;
        }
    }

    bool ModuleLoader::has_circular_dependency(const std::string &module_path)
    {
        return _loading_modules.find(module_path) != _loading_modules.end();
    }

    TypeRef ModuleLoader::resolve_generic_type_string(const std::string &type_str, TypeArena *type_arena)
    {
        // Parse "Maybe<int>" -> base="Maybe", args=["int"]
        size_t angle_pos = type_str.find('<');
        if (angle_pos == std::string::npos || type_str.back() != '>')
            return TypeRef{};

        std::string base_name = type_str.substr(0, angle_pos);
        std::string args_str = type_str.substr(angle_pos + 1, type_str.size() - angle_pos - 2);

        // Look up the generic base type in the arena
        TypeRef generic_base = type_arena->lookup_type_by_name(base_name);
        if (!generic_base.is_valid())
            return TypeRef{};

        // Parse comma-separated type arguments, respecting nested angle brackets
        std::vector<TypeRef> type_args;
        size_t start = 0;
        int depth = 0;
        for (size_t i = 0; i <= args_str.size(); ++i)
        {
            if (i == args_str.size() || (args_str[i] == ',' && depth == 0))
            {
                std::string arg = args_str.substr(start, i - start);
                while (!arg.empty() && std::isspace(arg.front()))
                    arg.erase(0, 1);
                while (!arg.empty() && std::isspace(arg.back()))
                    arg.pop_back();

                TypeRef resolved_arg;
                // Try resolving as a nested generic first
                if (arg.find('<') != std::string::npos)
                    resolved_arg = resolve_generic_type_string(arg, type_arena);
                // Then try arena lookup (handles primitives and named types)
                if (!resolved_arg.is_valid())
                    resolved_arg = type_arena->lookup_type_by_name(arg);
                // Try common primitive aliases
                if (!resolved_arg.is_valid())
                {
                    if (arg == "int")
                        resolved_arg = type_arena->get_i32();
                    else if (arg == "string")
                        resolved_arg = type_arena->get_string();
                    else if (arg == "boolean")
                        resolved_arg = type_arena->get_bool();
                    else if (arg == "float")
                        resolved_arg = type_arena->get_f32();
                    else if (arg == "double")
                        resolved_arg = type_arena->get_f64();
                    else if (arg == "char")
                        resolved_arg = type_arena->get_char();
                    else if (arg == "void")
                        resolved_arg = type_arena->get_void();
                }

                if (!resolved_arg.is_valid())
                    return TypeRef{};
                type_args.push_back(resolved_arg);
                start = i + 1;
            }
            else if (args_str[i] == '<')
            {
                depth++;
            }
            else if (args_str[i] == '>')
            {
                depth--;
            }
        }

        if (type_args.empty())
            return TypeRef{};

        TypeRef result = type_arena->create_instantiation(generic_base, std::move(type_args));
        if (result.is_valid())
        {
            type_arena->register_instantiated_by_name(result);
            LOG_DEBUG(LogComponent::GENERAL,
                      "ModuleLoader: Resolved generic type string '{}' -> {}",
                      type_str, result->display_name());
        }
        return result;
    }

    TypeRef ModuleLoader::create_function_type_from_declaration(const FunctionDeclarationNode *func_decl, TypeArena *type_arena)
    {
        if (!func_decl || !type_arena)
        {
            return TypeRef{};
        }

        // Get return type
        TypeRef return_type = func_decl->get_resolved_return_type();
        const std::string return_type_str = return_type.is_valid() ? return_type->display_name() : "void";
        if (!return_type.is_valid() || return_type_str == "void")
        {
            // Default to void for functions without explicit return type or explicit void
            return_type = type_arena->get_void();
        }

        // If return type is an error containing an unresolved generic, try to resolve it now.
        // This happens when the parser defers generic return types like Maybe<int> as error types.
        if (return_type.is_error())
        {
            const std::string display = return_type->display_name();
            const std::string prefix = "<error: unresolved generic: ";
            if (display.find(prefix) == 0 && display.back() == '>')
            {
                std::string generic_str = display.substr(prefix.size(), display.size() - prefix.size() - 1);
                TypeRef resolved = resolve_generic_type_string(generic_str, type_arena);
                if (resolved.is_valid())
                {
                    LOG_DEBUG(LogComponent::GENERAL,
                              "ModuleLoader: Resolved unresolved generic return type for '{}': {} -> {}",
                              func_decl->name(), display, resolved->display_name());
                    return_type = resolved;
                }
            }
        }

        // Get parameter types
        std::vector<TypeRef> parameter_types;
        for (const auto &param : func_decl->parameters())
        {
            TypeRef param_type = param->get_resolved_type();
            if (param_type.is_valid())
            {
                parameter_types.push_back(param_type);
            }
            else
            {
                const std::string param_type_str = param_type.is_valid() ? param_type->display_name() : "unknown";
                std::cerr << "Warning: Failed to get resolved type for parameter '" << param->name()
                          << "' (type: " << param_type_str << ") in function '" << func_decl->name() << "'" << std::endl;
                return TypeRef{};
            }
        }

        // Create FunctionType
        return type_arena->get_function(return_type, parameter_types, func_decl->is_variadic());
    }

    TypeRef ModuleLoader::create_function_type_from_declaration(const IntrinsicDeclarationNode *intrinsic_decl, TypeArena *type_arena)
    {
        if (!intrinsic_decl || !type_arena)
        {
            return TypeRef{};
        }

        const std::string &intrinsic_name = intrinsic_decl->name();
        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Creating function type for intrinsic '{}'", intrinsic_name);

        // Get return type
        TypeRef return_type = intrinsic_decl->get_resolved_return_type();
        const std::string return_type_str = return_type.is_valid() ? return_type->display_name() : "void";

        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Intrinsic '{}' has_resolved_return_type={}, return_type_str='{}'",
                  intrinsic_name, intrinsic_decl->has_resolved_return_type(), return_type_str);

        if (!return_type.is_valid() || return_type_str == "void")
        {
            // Default to void for intrinsics without explicit return type or explicit void
            return_type = type_arena->get_void();
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Intrinsic '{}' using void return type", intrinsic_name);
        }

        // Get parameter types
        std::vector<TypeRef> parameter_types;
        for (const auto &param : intrinsic_decl->parameters())
        {
            TypeRef param_type = param->get_resolved_type();
            if (param_type.is_valid())
            {
                parameter_types.push_back(param_type);
                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Intrinsic '{}' param '{}' resolved to '{}'",
                          intrinsic_name, param->name(), param_type->display_name());
            }
            else
            {
                LOG_WARN(LogComponent::GENERAL, "ModuleLoader: Failed to get resolved type for parameter '{}' in intrinsic '{}' - intrinsic will not be available",
                         param->name(), intrinsic_name);
                return TypeRef{};
            }
        }

        // Create FunctionType
        TypeRef func_type = type_arena->get_function(return_type, parameter_types);
        if (func_type.is_valid())
        {
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Successfully created function type for intrinsic '{}': {}",
                      intrinsic_name, func_type->display_name());
        }
        else
        {
            LOG_WARN(LogComponent::GENERAL, "ModuleLoader: Failed to create function type for intrinsic '{}'", intrinsic_name);
        }
        return func_type;
    }

    ModuleLoader::ImportResult ModuleLoader::filter_specific_imports(ImportResult result, const std::vector<std::string> &specific_imports)
    {
        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Filtering imports for specific symbols");

        if (!result.success)
        {
            return result;
        }

        // Create filtered symbol map and exported symbols list
        std::unordered_map<std::string, Symbol> filtered_symbol_map;
        std::vector<std::string> filtered_exported_symbols;

        for (const std::string &import_name : specific_imports)
        {
            // Check if the symbol exists in the module
            auto symbol_it = result.symbol_map.find(import_name);
            if (symbol_it != result.symbol_map.end())
            {
                filtered_symbol_map[import_name] = symbol_it->second;
                filtered_exported_symbols.push_back(import_name);
                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Found and included symbol: {}", import_name);
            }
            else
            {
                // Symbol not found - this should be a compilation error
                result.success = false;
                result.error_message = "Symbol '" + import_name + "' not found in module '" + result.module_name + "'";
                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Error - Symbol not found: {}", import_name);
                return result;
            }

            // Also include methods of this type (TypeName::method patterns)
            std::string prefix = import_name + "::";
            for (const auto &[name, sym] : result.symbol_map)
            {
                if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0)
                {
                    filtered_symbol_map[name] = sym;
                    LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Also included method symbol: {}", name);
                }
            }
        }

        // Update the result with filtered symbols
        result.symbol_map = std::move(filtered_symbol_map);
        result.exported_symbols = std::move(filtered_exported_symbols);

        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Filtered to {} symbols", result.symbol_map.size());
        return result;
    }

    std::string ModuleLoader::get_parent_directory(const std::string &file_path)
    {
        std::filesystem::path path(file_path);
        return path.parent_path().string();
    }

    void ModuleLoader::register_templates_from_ast(const ProgramNode &ast, const std::string &module_name)
    {
        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Registering templates from module: {}", module_name);
        LOG_DEBUG(LogComponent::GENERAL, "=== STRUCT_REG: Starting registration for module '{}' ===", module_name);

        // Process all top-level declarations and register generic templates
        for (const auto &statement : ast.statements())
        {
            if (auto decl = dynamic_cast<DeclarationNode *>(statement.get()))
            {
                if (auto class_decl = dynamic_cast<ClassDeclarationNode *>(decl))
                {
                    // Register generic class templates
                    if (!class_decl->generic_parameters().empty())
                    {
                        _template_registry.register_class_template(
                            class_decl->name(), // base_name
                            class_decl,         // template_node
                            module_name,        // module_namespace
                            "imported_module"   // source_file (placeholder)
                        );
                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Registered generic class template: {} from module: {}", class_decl->name(), module_name);

                        // Also register in GenericRegistry for the new type system
                        if (_generic_registry)
                        {
                            // Skip if template is already registered - don't overwrite existing one with fields
                            if (_generic_registry->get_template_by_name(class_decl->name()))
                            {
                                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Template '{}' already registered in GenericRegistry, skipping to preserve field info",
                                          class_decl->name());
                            }
                            else
                            {
                                TypeArena &arena = _ast_context.types();

                                // Look up the existing ClassType created by create_symbol_map (which has fields)
                                // instead of creating a new empty one
                                TypeRef class_type = arena.lookup_type_by_name(class_decl->name());
                                if (!class_type.is_valid())
                                {
                                    // Fallback: create a new one if not found (shouldn't happen normally)
                                    class_type = arena.create_class(QualifiedTypeName{ModuleID::invalid(), class_decl->name()});
                                    LOG_WARN(LogComponent::GENERAL, "ModuleLoader: Had to create new ClassType for '{}' - fields may be missing",
                                             class_decl->name());
                                }
                                else
                                {
                                    LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Using existing ClassType for '{}' with fields",
                                              class_decl->name());
                                }

                                std::vector<GenericParam> params;
                                for (size_t i = 0; i < class_decl->generic_parameters().size(); ++i)
                                {
                                    auto &param_node = class_decl->generic_parameters()[i];
                                    TypeRef param_type = arena.create_generic_param(param_node->name(), i);
                                    params.emplace_back(param_node->name(), i, param_type);
                                }
                                _generic_registry->register_template(class_type, params,
                                                                     ModuleID::invalid(), class_decl, class_decl->name());
                                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Registered class '{}' in GenericRegistry with {} params",
                                          class_decl->name(), params.size());
                            }
                        }
                    }

                    else
                    {
                        // For non-generic classes, register their field types for cross-module resolution
                        // (mirrors the non-generic struct handling above)
                        std::string qualified_name = module_name.empty() ? class_decl->name() : module_name + "::" + class_decl->name();
                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Processing non-generic class '{}' (qualified: '{}')", class_decl->name(), qualified_name);

                        std::vector<std::string> field_names;
                        std::vector<TypeRef> field_types;
                        std::vector<std::string> field_annotations;

                        for (const auto &field : class_decl->fields())
                        {
                            if (field)
                            {
                                field_names.push_back(field->name());
                                std::string annotation = field->has_type_annotation() ? field->type_annotation()->to_string() : "";
                                field_annotations.push_back(annotation);
                                TypeRef field_type = field->get_resolved_type();
                                if (field_type.is_valid() && !field_type.is_error())
                                {
                                    field_types.push_back(field_type);
                                }
                                else
                                {
                                    std::string type_str = field->type_annotation() ? field->type_annotation()->to_string() : "unknown";
                                    field_type = resolve_primitive_type(type_str, _ast_context.types());
                                    if (field_type.is_valid() && !field_type.is_error())
                                    {
                                        field_types.push_back(field_type);
                                    }
                                    else
                                    {
                                        Symbol *type_sym = _symbol_table.lookup_symbol(type_str);
                                        if (type_sym && type_sym->type.is_valid() && !type_sym->type.is_error())
                                        {
                                            field_type = type_sym->type;
                                        }
                                        field_types.push_back(field_type);
                                    }
                                }
                            }
                        }

                        if (!field_types.empty())
                        {
                            _template_registry.register_struct_field_types(qualified_name, field_names, field_types, module_name, field_annotations);
                            if (qualified_name != class_decl->name())
                            {
                                _template_registry.register_struct_field_types(class_decl->name(), field_names, field_types, module_name, field_annotations);
                            }
                            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Registered class field types: {} with {} fields", qualified_name, field_types.size());
                        }
                    }

                    // Register method return type annotations for class methods
                    for (const auto &method : class_decl->methods())
                    {
                        if (method)
                        {
                            std::string qualified_type = module_name.empty() ? class_decl->name() : module_name + "::" + class_decl->name();
                            std::string return_type_str = method->return_type_annotation() ? method->return_type_annotation()->to_string() : "";
                            std::string qualified_method_name = qualified_type + "::" + method->name();

                            _template_registry.register_method_is_static(qualified_method_name, method->is_static());

                            if (!return_type_str.empty())
                            {
                                std::string qualified_return_type = qualify_type_annotation(return_type_str);
                                _template_registry.register_method_return_type_annotation(qualified_method_name, qualified_return_type);
                                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Registered class method return type annotation: {} -> {}",
                                          qualified_method_name, qualified_return_type);

                                TypeArena &arena = _ast_context.types();
                                TypeRef return_type = resolve_primitive_type(return_type_str, arena);
                                if (return_type.is_valid())
                                {
                                    _template_registry.register_method_return_type(qualified_method_name, return_type);
                                }
                            }
                            else
                            {
                                _template_registry.register_method_return_type_annotation(qualified_method_name, "void");
                            }
                        }
                    }
                }
                else if (auto enum_decl = dynamic_cast<EnumDeclarationNode *>(decl))
                {
                    // Register generic enum templates
                    if (!enum_decl->generic_parameters().empty())
                    {
                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Before registration - enum {} has {} generic parameters", enum_decl->name(), enum_decl->generic_parameters().size());

                        // Debug first parameter
                        if (!enum_decl->generic_parameters().empty())
                        {
                            auto first_param = enum_decl->generic_parameters()[0].get();
                            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: First parameter pointer: {}", (void *)first_param);
                            if (first_param)
                            {
                                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: First parameter name: {}", first_param->name());
                            }
                        }

                        _template_registry.register_enum_template(
                            enum_decl->name(), // base_name
                            enum_decl,         // template_node
                            module_name,       // module_namespace
                            "imported_module"  // source_file (placeholder)
                        );
                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Registered generic enum template: {} from module: {}", enum_decl->name(), module_name);

                        // Also register in GenericRegistry for the new type system
                        if (_generic_registry)
                        {
                            // Skip if template is already registered - don't overwrite existing one with variants
                            if (_generic_registry->get_template_by_name(enum_decl->name()))
                            {
                                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Template '{}' already registered in GenericRegistry, skipping to preserve variant info",
                                          enum_decl->name());
                            }
                            else
                            {
                                TypeArena &arena = _ast_context.types();

                                // Look up the existing EnumType created by create_symbol_map (which has variant info)
                                // instead of creating a new empty one
                                TypeRef enum_type = arena.lookup_type_by_name(enum_decl->name());
                                if (!enum_type.is_valid())
                                {
                                    // Fallback: create a new one if not found (shouldn't happen normally)
                                    enum_type = arena.create_enum(QualifiedTypeName{ModuleID::invalid(), enum_decl->name()});
                                    LOG_WARN(LogComponent::GENERAL, "ModuleLoader: Had to create new EnumType for '{}' - variants may be missing",
                                             enum_decl->name());
                                }
                                else
                                {
                                    LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Using existing EnumType for '{}' with variants",
                                              enum_decl->name());
                                }

                                std::vector<GenericParam> params;
                                for (size_t i = 0; i < enum_decl->generic_parameters().size(); ++i)
                                {
                                    auto &param_node = enum_decl->generic_parameters()[i];
                                    TypeRef param_type = arena.create_generic_param(param_node->name(), i);
                                    params.emplace_back(param_node->name(), i, param_type);
                                }
                                _generic_registry->register_template(enum_type, params,
                                                                     ModuleID::invalid(), enum_decl, enum_decl->name());
                                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Registered enum '{}' in GenericRegistry with {} params",
                                          enum_decl->name(), params.size());
                            }
                        }
                    }
                }
                else if (auto struct_decl = dynamic_cast<StructDeclarationNode *>(decl))
                {
                    // Register generic struct templates
                    if (!struct_decl->generic_parameters().empty())
                    {
                        _template_registry.register_struct_template(
                            struct_decl->name(), // base_name
                            struct_decl,         // template_node
                            module_name,         // module_namespace
                            "imported_module"    // source_file (placeholder)
                        );
                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Registered generic struct template: {} from module: {}", struct_decl->name(), module_name);

                        // Also register in GenericRegistry for the new type system
                        if (_generic_registry)
                        {
                            // Skip if template is already registered - don't overwrite existing one with fields
                            if (_generic_registry->get_template_by_name(struct_decl->name()))
                            {
                                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Template '{}' already registered in GenericRegistry, skipping to preserve field info",
                                          struct_decl->name());
                            }
                            else
                            {
                                TypeArena &arena = _ast_context.types();

                                // Look up the existing StructType created by create_symbol_map (which has fields)
                                // instead of creating a new empty one
                                TypeRef struct_type = arena.lookup_type_by_name(struct_decl->name());
                                if (!struct_type.is_valid())
                                {
                                    // Fallback: create a new one if not found (shouldn't happen normally)
                                    struct_type = arena.create_struct(QualifiedTypeName{ModuleID::invalid(), struct_decl->name()});
                                    LOG_WARN(LogComponent::GENERAL, "ModuleLoader: Had to create new StructType for '{}' - fields may be missing",
                                             struct_decl->name());
                                }
                                else
                                {
                                    LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Using existing StructType for '{}' with fields",
                                              struct_decl->name());
                                }

                                std::vector<GenericParam> params;
                                for (size_t i = 0; i < struct_decl->generic_parameters().size(); ++i)
                                {
                                    auto &param_node = struct_decl->generic_parameters()[i];
                                    TypeRef param_type = arena.create_generic_param(param_node->name(), i);
                                    params.emplace_back(param_node->name(), i, param_type);
                                }
                                _generic_registry->register_template(struct_type, params,
                                                                     ModuleID::invalid(), struct_decl, struct_decl->name());
                                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Registered struct '{}' in GenericRegistry with {} params",
                                          struct_decl->name(), params.size());
                            }
                        }
                    }
                    else
                    {
                        // For non-generic structs, register their field types for cross-module resolution
                        std::string qualified_name = module_name.empty() ? struct_decl->name() : module_name + "::" + struct_decl->name();
                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Processing non-generic struct '{}' (qualified: '{}')", struct_decl->name(), qualified_name);
                        LOG_DEBUG(LogComponent::GENERAL, "=== STRUCT_REG: Found non-generic struct '{}' with {} fields ===", struct_decl->name(), struct_decl->fields().size());

                        std::vector<std::string> field_names;
                        std::vector<TypeRef> field_types;
                        std::vector<std::string> field_annotations;

                        for (const auto &field : struct_decl->fields())
                        {
                            if (field)
                            {
                                field_names.push_back(field->name());
                                std::string annotation = field->has_type_annotation() ? field->type_annotation()->to_string() : "";
                                field_annotations.push_back(annotation);
                                TypeRef field_type = field->get_resolved_type();
                                // Check both is_valid() AND !is_error() - error types have valid IDs
                                // but shouldn't be used directly; they need to be re-resolved
                                if (field_type.is_valid() && !field_type.is_error())
                                {
                                    LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Field '{}' has resolved type: {}", field->name(), field_type->display_name());
                                    field_types.push_back(field_type);
                                }
                                else
                                {
                                    // Try to resolve from type annotation (either invalid type or error type from parsing)
                                    std::string type_str = field->type_annotation() ? field->type_annotation()->to_string() : "unknown";
                                    LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Field '{}' needs type resolution from annotation: '{}'", field->name(), type_str);
                                    field_type = resolve_primitive_type(type_str, _ast_context.types());
                                    if (field_type.is_valid() && !field_type.is_error())
                                    {
                                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Resolved '{}' to primitive type: {}", type_str, field_type->display_name());
                                        field_types.push_back(field_type);
                                    }
                                    else
                                    {
                                        // For complex types, try to look up in symbol table
                                        Symbol *type_sym = _symbol_table.lookup_symbol(type_str);
                                        if (type_sym && type_sym->type.is_valid() && !type_sym->type.is_error())
                                        {
                                            field_type = type_sym->type;
                                            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Resolved '{}' from symbol table: {}", type_str, field_type->display_name());
                                        }
                                        else
                                        {
                                            LOG_WARN(LogComponent::GENERAL, "ModuleLoader: Could not resolve field type for '{}': '{}'", field->name(), type_str);
                                        }
                                        field_types.push_back(field_type); // May be invalid for unresolvable types
                                    }
                                }
                            }
                        }

                        if (!field_types.empty())
                        {
                            _template_registry.register_struct_field_types(qualified_name, field_names, field_types, module_name, field_annotations);
                            // Also register with simple name for flexibility
                            if (qualified_name != struct_decl->name())
                            {
                                _template_registry.register_struct_field_types(struct_decl->name(), field_names, field_types, module_name, field_annotations);
                            }
                            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Registered struct field types: {} with {} fields (simple name: {}, source_namespace: {})", qualified_name, field_types.size(), struct_decl->name(), module_name);
                        }
                        else
                        {
                            LOG_WARN(LogComponent::GENERAL, "ModuleLoader: No field types found for struct '{}', not registering", struct_decl->name());
                        }
                    }

                    // Register method return type annotations for struct-embedded methods
                    // This is critical for cross-module method resolution
                    for (const auto &method : struct_decl->methods())
                    {
                        if (method)
                        {
                            std::string qualified_type = module_name.empty() ? struct_decl->name() : module_name + "::" + struct_decl->name();
                            std::string return_type_str = method->return_type_annotation() ? method->return_type_annotation()->to_string() : "";
                            std::string qualified_method_name = qualified_type + "::" + method->name();

                            _template_registry.register_method_is_static(qualified_method_name, method->is_static());

                            if (!return_type_str.empty())
                            {
                                // Qualify the return type (e.g., "Option<u64>" -> "std::core::option::Option<u64>")
                                std::string qualified_return_type = qualify_type_annotation(return_type_str);
                                _template_registry.register_method_return_type_annotation(qualified_method_name, qualified_return_type);
                                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Registered struct method return type annotation: {} -> {}",
                                          qualified_method_name, qualified_return_type);

                                // Also try to resolve to TypeRef for primitives
                                TypeArena &arena = _ast_context.types();
                                TypeRef return_type = resolve_primitive_type(return_type_str, arena);
                                if (return_type.is_valid())
                                {
                                    _template_registry.register_method_return_type(qualified_method_name, return_type);
                                    LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Also registered TypeRef for struct method: {} -> {}",
                                              qualified_method_name, return_type->display_name());
                                }
                            }
                            else
                            {
                                // Method returns void - register that explicitly
                                _template_registry.register_method_return_type_annotation(qualified_method_name, "void");
                                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Registered void return type for struct method: {}",
                                          qualified_method_name);
                            }
                        }
                    }
                }
                else if (auto func_decl = dynamic_cast<FunctionDeclarationNode *>(decl))
                {
                    // Register generic function templates
                    if (!func_decl->generic_parameters().empty())
                    {
                        _template_registry.register_function_template(
                            func_decl->name(), // base_name
                            func_decl,         // template_node
                            module_name,       // module_namespace
                            "imported_module"  // source_file (placeholder)
                        );
                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Registered generic function template: {} from module: {}", func_decl->name(), module_name);
                    }
                }
                else if (auto trait_decl = dynamic_cast<TraitDeclarationNode *>(decl))
                {
                    // Register generic trait templates
                    if (!trait_decl->generic_parameters().empty())
                    {
                        _template_registry.register_trait_template(
                            trait_decl->name(), // base_name
                            trait_decl,         // template_node
                            module_name,        // module_namespace
                            "imported_module"   // source_file (placeholder)
                        );
                        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Registered generic trait template: {} from module: {}", trait_decl->name(), module_name);
                    }
                }
                else if (auto alias_decl = dynamic_cast<TypeAliasDeclarationNode *>(decl))
                {
                    // Register generic type aliases as templates
                    if (alias_decl->is_generic() && _generic_registry)
                    {
                        // Skip if template is already registered - don't overwrite existing one
                        // This prevents dangling ast_node pointers when the same type alias
                        // is defined in multiple modules
                        if (_generic_registry->get_template_by_name(alias_decl->alias_name()))
                        {
                            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Type alias template '{}' already registered in GenericRegistry, skipping to preserve ast_node",
                                      alias_decl->alias_name());
                        }
                        else
                        {
                            TypeArena &arena = _ast_context.types();
                            // Create a type alias type for the template
                            TypeRef alias_type = arena.create_type_alias(
                                QualifiedTypeName{ModuleID::invalid(), alias_decl->alias_name()},
                                TypeRef{} // Target will be resolved during instantiation
                            );

                            std::vector<GenericParam> params;
                            for (size_t i = 0; i < alias_decl->generic_params().size(); ++i)
                            {
                                const std::string &param_name = alias_decl->generic_params()[i];
                                TypeRef param_type = arena.create_generic_param(param_name, i);
                                params.emplace_back(param_name, i, param_type);
                            }
                            _generic_registry->register_template(alias_type, params,
                                                                 ModuleID::invalid(), alias_decl, alias_decl->alias_name());
                            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Registered generic type alias '{}' in GenericRegistry with {} params",
                                      alias_decl->alias_name(), params.size());
                        }
                    }
                }
                // Process implementation blocks to register method return types
                else if (auto impl_block = dynamic_cast<ImplementationBlockNode *>(decl))
                {
                    std::string type_name = impl_block->target_type();
                    // Extract base type name (remove generics like "Option<T>" -> "Option")
                    std::string base_type_name = type_name;
                    size_t generic_start = type_name.find('<');
                    bool is_generic = (generic_start != std::string::npos);
                    if (is_generic)
                    {
                        base_type_name = type_name.substr(0, generic_start);
                    }

                    std::string qualified_type = module_name.empty() ? base_type_name : module_name + "::" + base_type_name;
                    LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Processing impl block for type: {} (qualified: {})", type_name, qualified_type);

                    // If this is an impl block for a generic type, check if it's for an enum template
                    // and register the impl block for use during monomorphization
                    if (is_generic)
                    {
                        const TemplateRegistry::TemplateInfo *tmpl_info = _template_registry.find_template(base_type_name);
                        if (tmpl_info && tmpl_info->enum_template)
                        {
                            _template_registry.register_enum_impl_block(base_type_name, impl_block, module_name);
                            LOG_DEBUG(LogComponent::GENERAL,
                                      "ModuleLoader: Registered enum impl block for '{}' (base: {}) with {} methods",
                                      type_name, base_type_name, impl_block->method_implementations().size());
                        }
                    }
                    // Also register non-generic enum impl blocks so resolve_method_by_name
                    // can look up parameter types when creating forward declarations
                    else
                    {
                        TypeRef enum_type = _symbol_table.lookup_enum_type(base_type_name);
                        if (enum_type.is_valid() && enum_type->kind() == TypeKind::Enum)
                        {
                            _template_registry.register_enum_impl_block(base_type_name, impl_block, module_name);
                            LOG_DEBUG(LogComponent::GENERAL,
                                      "ModuleLoader: Registered non-generic enum impl block for '{}' with {} methods",
                                      base_type_name, impl_block->method_implementations().size());
                        }
                    }

                    for (const auto &method : impl_block->method_implementations())
                    {
                        if (method)
                        {
                            std::string return_type_str = method->return_type_annotation() ? method->return_type_annotation()->to_string() : "";
                            std::string qualified_method_name = qualified_type + "::" + method->name();

                            _template_registry.register_method_is_static(qualified_method_name, method->is_static());

                            // Always register the string annotation for cross-module lookups
                            // Qualify the return type to include its full namespace path
                            if (!return_type_str.empty())
                            {
                                // Qualify the return type (e.g., "Option<u64>" -> "std::core::option::Option<u64>")
                                std::string qualified_return_type = qualify_type_annotation(return_type_str);
                                _template_registry.register_method_return_type_annotation(qualified_method_name, qualified_return_type);
                                LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Registered method return type annotation: {} -> {}",
                                          qualified_method_name, qualified_return_type);

                                // Also try to resolve to TypeRef for primitives (enables faster lookup)
                                TypeArena &arena = _ast_context.types();
                                TypeRef return_type = resolve_primitive_type(return_type_str, arena);
                                if (return_type.is_valid())
                                {
                                    _template_registry.register_method_return_type(qualified_method_name, return_type);
                                    LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Also registered TypeRef for: {} -> {}",
                                              qualified_method_name, return_type->display_name());
                                }
                            }
                            else
                            {
                                // Method returns void - register that explicitly
                                _template_registry.register_method_return_type_annotation(qualified_method_name, "void");
                            }
                        }
                    }
                }
            }
        }

        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Finished registering templates from module: {}", module_name);
    }

    // Helper to resolve primitive type annotations to Type objects
    TypeRef ModuleLoader::resolve_primitive_type(const std::string &type_str, TypeArena &arena)
    {
        // Handle pointer types (e.g., "u8*", "i32*")
        if (type_str.size() > 1 && type_str.back() == '*')
        {
            std::string base_type_str = type_str.substr(0, type_str.size() - 1);
            TypeRef base_type = resolve_primitive_type(base_type_str, arena);
            if (base_type.is_valid())
            {
                return arena.get_pointer_to(base_type);
            }
            // If base type is not a primitive, try user-defined types via arena lookup
            base_type = arena.lookup_type_by_name(base_type_str);
            if (base_type.is_valid())
            {
                return arena.get_pointer_to(base_type);
            }

            // Base type not found — return invalid so callers can try other resolution
            return TypeRef{};
        }

        // Handle dynamic array types (e.g., "i32[]", "string[]", "GenericParamNode*[]")
        if (type_str.size() > 2 && type_str.compare(type_str.size() - 2, 2, "[]") == 0)
        {
            std::string elem_str = type_str.substr(0, type_str.size() - 2);
            TypeRef elem_type = resolve_primitive_type(elem_str, arena);
            if (!elem_type.is_valid())
                elem_type = arena.lookup_type_by_name(elem_str);
            if (elem_type.is_valid())
            {
                return arena.get_array_of(elem_type); // dynamic (no size)
            }
            return TypeRef{};
        }

        // Handle primitive types
        if (type_str == "boolean" || type_str == "bool")
            return arena.get_bool();
        if (type_str == "void")
            return arena.get_void();
        if (type_str == "i8")
            return arena.get_i8();
        if (type_str == "i16")
            return arena.get_i16();
        if (type_str == "i32" || type_str == "int")
            return arena.get_i32();
        if (type_str == "i64")
            return arena.get_i64();
        if (type_str == "u8")
            return arena.get_u8();
        if (type_str == "u16")
            return arena.get_u16();
        if (type_str == "u32" || type_str == "uint")
            return arena.get_u32();
        if (type_str == "u64")
            return arena.get_u64();
        if (type_str == "f32" || type_str == "float")
            return arena.get_f32();
        if (type_str == "f64" || type_str == "double")
            return arena.get_f64();
        if (type_str == "char")
            return arena.get_char();
        if (type_str == "string")
            return arena.get_string();

        // For non-primitive types (generics, user-defined), return invalid TypeRef
        // These would need more complex resolution
        return TypeRef{};
    }

    std::string ModuleLoader::qualify_type_annotation(const std::string &type_annotation)
    {
        if (type_annotation.empty())
            return type_annotation;

        // Already qualified - return as-is
        if (type_annotation.find("::") != std::string::npos)
            return type_annotation;

        // Extract base type name (for generics like "Option<u64>" -> "Option")
        std::string base_type = type_annotation;
        std::string type_suffix;
        size_t angle_pos = type_annotation.find('<');
        if (angle_pos != std::string::npos)
        {
            base_type = type_annotation.substr(0, angle_pos);
            type_suffix = type_annotation.substr(angle_pos);
        }

        // Don't qualify primitive types
        static const std::unordered_set<std::string> primitives = {
            "void", "bool", "boolean", "i8", "i16", "i32", "i64",
            "u8", "u16", "u32", "u64", "f32", "f64", "float", "double",
            "char", "string", "int", "uint"};
        if (primitives.find(base_type) != primitives.end())
            return type_annotation;

        // Look up the base type in TemplateRegistry to find its namespace
        const TemplateRegistry::TemplateInfo *template_info = _template_registry.find_template(base_type);
        if (template_info && !template_info->module_namespace.empty())
        {
            std::string qualified = template_info->module_namespace + "::" + base_type + type_suffix;
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Qualified type annotation '{}' to '{}'",
                      type_annotation, qualified);
            return qualified;
        }

        // Check struct field types registry for non-template types
        const TemplateRegistry::StructFieldInfo *field_info = _template_registry.get_struct_field_types(base_type);
        if (field_info && !field_info->source_namespace.empty())
        {
            std::string qualified = field_info->source_namespace + "::" + base_type + type_suffix;
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Qualified type annotation '{}' to '{}' (from struct registry)",
                      type_annotation, qualified);
            return qualified;
        }

        // Could not qualify - return original
        LOG_TRACE(LogComponent::GENERAL, "ModuleLoader: Could not qualify type annotation '{}', returning as-is",
                  type_annotation);
        return type_annotation;
    }

    void ModuleLoader::mark_declarations_as_imported(ProgramNode &ast, const std::string &module_name)
    {
        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Marking declarations from module '{}' as imported", module_name);

        // Iterate through all top-level statements and mark declarations
        for (const auto &statement : ast.statements())
        {
            if (auto decl = dynamic_cast<DeclarationNode *>(statement.get()))
            {
                // Set the source module for this declaration
                decl->set_source_module(module_name);

                // Log the marking for debugging
                if (auto func_decl = dynamic_cast<FunctionDeclarationNode *>(decl))
                {
                    LOG_TRACE(LogComponent::GENERAL, "  Marked function '{}' as from module '{}'", func_decl->name(), module_name);
                }
                else if (auto struct_decl = dynamic_cast<StructDeclarationNode *>(decl))
                {
                    LOG_TRACE(LogComponent::GENERAL, "  Marked struct '{}' as from module '{}'", struct_decl->name(), module_name);

                    // Also mark all methods (including constructors) within the struct
                    for (const auto &method : struct_decl->methods())
                    {
                        if (method)
                        {
                            method->set_source_module(module_name);
                            LOG_TRACE(LogComponent::GENERAL, "    Marked struct method '{}::{}' as from module '{}'",
                                      struct_decl->name(), method->name(), module_name);
                        }
                    }
                }
                else if (auto class_decl = dynamic_cast<ClassDeclarationNode *>(decl))
                {
                    LOG_TRACE(LogComponent::GENERAL, "  Marked class '{}' as from module '{}'", class_decl->name(), module_name);

                    // Also mark all methods (including constructors) within the class
                    for (const auto &method : class_decl->methods())
                    {
                        if (method)
                        {
                            method->set_source_module(module_name);
                            LOG_TRACE(LogComponent::GENERAL, "    Marked class method '{}::{}' as from module '{}'",
                                      class_decl->name(), method->name(), module_name);
                        }
                    }
                }
                else if (auto enum_decl = dynamic_cast<EnumDeclarationNode *>(decl))
                {
                    LOG_TRACE(LogComponent::GENERAL, "  Marked enum '{}' as from module '{}'", enum_decl->name(), module_name);
                }
                else if (auto var_decl = dynamic_cast<VariableDeclarationNode *>(decl))
                {
                    LOG_TRACE(LogComponent::GENERAL, "  Marked variable '{}' as from module '{}'", var_decl->name(), module_name);
                }
            }
        }

        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Finished marking declarations from module '{}'", module_name);
    }

    const std::unordered_map<std::string, std::unique_ptr<ProgramNode>> &ModuleLoader::get_imported_asts() const
    {
        return _imported_asts;
    }

    void ModuleLoader::resolve_this_parameters_in_ast(ProgramNode &ast)
    {
        // After types are registered in create_symbol_map, we need to resolve any
        // 'this' parameters that have error types (unresolved_this:X) because the
        // Parser couldn't find the type at parse time.

        TypeArena &arena = _ast_context.types();

        for (auto &stmt : ast.statements())
        {
            // Check for implementation blocks
            auto *impl = dynamic_cast<ImplementationBlockNode *>(stmt.get());
            if (!impl)
                continue;

            // Extract base type name from impl target (e.g., "Option<T>" -> "Option")
            const std::string &target = impl->target_type();
            std::string base_name = target;
            size_t angle_pos = target.find('<');
            if (angle_pos != std::string::npos)
            {
                base_name = target.substr(0, angle_pos);
            }

            // Try to look up the base type
            TypeRef base_type = arena.lookup_type_by_name(base_name);

            // Fallback: try symbol table lookups
            if (!base_type.is_valid() || base_type.is_error())
            {
                base_type = _symbol_table.lookup_enum_type(base_name);
            }
            if (!base_type.is_valid() || base_type.is_error())
            {
                base_type = _symbol_table.lookup_struct_type(base_name);
            }
            if (!base_type.is_valid() || base_type.is_error())
            {
                base_type = _symbol_table.lookup_class_type(base_name);
            }

            if (!base_type.is_valid() || base_type.is_error())
            {
                LOG_DEBUG(LogComponent::GENERAL,
                          "ModuleLoader: Could not resolve base type '{}' for impl block '{}'",
                          base_name, target);
                continue;
            }

            // Extract generic parameters from impl target (e.g., "Option<T>" -> ["T"])
            std::vector<std::string> impl_generic_params;
            if (angle_pos != std::string::npos && target.back() == '>')
            {
                std::string params_str = target.substr(angle_pos + 1, target.size() - angle_pos - 2);
                size_t start = 0;
                for (size_t i = 0; i <= params_str.size(); ++i)
                {
                    if (i == params_str.size() || params_str[i] == ',')
                    {
                        std::string param = params_str.substr(start, i - start);
                        // Trim whitespace
                        while (!param.empty() && (param.front() == ' ' || param.front() == '\t'))
                            param.erase(0, 1);
                        while (!param.empty() && (param.back() == ' ' || param.back() == '\t'))
                            param.pop_back();
                        if (!param.empty())
                            impl_generic_params.push_back(param);
                        start = i + 1;
                    }
                }
            }

            // For generic impl blocks, create the full generic type (e.g., Option<T>)
            TypeRef impl_type = base_type;
            if (!impl_generic_params.empty())
            {
                // Create GenericParamTypes for each parameter and construct an InstantiatedType
                std::vector<TypeRef> type_args;
                for (size_t i = 0; i < impl_generic_params.size(); ++i)
                {
                    TypeRef param_type = arena.create_generic_param(impl_generic_params[i], i);
                    type_args.push_back(param_type);
                }
                // Create an instantiated type: Option<T> where T is a GenericParamType
                impl_type = arena.create_instantiation(base_type, type_args);
                LOG_DEBUG(LogComponent::GENERAL,
                          "ModuleLoader: Created generic impl type '{}' for impl block '{}'",
                          impl_type->display_name(), target);
            }

            // Process each method in the impl block
            for (auto &method : impl->method_implementations())
            {
                for (auto &param : method->parameters())
                {
                    if (param->name() == "this" && param->has_resolved_type())
                    {
                        TypeRef current_type = param->get_resolved_type();
                        if (current_type.is_error())
                        {
                            // This is an unresolved 'this' parameter - fix it using the full impl type
                            TypeRef this_type = arena.get_reference_to(impl_type);
                            param->set_resolved_type(this_type);
                            LOG_DEBUG(LogComponent::GENERAL,
                                      "ModuleLoader: Resolved 'this' param for impl '{}::{}' to '{}'",
                                      target, method->name(), this_type->display_name());
                        }
                    }
                    else if (param->has_resolved_type())
                    {
                        // Check if this is an unresolved generic type that uses impl's generic params
                        TypeRef current_type = param->get_resolved_type();
                        if (current_type.is_error())
                        {
                            std::string error_name = current_type->display_name();
                            // Check if it's an "unresolved generic" error
                            const std::string prefix = "<error: unresolved generic: ";
                            if (error_name.find(prefix) == 0 && error_name.back() == '>')
                            {
                                // Extract the type string (e.g., "Option<T>" from "<error: unresolved generic: Option<T>>")
                                std::string type_str = error_name.substr(prefix.length(), error_name.length() - prefix.length() - 1);

                                // Check if this type only uses generic params from the impl block
                                // For now, we'll mark it as a valid generic type if the base type exists
                                // and all type args are impl generic params
                                size_t type_angle = type_str.find('<');
                                if (type_angle != std::string::npos)
                                {
                                    std::string param_base = type_str.substr(0, type_angle);
                                    std::string args_part = type_str.substr(type_angle + 1, type_str.size() - type_angle - 2);

                                    // Check if base type is valid
                                    TypeRef param_base_type = arena.lookup_type_by_name(param_base);
                                    if (!param_base_type.is_valid() || param_base_type.is_error())
                                    {
                                        param_base_type = _symbol_table.lookup_enum_type(param_base);
                                    }
                                    if (!param_base_type.is_valid() || param_base_type.is_error())
                                    {
                                        param_base_type = _symbol_table.lookup_struct_type(param_base);
                                    }

                                    if (param_base_type.is_valid() && !param_base_type.is_error())
                                    {
                                        // Check if all type args are impl generic params
                                        bool all_generic_params = true;
                                        std::vector<std::string> type_args;
                                        size_t arg_start = 0;
                                        int depth = 0;
                                        for (size_t i = 0; i <= args_part.size(); ++i)
                                        {
                                            if (i < args_part.size())
                                            {
                                                if (args_part[i] == '<' || args_part[i] == '(')
                                                    depth++;
                                                else if (args_part[i] == '>' || args_part[i] == ')')
                                                    depth--;
                                            }
                                            if (i == args_part.size() || (args_part[i] == ',' && depth == 0))
                                            {
                                                std::string arg = args_part.substr(arg_start, i - arg_start);
                                                while (!arg.empty() && std::isspace(arg.front()))
                                                    arg.erase(0, 1);
                                                while (!arg.empty() && std::isspace(arg.back()))
                                                    arg.pop_back();
                                                type_args.push_back(arg);
                                                arg_start = i + 1;
                                            }
                                        }

                                        for (const auto &arg : type_args)
                                        {
                                            bool found = false;
                                            for (const auto &impl_param : impl_generic_params)
                                            {
                                                if (arg == impl_param)
                                                {
                                                    found = true;
                                                    break;
                                                }
                                            }
                                            if (!found)
                                            {
                                                all_generic_params = false;
                                                break;
                                            }
                                        }

                                        if (all_generic_params)
                                        {
                                            // This is a valid generic type using impl's generic params
                                            // Create an InstantiatedType with GenericParamTypes so that
                                            // substitute_type_params() can properly substitute T -> concrete type
                                            std::vector<TypeRef> generic_type_args;
                                            for (size_t i = 0; i < type_args.size(); ++i)
                                            {
                                                // Find the index of this type arg in impl_generic_params
                                                size_t param_index = 0;
                                                for (size_t j = 0; j < impl_generic_params.size(); ++j)
                                                {
                                                    if (type_args[i] == impl_generic_params[j])
                                                    {
                                                        param_index = j;
                                                        break;
                                                    }
                                                }
                                                TypeRef gp_type = arena.create_generic_param(type_args[i], param_index);
                                                generic_type_args.push_back(gp_type);
                                            }
                                            // Create InstantiatedType: e.g., Option<T> where T is GenericParamType
                                            TypeRef instantiated_param_type = arena.create_instantiation(param_base_type, generic_type_args);
                                            param->set_resolved_type(instantiated_param_type);
                                            LOG_DEBUG(LogComponent::GENERAL,
                                                      "ModuleLoader: Resolved generic param '{}' for impl '{}::{}' to instantiated type '{}'",
                                                      param->name(), target, method->name(), instantiated_param_type->display_name());
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    void ModuleLoader::resolve_method_return_types_in_ast(ProgramNode &ast, const std::string &module_name)
    {
        // Imported modules don't go through the TypeResolutionPass, so struct/class
        // method return types that are "unresolved generic" ErrorType placeholders
        // (created by the parser for generic type annotations like "Types::Outcome<boolean,string>")
        // never get resolved. We fix them up here using the TypeResolver.

        if (!_generic_registry)
            return;

        TypeArena &arena = _ast_context.types();
        ModuleTypeRegistry &module_registry = _ast_context.modules();
        GenericRegistry &generic_registry = *_generic_registry;

        TypeResolver resolver(arena, module_registry, generic_registry, _diagnostics);

        // Set up resolution context for this module
        ResolutionContext res_ctx;
        ModuleID module_id = module_registry.get_or_create_module(module_name);
        res_ctx.current_module = module_id;

        // Collect imports from the AST for the resolution context
        auto module_info = module_registry.get_module_info(module_id);
        if (module_info)
        {
            res_ctx.imports = module_info->imports;
        }

        // Check for both "unresolved generic: ..." and "unresolved type: ..." errors.
        // The parser creates "unresolved generic" for generic type annotations (e.g., Result<T,E>)
        // and "unresolved type" for simple imported types (e.g., File from Utils::File).
        // Both need resolution since imported modules skip the TypeResolutionPass.
        auto is_resolvable_error = [](const ErrorType *err) -> bool
        {
            if (!err)
                return false;
            const std::string &reason = err->reason();
            return reason.find("unresolved generic: ") != std::string::npos ||
                   reason.find("unresolved type: ") != std::string::npos;
        };

        for (auto &stmt : ast.statements())
        {
            // Process struct declarations
            auto *struct_decl = dynamic_cast<StructDeclarationNode *>(stmt.get());
            if (struct_decl)
            {
                for (auto &method : struct_decl->methods())
                {
                    TypeRef ret_type = method->get_resolved_return_type();
                    auto *ann = method->return_type_annotation();
                    if (ret_type.is_error() && ann)
                    {
                        const ErrorType *err = static_cast<const ErrorType *>(ret_type.get());
                        if (is_resolvable_error(err))
                        {
                            TypeRef resolved = resolver.resolve(*ann, res_ctx);
                            if (!resolved.is_valid() || resolved.is_error())
                            {
                                std::string type_str = ann->to_string();
                                Symbol *type_sym = _symbol_table.lookup_symbol(type_str);
                                if (type_sym && type_sym->type.is_valid() && !type_sym->type.is_error())
                                    resolved = type_sym->type;
                            }
                            if (resolved.is_valid() && !resolved.is_error())
                            {
                                method->set_resolved_return_type(resolved);
                                LOG_DEBUG(LogComponent::GENERAL,
                                          "ModuleLoader: Resolved method '{}::{}' return type to '{}'",
                                          struct_decl->name(), method->name(), resolved->display_name());
                            }
                        }
                    }

                    // Also resolve parameter types that are unresolved
                    for (auto &param : method->parameters())
                    {
                        if (!param->has_resolved_type())
                            continue;
                        TypeRef param_type = param->get_resolved_type();
                        auto *param_ann = param->type_annotation();
                        if (param_type.is_error() && param_ann)
                        {
                            const ErrorType *err = static_cast<const ErrorType *>(param_type.get());
                            if (is_resolvable_error(err))
                            {
                                // First try the TypeResolver
                                TypeRef resolved = resolver.resolve(*param_ann, res_ctx);
                                // If TypeResolver fails (e.g., no imports in context), fall back
                                // to symbol table lookup — imported types are registered there.
                                if (!resolved.is_valid() || resolved.is_error())
                                {
                                    std::string type_str = param_ann->to_string();
                                    Symbol *type_sym = _symbol_table.lookup_symbol(type_str);
                                    if (type_sym && type_sym->type.is_valid() && !type_sym->type.is_error())
                                    {
                                        resolved = type_sym->type;
                                    }
                                }
                                if (resolved.is_valid() && !resolved.is_error())
                                {
                                    param->set_resolved_type(resolved);
                                    LOG_DEBUG(LogComponent::GENERAL,
                                              "ModuleLoader: Resolved param '{}::{}::{}' type to '{}'",
                                              struct_decl->name(), method->name(), param->name(), resolved->display_name());
                                }
                            }
                        }
                    }
                }
                continue;
            }

            // Process class declarations
            auto *class_decl = dynamic_cast<ClassDeclarationNode *>(stmt.get());
            if (class_decl)
            {
                for (auto &method : class_decl->methods())
                {
                    TypeRef ret_type = method->get_resolved_return_type();
                    auto *ann = method->return_type_annotation();
                    if (ret_type.is_error() && ann)
                    {
                        const ErrorType *err = static_cast<const ErrorType *>(ret_type.get());
                        if (is_resolvable_error(err))
                        {
                            TypeRef resolved = resolver.resolve(*ann, res_ctx);
                            if (!resolved.is_valid() || resolved.is_error())
                            {
                                std::string type_str = ann->to_string();
                                Symbol *type_sym = _symbol_table.lookup_symbol(type_str);
                                if (type_sym && type_sym->type.is_valid() && !type_sym->type.is_error())
                                    resolved = type_sym->type;
                            }
                            if (resolved.is_valid() && !resolved.is_error())
                            {
                                method->set_resolved_return_type(resolved);
                                LOG_DEBUG(LogComponent::GENERAL,
                                          "ModuleLoader: Resolved method '{}::{}' return type to '{}'",
                                          class_decl->name(), method->name(), resolved->display_name());
                            }
                        }
                    }

                    // Also resolve parameter types that are unresolved
                    for (auto &param : method->parameters())
                    {
                        if (!param->has_resolved_type())
                            continue;
                        TypeRef param_type = param->get_resolved_type();
                        auto *param_ann = param->type_annotation();
                        if (param_type.is_error() && param_ann)
                        {
                            const ErrorType *err = static_cast<const ErrorType *>(param_type.get());
                            if (is_resolvable_error(err))
                            {
                                TypeRef resolved = resolver.resolve(*param_ann, res_ctx);
                                if (!resolved.is_valid() || resolved.is_error())
                                {
                                    std::string type_str = param_ann->to_string();
                                    Symbol *type_sym = _symbol_table.lookup_symbol(type_str);
                                    if (type_sym && type_sym->type.is_valid() && !type_sym->type.is_error())
                                    {
                                        resolved = type_sym->type;
                                    }
                                }
                                if (resolved.is_valid() && !resolved.is_error())
                                {
                                    param->set_resolved_type(resolved);
                                    LOG_DEBUG(LogComponent::GENERAL,
                                              "ModuleLoader: Resolved param '{}::{}::{}' type to '{}'",
                                              class_decl->name(), method->name(), param->name(), resolved->display_name());
                                }
                            }
                        }
                    }
                }
            }
        }
    }

} // namespace Cryo