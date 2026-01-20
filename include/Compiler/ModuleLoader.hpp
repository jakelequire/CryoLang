#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <vector>
#include <functional>

#include "AST/ASTNode.hpp"
#include "Types/SymbolTable.hpp"
#include "AST/TemplateRegistry.hpp"

namespace Cryo
{
    // Forward declarations
    class Parser;
    class CompilerInstance;
    class ASTContext;
    class GenericRegistry;

    /**
     * @brief Handles module loading and import resolution for the Cryo compiler
     */
    class ModuleLoader
    {
    public:
        struct ImportResult
        {
            bool success = false;
            std::string error_message;
            std::string module_name;
            std::unordered_map<std::string, Symbol> symbol_map;
            std::vector<std::string> exported_symbols;
            std::string namespace_alias; // For single specific imports treated as namespace aliases
        };

    private:
        std::string _stdlib_root;                                      // Root directory for standard library
        std::string _current_file_dir;                                 // Directory of currently compiling file
        std::unordered_map<std::string, ImportResult> _loaded_modules; // Cache of loaded modules
        std::unordered_set<std::string> _loading_modules;              // Track modules currently being loaded (cycle detection)
        SymbolTable &_symbol_table;                                    // Reference to main symbol table
        TemplateRegistry &_template_registry;                          // Reference to template registry for cross-module templates
        ASTContext &_ast_context;                                      // Reference to main AST context for type consistency
        GenericRegistry *_generic_registry = nullptr;                  // Optional reference to new type system generic registry

        // Storage for imported ASTs to keep them alive for template registration
        std::unordered_map<std::string, std::unique_ptr<ProgramNode>> _imported_asts;

        // Callback for triggering auto-imports on dependencies
        std::function<void(SymbolTable *, const std::string &, const std::string &)> _auto_import_callback;

        // Static storage for global executable path
        static std::string _global_executable_path;

    public:
        explicit ModuleLoader(SymbolTable &symbol_table, TemplateRegistry &template_registry, ASTContext &ast_context);
        ~ModuleLoader() = default;

        /**
         * @brief Set the standard library root directory
         * @param stdlib_root Path to the standard library directory
         */
        void set_stdlib_root(const std::string &stdlib_root);

        /**
         * @brief Set the generic registry for the new type system
         * @param registry Pointer to the GenericRegistry
         */
        void set_generic_registry(GenericRegistry *registry) { _generic_registry = registry; }

        /**
         * @brief Auto-detect and set the standard library root directory
         * @param executable_path Path to the cryo executable (from argv[0])
         * @return True if stdlib was found and set, false otherwise
         */
        bool auto_detect_stdlib_root(const std::string &executable_path = "");

        /**
         * @brief Set the global executable path for stdlib auto-detection
         * @param executable_path Path to the cryo executable (from argv[0])
         */
        static void set_global_executable_path(const std::string &executable_path);

        /**
         * @brief Find the standard library directory dynamically
         * @param executable_path Path to the cryo executable (from argv[0])
         * @return Path to stdlib directory, or empty string if not found
         */
        static std::string find_stdlib_directory(const std::string &executable_path = "");

        /**
         * @brief Set the current file directory for relative imports
         * @param current_file_path Path to the currently compiling file
         */
        void set_current_file(const std::string &current_file_path);

        /**
         * @brief Set callback for auto-imports on runtime dependencies
         * @param callback Function to call for auto-imports (symbol_table, scope_name, source_file)
         */
        void set_auto_import_callback(std::function<void(SymbolTable *, const std::string &, const std::string &)> callback);

        /**
         * @brief Resolve module file path, checking for _module.cryo files
         * @param module_path The module path to resolve
         * @param import_type Type of import (relative or absolute)
         * @return Resolved file path
         */
        std::string resolve_module_file_path(const std::string &module_path, ImportDeclarationNode::ImportStyle import_type);

        /**
         * @brief Check if a path represents a module directory (contains _module.cryo)
         * @param path The path to check
         * @return True if the path is a module directory
         */
        bool is_module_directory(const std::string &path);

        /**
         * @brief Load and process an import declaration
         * @param import_node The import declaration node from the AST
         * @return ImportResult containing success status and any symbols to add
         */
        ImportResult load_import(const ImportDeclarationNode &import_node);

        /**
         * @brief Resolve import path to absolute filesystem path
         * @param import_path The module path from the declaration (e.g., \"core::option\")
         * @return Resolved absolute filesystem path
         */
        std::string resolve_import_path(const std::string &import_path);

        /**
         * @brief Clear the module cache (useful for testing)
         */
        void clear_cache();

        /**
         * @brief Get all imported AST nodes for additional processing
         * @return Map of module names to their parsed AST nodes
         */
        const std::unordered_map<std::string, std::unique_ptr<ProgramNode>> &get_imported_asts() const;

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
         * @brief Register generic templates from a module's AST into the template registry
         * @param ast The parsed AST of the module
         * @param module_name Name of the module/namespace for debugging
         */
        void register_templates_from_ast(const ProgramNode &ast, const std::string &module_name);

        /**
         * @brief Mark all declarations in an imported AST with their source module
         * @param ast The parsed AST of the imported module
         * @param module_name Name of the source module
         */
        void mark_declarations_as_imported(ProgramNode &ast, const std::string &module_name);

        /**
         * @brief Create a FunctionType from a FunctionDeclarationNode
         * @param func_decl The function declaration node
         * @param type_arena TypeArena for creating type objects
         * @return FunctionType object or invalid TypeRef if creation fails
         */
        TypeRef create_function_type_from_declaration(const FunctionDeclarationNode *func_decl, TypeArena *type_arena);

        /**
         * @brief Create a FunctionType from an IntrinsicDeclarationNode
         * @param intrinsic_decl The intrinsic declaration node
         * @param type_arena TypeArena for creating type objects
         * @return FunctionType object or invalid TypeRef if creation fails
         */
        TypeRef create_function_type_from_declaration(const IntrinsicDeclarationNode *intrinsic_decl, TypeArena *type_arena);

        /**
         * @brief Resolve a primitive type annotation string to a Type object
         * @param type_str The type annotation string (e.g., "boolean", "i32")
         * @param arena TypeArena for getting primitive types
         * @return Type object or invalid TypeRef if not a primitive type
         */
        TypeRef resolve_primitive_type(const std::string &type_str, TypeArena &arena);

        /**
         * @brief Qualify a type annotation with its full namespace path
         * @param type_annotation The type annotation to qualify (e.g., "Option<u64>")
         * @return Fully qualified type annotation (e.g., "std::core::option::Option<u64>")
         *
         * Uses TemplateRegistry to find the defining namespace for user-defined types.
         * Primitives are returned unqualified.
         */
        std::string qualify_type_annotation(const std::string &type_annotation);

        /**
         * @brief Filter import result to only include specific symbols
         * @param result The import result to filter
         * @param specific_imports List of specific symbols to include
         * @return Filtered import result with only the requested symbols
         */
        ImportResult filter_specific_imports(ImportResult result, const std::vector<std::string> &specific_imports);

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