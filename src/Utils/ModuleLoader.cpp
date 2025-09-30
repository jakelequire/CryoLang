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
    ModuleLoader::ModuleLoader(SymbolTable &symbol_table)
        : _symbol_table(symbol_table)
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

            // Extract exported symbols (legacy)
            result.exported_symbols = extract_exported_symbols(*ast);
            std::cout << "[DEBUG] ModuleLoader: Found " << result.exported_symbols.size()
                      << " exported symbols" << std::endl;

            // Create full symbol map with proper symbol information
            result.symbol_map = create_symbol_map(*ast, result.module_name);
            std::cout << "[DEBUG] ModuleLoader: Created symbol map with " << result.symbol_map.size()
                      << " symbols" << std::endl;

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
                    // Create function symbol with nullptr type for now to avoid memory issues
                    // The proper type will be resolved when the symbol is actually used
                    Symbol symbol(func_decl->name(), SymbolKind::Function, func_decl->location(), nullptr, module_name);
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
                    // Create type symbol for enum
                    Symbol symbol(enum_decl->name(), SymbolKind::Type, enum_decl->location(), nullptr, module_name);
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
        const std::string &return_type_str = func_decl->return_type_annotation();
        if (!return_type_str.empty() && return_type_str != "void")
        {
            return_type = type_context->parse_type_from_string(return_type_str);
        }
        else
        {
            // Default to void for functions without explicit return type or explicit void
            return_type = type_context->get_void_type();
        }

        // Get parameter types
        std::vector<Type *> parameter_types;
        for (const auto &param : func_decl->parameters())
        {
            const std::string &param_type_str = param->type_annotation();
            if (!param_type_str.empty())
            {
                Type *param_type = type_context->parse_type_from_string(param_type_str);
                if (param_type)
                {
                    parameter_types.push_back(param_type);
                }
                else
                {
                    std::cerr << "Warning: Failed to parse parameter type '" << param_type_str
                              << "' for function '" << func_decl->name() << "'" << std::endl;
                    return nullptr;
                }
            }
            else
            {
                std::cerr << "Warning: Parameter '" << param->name()
                          << "' in function '" << func_decl->name() << "' has no type annotation" << std::endl;
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

} // namespace Cryo