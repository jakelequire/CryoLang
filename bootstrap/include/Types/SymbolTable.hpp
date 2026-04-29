#pragma once
/******************************************************************************
 * @file SymbolTable.hpp
 * @brief Symbol table using the new Types system
 *
 * SymbolTable provides symbol management with TypeRef-based types.
 * Key improvements over the old SymbolTable:
 *
 * - Uses TypeRef instead of raw Type* pointers
 * - Module-aware with ModuleID integration
 * - Integrates with ModuleTypeRegistry for cross-module lookup
 * - Clean scope management with RAII
 ******************************************************************************/

#include "Types/TypeID.hpp"
#include "Types/Type.hpp"
#include "Types/TypeArena.hpp"
#include "Types/ModuleTypeRegistry.hpp"
#include "Lexer/lexer.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <memory>
#include <functional>

namespace Cryo
{
    /**************************************************************************
     * @brief Symbol kinds in the symbol table
     **************************************************************************/
    enum class SymbolKind
    {
        Variable,    // Local/global variable
        Parameter,   // Function parameter
        Function,    // Function declaration
        Method,      // Method (associated with type)
        Type,        // Type (struct, class, enum, etc.)
        TypeAlias,   // Type alias
        Constant,    // Compile-time constant
        Field,       // Struct/class field
        EnumVariant, // Enum variant
        GenericParam,// Generic type parameter
        Namespace,   // Namespace
        Import,      // Imported symbol
        Intrinsic,   // Compiler intrinsic
    };

    /**************************************************************************
     * @brief Visibility levels for symbols
     **************************************************************************/
    enum class SymbolVisibility
    {
        Private,   // Only visible in declaring module
        Internal,  // Visible in declaring module and submodules
        Public,    // Visible everywhere
    };

    /**************************************************************************
     * @brief Symbol information
     **************************************************************************/
    struct Symbol
    {
        std::string name;                // Symbol name
        SymbolKind kind;                // Symbol kind
        TypeRef type;                    // Symbol's type
        ModuleID module;                 // Declaring module
        SourceLocation location;         // Declaration location
        SymbolVisibility visibility;     // Visibility
        bool is_mutable;                 // For variables
        bool is_initialized;             // For variables

        // Optional metadata
        std::string qualified_name;      // Fully qualified name
        std::string documentation;       // Documentation string
        std::string scope;               // Namespace scope (for compatibility)


        Symbol() : kind(SymbolKind::Variable),
                    visibility(SymbolVisibility::Private),
                    is_mutable(false), is_initialized(false) {}

        Symbol(const std::string &n, SymbolKind k, TypeRef t,
                ModuleID mod, SourceLocation loc)
            : name(n), kind(k), type(t), module(mod), location(loc),
              visibility(SymbolVisibility::Private),
              is_mutable(false), is_initialized(false) {}

        // Check if symbol is accessible from a module
        bool is_accessible_from(ModuleID from_module) const
        {
            if (visibility == SymbolVisibility::Public)
                return true;
            if (visibility == SymbolVisibility::Private)
                return from_module == module;
            // Internal: accessible from same module or submodules
            // This would need module hierarchy info
            return from_module == module;
        }
    };

    /**************************************************************************
     * @brief Scope in the symbol table
     **************************************************************************/
    class Scope
    {
    private:
        std::unordered_map<std::string, Symbol> _symbols;
        std::unordered_map<std::string, std::vector<Symbol>> _function_overloads;
        Scope *_parent;
        std::string _name;
        ModuleID _module;

    public:
        Scope(Scope *parent = nullptr,
               const std::string &name = "",
               ModuleID mod = ModuleID::invalid())
            : _parent(parent), _name(name), _module(mod) {}

        // Symbol management
        bool declare(const std::string &name, const Symbol &symbol);
        Symbol *lookup(const std::string &name);
        const Symbol *lookup(const std::string &name) const;
        Symbol *lookup_local(const std::string &name);
        const Symbol *lookup_local(const std::string &name) const;

        // Overload support
        std::vector<const Symbol *> lookup_overloads(const std::string &name) const;
        const std::unordered_map<std::string, std::vector<Symbol>> &function_overloads() const
        {
            return _function_overloads;
        }

        // Scope info
        bool has_symbol(const std::string &name) const;
        bool has_symbol_local(const std::string &name) const;
        Scope *parent() { return _parent; }
        const Scope *parent() const { return _parent; }
        const std::string &name() const { return _name; }
        ModuleID module() const { return _module; }

        // Iteration
        const std::unordered_map<std::string, Symbol> &symbols() const
        {
            return _symbols;
        }

        size_t symbol_count() const;
        size_t primary_symbol_count() const { return _symbols.size(); }
    };

    /**************************************************************************
     * @brief RAII scope guard for automatic scope management
     **************************************************************************/
    class ScopeGuard
    {
    private:
        class SymbolTable &_table;
        bool _active;

    public:
        explicit ScopeGuard(SymbolTable &table);
        ~ScopeGuard();

        // Non-copyable, non-movable
        ScopeGuard(const ScopeGuard &) = delete;
        ScopeGuard &operator=(const ScopeGuard &) = delete;

        // Cancel the scope exit
        void release() { _active = false; }
    };

    /**************************************************************************
     * @brief Symbol table using Types system
     *
     * Manages symbols with proper type references and module awareness.
     *
     * Usage:
     *   SymbolTable symbols(arena, modules);
     *
     *   // Declare a variable
     *   Symbol var;
     *   var.name = "x";
     *   var.kind = SymbolKind::Variable;
     *   var.type = arena.get_i32();
     *   symbols.declare(var);
     *
     *   // Look up a symbol
     *   auto sym = symbols.lookup("x");
     **************************************************************************/
    class SymbolTable
    {
    private:
        TypeArena &_arena;
        ModuleTypeRegistry &_modules;

        // Scope stack
        std::vector<std::unique_ptr<Scope>> _scopes;
        Scope *_current_scope;

        // Current context
        ModuleID _current_module;
        std::vector<ImportDecl> _active_imports;

    public:
        // ====================================================================
        // Construction
        // ====================================================================

        SymbolTable(TypeArena &arena, ModuleTypeRegistry &modules);
        ~SymbolTable() = default;

        // Non-copyable
        SymbolTable(const SymbolTable &) = delete;
        SymbolTable &operator=(const SymbolTable &) = delete;

        // ====================================================================
        // Module Context
        // ====================================================================

        void set_current_module(ModuleID mod) { _current_module = mod; }
        ModuleID current_module() const { return _current_module; }

        void add_import(const ImportDecl &import);
        void clear_imports() { _active_imports.clear(); }
        const std::vector<ImportDecl> &imports() const { return _active_imports; }

        // ====================================================================
        // Scope Management
        // ====================================================================

        /**
         * @brief Enter a new scope
         * @param name Optional scope name (for debugging)
         */
        void enter_scope(const std::string &name = "");

        /**
         * @brief Exit current scope
         */
        void exit_scope();

        /**
         * @brief Get RAII scope guard
         */
        ScopeGuard scope_guard();

        /**
         * @brief Get current scope depth
         */
        size_t scope_depth() const { return _scopes.size(); }

        /**
         * @brief Get current scope
         */
        Scope *current_scope() { return _current_scope; }
        const Scope *current_scope() const { return _current_scope; }

        // ====================================================================
        // Symbol Declaration
        // ====================================================================

        /**
         * @brief Declare a symbol in the current scope
         * @return true if successful, false if already declared
         */
        bool declare(const Symbol &symbol);

        /**
         * @brief Declare a variable
         */
        bool declare_variable(const std::string &name,
                              TypeRef type,
                              SourceLocation loc,
                              bool is_mutable = true);

        /**
         * @brief Declare a function
         */
        bool declare_function(const std::string &name,
                              TypeRef type,
                              SourceLocation loc);

        /**
         * @brief Declare a type
         */
        bool declare_type(const std::string &name,
                          TypeRef type,
                          SourceLocation loc);

        /**
         * @brief Declare a constant
         */
        bool declare_constant(const std::string &name,
                              TypeRef type,
                              SourceLocation loc);

        /**
         * @brief Declare an intrinsic function
         */
        bool declare_intrinsic(const std::string &name,
                               TypeRef type,
                               SourceLocation loc);

        /**
         * @brief Declare a symbol with custom kind (for flexibility)
         */
        bool declare_symbol(const std::string &name,
                            SymbolKind kind,
                            SourceLocation loc,
                            TypeRef type,
                            const std::string &scope_name = "");

        /**
         * @brief Register a namespace with its symbol map
         * For import handling - maps namespace name to its symbols
         */
        void register_namespace(const std::string &namespace_name,
                                const std::unordered_map<std::string, Symbol> &symbols);

        // ====================================================================
        // Symbol Lookup
        // ====================================================================

        /**
         * @brief Look up a symbol by name
         * @return Symbol or nullptr if not found
         */
        Symbol *lookup(const std::string &name);
        const Symbol *lookup(const std::string &name) const;

        /**
         * @brief Look up a symbol in current scope only
         */
        Symbol *lookup_local(const std::string &name);
        const Symbol *lookup_local(const std::string &name) const;

        /**
         * @brief Look up a qualified symbol (e.g., "std::collections::Array")
         */
        Symbol *lookup_qualified(const std::vector<std::string> &path);
        const Symbol *lookup_qualified(const std::vector<std::string> &path) const;

        /**
         * @brief Look up a symbol considering imports
         */
        Symbol *lookup_with_imports(const std::string &name);
        const Symbol *lookup_with_imports(const std::string &name) const;

        /**
         * @brief Check if a symbol exists
         */
        bool has_symbol(const std::string &name) const;

        /**
         * @brief Check if a symbol exists in current scope only
         */
        bool has_symbol_local(const std::string &name) const;

        // ====================================================================
        // Type Resolution
        // ====================================================================

        /**
         * @brief Get the type of a symbol
         */
        std::optional<TypeRef> get_type(const std::string &name) const;

        /**
         * @brief Resolve a type name to TypeRef
         */
        std::optional<TypeRef> resolve_type(const std::string &name) const;

        /**
         * @brief Look up a struct type by name
         * @return TypeRef if found (and is a struct), invalid TypeRef otherwise
         */
        TypeRef lookup_struct_type(const std::string &name) const;

        /**
         * @brief Look up a class type by name
         * @return TypeRef if found (and is a class), invalid TypeRef otherwise
         */
        TypeRef lookup_class_type(const std::string &name) const;

        /**
         * @brief Look up an enum type by name
         * @return TypeRef if found (and is an enum), invalid TypeRef otherwise
         */
        TypeRef lookup_enum_type(const std::string &name) const;

        // ====================================================================
        // Iteration
        // ====================================================================

        /**
         * @brief Iterate over all visible symbols
         */
        void for_each_symbol(std::function<void(const Symbol &)> callback) const;

        /**
         * @brief Iterate over symbols in current scope
         */
        void for_each_local(std::function<void(const Symbol &)> callback) const;

        /**
         * @brief Get all functions with a given name (overloads)
         */
        std::vector<const Symbol *> get_overloads(const std::string &name) const;

        // ====================================================================
        // Context Access
        // ====================================================================

        TypeArena &arena() { return _arena; }
        const TypeArena &arena() const { return _arena; }
        ModuleTypeRegistry &modules() { return _modules; }
        const ModuleTypeRegistry &modules() const { return _modules; }

        // ====================================================================
        // Backward Compatibility Methods
        // ====================================================================

        /**
         * @brief Look up a symbol by name (backward compatibility)
         * @return Symbol pointer or nullptr if not found
         */
        Symbol *lookup_symbol(const std::string &name) { return lookup(name); }
        const Symbol *lookup_symbol(const std::string &name) const { return lookup(name); }

        /**
         * @brief Look up a symbol in a specific namespace
         * @param ns Namespace to look in
         * @param name Symbol name
         * @return Symbol pointer or nullptr if not found
         */
        Symbol *lookup_namespaced_symbol(const std::string &ns, const std::string &name);
        const Symbol *lookup_namespaced_symbol(const std::string &ns, const std::string &name) const;

        /**
         * @brief Get all symbols for LSP support
         */
        std::vector<Symbol> get_all_symbols_for_lsp() const;

        // ====================================================================
        // Debug
        // ====================================================================

        /**
         * @brief Print symbol table contents
         */
        void dump(std::ostream &os) const;

    private:
        /**
         * @brief Ensure there's at least a global scope
         */
        void ensure_global_scope();
    };

    /**************************************************************************
     * @brief Convert SymbolKind to string
     **************************************************************************/
    std::string symbol_kind_to_string(SymbolKind kind);

    /**************************************************************************
     * @brief Convert SymbolVisibility to string
     **************************************************************************/
    std::string visibility_to_string(SymbolVisibility vis);

} // namespace Cryo
