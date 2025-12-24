#include "Codegen/Expressions/CastCodegen.hpp"
#include "Utils/Logger.hpp"

namespace Cryo::Codegen
{
    //===================================================================
    // Construction
    //===================================================================

    CastCodegen::CastCodegen(CodegenContext &ctx)
        : ICodegenComponent(ctx)
    {
    }

    //===================================================================
    // Main Entry Points
    //===================================================================

    llvm::Value *CastCodegen::generate_cast_expression(Cryo::CastExpressionNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0641_NULL_CAST_EXPRESSION, "Null cast expression");
            return nullptr;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CastCodegen: Generating cast expression");

        // Generate source expression
        node->expression()->accept(*ctx().visitor());
        llvm::Value *value = get_result();

        if (!value)
        {
            report_error(ErrorCode::E0641_NULL_CAST_EXPRESSION, node,
                         "Failed to generate cast source expression");
            return nullptr;
        }

        return generate_implicit_cast(value, node->get_resolved_type());
    }

    llvm::Value *CastCodegen::generate_implicit_cast(llvm::Value *value, Cryo::Type *target_type)
    {
        if (!value || !target_type)
            return value;

        llvm::Type *llvm_target = get_llvm_type(target_type);
        if (!llvm_target)
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN, "CastCodegen: Unknown target type");
            return value;
        }

        bool is_signed = is_signed_type(target_type);
        return cast_to(value, llvm_target, "cast");
    }

    llvm::Value *CastCodegen::cast_to(llvm::Value *value, llvm::Type *target_type,
                                      const std::string &name)
    {
        if (!value || !target_type)
            return value;

        llvm::Type *source_type = value->getType();

        // No cast needed
        if (source_type == target_type)
        {
            return value;
        }

        std::string cast_name = name.empty() ? "cast" : name;

        // Integer to integer
        if (is_int_type(source_type) && is_int_type(target_type))
        {
            return cast_integer(value, target_type, true, true);
        }

        // Float to float
        if (is_float_type(source_type) && is_float_type(target_type))
        {
            return cast_float(value, target_type);
        }

        // Integer to float
        if (is_int_type(source_type) && is_float_type(target_type))
        {
            return int_to_float(value, target_type, true);
        }

        // Float to integer
        if (is_float_type(source_type) && is_int_type(target_type))
        {
            return float_to_int(value, target_type, true);
        }

        // Pointer to integer
        if (is_pointer_type(source_type) && is_int_type(target_type))
        {
            return pointer_to_int(value, target_type);
        }

        // Integer to pointer
        if (is_int_type(source_type) && is_pointer_type(target_type))
        {
            return int_to_pointer(value, target_type);
        }

        // Pointer to pointer
        if (is_pointer_type(source_type) && is_pointer_type(target_type))
        {
            return pointer_cast(value, target_type);
        }

        // Integer/float to boolean
        if (target_type->isIntegerTy(1))
        {
            return to_bool(value);
        }

        // Boolean to integer
        if (source_type->isIntegerTy(1) && is_int_type(target_type))
        {
            return bool_to_int(value, target_type);
        }

        LOG_WARN(Cryo::LogComponent::CODEGEN, "CastCodegen: Unable to cast between types");
        return value;
    }

    //===================================================================
    // Integer Conversions
    //===================================================================

    llvm::Value *CastCodegen::cast_integer(llvm::Value *value, llvm::Type *target_type,
                                           bool source_signed, bool target_signed)
    {
        if (!value || !target_type)
            return value;

        llvm::Type *source_type = value->getType();
        if (!is_int_type(source_type) || !is_int_type(target_type))
        {
            return value;
        }

        unsigned src_bits = get_int_bits(source_type);
        unsigned dst_bits = get_int_bits(target_type);

        if (dst_bits > src_bits)
        {
            // Extension
            if (source_signed)
            {
                return sign_extend(value, target_type);
            }
            else
            {
                return zero_extend(value, target_type);
            }
        }
        else if (dst_bits < src_bits)
        {
            // Truncation
            return truncate(value, target_type);
        }

        // Same size - might need sign interpretation change
        return value;
    }

    llvm::Value *CastCodegen::sign_extend(llvm::Value *value, llvm::Type *target_type)
    {
        if (!value || !target_type)
            return value;

        return builder().CreateSExt(value, target_type, "sext");
    }

    llvm::Value *CastCodegen::zero_extend(llvm::Value *value, llvm::Type *target_type)
    {
        if (!value || !target_type)
            return value;

        return builder().CreateZExt(value, target_type, "zext");
    }

    llvm::Value *CastCodegen::truncate(llvm::Value *value, llvm::Type *target_type)
    {
        if (!value || !target_type)
            return value;

        return builder().CreateTrunc(value, target_type, "trunc");
    }

    //===================================================================
    // Floating-Point Conversions
    //===================================================================

    llvm::Value *CastCodegen::cast_float(llvm::Value *value, llvm::Type *target_type)
    {
        if (!value || !target_type)
            return value;

        llvm::Type *source_type = value->getType();

        bool src_is_double = source_type->isDoubleTy();
        bool dst_is_double = target_type->isDoubleTy();

        if (!src_is_double && dst_is_double)
        {
            return float_extend(value);
        }
        else if (src_is_double && !dst_is_double)
        {
            return float_truncate(value);
        }

        return value;
    }

    llvm::Value *CastCodegen::float_extend(llvm::Value *value)
    {
        if (!value)
            return nullptr;

        llvm::Type *double_type = llvm::Type::getDoubleTy(llvm_ctx());
        return builder().CreateFPExt(value, double_type, "fpext");
    }

    llvm::Value *CastCodegen::float_truncate(llvm::Value *value)
    {
        if (!value)
            return nullptr;

        llvm::Type *float_type = llvm::Type::getFloatTy(llvm_ctx());
        return builder().CreateFPTrunc(value, float_type, "fptrunc");
    }

    //===================================================================
    // Integer <-> Float Conversions
    //===================================================================

    llvm::Value *CastCodegen::int_to_float(llvm::Value *value, llvm::Type *target_type,
                                           bool is_signed)
    {
        if (!value || !target_type)
            return value;

        if (is_signed)
        {
            return builder().CreateSIToFP(value, target_type, "sitofp");
        }
        else
        {
            return builder().CreateUIToFP(value, target_type, "uitofp");
        }
    }

    llvm::Value *CastCodegen::float_to_int(llvm::Value *value, llvm::Type *target_type,
                                           bool is_signed)
    {
        if (!value || !target_type)
            return value;

        if (is_signed)
        {
            return builder().CreateFPToSI(value, target_type, "fptosi");
        }
        else
        {
            return builder().CreateFPToUI(value, target_type, "fptoui");
        }
    }

    //===================================================================
    // Pointer Conversions
    //===================================================================

    llvm::Value *CastCodegen::pointer_to_int(llvm::Value *value, llvm::Type *target_type)
    {
        if (!value || !target_type)
            return value;

        return builder().CreatePtrToInt(value, target_type, "ptrtoint");
    }

    llvm::Value *CastCodegen::int_to_pointer(llvm::Value *value, llvm::Type *target_type)
    {
        if (!value || !target_type)
            return value;

        return builder().CreateIntToPtr(value, target_type, "inttoptr");
    }

    llvm::Value *CastCodegen::pointer_cast(llvm::Value *value, llvm::Type *target_type)
    {
        if (!value || !target_type)
            return value;

        // With opaque pointers in modern LLVM, pointer-to-pointer casts
        // are often no-ops since all pointers have the same type
        if (value->getType() == target_type)
        {
            return value;
        }

        // For different address spaces, use address space cast
        auto *src_ptr = llvm::cast<llvm::PointerType>(value->getType());
        auto *dst_ptr = llvm::cast<llvm::PointerType>(target_type);

        if (src_ptr->getAddressSpace() != dst_ptr->getAddressSpace())
        {
            return builder().CreateAddrSpaceCast(value, target_type, "addrspacecast");
        }

        return value;
    }

    //===================================================================
    // Boolean Conversions
    //===================================================================

    llvm::Value *CastCodegen::to_bool(llvm::Value *value)
    {
        if (!value)
            return nullptr;

        llvm::Type *type = value->getType();

        // Already boolean
        if (type->isIntegerTy(1))
        {
            return value;
        }

        // Integer to boolean
        if (type->isIntegerTy())
        {
            return builder().CreateICmpNE(
                value,
                llvm::ConstantInt::get(type, 0),
                "tobool");
        }

        // Float to boolean
        if (type->isFloatingPointTy())
        {
            return builder().CreateFCmpUNE(
                value,
                llvm::ConstantFP::get(type, 0.0),
                "tobool");
        }

        // Pointer to boolean (null check)
        if (type->isPointerTy())
        {
            llvm::Value *null_ptr = llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(type));
            return builder().CreateICmpNE(value, null_ptr, "tobool");
        }

        LOG_WARN(Cryo::LogComponent::CODEGEN, "CastCodegen: Cannot convert type to boolean");
        return value;
    }

    llvm::Value *CastCodegen::bool_to_int(llvm::Value *value, llvm::Type *target_type)
    {
        if (!value || !target_type)
            return value;

        // Booleans are already i1, just zero-extend to target width
        return builder().CreateZExt(value, target_type, "booltoint");
    }

    //===================================================================
    // Safe/Checked Casts
    //===================================================================

    llvm::Value *CastCodegen::checked_int_cast(llvm::Value *value, llvm::Type *target_type,
                                               bool source_signed, bool target_signed,
                                               llvm::BasicBlock *on_overflow)
    {
        if (!value || !target_type)
            return value;

        llvm::Type *source_type = value->getType();
        unsigned src_bits = get_int_bits(source_type);
        unsigned dst_bits = get_int_bits(target_type);

        // Extension is always safe
        if (dst_bits >= src_bits)
        {
            return cast_integer(value, target_type, source_signed, target_signed);
        }

        // Truncation may overflow
        if (!on_overflow)
        {
            // No overflow handler - just do unchecked truncation
            return truncate(value, target_type);
        }

        // Generate overflow check
        llvm::Function *fn = builder().GetInsertBlock()->getParent();
        llvm::BasicBlock *no_overflow_block = llvm::BasicBlock::Create(llvm_ctx(), "no_overflow", fn);

        // Check if value fits in target type
        llvm::Value *truncated = truncate(value, target_type);
        llvm::Value *extended = source_signed
                                    ? sign_extend(truncated, source_type)
                                    : zero_extend(truncated, source_type);
        llvm::Value *overflowed = builder().CreateICmpNE(value, extended, "overflow_check");

        builder().CreateCondBr(overflowed, on_overflow, no_overflow_block);
        builder().SetInsertPoint(no_overflow_block);

        return truncated;
    }

    //===================================================================
    // Type Queries
    //===================================================================

    bool CastCodegen::needs_cast(llvm::Type *source, llvm::Type *target) const
    {
        return source != target;
    }

    bool CastCodegen::is_lossless(llvm::Type *source, llvm::Type *target) const
    {
        if (!source || !target)
            return false;

        if (source == target)
            return true;

        // Integer widening is lossless
        if (is_int_type(source) && is_int_type(target))
        {
            return get_int_bits(target) >= get_int_bits(source);
        }

        // Float widening is lossless
        if (is_float_type(source) && is_float_type(target))
        {
            return target->isDoubleTy() || !source->isDoubleTy();
        }

        // Pointer casts are lossless (within same address space)
        if (is_pointer_type(source) && is_pointer_type(target))
        {
            return true;
        }

        return false;
    }

    bool CastCodegen::is_signed_type(Cryo::Type *type) const
    {
        if (!type)
            return true; // Default to signed

        std::string name = type->to_string();
        // Unsigned types in Cryo start with 'u'
        return name.empty() || name[0] != 'u';
    }

    //===================================================================
    // Internal Helpers
    //===================================================================

    unsigned CastCodegen::get_int_bits(llvm::Type *type) const
    {
        if (!type || !type->isIntegerTy())
            return 0;

        return type->getIntegerBitWidth();
    }

    bool CastCodegen::is_float_type(llvm::Type *type) const
    {
        return type && type->isFloatingPointTy();
    }

    bool CastCodegen::is_int_type(llvm::Type *type) const
    {
        return type && type->isIntegerTy();
    }

    bool CastCodegen::is_pointer_type(llvm::Type *type) const
    {
        return type && type->isPointerTy();
    }

} // namespace Cryo::Codegen
