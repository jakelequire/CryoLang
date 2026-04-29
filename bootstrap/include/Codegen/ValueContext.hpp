#pragma once

#include <llvm/IR/Value.h>
#include <llvm/IR/Instructions.h>
#include <memory>
#include <stack>
#include <unordered_map>
#include <string>
#include <vector>

namespace Cryo::Codegen
{
    /**
     * @brief Manages LLVM values and their associations during code generation
     *
     * ValueContext provides a structured way to track and manage LLVM values
     * throughout the code generation process. It maintains mappings between
     * CryoLang identifiers and their corresponding LLVM values, handles scope
     * management for local variables, and provides utilities for value
     * manipulation and type conversion.
     */
    class ValueContext
    {
    public:
        /**
         * @brief Scope frame for managing local variables
         */
        struct ScopeFrame
        {
            std::unordered_map<std::string, llvm::Value *> values;
            std::unordered_map<std::string, llvm::AllocaInst *> allocas;
            std::unordered_map<std::string, llvm::Type *> alloca_types; // Track element types for allocas
            std::string scope_name;

            explicit ScopeFrame(const std::string &name = "") : scope_name(name) {}
        };

        //===================================================================
        // Construction
        //===================================================================

        ValueContext() = default;
        ~ValueContext() = default;

        //===================================================================
        // Scope Management
        //===================================================================

        /**
         * @brief Enter a new scope
         * @param scope_name Optional name for the scope
         */
        void enter_scope(const std::string &scope_name = "");

        /**
         * @brief Exit current scope
         */
        void exit_scope();

        /**
         * @brief Get current scope depth
         * @return Number of nested scopes
         */
        size_t get_scope_depth() const { return _scope_stack.size(); }

        /**
         * @brief Check if currently in global scope
         * @return true if at global scope
         */
        bool is_global_scope() const { return _scope_stack.empty(); }

        //===================================================================
        // Value Management
        //===================================================================

        /**
         * @brief Register value in current scope
         * @param name Variable/identifier name
         * @param value LLVM value
         * @param alloca Optional alloca instruction for the value
         * @param alloca_type Optional element type for the alloca
         */
        void set_value(const std::string &name, llvm::Value *value, llvm::AllocaInst *alloca = nullptr, llvm::Type *alloca_type = nullptr);

        /**
         * @brief Lookup value by name (searches up scope chain)
         * @param name Variable/identifier name
         * @return LLVM value or nullptr if not found
         */
        llvm::Value *get_value(const std::string &name);

        /**
         * @brief Lookup alloca by name (searches up scope chain)
         * @param name Variable/identifier name
         * @return LLVM alloca instruction or nullptr if not found
         */
        llvm::AllocaInst *get_alloca(const std::string &name);

        /**
         * @brief Lookup alloca element type by name (searches up scope chain)
         * @param name Variable/identifier name
         * @return LLVM element type or nullptr if not found
         */
        llvm::Type *get_alloca_type(const std::string &name);

        /**
         * @brief Check if value exists in any scope
         * @param name Variable/identifier name
         * @return true if value exists
         */
        bool has_value(const std::string &name);

        /**
         * @brief Get all values in current scope
         * @return Map of name to value in current scope
         */
        const std::unordered_map<std::string, llvm::Value *> &get_current_scope_values();

        //===================================================================
        // Global Value Management
        //===================================================================

        /**
         * @brief Set global value (accessible from any scope)
         * @param name Global name
         * @param value LLVM value
         */
        void set_global_value(const std::string &name, llvm::Value *value);

        /**
         * @brief Get global value
         * @param name Global name
         * @return LLVM value or nullptr if not found
         */
        llvm::Value *get_global_value(const std::string &name);

        /**
         * @brief Check if global value exists
         * @param name Global name
         * @return true if global exists
         */
        bool has_global_value(const std::string &name);

        //===================================================================
        // Temporary Value Management
        //===================================================================

        /**
         * @brief Create temporary value name
         * @param prefix Optional prefix for the name
         * @return Unique temporary name
         */
        std::string create_temp_name(const std::string &prefix = "tmp");

        /**
         * @brief Register temporary value
         * @param value LLVM value
         * @param prefix Optional prefix for the name
         * @return Generated temporary name
         */
        std::string register_temp_value(llvm::Value *value, const std::string &prefix = "tmp");

        //===================================================================
        // Value Utilities
        //===================================================================

        /**
         * @brief Check if value is an lvalue (can be assigned to)
         * @param value LLVM value to check
         * @return true if value is an lvalue
         */
        bool is_lvalue(llvm::Value *value);

        /**
         * @brief Get the alloca instruction for a value if it exists
         * @param value LLVM value
         * @return Alloca instruction or nullptr
         */
        llvm::AllocaInst *get_value_alloca(llvm::Value *value);

        /**
         * @brief Mark value as constant (immutable)
         * @param name Value name
         */
        void mark_constant(const std::string &name);

        /**
         * @brief Check if value is marked as constant
         * @param name Value name
         * @return true if value is constant
         */
        bool is_constant(const std::string &name);

        //===================================================================
        // Debug and Inspection
        //===================================================================

        /**
         * @brief Print current scope stack (for debugging)
         * @param os Output stream
         */
        void print_scope_stack(std::ostream &os) const;

        /**
         * @brief Get current scope name
         * @return Name of current scope or empty if global
         */
        std::string get_current_scope_name() const;

        /**
         * @brief Get number of values in all scopes
         * @return Total value count
         */
        size_t get_total_value_count() const;

    private:
        //===================================================================
        // Private Implementation
        //===================================================================

        // Scope management
        std::vector<ScopeFrame> _scope_stack;

        // Global values (functions, global variables, etc.)
        std::unordered_map<std::string, llvm::Value *> _global_values;

        // Constant tracking
        std::unordered_map<std::string, bool> _constants;

        // Temporary name generation
        size_t _temp_counter = 0;

        //===================================================================
        // Private Methods
        //===================================================================

        /**
         * @brief Get current scope frame
         * @return Reference to current scope or creates global frame
         */
        ScopeFrame &get_current_scope();

        /**
         * @brief Search for value in scope chain
         * @param name Value name
         * @return Pair of (value, alloca) or (nullptr, nullptr)
         */
        std::pair<llvm::Value *, llvm::AllocaInst *> search_scopes(const std::string &name);
    };

} // namespace Cryo::Codegen