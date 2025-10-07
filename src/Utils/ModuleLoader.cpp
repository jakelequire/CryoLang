#include "Utils/ModuleLoader.hpp"
#include "Parser/Parser.hpp"
#include "Lexer/lexer.hpp"
#include "Utils/file.hpp"
#include "AST/ASTContext.hpp"
#include "AST/Type.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace Cryo
{
    ModuleLoader::ModuleLoader(SymbolTable &symbol_table, TemplateRegistry &template_registry)
        : _symbol_table(symbol_table), _template_registry(template_registry)
    {
        // Default stdlib location - can be overridden
        _stdlib_root = "./stdlib";
    }

    void ModuleLoader::set_stdlib_root(const std::string &stdlib_root)
    {
        _stdlib_root = stdlib_root;
    }

    void ModuleLoader::set_current_file(const std::string &current_file_path)
    {
        _current_file_dir = get_parent_directory(current_file_path);
    }

    ModuleLoader::ImportResult ModuleLoader::load_import(const ImportDeclarationNode &import_node)
    {
        std::string import_path = import_node.path();
        std::string resolved_path = resolve_import_path(import_path, import_node.import_type());

        std::cout << "[DEBUG] ModuleLoader: Loading import '" << import_path
                  << "' -> '" << resolved_path << "'" << std::endl;

        // Check for specific vs wildcard import
        if (import_node.is_specific_import())
        {
            std::cout << "[DEBUG] ModuleLoader: Processing specific import with symbols: ";
            for (const auto &symbol : import_node.specific_imports())
            {
                std::cout << symbol << " ";
            }
            std::cout << std::endl;
        }
        else
        {
            std::cout << "[DEBUG] ModuleLoader: Processing wildcard import" << std::endl;
        }

        // Check if already loaded
        auto cached = _loaded_modules.find(resolved_path);
        if (cached != _loaded_modules.end())
        {
            std::cout << "[DEBUG] ModuleLoader: Using cached module" << std::endl;

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
                std::cout << "[DEBUG] ModuleLoader: Treating single specific import as namespace alias: "
                          << import_node.specific_imports()[0] << std::endl;
                // For namespace alias, we keep all symbols but set the alias
                result.namespace_alias = import_node.specific_imports()[0];
            }
            else
            {
                std::cout << "[DEBUG] ModuleLoader: Treating multiple specific imports as symbol imports" << std::endl;
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
        if (import_type == ImportDeclarationNode::ImportType::Relative)
        {
            // Relative import: resolve relative to current file's directory
            std::filesystem::path current_dir(_current_file_dir);
            std::filesystem::path relative_path(import_path);
            std::filesystem::path resolved = current_dir / relative_path;

            return std::filesystem::absolute(resolved).string();
        }
        else // Absolute (stdlib)
        {
            // Absolute import: resolve relative to stdlib root with .cryo extension
            std::filesystem::path stdlib_root(_stdlib_root);
            std::filesystem::path import_file(import_path + ".cryo");
            std::filesystem::path resolved = stdlib_root / import_file;

            return std::filesystem::absolute(resolved).string();
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
            std::cout << "[DEBUG] ModuleLoader: Reading file " << file_path << std::endl;

            // Create a File object from the path
            auto file = make_file_from_path(file_path);
            if (!file)
            {
                result.success = false;
                result.error_message = "Failed to create File object for: " + file_path;
                return result;
            }

            std::cout << "[DEBUG] ModuleLoader: Created File object for " << file_path << std::endl;

            // Create lexer with the file
            auto lexer = std::make_unique<Lexer>(std::move(file));
            std::cout << "[DEBUG] ModuleLoader: Created Lexer" << std::endl;

            // Create ASTContext for parsing
            ASTContext ast_context;

            // Create parser with the lexer and context
            Parser parser(std::move(lexer), ast_context);
            std::cout << "[DEBUG] ModuleLoader: Created Parser" << std::endl;

            // Parse the program
            auto ast = parser.parse_program();

            if (!ast)
            {
                result.success = false;
                result.error_message = "Failed to parse import file: " + import_path;
                return result;
            }

            std::cout << "[DEBUG] ModuleLoader: Successfully parsed " << import_path << std::endl;

            // Extract module name from parser's namespace information
            result.module_name = parser.current_namespace();
            if (result.module_name.empty() || result.module_name == "Global")
            {
                // Fall back to filename
                std::filesystem::path path(import_path);
                result.module_name = path.stem().string();
            }

            std::cout << "[DEBUG] ModuleLoader: Detected module namespace: " << result.module_name << std::endl;

            // Do ALL processing on the original AST and keep it alive
            // 1. Register generic templates from the original AST
            register_templates_from_ast(*ast, result.module_name);

            // 2. Extract symbols from the original AST
            result.exported_symbols = extract_exported_symbols(*ast);
            std::cout << "[DEBUG] ModuleLoader: Found " << result.exported_symbols.size() << " exported symbols" << std::endl;

            // 3. Create symbol map from the original AST
            result.symbol_map = create_symbol_map(*ast, result.module_name);
            std::cout << "[DEBUG] ModuleLoader: Created symbol map with " << result.symbol_map.size() << " symbols" << std::endl;

            // Keep the AST alive for template access by storing it
            // Note: Template registry has raw pointers to nodes in this AST
            std::cout << "[DEBUG] ModuleLoader: Storing AST to keep template nodes alive" << std::endl;
            _imported_asts[result.module_name] = std::move(ast); // Move is necessary to transfer ownership
            std::cout << "[DEBUG] ModuleLoader: Stored AST successfully. Map now has " << _imported_asts.size() << " entries" << std::endl;

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
                            std::cout << "[DEBUG] ModuleLoader: Found parameterized enum template for " << enum_decl->name() << std::endl;
                        }
                        else
                        {
                            std::cout << "[DEBUG] ModuleLoader: No parameterized enum template found for " << enum_decl->name() << std::endl;
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
                    // Create intrinsic symbol
                    Symbol symbol(intrinsic_decl->name(), SymbolKind::Intrinsic, intrinsic_decl->location(), nullptr, module_name);
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

    ModuleLoader::ImportResult ModuleLoader::filter_specific_imports(ImportResult result, const std::vector<std::string> &specific_imports)
    {
        std::cout << "[DEBUG] ModuleLoader: Filtering imports for specific symbols" << std::endl;

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
                std::cout << "[DEBUG] ModuleLoader: Found and included symbol: " << import_name << std::endl;
            }
            else
            {
                // Symbol not found - this should be a compilation error
                result.success = false;
                result.error_message = "Symbol '" + import_name + "' not found in module '" + result.module_name + "'";
                std::cout << "[DEBUG] ModuleLoader: Error - Symbol not found: " << import_name << std::endl;
                return result;
            }
        }

        // Update the result with filtered symbols
        result.symbol_map = std::move(filtered_symbol_map);
        result.exported_symbols = std::move(filtered_exported_symbols);

        std::cout << "[DEBUG] ModuleLoader: Filtered to " << result.symbol_map.size() << " symbols" << std::endl;
        return result;
    }

    std::string ModuleLoader::get_parent_directory(const std::string &file_path)
    {
        std::filesystem::path path(file_path);
        return path.parent_path().string();
    }

    void ModuleLoader::register_templates_from_ast(const ProgramNode &ast, const std::string &module_name)
    {
        std::cout << "[DEBUG] ModuleLoader: Registering templates from module: " << module_name << std::endl;

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
                        std::cout << "[DEBUG] ModuleLoader: Registered generic class template: "
                                  << class_decl->name() << " from module: " << module_name << std::endl;
                    }
                }
                else if (auto enum_decl = dynamic_cast<EnumDeclarationNode *>(decl))
                {
                    // Register generic enum templates
                    if (!enum_decl->generic_parameters().empty())
                    {
                        std::cout << "[DEBUG] ModuleLoader: Before registration - enum " << enum_decl->name()
                                  << " has " << enum_decl->generic_parameters().size() << " generic parameters" << std::endl;

                        // Debug first parameter
                        if (!enum_decl->generic_parameters().empty())
                        {
                            auto first_param = enum_decl->generic_parameters()[0].get();
                            std::cout << "[DEBUG] ModuleLoader: First parameter pointer: " << first_param << std::endl;
                            if (first_param)
                            {
                                std::cout << "[DEBUG] ModuleLoader: First parameter name: " << first_param->name() << std::endl;
                            }
                        }

                        _template_registry.register_enum_template(
                            enum_decl->name(), // base_name
                            enum_decl,         // template_node
                            module_name,       // module_namespace
                            "imported_module"  // source_file (placeholder)
                        );
                        std::cout << "[DEBUG] ModuleLoader: Registered generic enum template: "
                                  << enum_decl->name() << " from module: " << module_name << std::endl;
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
                        std::cout << "[DEBUG] ModuleLoader: Registered generic struct template: "
                                  << struct_decl->name() << " from module: " << module_name << std::endl;
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
                        std::cout << "[DEBUG] ModuleLoader: Registered generic function template: "
                                  << func_decl->name() << " from module: " << module_name << std::endl;
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
                        std::cout << "[DEBUG] ModuleLoader: Registered generic trait template: "
                                  << trait_decl->name() << " from module: " << module_name << std::endl;
                    }
                }
            }
        }

        std::cout << "[DEBUG] ModuleLoader: Finished registering templates from module: " << module_name << std::endl;
    }

    const std::unordered_map<std::string, std::unique_ptr<ProgramNode>> &ModuleLoader::get_imported_asts() const
    {
        return _imported_asts;
    }

} // namespace Cryo