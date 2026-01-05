#pragma once

#include "Codegen/ICodegenComponent.hpp"
#include "Codegen/CodegenContext.hpp"

#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>
#include <string>
#include <unordered_map>

namespace Cryo::Codegen
{
    /**
     * @brief Centralizes all memory-related IR generation
     *
     * This class consolidates the 28+ alloca checks and scattered
     * load/store operations from CodegenVisitor into a single,
     * consistent API.
     *
     * Key responsibilities:
     * - Stack allocation (alloca instructions)
     * - Heap allocation (malloc/new calls)
     * - Load/store operations with proper typing
     * - GEP (GetElementPtr) operations for struct/array access
     * - Memory intrinsics (memcpy, memset, etc.)
     */
    class MemoryCodegen : public ICodegenComponent
    {
    public:
        //===================================================================
        // Construction
        //===================================================================

        explicit MemoryCodegen(CodegenContext &ctx);
        ~MemoryCodegen() = default;

        //===================================================================
        // Stack Allocation
        //===================================================================

        /**
         * @brief Create an alloca in the entry block of a function
         * @param fn Function to create alloca in
         * @param type Type to allocate
         * @param name Name for the alloca
         * @return Created AllocaInst
         *
         * Entry block allocas are preferred for better optimization.
         */
        llvm::AllocaInst *create_entry_block_alloca(llvm::Function *fn,
                                                     llvm::Type *type,
                                                     const std::string &name);

        /**
         * @brief Create an alloca in current function's entry block
         * @param type Type to allocate
         * @param name Name for the alloca
         * @return Created AllocaInst, or nullptr if no current function
         */
        llvm::AllocaInst *create_entry_block_alloca(llvm::Type *type,
                                                     const std::string &name);

        /**
         * @brief Create an alloca at the current insertion point
         * @param type Type to allocate
         * @param name Name for the alloca
         * @return Created AllocaInst
         *
         * Use sparingly - entry block allocas are preferred.
         */
        llvm::AllocaInst *create_stack_alloca(llvm::Type *type,
                                               const std::string &name);

        /**
         * @brief Create an array alloca in the entry block
         * @param fn Function to create alloca in
         * @param element_type Element type
         * @param array_size Number of elements
         * @param name Name for the alloca
         * @return Created AllocaInst
         */
        llvm::AllocaInst *create_array_alloca(llvm::Function *fn,
                                               llvm::Type *element_type,
                                               llvm::Value *array_size,
                                               const std::string &name);

        //===================================================================
        // Heap Allocation
        //===================================================================

        /**
         * @brief Allocate memory on the heap
         * @param type Type to allocate
         * @param name Name for the result
         * @return Pointer to allocated memory
         */
        llvm::Value *create_heap_alloc(llvm::Type *type,
                                        const std::string &name);

        /**
         * @brief Allocate array on the heap
         * @param element_type Element type
         * @param count Number of elements
         * @param name Name for the result
         * @return Pointer to allocated memory
         */
        llvm::Value *create_heap_array_alloc(llvm::Type *element_type,
                                              llvm::Value *count,
                                              const std::string &name);

        /**
         * @brief Free heap-allocated memory
         * @param ptr Pointer to free
         */
        void create_heap_free(llvm::Value *ptr);

        /**
         * @brief Reallocate heap memory
         * @param ptr Existing pointer
         * @param new_size New size in bytes
         * @param name Name for the result
         * @return New pointer (may be different from original)
         */
        llvm::Value *create_heap_realloc(llvm::Value *ptr,
                                          llvm::Value *new_size,
                                          const std::string &name);

        //===================================================================
        // Load Operations
        //===================================================================

        /**
         * @brief Create a load instruction
         * @param ptr Pointer to load from
         * @param type Type to load (required for opaque pointers)
         * @param name Optional name for the result
         * @return Loaded value
         */
        llvm::Value *create_load(llvm::Value *ptr,
                                  llvm::Type *type,
                                  const std::string &name = "");

        /**
         * @brief Create a volatile load instruction
         * @param ptr Pointer to load from
         * @param type Type to load
         * @param name Optional name for the result
         * @return Loaded value
         */
        llvm::Value *create_volatile_load(llvm::Value *ptr,
                                           llvm::Type *type,
                                           const std::string &name = "");

        /**
         * @brief Create an atomic load instruction
         * @param ptr Pointer to load from
         * @param type Type to load
         * @param ordering Memory ordering
         * @param name Optional name for the result
         * @return Loaded value
         */
        llvm::Value *create_atomic_load(llvm::Value *ptr,
                                         llvm::Type *type,
                                         llvm::AtomicOrdering ordering,
                                         const std::string &name = "");

        //===================================================================
        // Store Operations
        //===================================================================

        /**
         * @brief Create a store instruction
         * @param value Value to store
         * @param ptr Pointer to store to
         */
        void create_store(llvm::Value *value, llvm::Value *ptr);

        /**
         * @brief Create a volatile store instruction
         * @param value Value to store
         * @param ptr Pointer to store to
         */
        void create_volatile_store(llvm::Value *value, llvm::Value *ptr);

        /**
         * @brief Create an atomic store instruction
         * @param value Value to store
         * @param ptr Pointer to store to
         * @param ordering Memory ordering
         */
        void create_atomic_store(llvm::Value *value,
                                  llvm::Value *ptr,
                                  llvm::AtomicOrdering ordering);

        //===================================================================
        // GEP (GetElementPtr) Operations
        //===================================================================

        /**
         * @brief Create a GEP for struct field access
         * @param struct_type Type of the struct
         * @param ptr Pointer to struct
         * @param field_idx Field index
         * @param name Optional name
         * @return Pointer to the field
         */
        llvm::Value *create_struct_gep(llvm::Type *struct_type,
                                        llvm::Value *ptr,
                                        unsigned field_idx,
                                        const std::string &name = "");

        /**
         * @brief Create a GEP for array element access
         * @param element_type Element type of the array
         * @param ptr Pointer to array
         * @param index Index value
         * @param name Optional name
         * @return Pointer to the element
         */
        llvm::Value *create_array_gep(llvm::Type *element_type,
                                       llvm::Value *ptr,
                                       llvm::Value *index,
                                       const std::string &name = "");

        /**
         * @brief Create an inbounds GEP with multiple indices
         * @param type Base type for GEP
         * @param ptr Base pointer
         * @param indices Array of indices
         * @param name Optional name
         * @return Resulting pointer
         */
        llvm::Value *create_inbounds_gep(llvm::Type *type,
                                          llvm::Value *ptr,
                                          llvm::ArrayRef<llvm::Value *> indices,
                                          const std::string &name = "");

        /**
         * @brief Create a GEP with constant indices (common case)
         * @param type Base type
         * @param ptr Base pointer
         * @param idx0 First index
         * @param idx1 Second index
         * @param name Optional name
         * @return Resulting pointer
         */
        llvm::Value *create_const_gep2(llvm::Type *type,
                                        llvm::Value *ptr,
                                        unsigned idx0,
                                        unsigned idx1,
                                        const std::string &name = "");

        //===================================================================
        // Memory Intrinsics
        //===================================================================

        /**
         * @brief Create a memcpy call
         * @param dest Destination pointer
         * @param src Source pointer
         * @param size Size in bytes
         * @param is_volatile Whether the copy is volatile
         */
        void create_memcpy(llvm::Value *dest,
                           llvm::Value *src,
                           llvm::Value *size,
                           bool is_volatile = false);

        /**
         * @brief Create a memcpy call with constant size
         * @param dest Destination pointer
         * @param src Source pointer
         * @param size Size in bytes (constant)
         */
        void create_memcpy(llvm::Value *dest,
                           llvm::Value *src,
                           uint64_t size);

        /**
         * @brief Create a memset call
         * @param dest Destination pointer
         * @param value Value to set (i8)
         * @param size Size in bytes
         * @param is_volatile Whether the set is volatile
         */
        void create_memset(llvm::Value *dest,
                           llvm::Value *value,
                           llvm::Value *size,
                           bool is_volatile = false);

        /**
         * @brief Create a memset call with constant value and size
         * @param dest Destination pointer
         * @param value Value to set
         * @param size Size in bytes
         */
        void create_memset(llvm::Value *dest,
                           uint8_t value,
                           uint64_t size);

        /**
         * @brief Create a memmove call
         * @param dest Destination pointer
         * @param src Source pointer
         * @param size Size in bytes
         * @param is_volatile Whether the move is volatile
         */
        void create_memmove(llvm::Value *dest,
                            llvm::Value *src,
                            llvm::Value *size,
                            bool is_volatile = false);

        //===================================================================
        // Type Queries
        //===================================================================

        /**
         * @brief Check if a value is an alloca instruction
         * @param value Value to check
         * @return true if value is an AllocaInst
         */
        bool is_alloca(llvm::Value *value) const;

        /**
         * @brief Get the allocated type from an alloca
         * @param alloca Alloca instruction
         * @return Allocated type, or nullptr if not an alloca
         */
        llvm::Type *get_allocated_type(llvm::Value *alloca) const;

        /**
         * @brief Get size of a type in bytes
         * @param type Type to get size of
         * @return Size in bytes
         */
        uint64_t get_type_size(llvm::Type *type) const;

        /**
         * @brief Get alignment of a type in bytes
         * @param type Type to get alignment of
         * @return Alignment in bytes
         */
        uint64_t get_type_alignment(llvm::Type *type) const;

        /**
         * @brief Create a size constant for the given type
         * @param type Type to get size of
         * @return i64 constant with the size
         */
        llvm::Value *create_sizeof(llvm::Type *type);

    private:
        //===================================================================
        // Cached Intrinsic Functions
        //===================================================================

        llvm::Function *_malloc_fn = nullptr;
        llvm::Function *_free_fn = nullptr;
        llvm::Function *_realloc_fn = nullptr;

        /**
         * @brief Get or create malloc function declaration
         */
        llvm::Function *get_malloc();

        /**
         * @brief Get or create free function declaration
         */
        llvm::Function *get_free();

        /**
         * @brief Get or create realloc function declaration
         */
        llvm::Function *get_realloc();

        /**
         * @brief Get the memcpy intrinsic
         */
        llvm::Function *get_memcpy_intrinsic();

        /**
         * @brief Get the memset intrinsic
         */
        llvm::Function *get_memset_intrinsic();

        /**
         * @brief Get the memmove intrinsic
         */
        llvm::Function *get_memmove_intrinsic();
    };

} // namespace Cryo::Codegen
