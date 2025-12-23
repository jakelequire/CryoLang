#pragma once

#include "Codegen/CodegenContext.hpp"

#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <string>

namespace Cryo::Codegen
{
    /**
     * @brief Base interface for all codegen component classes
     *
     * Provides common functionality and ensures consistent access patterns
     * across all generators. This is the base class that all specialized
     * codegen components (OperatorCodegen, CallCodegen, etc.) inherit from.
     *
     * The design follows the composition pattern - each component receives
     * a reference to the shared CodegenContext and can access any infrastructure
     * through it.
     */
    class ICodegenComponent
    {
    public:
        //===================================================================
        // Construction
        //===================================================================

        explicit ICodegenComponent(CodegenContext &ctx) : _ctx(ctx) {}
        virtual ~ICodegenComponent() = default;

        // Non-copyable (components hold references)
        ICodegenComponent(const ICodegenComponent &) = delete;
        ICodegenComponent &operator=(const ICodegenComponent &) = delete;

    protected:
        //===================================================================
        // Context Access
        //===================================================================

        /** @brief Get the shared codegen context */
        CodegenContext &ctx() { return _ctx; }
        const CodegenContext &ctx() const { return _ctx; }

        //===================================================================
        // Convenience Accessors (frequently used)
        //===================================================================

        /** @brief Get IR builder */
        llvm::IRBuilder<> &builder() { return _ctx.builder(); }

        /** @brief Get LLVM context */
        llvm::LLVMContext &llvm_ctx() { return _ctx.llvm_context(); }

        /** @brief Get current module */
        llvm::Module *module() { return _ctx.module(); }

        /** @brief Get type mapper */
        TypeMapper &types() { return _ctx.types(); }

        /** @brief Get value context */
        ValueContext &values() { return _ctx.values(); }

        /** @brief Get symbol table */
        Cryo::SymbolTable &symbols() { return _ctx.symbols(); }

        //===================================================================
        // Common Memory Operations
        //===================================================================

        /**
         * @brief Create an alloca instruction in the entry block of a function
         * @param fn Function to create alloca in
         * @param type Type to allocate
         * @param name Name for the alloca
         * @return Created AllocaInst
         */
        llvm::AllocaInst *create_entry_alloca(llvm::Function *fn, llvm::Type *type, const std::string &name);

        /**
         * @brief Create an alloca in the current function's entry block
         * @param type Type to allocate
         * @param name Name for the alloca
         * @return Created AllocaInst, or nullptr if no current function
         */
        llvm::AllocaInst *create_entry_alloca(llvm::Type *type, const std::string &name);

        /**
         * @brief Create a load instruction with proper typing
         * @param ptr Pointer to load from
         * @param type Expected type (for opaque pointers)
         * @param name Optional name for the load result
         * @return Loaded value
         */
        llvm::Value *create_load(llvm::Value *ptr, llvm::Type *type, const std::string &name = "");

        /**
         * @brief Create a store instruction
         * @param value Value to store
         * @param ptr Pointer to store to
         */
        void create_store(llvm::Value *value, llvm::Value *ptr);

        /**
         * @brief Create a GEP instruction for struct field access
         * @param struct_type Type of the struct
         * @param ptr Pointer to struct
         * @param field_idx Field index
         * @param name Optional name
         * @return Pointer to the field
         */
        llvm::Value *create_struct_gep(llvm::Type *struct_type, llvm::Value *ptr,
                                       unsigned field_idx, const std::string &name = "");

        /**
         * @brief Create a GEP instruction for array element access
         * @param element_type Element type of the array
         * @param ptr Pointer to array
         * @param index Index value
         * @param name Optional name
         * @return Pointer to the element
         */
        llvm::Value *create_array_gep(llvm::Type *element_type, llvm::Value *ptr,
                                      llvm::Value *index, const std::string &name = "");

        //===================================================================
        // Common Control Flow Operations
        //===================================================================

        /**
         * @brief Create a new basic block in the current function
         * @param name Block name
         * @return Created BasicBlock, or nullptr if no current function
         */
        llvm::BasicBlock *create_block(const std::string &name);

        /**
         * @brief Create a new basic block in a specific function
         * @param name Block name
         * @param fn Function to create block in
         * @return Created BasicBlock
         */
        llvm::BasicBlock *create_block(const std::string &name, llvm::Function *fn);

        /**
         * @brief Ensure the builder has a valid insertion point
         *
         * If the current block is terminated or doesn't exist,
         * creates a fallback block to prevent crashes.
         */
        void ensure_valid_insertion_point();

        //===================================================================
        // Common Type Operations
        //===================================================================

        /**
         * @brief Get LLVM type for a Cryo type
         * @param cryo_type Cryo type to convert
         * @return Corresponding LLVM type
         */
        llvm::Type *get_llvm_type(Cryo::Type *cryo_type);

        /**
         * @brief Cast a value to a target type if necessary
         * @param value Value to cast
         * @param target_type Target LLVM type
         * @return Cast value, or original if no cast needed
         */
        llvm::Value *cast_if_needed(llvm::Value *value, llvm::Type *target_type);

        /**
         * @brief Check if a value is an alloca instruction
         * @param value Value to check
         * @return true if value is an AllocaInst
         */
        bool is_alloca(llvm::Value *value) const;

        //===================================================================
        // Error Reporting
        //===================================================================

        /**
         * @brief Report an error with node context
         * @param code Error code
         * @param node AST node for location info
         * @param msg Error message
         */
        void report_error(ErrorCode code, Cryo::ASTNode *node, const std::string &msg);

        /**
         * @brief Report an error without node context
         * @param code Error code
         * @param msg Error message
         */
        void report_error(ErrorCode code, const std::string &msg);

        //===================================================================
        // Value Registration
        //===================================================================

        /**
         * @brief Register a generated value for an AST node
         * @param node AST node
         * @param value Generated LLVM value
         */
        void register_value(Cryo::ASTNode *node, llvm::Value *value);

        /**
         * @brief Set the current expression result
         * @param value Result value
         */
        void set_result(llvm::Value *value);

        /**
         * @brief Get the current expression result
         * @return Current result value
         */
        llvm::Value *get_result();

    private:
        CodegenContext &_ctx;
    };

} // namespace Cryo::Codegen
