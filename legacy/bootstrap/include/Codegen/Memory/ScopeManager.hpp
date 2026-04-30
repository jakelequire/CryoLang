#pragma once

#include "Codegen/ICodegenComponent.hpp"
#include "Codegen/CodegenContext.hpp"

#include <llvm/IR/Value.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/BasicBlock.h>
#include <string>
#include <vector>
#include <stack>
#include <functional>

namespace Cryo::Codegen
{
    /**
     * @brief Manages scope entry/exit with automatic cleanup
     *
     * This class provides RAII-based scope management, replacing the manual
     * enter_scope/exit_scope calls in the original CodegenVisitor with
     * automatic, exception-safe cleanup.
     *
     * Key features:
     * - RAII scope guards for automatic cleanup
     * - Destructor tracking and invocation
     * - Variable registration within scopes
     * - Breakable context management (loops, switch)
     */
    class ScopeManager : public ICodegenComponent
    {
    public:
        //===================================================================
        // Construction
        //===================================================================

        explicit ScopeManager(CodegenContext &ctx);
        ~ScopeManager() = default;

        //===================================================================
        // RAII Scope Guard
        //===================================================================

        /**
         * @brief RAII guard for automatic scope cleanup
         *
         * Automatically enters a scope on construction and exits it
         * on destruction, including running any registered destructors.
         *
         * Usage:
         *   {
         *       ScopeManager::ScopeGuard guard(scope_mgr, entry_block);
         *       // ... generate code within this scope ...
         *   } // Automatic cleanup here
         */
        class ScopeGuard
        {
        public:
            /**
             * @brief Construct a scope guard
             * @param mgr Reference to the ScopeManager
             * @param entry Optional entry block for the scope
             * @param exit Optional exit block for the scope
             */
            ScopeGuard(ScopeManager &mgr, llvm::BasicBlock *entry = nullptr,
                       llvm::BasicBlock *exit = nullptr);

            /**
             * @brief Destructor - automatically exits scope and runs destructors
             */
            ~ScopeGuard();

            // Disable copy and move
            ScopeGuard(const ScopeGuard &) = delete;
            ScopeGuard &operator=(const ScopeGuard &) = delete;
            ScopeGuard(ScopeGuard &&) = delete;
            ScopeGuard &operator=(ScopeGuard &&) = delete;

            /**
             * @brief Early exit the scope (before destruction)
             *
             * Useful when you need to exit a scope early but want
             * the guard to remain in scope for other reasons.
             */
            void exit_early();

            /**
             * @brief Check if scope has been exited
             */
            bool is_exited() const { return _exited; }

        private:
            ScopeManager &_mgr;
            bool _exited;
        };

        //===================================================================
        // Manual Scope Management
        //===================================================================

        /**
         * @brief Enter a new scope
         * @param entry Optional entry block
         * @param exit Optional exit block
         */
        void enter_scope(llvm::BasicBlock *entry = nullptr, llvm::BasicBlock *exit = nullptr);

        /**
         * @brief Exit current scope (runs destructors first)
         */
        void exit_scope();

        /**
         * @brief Get current scope depth
         */
        size_t scope_depth() const;

        /**
         * @brief Check if we're at global scope (no local scopes)
         */
        bool is_global_scope() const;

        /**
         * @brief Get current scope context
         * @return Reference to current scope, or throws if no scope
         */
        ScopeContext &current_scope();

        /**
         * @brief Check if a scope exists
         */
        bool has_scope() const;

        //===================================================================
        // Variable Tracking Within Scope
        //===================================================================

        /**
         * @brief Register a local variable in the current scope
         * @param name Variable name
         * @param value LLVM value
         * @param alloca Optional alloca for the variable
         */
        void register_local(const std::string &name, llvm::Value *value,
                            llvm::AllocaInst *alloca = nullptr);

        /**
         * @brief Lookup a local variable by name (searches scope chain)
         * @param name Variable name
         * @return LLVM value or nullptr if not found
         */
        llvm::Value *lookup_local(const std::string &name);

        /**
         * @brief Lookup an alloca by name (searches scope chain)
         * @param name Variable name
         * @return AllocaInst or nullptr if not found
         */
        llvm::AllocaInst *lookup_alloca(const std::string &name);

        /**
         * @brief Check if a local variable exists in any scope
         * @param name Variable name
         * @return true if variable exists
         */
        bool has_local(const std::string &name);

        //===================================================================
        // Destructor Management
        //===================================================================

        /**
         * @brief Register a variable for destruction when scope exits
         * @param var_name Variable name
         * @param value LLVM value of the variable
         * @param type_name Type name (for destructor lookup)
         * @param is_heap Whether the variable is heap-allocated
         */
        void register_destructor(const std::string &var_name, llvm::Value *value,
                                 const std::string &type_name, bool is_heap = false);

        /**
         * @brief Run all destructors for the current scope
         *
         * Called automatically by exit_scope(). You normally don't
         * need to call this directly.
         */
        void run_scope_destructors();

        /**
         * @brief Run all destructors for all active scopes
         *
         * Used for early return or exception handling where we need
         * to clean up multiple scopes.
         */
        void run_all_destructors();

        /**
         * @brief Set the destructor generation callback
         * @param callback Function to generate destructor call IR
         *
         * The callback receives: (variable_name, value, type_name, is_heap)
         */
        using DestructorCallback = std::function<void(const std::string &, llvm::Value *, const std::string &, bool)>;
        void set_destructor_callback(DestructorCallback callback);

        //===================================================================
        // Breakable Context Management (loops, switch)
        //===================================================================

        /**
         * @brief Push a loop context
         * @param condition Loop condition block
         * @param body Loop body block
         * @param continue_target Continue target block
         * @param break_target Break target block
         */
        void push_loop(llvm::BasicBlock *condition, llvm::BasicBlock *body,
                       llvm::BasicBlock *continue_target, llvm::BasicBlock *break_target);

        /**
         * @brief Push a switch context
         * @param break_target Break target block
         */
        void push_switch(llvm::BasicBlock *break_target);

        /**
         * @brief Pop the current breakable context
         */
        void pop_breakable();

        /**
         * @brief Get current breakable context (may be null)
         */
        BreakableContext *current_breakable();

        /**
         * @brief Check if we're inside a loop
         */
        bool in_loop() const;

        /**
         * @brief Check if we're inside a switch
         */
        bool in_switch() const;

        /**
         * @brief Check if we're in any breakable context
         */
        bool in_breakable() const;

        /**
         * @brief Generate break (runs destructors, branches to break target)
         * @return true if break was generated successfully
         */
        bool generate_break();

        /**
         * @brief Generate continue (runs destructors, branches to continue target)
         * @return true if continue was generated successfully
         */
        bool generate_continue();

    private:
        //===================================================================
        // Internal State
        //===================================================================

        std::vector<ScopeContext> _scope_stack;
        std::stack<BreakableContext> _breakable_stack;
        DestructorCallback _destructor_callback;

        /**
         * @brief Check if a type has a destructor
         * @param type_name Type name to check
         * @return true if type has a destructor
         */
        bool has_destructor(const std::string &type_name);
    };

    //=======================================================================
    // RAII Helper for Breakable Contexts
    //=======================================================================

    /**
     * @brief RAII guard for loop contexts
     */
    class LoopGuard
    {
    public:
        LoopGuard(ScopeManager &mgr, llvm::BasicBlock *condition,
                  llvm::BasicBlock *body, llvm::BasicBlock *continue_target,
                  llvm::BasicBlock *break_target)
            : _mgr(mgr)
        {
            _mgr.push_loop(condition, body, continue_target, break_target);
        }

        ~LoopGuard()
        {
            _mgr.pop_breakable();
        }

        LoopGuard(const LoopGuard &) = delete;
        LoopGuard &operator=(const LoopGuard &) = delete;

    private:
        ScopeManager &_mgr;
    };

    /**
     * @brief RAII guard for switch contexts
     */
    class SwitchGuard
    {
    public:
        SwitchGuard(ScopeManager &mgr, llvm::BasicBlock *break_target)
            : _mgr(mgr)
        {
            _mgr.push_switch(break_target);
        }

        ~SwitchGuard()
        {
            _mgr.pop_breakable();
        }

        SwitchGuard(const SwitchGuard &) = delete;
        SwitchGuard &operator=(const SwitchGuard &) = delete;

    private:
        ScopeManager &_mgr;
    };

} // namespace Cryo::Codegen
