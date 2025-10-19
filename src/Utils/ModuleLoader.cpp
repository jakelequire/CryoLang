#include "Utils/ModuleLoader.hpp"
#include "Parser/Parser.hpp"
#include "Lexer/lexer.hpp"
#include "Utils/File.hpp"
#include "Utils/Logger.hpp"
#include "Utils/OS.hpp"
#include "AST/ASTContext.hpp"
#include "AST/Type.hpp"

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
        std::vector<std::string> search_paths;

        // If executable path is provided, search relative to it
        if (!executable_path.empty())
        {
            try
            {
                // Get the directory containing the executable using OS utility
                auto& os = Cryo::Utils::OS::instance();
                std::string exe_dir = std::filesystem::path(os.absolute_path(executable_path)).parent_path().string();

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
            auto& os = Cryo::Utils::OS::instance();
            std::string cwd = os.get_working_directory();

            // Common development and project patterns:
            search_paths.push_back(os.join_path(cwd, "stdlib"));       // ./stdlib
            search_paths.push_back(os.join_path(cwd, "../stdlib"));    // ../stdlib (for project dirs)
            search_paths.push_back(os.join_path(cwd, "../../stdlib")); // ../../stdlib (for nested dirs)
        }
        catch (const std::exception &)
        {
            // If current_path fails, add basic fallbacks
            search_paths.push_back("./stdlib");
            search_paths.push_back("../stdlib");
        }

        // System-wide installation paths
        search_paths.push_back("/usr/local/lib/cryo/stdlib"); // Unix/Linux standard
        search_paths.push_back("/usr/lib/cryo/stdlib");       // Unix/Linux alternative
        search_paths.push_back("/opt/cryo/stdlib");           // Unix/Linux /opt

        // Windows system paths (if we're on Windows)
        // Platform-specific search paths
        auto& os = Cryo::Utils::OS::instance();
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

                // Check if this looks like a valid stdlib directory
                // by looking for key files that should exist
                std::filesystem::path core_types = stdlib_path / "core" / "types.cryo";
                std::filesystem::path io_stdio = stdlib_path / "io" / "stdio.cryo";

                if (std::filesystem::exists(core_types) && std::filesystem::exists(io_stdio))
                {
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

    ModuleLoader::ImportResult ModuleLoader::load_import(const ImportDeclarationNode &import_node)
    {
        std::string import_path = import_node.path();
        std::string resolved_path = resolve_import_path(import_path, import_node.import_type());

        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Loading import '{}' -> '{}'", import_path, resolved_path);

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

            // Create a copy of the cached result to return
            ImportResult cached_result;
            cached_result.success = cached->second.success;
            cached_result.error_message = cached->second.error_message;
            cached_result.module_name = cached->second.module_name;
            cached_result.namespace_alias = cached->second.namespace_alias;
            cached_result.symbol_map = cached->second.symbol_map;
            cached_result.exported_symbols = cached->second.exported_symbols;

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
            ImportResult result;
            result.success = false;
            result.error_message = "Circular import dependency detected for: " + import_path;
            return result;
        }

        // Load the module
        _loading_modules.insert(resolved_path);
        ImportResult result = load_module_from_file(resolved_path, import_path);
        _loading_modules.erase(resolved_path);

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
        cache_copy.error_message = result.error_message;
        cache_copy.module_name = result.module_name;
        cache_copy.namespace_alias = result.namespace_alias;
        cache_copy.symbol_map = result.symbol_map; // This should copy the map
        cache_copy.exported_symbols = result.exported_symbols;

        // Cache the copy
        _loaded_modules[resolved_path] = std::move(cache_copy);

        return result;
    }

    std::string ModuleLoader::resolve_import_path(const std::string &import_path, ImportDeclarationNode::ImportType import_type)
    {
        auto& os = Cryo::Utils::OS::instance();
        
        if (import_type == ImportDeclarationNode::ImportType::Relative)
        {
            // Relative import: resolve relative to current file's directory
            std::string resolved = os.join_path(_current_file_dir, import_path);
            return os.absolute_path(resolved);
        }
        else // Absolute (stdlib)
        {
            // Absolute import: resolve relative to stdlib root with .cryo extension
            std::string import_file = import_path + ".cryo";
            std::string resolved = os.join_path(_stdlib_root, import_file);
            return os.absolute_path(resolved);
        }
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
            return result;
        }

        try
        {
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Reading file {}", file_path);

            // Create a File object from the path
            auto file = make_file_from_path(file_path);
            if (!file)
            {
                result.success = false;
                result.error_message = "Failed to create File object for: " + file_path;
                return result;
            }

            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Created File object for {}", file_path);

            // Create lexer with the file
            auto lexer = std::make_unique<Lexer>(std::move(file));
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Created Lexer");

            // Use the main ASTContext instead of creating a new one
            // This ensures all types use the same TypeContext and prevents corruption
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Using main ASTContext for type consistency");

            // Create parser with the lexer and main context
            Parser parser(std::move(lexer), _ast_context);
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Created Parser");

            // Parse the program
            auto ast = parser.parse_program();

            if (!ast)
            {
                result.success = false;
                result.error_message = "Failed to parse import file: " + import_path;
                return result;
            }

            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Successfully parsed {}", import_path);

            // Extract module name from parser's namespace information
            result.module_name = parser.current_namespace();
            if (result.module_name.empty() || result.module_name == "Global")
            {
                // Fall back to filename
                std::filesystem::path path(import_path);
                result.module_name = path.stem().string();
            }

            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Detected module namespace: {}", result.module_name);

            // Do ALL processing on the original AST and keep it alive
            // 1. Register generic templates from the original AST
            register_templates_from_ast(*ast, result.module_name);

            // 2. Extract symbols from the original AST
            result.exported_symbols = extract_exported_symbols(*ast);
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Found {} exported symbols", result.exported_symbols.size());

            // 3. Create symbol map from the original AST
            result.symbol_map = create_symbol_map(*ast, result.module_name);
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Created symbol map with {} symbols", result.symbol_map.size());

            // Keep the AST alive for template access by storing it
            // Note: Template registry has raw pointers to nodes in this AST
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Storing AST to keep template nodes alive");
            _imported_asts[result.module_name] = std::move(ast); // Move is necessary to transfer ownership
            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Stored AST successfully. Map now has {} entries", _imported_asts.size());

            result.success = true;
            return result;
        }
        catch (const std::exception &e)
        {
            result.success = false;
            result.error_message = "Exception while loading import '" + import_path + "': " + e.what();
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
            }
        }

        return exported_symbols;
    }

    std::unordered_map<std::string, Symbol> ModuleLoader::create_symbol_map(const ProgramNode &ast, const std::string &module_name)
    {
        std::unordered_map<std::string, Symbol> symbol_map;

        // Get TypeContext for creating proper type objects
        TypeContext *type_context = _symbol_table.get_type_context();

        // Process all top-level declarations
        for (const auto &statement : ast.statements())
        {
            if (auto decl = dynamic_cast<DeclarationNode *>(statement.get()))
            {
                if (auto func_decl = dynamic_cast<FunctionDeclarationNode *>(decl))
                {
                    // Create function symbol with proper type information
                    Type *func_type = create_function_type_from_declaration(func_decl, type_context);
                    Symbol symbol(func_decl->name(), SymbolKind::Function, func_decl->location(), func_type, module_name);
                    symbol_map[func_decl->name()] = symbol;
                }
                else if (auto var_decl = dynamic_cast<VariableDeclarationNode *>(decl))
                {
                    // Create variable symbol
                    Symbol symbol(var_decl->name(), SymbolKind::Variable, var_decl->location(), nullptr, module_name);
                    symbol_map[var_decl->name()] = symbol;
                }
                else if (auto struct_decl = dynamic_cast<StructDeclarationNode *>(decl))
                {
                    // Create type symbol for struct
                    Symbol symbol(struct_decl->name(), SymbolKind::Type, struct_decl->location(), nullptr, module_name);
                    symbol_map[struct_decl->name()] = symbol;
                }
                else if (auto class_decl = dynamic_cast<ClassDeclarationNode *>(decl))
                {
                    // Create type symbol for class
                    Symbol symbol(class_decl->name(), SymbolKind::Type, class_decl->location(), nullptr, module_name);
                    symbol_map[class_decl->name()] = symbol;
                }
                else if (auto enum_decl = dynamic_cast<EnumDeclarationNode *>(decl))
                {
                    Type *enum_type = nullptr;
                    if (!enum_decl->generic_parameters().empty())
                    {
                        // For generic enums, try to get the parameterized enum template from TypeContext
                        auto parameterized_enum = type_context->get_parameterized_enum_template(enum_decl->name());
                        if (parameterized_enum)
                        {
                            enum_type = parameterized_enum.get();
                            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Found parameterized enum template for {}", enum_decl->name());
                        }
                        else
                        {
                            LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: No parameterized enum template found for {}", enum_decl->name());
                        }
                    }
                    else
                    {
                        // For non-generic enums, create a regular enum type
                        std::vector<std::string> variant_names;
                        for (const auto &variant : enum_decl->variants())
                        {
                            variant_names.push_back(variant->name());
                        }
                        enum_type = type_context->get_enum_type(enum_decl->name(), std::move(variant_names), true);
                    }

                    // Create type symbol for enum
                    Symbol symbol(enum_decl->name(), SymbolKind::Type, enum_decl->location(), enum_type, module_name);
                    symbol_map[enum_decl->name()] = symbol;
                }
                else if (auto intrinsic_decl = dynamic_cast<IntrinsicDeclarationNode *>(decl))
                {
                    // Create intrinsic symbol with proper type information (same as regular functions)
                    Type *intrinsic_type = create_function_type_from_declaration(intrinsic_decl, type_context);
                    Symbol symbol(intrinsic_decl->name(), SymbolKind::Intrinsic, intrinsic_decl->location(), intrinsic_type, module_name);
                    symbol_map[intrinsic_decl->name()] = symbol;
                }
            }
        }

        return symbol_map;
    }

    bool ModuleLoader::has_circular_dependency(const std::string &module_path)
    {
        return _loading_modules.find(module_path) != _loading_modules.end();
    }

    Type *ModuleLoader::create_function_type_from_declaration(const FunctionDeclarationNode *func_decl, TypeContext *type_context)
    {
        if (!func_decl || !type_context)
        {
            return nullptr;
        }

        // Get return type
        Type *return_type = nullptr;
        return_type = func_decl->get_resolved_return_type();
        const std::string &return_type_str = return_type ? return_type->to_string() : "void";
        if (!return_type || return_type_str == "void")
        {
            // Default to void for functions without explicit return type or explicit void
            return_type = type_context->get_void_type();
        }

        // Get parameter types
        std::vector<Type *> parameter_types;
        for (const auto &param : func_decl->parameters())
        {
            Type *param_type = param->get_resolved_type();
            if (param_type)
            {
                parameter_types.push_back(param_type);
            }
            else
            {
                const std::string &param_type_str = param_type ? param_type->to_string() : "unknown";
                std::cerr << "Warning: Failed to get resolved type for parameter '" << param->name()
                          << "' (type: " << param_type_str << ") in function '" << func_decl->name() << "'" << std::endl;
                return nullptr;
            }
        }

        // Create FunctionType
        return type_context->create_function_type(return_type, parameter_types);
    }

    Type *ModuleLoader::create_function_type_from_declaration(const IntrinsicDeclarationNode *intrinsic_decl, TypeContext *type_context)
    {
        if (!intrinsic_decl || !type_context)
        {
            return nullptr;
        }

        // Get return type
        Type *return_type = nullptr;
        return_type = intrinsic_decl->get_resolved_return_type();
        const std::string &return_type_str = return_type ? return_type->to_string() : "void";
        if (!return_type || return_type_str == "void")
        {
            // Default to void for intrinsics without explicit return type or explicit void
            return_type = type_context->get_void_type();
        }

        // Get parameter types
        std::vector<Type *> parameter_types;
        for (const auto &param : intrinsic_decl->parameters())
        {
            Type *param_type = param->get_resolved_type();
            if (param_type)
            {
                parameter_types.push_back(param_type);
            }
            else
            {
                const std::string &param_type_str = param_type ? param_type->to_string() : "unknown";
                std::cerr << "Warning: Failed to get resolved type for parameter '" << param->name()
                          << "' (type: " << param_type_str << ") in intrinsic '" << intrinsic_decl->name() << "'" << std::endl;
                return nullptr;
            }
        }

        // Create FunctionType
        return type_context->create_function_type(return_type, parameter_types);
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
            }
        }

        LOG_DEBUG(LogComponent::GENERAL, "ModuleLoader: Finished registering templates from module: {}", module_name);
    }

    const std::unordered_map<std::string, std::unique_ptr<ProgramNode>> &ModuleLoader::get_imported_asts() const
    {
        return _imported_asts;
    }

} // namespace Cryo