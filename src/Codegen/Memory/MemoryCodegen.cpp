#include "Codegen/Memory/MemoryCodegen.hpp"
#include "Utils/Logger.hpp"

#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/DataLayout.h>

namespace Cryo::Codegen
{
    //===================================================================
    // Construction
    //===================================================================

    MemoryCodegen::MemoryCodegen(CodegenContext &ctx)
        : ICodegenComponent(ctx)
    {
    }

    //===================================================================
    // Stack Allocation
    //===================================================================

    llvm::AllocaInst *MemoryCodegen::create_entry_block_alloca(llvm::Function *fn,
                                                                 llvm::Type *type,
                                                                 const std::string &name)
    {
        if (!fn || !type)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_entry_block_alloca: null function or type");
            return nullptr;
        }

        // Save current insertion point
        llvm::IRBuilder<> &b = builder();
        llvm::BasicBlock *current_block = b.GetInsertBlock();
        llvm::BasicBlock::iterator current_point = b.GetInsertPoint();

        // Get the entry block
        llvm::BasicBlock &entry = fn->getEntryBlock();

        // Find insertion point after existing allocas
        llvm::BasicBlock::iterator insert_point = entry.begin();
        while (insert_point != entry.end() && llvm::isa<llvm::AllocaInst>(&*insert_point))
        {
            ++insert_point;
        }

        // Set insertion point
        if (insert_point == entry.end())
        {
            b.SetInsertPoint(&entry);
        }
        else
        {
            b.SetInsertPoint(&entry, insert_point);
        }

        // Create the alloca
        llvm::AllocaInst *alloca = b.CreateAlloca(type, nullptr, name);

        // Restore insertion point
        if (current_block)
        {
            b.SetInsertPoint(current_block, current_point);
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Created entry block alloca: {} (type: {})",
                  name, type ? "valid" : "null");

        return alloca;
    }

    llvm::AllocaInst *MemoryCodegen::create_entry_block_alloca(llvm::Type *type,
                                                                 const std::string &name)
    {
        FunctionContext *fn_ctx = ctx().current_function();
        if (!fn_ctx || !fn_ctx->function)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_entry_block_alloca: no current function");
            return nullptr;
        }

        return create_entry_block_alloca(fn_ctx->function, type, name);
    }

    llvm::AllocaInst *MemoryCodegen::create_stack_alloca(llvm::Type *type,
                                                          const std::string &name)
    {
        if (!type)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_stack_alloca: null type");
            return nullptr;
        }

        return builder().CreateAlloca(type, nullptr, name);
    }

    llvm::AllocaInst *MemoryCodegen::create_array_alloca(llvm::Function *fn,
                                                          llvm::Type *element_type,
                                                          llvm::Value *array_size,
                                                          const std::string &name)
    {
        if (!fn || !element_type || !array_size)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_array_alloca: null parameter");
            return nullptr;
        }

        // Save current insertion point
        llvm::IRBuilder<> &b = builder();
        llvm::BasicBlock *current_block = b.GetInsertBlock();
        llvm::BasicBlock::iterator current_point = b.GetInsertPoint();

        // Set insertion point to entry block
        llvm::BasicBlock &entry = fn->getEntryBlock();
        llvm::BasicBlock::iterator insert_point = entry.begin();
        while (insert_point != entry.end() && llvm::isa<llvm::AllocaInst>(&*insert_point))
        {
            ++insert_point;
        }
        b.SetInsertPoint(&entry, insert_point);

        // Create array alloca
        llvm::AllocaInst *alloca = b.CreateAlloca(element_type, array_size, name);

        // Restore insertion point
        if (current_block)
        {
            b.SetInsertPoint(current_block, current_point);
        }

        return alloca;
    }

    //===================================================================
    // Heap Allocation
    //===================================================================

    llvm::Value *MemoryCodegen::create_heap_alloc(llvm::Type *type,
                                                    const std::string &name)
    {
        if (!type)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_heap_alloc: null type");
            return nullptr;
        }

        uint64_t size = get_type_size(type);
        llvm::Value *size_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), size);

        llvm::Function *malloc_fn = get_malloc();
        if (!malloc_fn)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_heap_alloc: could not get malloc");
            return nullptr;
        }

        return builder().CreateCall(malloc_fn, {size_val}, name);
    }

    llvm::Value *MemoryCodegen::create_heap_array_alloc(llvm::Type *element_type,
                                                          llvm::Value *count,
                                                          const std::string &name)
    {
        if (!element_type || !count)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_heap_array_alloc: null parameter");
            return nullptr;
        }

        uint64_t element_size = get_type_size(element_type);
        llvm::Value *element_size_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), element_size);

        // Calculate total size: count * element_size
        llvm::Value *count_64 = builder().CreateZExtOrTrunc(count, llvm::Type::getInt64Ty(llvm_ctx()), "count.ext");
        llvm::Value *total_size = builder().CreateMul(count_64, element_size_val, "total.size");

        llvm::Function *malloc_fn = get_malloc();
        if (!malloc_fn)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_heap_array_alloc: could not get malloc");
            return nullptr;
        }

        return builder().CreateCall(malloc_fn, {total_size}, name);
    }

    void MemoryCodegen::create_heap_free(llvm::Value *ptr)
    {
        if (!ptr)
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN, "create_heap_free: null pointer");
            return;
        }

        llvm::Function *free_fn = get_free();
        if (!free_fn)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_heap_free: could not get free");
            return;
        }

        builder().CreateCall(free_fn, {ptr});
    }

    llvm::Value *MemoryCodegen::create_heap_realloc(llvm::Value *ptr,
                                                      llvm::Value *new_size,
                                                      const std::string &name)
    {
        if (!ptr || !new_size)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_heap_realloc: null parameter");
            return nullptr;
        }

        llvm::Function *realloc_fn = get_realloc();
        if (!realloc_fn)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_heap_realloc: could not get realloc");
            return nullptr;
        }

        llvm::Value *size_64 = builder().CreateZExtOrTrunc(new_size, llvm::Type::getInt64Ty(llvm_ctx()), "size.ext");
        return builder().CreateCall(realloc_fn, {ptr, size_64}, name);
    }

    //===================================================================
    // Load Operations
    //===================================================================

    llvm::Value *MemoryCodegen::create_load(llvm::Value *ptr,
                                              llvm::Type *type,
                                              const std::string &name)
    {
        if (!ptr)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_load: null pointer");
            return nullptr;
        }

        if (!ptr->getType()->isPointerTy())
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN, "create_load: value is not a pointer, returning as-is");
            return ptr;
        }

        if (!type)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_load: null type (required for opaque pointers)");
            return nullptr;
        }

        return builder().CreateLoad(type, ptr, name.empty() ? "load" : name);
    }

    llvm::Value *MemoryCodegen::create_volatile_load(llvm::Value *ptr,
                                                       llvm::Type *type,
                                                       const std::string &name)
    {
        if (!ptr || !type)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_volatile_load: null parameter");
            return nullptr;
        }

        llvm::LoadInst *load = builder().CreateLoad(type, ptr, name.empty() ? "vload" : name);
        load->setVolatile(true);
        return load;
    }

    llvm::Value *MemoryCodegen::create_atomic_load(llvm::Value *ptr,
                                                     llvm::Type *type,
                                                     llvm::AtomicOrdering ordering,
                                                     const std::string &name)
    {
        if (!ptr || !type)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_atomic_load: null parameter");
            return nullptr;
        }

        llvm::LoadInst *load = builder().CreateLoad(type, ptr, name.empty() ? "aload" : name);
        load->setAtomic(ordering);
        return load;
    }

    //===================================================================
    // Store Operations
    //===================================================================

    void MemoryCodegen::create_store(llvm::Value *value, llvm::Value *ptr)
    {
        if (!value || !ptr)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_store: null parameter");
            return;
        }

        if (!ptr->getType()->isPointerTy())
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_store: destination is not a pointer");
            return;
        }

        builder().CreateStore(value, ptr);
    }

    void MemoryCodegen::create_volatile_store(llvm::Value *value, llvm::Value *ptr)
    {
        if (!value || !ptr)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_volatile_store: null parameter");
            return;
        }

        llvm::StoreInst *store = builder().CreateStore(value, ptr);
        store->setVolatile(true);
    }

    void MemoryCodegen::create_atomic_store(llvm::Value *value,
                                              llvm::Value *ptr,
                                              llvm::AtomicOrdering ordering)
    {
        if (!value || !ptr)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_atomic_store: null parameter");
            return;
        }

        llvm::StoreInst *store = builder().CreateStore(value, ptr);
        store->setAtomic(ordering);
    }

    //===================================================================
    // GEP Operations
    //===================================================================

    llvm::Value *MemoryCodegen::create_struct_gep(llvm::Type *struct_type,
                                                    llvm::Value *ptr,
                                                    unsigned field_idx,
                                                    const std::string &name)
    {
        if (!struct_type || !ptr)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_struct_gep: null parameter");
            return nullptr;
        }

        return builder().CreateStructGEP(struct_type, ptr, field_idx,
                                          name.empty() ? "field.ptr" : name);
    }

    llvm::Value *MemoryCodegen::create_array_gep(llvm::Type *element_type,
                                                   llvm::Value *ptr,
                                                   llvm::Value *index,
                                                   const std::string &name)
    {
        if (!element_type || !ptr || !index)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_array_gep: null parameter");
            return nullptr;
        }

        return builder().CreateGEP(element_type, ptr, index,
                                    name.empty() ? "elem.ptr" : name);
    }

    llvm::Value *MemoryCodegen::create_inbounds_gep(llvm::Type *type,
                                                      llvm::Value *ptr,
                                                      llvm::ArrayRef<llvm::Value *> indices,
                                                      const std::string &name)
    {
        if (!type || !ptr)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_inbounds_gep: null parameter");
            return nullptr;
        }

        return builder().CreateInBoundsGEP(type, ptr, indices,
                                            name.empty() ? "gep" : name);
    }

    llvm::Value *MemoryCodegen::create_const_gep2(llvm::Type *type,
                                                    llvm::Value *ptr,
                                                    unsigned idx0,
                                                    unsigned idx1,
                                                    const std::string &name)
    {
        if (!type || !ptr)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_const_gep2: null parameter");
            return nullptr;
        }

        return builder().CreateConstGEP2_32(type, ptr, idx0, idx1,
                                             name.empty() ? "gep" : name);
    }

    //===================================================================
    // Memory Intrinsics
    //===================================================================

    void MemoryCodegen::create_memcpy(llvm::Value *dest,
                                        llvm::Value *src,
                                        llvm::Value *size,
                                        bool is_volatile)
    {
        if (!dest || !src || !size)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_memcpy: null parameter");
            return;
        }

        llvm::Function *memcpy_fn = get_memcpy_intrinsic();
        if (!memcpy_fn)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_memcpy: could not get memcpy intrinsic");
            return;
        }

        llvm::Value *vol = is_volatile ? builder().getTrue() : builder().getFalse();
        builder().CreateCall(memcpy_fn, {dest, src, size, vol});
    }

    void MemoryCodegen::create_memcpy(llvm::Value *dest,
                                        llvm::Value *src,
                                        uint64_t size)
    {
        llvm::Value *size_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), size);
        create_memcpy(dest, src, size_val, false);
    }

    void MemoryCodegen::create_memset(llvm::Value *dest,
                                        llvm::Value *value,
                                        llvm::Value *size,
                                        bool is_volatile)
    {
        if (!dest || !value || !size)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_memset: null parameter");
            return;
        }

        llvm::Function *memset_fn = get_memset_intrinsic();
        if (!memset_fn)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_memset: could not get memset intrinsic");
            return;
        }

        llvm::Value *vol = is_volatile ? builder().getTrue() : builder().getFalse();
        builder().CreateCall(memset_fn, {dest, value, size, vol});
    }

    void MemoryCodegen::create_memset(llvm::Value *dest,
                                        uint8_t value,
                                        uint64_t size)
    {
        llvm::Value *val = llvm::ConstantInt::get(llvm::Type::getInt8Ty(llvm_ctx()), value);
        llvm::Value *size_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), size);
        create_memset(dest, val, size_val, false);
    }

    void MemoryCodegen::create_memmove(llvm::Value *dest,
                                         llvm::Value *src,
                                         llvm::Value *size,
                                         bool is_volatile)
    {
        if (!dest || !src || !size)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_memmove: null parameter");
            return;
        }

        llvm::Function *memmove_fn = get_memmove_intrinsic();
        if (!memmove_fn)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_memmove: could not get memmove intrinsic");
            return;
        }

        llvm::Value *vol = is_volatile ? builder().getTrue() : builder().getFalse();
        builder().CreateCall(memmove_fn, {dest, src, size, vol});
    }

    //===================================================================
    // Type Queries
    //===================================================================

    bool MemoryCodegen::is_alloca(llvm::Value *value) const
    {
        return value && llvm::isa<llvm::AllocaInst>(value);
    }

    llvm::Type *MemoryCodegen::get_allocated_type(llvm::Value *alloca) const
    {
        if (auto *alloca_inst = llvm::dyn_cast_or_null<llvm::AllocaInst>(alloca))
        {
            return alloca_inst->getAllocatedType();
        }
        return nullptr;
    }

    uint64_t MemoryCodegen::get_type_size(llvm::Type *type) const
    {
        if (!type)
            return 0;

        llvm::Module *mod = module();
        if (!mod)
            return 0;

        const llvm::DataLayout &layout = mod->getDataLayout();
        return layout.getTypeAllocSize(type);
    }

    uint64_t MemoryCodegen::get_type_alignment(llvm::Type *type) const
    {
        if (!type)
            return 0;

        llvm::Module *mod = module();
        if (!mod)
            return 0;

        const llvm::DataLayout &layout = mod->getDataLayout();
        return layout.getABITypeAlign(type).value();
    }

    llvm::Value *MemoryCodegen::create_sizeof(llvm::Type *type)
    {
        uint64_t size = get_type_size(type);
        return llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), size);
    }

    //===================================================================
    // Private Helpers
    //===================================================================

    llvm::Function *MemoryCodegen::get_malloc()
    {
        if (_malloc_fn)
            return _malloc_fn;

        llvm::Module *mod = module();
        if (!mod)
            return nullptr;

        // Check if already declared
        _malloc_fn = mod->getFunction("malloc");
        if (_malloc_fn)
            return _malloc_fn;

        // Create declaration: void* malloc(size_t)
        llvm::Type *void_ptr = llvm::PointerType::get(llvm_ctx(), 0);
        llvm::Type *size_t_type = llvm::Type::getInt64Ty(llvm_ctx());
        llvm::FunctionType *malloc_type = llvm::FunctionType::get(void_ptr, {size_t_type}, false);

        _malloc_fn = llvm::Function::Create(malloc_type, llvm::Function::ExternalLinkage, "malloc", mod);
        return _malloc_fn;
    }

    llvm::Function *MemoryCodegen::get_free()
    {
        if (_free_fn)
            return _free_fn;

        llvm::Module *mod = module();
        if (!mod)
            return nullptr;

        // Check if already declared
        _free_fn = mod->getFunction("free");
        if (_free_fn)
            return _free_fn;

        // Create declaration: void free(void*)
        llvm::Type *void_type = llvm::Type::getVoidTy(llvm_ctx());
        llvm::Type *void_ptr = llvm::PointerType::get(llvm_ctx(), 0);
        llvm::FunctionType *free_type = llvm::FunctionType::get(void_type, {void_ptr}, false);

        _free_fn = llvm::Function::Create(free_type, llvm::Function::ExternalLinkage, "free", mod);
        return _free_fn;
    }

    llvm::Function *MemoryCodegen::get_realloc()
    {
        if (_realloc_fn)
            return _realloc_fn;

        llvm::Module *mod = module();
        if (!mod)
            return nullptr;

        // Check if already declared
        _realloc_fn = mod->getFunction("realloc");
        if (_realloc_fn)
            return _realloc_fn;

        // Create declaration: void* realloc(void*, size_t)
        llvm::Type *void_ptr = llvm::PointerType::get(llvm_ctx(), 0);
        llvm::Type *size_t_type = llvm::Type::getInt64Ty(llvm_ctx());
        llvm::FunctionType *realloc_type = llvm::FunctionType::get(void_ptr, {void_ptr, size_t_type}, false);

        _realloc_fn = llvm::Function::Create(realloc_type, llvm::Function::ExternalLinkage, "realloc", mod);
        return _realloc_fn;
    }

    llvm::Function *MemoryCodegen::get_memcpy_intrinsic()
    {
        llvm::Module *mod = module();
        if (!mod)
            return nullptr;

        llvm::Type *ptr_type = llvm::PointerType::get(llvm_ctx(), 0);
        llvm::Type *i64_type = llvm::Type::getInt64Ty(llvm_ctx());

        return llvm::Intrinsic::getDeclaration(mod, llvm::Intrinsic::memcpy,
                                                {ptr_type, ptr_type, i64_type});
    }

    llvm::Function *MemoryCodegen::get_memset_intrinsic()
    {
        llvm::Module *mod = module();
        if (!mod)
            return nullptr;

        llvm::Type *ptr_type = llvm::PointerType::get(llvm_ctx(), 0);
        llvm::Type *i64_type = llvm::Type::getInt64Ty(llvm_ctx());

        return llvm::Intrinsic::getDeclaration(mod, llvm::Intrinsic::memset,
                                                {ptr_type, i64_type});
    }

    llvm::Function *MemoryCodegen::get_memmove_intrinsic()
    {
        llvm::Module *mod = module();
        if (!mod)
            return nullptr;

        llvm::Type *ptr_type = llvm::PointerType::get(llvm_ctx(), 0);
        llvm::Type *i64_type = llvm::Type::getInt64Ty(llvm_ctx());

        return llvm::Intrinsic::getDeclaration(mod, llvm::Intrinsic::memmove,
                                                {ptr_type, ptr_type, i64_type});
    }

} // namespace Cryo::Codegen
