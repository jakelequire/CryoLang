#pragma once

#include "Codegen/ICodegenComponent.hpp"
#include "Codegen/CodegenContext.hpp"
#include "AST/ASTNode.hpp"

#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>
#include <string>

namespace Cryo::Codegen
{
    /**
     * @brief Handles all type casting and conversion operations
     *
     * This class centralizes:
     * - Explicit cast expressions (as keyword)
     * - Implicit type coercions
     * - Numeric conversions (int <-> float, widening, narrowing)
     * - Pointer conversions
     * - Reference/dereference operations
     *
     * Key features:
     * - Safe vs unsafe cast distinction
     * - Overflow checking for numeric casts (optional)
     * - Proper handling of signed/unsigned conversions
     */
    class CastCodegen : public ICodegenComponent
    {
    public:
        //===================================================================
        // Construction
        //===================================================================

        explicit CastCodegen(CodegenContext &ctx);
        ~CastCodegen() = default;

        //===================================================================
        // Main Entry Points
        //===================================================================

        /**
         * @brief Generate explicit cast expression
         * @param node Cast expression AST node
         * @return Cast value
         */
        llvm::Value *generate_cast_expression(Cryo::CastExpressionNode *node);

        /**
         * @brief Generate implicit cast/coercion
         * @param value Source value
         * @param target_type Target Cryo type
         * @return Cast value
         */
        llvm::Value *generate_implicit_cast(llvm::Value *value, Cryo::Type *target_type);

        /**
         * @brief Generate cast to specific LLVM type
         * @param value Source value
         * @param target_type Target LLVM type
         * @param name Optional name for result
         * @return Cast value
         */
        llvm::Value *cast_to(llvm::Value *value, llvm::Type *target_type,
                             const std::string &name = "");

        //===================================================================
        // Integer Conversions
        //===================================================================

        /**
         * @brief Convert between integer types
         * @param value Source integer value
         * @param target_type Target integer type
         * @param source_signed Whether source is signed
         * @param target_signed Whether target is signed
         * @return Converted value
         */
        llvm::Value *cast_integer(llvm::Value *value, llvm::Type *target_type,
                                  bool source_signed, bool target_signed);

        /**
         * @brief Sign-extend integer to larger type
         * @param value Source value
         * @param target_type Target type
         * @return Extended value
         */
        llvm::Value *sign_extend(llvm::Value *value, llvm::Type *target_type);

        /**
         * @brief Zero-extend integer to larger type
         * @param value Source value
         * @param target_type Target type
         * @return Extended value
         */
        llvm::Value *zero_extend(llvm::Value *value, llvm::Type *target_type);

        /**
         * @brief Truncate integer to smaller type
         * @param value Source value
         * @param target_type Target type
         * @return Truncated value
         */
        llvm::Value *truncate(llvm::Value *value, llvm::Type *target_type);

        //===================================================================
        // Floating-Point Conversions
        //===================================================================

        /**
         * @brief Convert between floating-point types
         * @param value Source float value
         * @param target_type Target float type
         * @return Converted value
         */
        llvm::Value *cast_float(llvm::Value *value, llvm::Type *target_type);

        /**
         * @brief Extend float to double
         * @param value Source float value
         * @return Double value
         */
        llvm::Value *float_extend(llvm::Value *value);

        /**
         * @brief Truncate double to float
         * @param value Source double value
         * @return Float value
         */
        llvm::Value *float_truncate(llvm::Value *value);

        //===================================================================
        // Integer <-> Float Conversions
        //===================================================================

        /**
         * @brief Convert integer to floating-point
         * @param value Source integer value
         * @param target_type Target float type
         * @param is_signed Whether source integer is signed
         * @return Float value
         */
        llvm::Value *int_to_float(llvm::Value *value, llvm::Type *target_type,
                                  bool is_signed = true);

        /**
         * @brief Convert floating-point to integer
         * @param value Source float value
         * @param target_type Target integer type
         * @param is_signed Whether target integer is signed
         * @return Integer value
         */
        llvm::Value *float_to_int(llvm::Value *value, llvm::Type *target_type,
                                  bool is_signed = true);

        //===================================================================
        // Pointer Conversions
        //===================================================================

        /**
         * @brief Convert pointer to integer
         * @param value Source pointer value
         * @param target_type Target integer type
         * @return Integer value
         */
        llvm::Value *pointer_to_int(llvm::Value *value, llvm::Type *target_type);

        /**
         * @brief Convert integer to pointer
         * @param value Source integer value
         * @param target_type Target pointer type
         * @return Pointer value
         */
        llvm::Value *int_to_pointer(llvm::Value *value, llvm::Type *target_type);

        /**
         * @brief Bitcast between pointer types
         * @param value Source pointer
         * @param target_type Target pointer type
         * @return Cast pointer
         *
         * Note: With opaque pointers, this is often a no-op.
         */
        llvm::Value *pointer_cast(llvm::Value *value, llvm::Type *target_type);

        //===================================================================
        // Boolean Conversions
        //===================================================================

        /**
         * @brief Convert value to boolean (i1)
         * @param value Source value
         * @return Boolean value (0 or 1)
         */
        llvm::Value *to_bool(llvm::Value *value);

        /**
         * @brief Convert boolean to integer type
         * @param value Source boolean (i1)
         * @param target_type Target integer type
         * @return Integer value (0 or 1)
         */
        llvm::Value *bool_to_int(llvm::Value *value, llvm::Type *target_type);

        //===================================================================
        // Safe/Checked Casts
        //===================================================================

        /**
         * @brief Generate checked integer cast with overflow detection
         * @param value Source value
         * @param target_type Target type
         * @param source_signed Source signedness
         * @param target_signed Target signedness
         * @param on_overflow Block to branch to on overflow (optional)
         * @return Cast value (or poison if overflow and no handler)
         */
        llvm::Value *checked_int_cast(llvm::Value *value, llvm::Type *target_type,
                                      bool source_signed, bool target_signed,
                                      llvm::BasicBlock *on_overflow = nullptr);

        //===================================================================
        // Type Queries
        //===================================================================

        /**
         * @brief Check if a cast is needed between types
         * @param source Source type
         * @param target Target type
         * @return true if cast is required
         */
        bool needs_cast(llvm::Type *source, llvm::Type *target) const;

        /**
         * @brief Check if cast would be lossless
         * @param source Source type
         * @param target Target type
         * @return true if no information would be lost
         */
        bool is_lossless(llvm::Type *source, llvm::Type *target) const;

        /**
         * @brief Determine signedness of a Cryo type
         * @param type Cryo type
         * @return true if signed
         */
        bool is_signed_type(Cryo::Type *type) const;

    private:
        //===================================================================
        // Internal Helpers
        //===================================================================

        /**
         * @brief Get bit width of integer type
         * @param type Integer type
         * @return Bit width
         */
        unsigned get_int_bits(llvm::Type *type) const;

        /**
         * @brief Check if type is floating-point
         * @param type Type to check
         * @return true if float or double
         */
        bool is_float_type(llvm::Type *type) const;

        /**
         * @brief Check if type is integer
         * @param type Type to check
         * @return true if integer type
         */
        bool is_int_type(llvm::Type *type) const;

        /**
         * @brief Check if type is pointer
         * @param type Type to check
         * @return true if pointer type
         */
        bool is_pointer_type(llvm::Type *type) const;
    };

} // namespace Cryo::Codegen
