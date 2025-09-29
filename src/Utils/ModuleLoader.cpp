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

        // Check if already loaded
        auto cached = _loaded_modules.find(resolved_path);
        if (cached != _loaded_modules.end())
        {
            std::cout << "[DEBUG] ModuleLoader: Using cached module" << std::endl;
            // Since we can't copy, we need to return a reference or handle this differently
            // For now, let's just reload it
            std::cout << "[DEBUG] ModuleLoader: Reloading cached module (copy issue)" << std::endl;
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

        // Cache the result (move it)
        _loaded_modules[resolved_path] = std::move(result);
        // We need to create a new result since the original was moved
        return load_module_from_file(resolved_path, import_path);
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
            result.ast = std::move(ast);
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
                    Type *function_type = nullptr;

                    if (type_context)
                    {
                        // Create proper FunctionType from the declaration
                        function_type = create_function_type_from_declaration(func_decl, type_context);
                    }

                    // Create function symbol with proper type information
                    Symbol symbol(func_decl->name(), SymbolKind::Function, func_decl->location(), function_type, module_name);
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

    std::string ModuleLoader::get_parent_directory(const std::string &file_path)
    {
        std::filesystem::path path(file_path);
        return path.parent_path().string();
    }

} // namespace Cryo