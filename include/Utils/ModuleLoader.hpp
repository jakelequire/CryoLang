#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <vector>

#include "AST/ASTNode.hpp"
#include "AST/SymbolTable.hpp"

namespace Cryo
{
    // Forward declarations
    class Parser;
    class CompilerInstance;

    /**
     * @brief Handles module loading and import resolution for the Cryo compiler
     */
    class ModuleLoader
    {
    public:
        struct ImportResult
        {
            bool success;
            std::string error_message;
            std::string module_name; // Name of the module/namespace
            std::unique_ptr<ProgramNode> ast;
            std::vector<std::string> exported_symbols;          // Legacy list of symbol names
            std::unordered_map<std::string, Symbol> symbol_map; // Full symbol information

            // Make it movable but not copyable
            ImportResult() = default;
            ImportResult(const ImportResult &) = delete;
            ImportResult &operator=(const ImportResult &) = delete;
            ImportResult(ImportResult &&) = default;
            ImportResult &operator=(ImportResult &&) = default;
        };

    private:
        std::string _stdlib_root;                                      // Root directory for standard library
        std::string _current_file_dir;                                 // Directory of currently compiling file
        std::unordered_map<std::string, ImportResult> _loaded_modules; // Cache of loaded modules
        std::unordered_set<std::string> _loading_modules;              // Track modules currently being loaded (cycle detection)
        SymbolTable &_symbol_table;                                    // Reference to main symbol table

    public:
        explicit ModuleLoader(SymbolTable &symbol_table);
        ~ModuleLoader() = default;

        /**
         * @brief Set the standard library root directory
         * @param stdlib_root Path to the standard library directory
         */
        void set_stdlib_root(const std::string &stdlib_root);

        /**
         * @brief Set the current file directory for relative imports
         * @param current_file_path Path to the currently compiling file
         */
        void set_current_file(const std::string &current_file_path);

        /**
         * @brief Load and process an import declaration
         * @param import_node The import declaration node from the AST
         * @return ImportResult containing success status and any symbols to add
         */
        ImportResult load_import(const ImportDeclarationNode &import_node);

        /**
         * @brief Resolve import path to absolute filesystem path
         * @param import_path The import path from the declaration
         * @param import_type Whether it's relative or absolute (stdlib)
         * @return Resolved absolute filesystem path
         */
        std::string resolve_import_path(const std::string &import_path, ImportDeclarationNode::ImportType import_type);

        /**
         * @brief Clear the module cache (useful for testing)
         */
        void clear_cache();

    private:
        /**
         * @brief Load a module from filesystem and parse it
         * @param file_path Absolute path to the module file
         * @param import_path Original import path for error messages
         * @return ImportResult with parsed AST and exported symbols
         */
        ImportResult load_module_from_file(const std::string &file_path, const std::string &import_path);

        /**
         * @brief Extract exported symbols from a module's AST
         * @param ast The parsed AST of the module
         * @return List of exported symbol names
         */
        std::vector<std::string> extract_exported_symbols(const ProgramNode &ast);

        /**
         * @brief Create full symbol map from a module's AST
         * @param ast The parsed AST of the module
         * @param module_name Name of the module/namespace
         * @return Map of symbol names to Symbol objects with full information
         */
        std::unordered_map<std::string, Symbol> create_symbol_map(const ProgramNode &ast, const std::string &module_name);

        /**
         * @brief Create a FunctionType from a FunctionDeclarationNode
         * @param func_decl The function declaration node
         * @param type_context TypeContext for creating type objects
         * @return FunctionType object or nullptr if creation fails
         */
        Type *create_function_type_from_declaration(const FunctionDeclarationNode *func_decl, TypeContext *type_context);

        /**
         * @brief Check if we have a circular import dependency
         * @param module_path The module path being loaded
         * @return True if circular dependency detected
         */
        bool has_circular_dependency(const std::string &module_path);

        /**
         * @brief Get parent directory from file path
         * @param file_path Full path to a file
         * @return Directory containing the file
         */
        std::string get_parent_directory(const std::string &file_path);
    };

} // namespace Cryo