#pragma once
/******************************************************************************
 * @file SymbolTable2.hpp
 * @brief Symbol table using the new Types2 system
 *
 * SymbolTable2 provides symbol management with TypeRef-based types.
 * Key improvements over the old SymbolTable:
 *
 * - Uses TypeRef instead of raw Type* pointers
 * - Module-aware with ModuleID integration
 * - Integrates with ModuleTypeRegistry for cross-module lookup
 * - Clean scope management with RAII
 ******************************************************************************/

#include "Types2/TypeID.hpp"
#include "Types2/Type.hpp"
#include "Types2/TypeArena.hpp"
#include "Types2/ModuleTypeRegistry.hpp"
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
    enum class SymbolKind2
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
    struct Symbol2
    {
        std::string name;                // Symbol name
        SymbolKind2 kind;                // Symbol kind
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


        Symbol2() : kind(SymbolKind2::Variable),
                    visibility(SymbolVisibility::Private),
                    is_mutable(false), is_initialized(false) {}

        Symbol2(const std::string &n, SymbolKind2 k, TypeRef t,
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
    class Scope2
    {
    private:
        std::unordered_map<std::string, Symbol2> _symbols;
        Scope2 *_parent;
        std::string _name;
        ModuleID _module;

    public:
        Scope2(Scope2 *parent = nullptr,
               const std::string &name = "",
               ModuleID mod = ModuleID::invalid())
            : _parent(parent), _name(name), _module(mod) {}

        // Symbol management
        bool declare(const std::string &name, const Symbol2 &symbol);
        Symbol2 *lookup(const std::string &name);
        const Symbol2 *lookup(const std::string &name) const;
        Symbol2 *lookup_local(const std::string &name);
        const Symbol2 *lookup_local(const std::string &name) const;

        // Scope info
        bool has_symbol(const std::string &name) const;
        bool has_symbol_local(const std::string &name) const;
        Scope2 *parent() { return _parent; }
        const Scope2 *parent() const { return _parent; }
        const std::string &name() const { return _name; }
        ModuleID module() const { return _module; }

        // Iteration
        const std::unordered_map<std::string, Symbol2> &symbols() const
        {
            return _symbols;
        }

        size_t symbol_count() const { return _symbols.size(); }
    };

    /**************************************************************************
     * @brief RAII scope guard for automatic scope management
     **************************************************************************/
    class ScopeGuard2
    {
    private:
        class SymbolTable2 &_table;
        bool _active;

    public:
        explicit ScopeGuard2(SymbolTable2 &table);
        ~ScopeGuard2();

        // Non-copyable, non-movable
        ScopeGuard2(const ScopeGuard2 &) = delete;
        ScopeGuard2 &operator=(const ScopeGuard2 &) = delete;

        // Cancel the scope exit
        void release() { _active = false; }
    };

    /**************************************************************************
     * @brief Symbol table using Types2 system
     *
     * Manages symbols with proper type references and module awareness.
     *
     * Usage:
     *   SymbolTable2 symbols(arena, modules);
     *
     *   // Declare a variable
     *   Symbol2 var;
     *   var.name = "x";
     *   var.kind = SymbolKind2::Variable;
     *   var.type = arena.get_i32();
     *   symbols.declare(var);
     *
     *   // Look up a symbol
     *   auto sym = symbols.lookup("x");
     **************************************************************************/
    class SymbolTable2
    {
    private:
        TypeArena &_arena;
        ModuleTypeRegistry &_modules;

        // Scope stack
        std::vector<std::unique_ptr<Scope2>> _scopes;
        Scope2 *_current_scope;

        // Current context
        ModuleID _current_module;
        std::vector<ImportDecl> _active_imports;

    public:
        // ====================================================================
        // Construction
        // ====================================================================

        SymbolTable2(TypeArena &arena, ModuleTypeRegistry &modules);
        ~SymbolTable2() = default;

        // Non-copyable
        SymbolTable2(const SymbolTable2 &) = delete;
        SymbolTable2 &operator=(const SymbolTable2 &) = delete;

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
        ScopeGuard2 scope_guard();

        /**
         * @brief Get current scope depth
         */
        size_t scope_depth() const { return _scopes.size(); }

        /**
         * @brief Get current scope
         */
        Scope2 *current_scope() { return _current_scope; }
        const Scope2 *current_scope() const { return _current_scope; }

        // ====================================================================
        // Symbol Declaration
        // ====================================================================

        /**
         * @brief Declare a symbol in the current scope
         * @return true if successful, false if already declared
         */
        bool declare(const Symbol2 &symbol);

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
                            SymbolKind2 kind,
                            SourceLocation loc,
                            TypeRef type,
                            const std::string &scope_name = "");

        /**
         * @brief Register a namespace with its symbol map
         * For import handling - maps namespace name to its symbols
         */
        void register_namespace(const std::string &namespace_name,
                                const std::unordered_map<std::string, Symbol2> &symbols);

        // ====================================================================
        // Symbol Lookup
        // ====================================================================

        /**
         * @brief Look up a symbol by name
         * @return Symbol or nullptr if not found
         */
        Symbol2 *lookup(const std::string &name);
        const Symbol2 *lookup(const std::string &name) const;

        /**
         * @brief Look up a symbol in current scope only
         */
        Symbol2 *lookup_local(const std::string &name);
        const Symbol2 *lookup_local(const std::string &name) const;

        /**
         * @brief Look up a qualified symbol (e.g., "std::collections::Array")
         */
        Symbol2 *lookup_qualified(const std::vector<std::string> &path);
        const Symbol2 *lookup_qualified(const std::vector<std::string> &path) const;

        /**
         * @brief Look up a symbol considering imports
         */
        Symbol2 *lookup_with_imports(const std::string &name);
        const Symbol2 *lookup_with_imports(const std::string &name) const;

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
        void for_each_symbol(std::function<void(const Symbol2 &)> callback) const;

        /**
         * @brief Iterate over symbols in current scope
         */
        void for_each_local(std::function<void(const Symbol2 &)> callback) const;

        /**
         * @brief Get all functions with a given name (overloads)
         */
        std::vector<const Symbol2 *> get_overloads(const std::string &name) const;

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
        Symbol2 *lookup_symbol(const std::string &name) { return lookup(name); }
        const Symbol2 *lookup_symbol(const std::string &name) const { return lookup(name); }

        /**
         * @brief Look up a symbol in a specific namespace
         * @param ns Namespace to look in
         * @param name Symbol name
         * @return Symbol pointer or nullptr if not found
         */
        Symbol2 *lookup_namespaced_symbol(const std::string &ns, const std::string &name);
        const Symbol2 *lookup_namespaced_symbol(const std::string &ns, const std::string &name) const;

        /**
         * @brief Get all symbols for LSP support
         */
        std::vector<Symbol2> get_all_symbols_for_lsp() const;

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
     * @brief Convert SymbolKind2 to string
     **************************************************************************/
    std::string symbol_kind_to_string(SymbolKind2 kind);

    /**************************************************************************
     * @brief Convert SymbolVisibility to string
     **************************************************************************/
    std::string visibility_to_string(SymbolVisibility vis);

} // namespace Cryo
