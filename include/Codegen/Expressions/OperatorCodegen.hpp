#pragma once

#include "Codegen/ICodegenComponent.hpp"
#include "Codegen/CodegenContext.hpp"
#include "AST/ASTNode.hpp"
#include "AST/ASTVisitor.hpp"
#include "Lexer/lexer.hpp"

#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>
#include <string>
#include <unordered_map>
#include <functional>

namespace Cryo::Codegen
{
    // Forward declarations
    class MemoryCodegen;

    /**
     * @brief Handles all binary and unary operator code generation
     *
     * This class replaces the 2,300+ line generate_binary_operation method
     * and the generate_unary_operation method from CodegenVisitor with a
     * clean, dispatch-based architecture.
     *
     * Key features:
     * - Dispatch tables for operator handling (no massive if-else chains)
     * - Separated logic for each operator category
     * - Consolidated assignment handling
     * - String operation support
     */
    class OperatorCodegen : public ICodegenComponent
    {
    public:
        //===================================================================
        // Construction
        //===================================================================

        explicit OperatorCodegen(CodegenContext &ctx);
        ~OperatorCodegen() = default;

        /**
         * @brief Set the memory codegen component (for assignments)
         */
        void set_memory_codegen(MemoryCodegen *memory) { _memory = memory; }

        /**
         * @brief Set the call codegen component (for operator overloading)
         */
        void set_call_codegen(class CallCodegen *calls) { _calls = calls; }

        //===================================================================
        // Main Entry Points
        //===================================================================

        /**
         * @brief Generate code for a binary expression
         * @param node Binary expression AST node
         * @return Generated LLVM value, or nullptr on error
         */
        llvm::Value *generate_binary(Cryo::BinaryExpressionNode *node);

        /**
         * @brief Generate code for a unary expression
         * @param node Unary expression AST node
         * @return Generated LLVM value, or nullptr on error
         */
        llvm::Value *generate_unary(Cryo::UnaryExpressionNode *node);

        //===================================================================
        // Operator Classification
        //===================================================================

        /**
         * @brief Classification of binary operators
         */
        enum class BinaryOpClass
        {
            Assignment,   // =, +=, -=, etc.
            Arithmetic,   // +, -, *, /, %
            Comparison,   // ==, !=, <, >, <=, >=
            Logical,      // &&, ||
            Bitwise,      // &, |, ^, <<, >>
            StringConcat, // + for strings
            Unknown
        };

        /**
         * @brief Classify a binary operator
         * @param op Token kind of the operator
         * @param left_type Type of left operand (for context-sensitive ops)
         * @return Operator classification
         */
        BinaryOpClass classify_binary_op(TokenKind op, llvm::Type *left_type = nullptr);

        //===================================================================
        // Assignment Operations
        //===================================================================

        /**
         * @brief Generate assignment operation
         * @param node Binary expression with assignment operator
         * @return Assigned value
         */
        llvm::Value *generate_assignment(Cryo::BinaryExpressionNode *node);

        /**
         * @brief Generate assignment to an identifier
         * @param target Identifier node (left side)
         * @param value_node Expression node (right side)
         * @return Assigned value
         */
        llvm::Value *generate_identifier_assignment(Cryo::IdentifierNode *target,
                                                    Cryo::ExpressionNode *value_node);

        /**
         * @brief Generate assignment to a member access
         * @param target Member access node (left side)
         * @param value_node Expression node (right side)
         * @return Assigned value
         */
        llvm::Value *generate_member_assignment(Cryo::MemberAccessNode *target,
                                                Cryo::ExpressionNode *value_node);

        /**
         * @brief Generate assignment to an array element
         * @param target Array access node (left side)
         * @param value_node Expression node (right side)
         * @return Assigned value
         */
        llvm::Value *generate_array_assignment(Cryo::ArrayAccessNode *target,
                                               Cryo::ExpressionNode *value_node);

        /**
         * @brief Generate assignment through a dereferenced pointer
         * @param target Unary dereference expression (left side)
         * @param value_node Expression node (right side)
         * @return Assigned value
         */
        llvm::Value *generate_deref_assignment(Cryo::UnaryExpressionNode *target,
                                               Cryo::ExpressionNode *value_node);

        /**
         * @brief Generate compound assignment (+=, -=, *=, /=, %=)
         * @param node Binary expression with compound assignment operator
         * @return Computed and assigned value
         */
        llvm::Value *generate_compound_assignment(Cryo::BinaryExpressionNode *node);

        //===================================================================
        // Arithmetic Operations
        //===================================================================

        /**
         * @brief Generate arithmetic operation
         * @param op Operator token kind
         * @param lhs Left operand value
         * @param rhs Right operand value
         * @param result_type Expected result type
         * @return Result value
         */
        llvm::Value *generate_arithmetic(TokenKind op,
                                         llvm::Value *lhs,
                                         llvm::Value *rhs,
                                         TypeRef result_type = TypeRef{});

        /**
         * @brief Generate integer arithmetic
         * @param op Operator token kind
         * @param lhs Left operand
         * @param rhs Right operand
         * @param is_signed Whether operands are signed
         * @return Result value
         */
        llvm::Value *generate_integer_arithmetic(TokenKind op,
                                                 llvm::Value *lhs,
                                                 llvm::Value *rhs,
                                                 bool is_signed = true);

        /**
         * @brief Generate float arithmetic
         * @param op Operator token kind
         * @param lhs Left operand
         * @param rhs Right operand
         * @return Result value
         */
        llvm::Value *generate_float_arithmetic(TokenKind op,
                                               llvm::Value *lhs,
                                               llvm::Value *rhs);

        /**
         * @brief Generate pointer arithmetic
         * @param op Operator token kind
         * @param ptr Pointer operand
         * @param offset Integer offset operand
         * @return Result value
         */
        llvm::Value *generate_pointer_arithmetic(TokenKind op,
                                                 llvm::Value *ptr,
                                                 llvm::Value *offset,
                                                 llvm::Type *element_type = nullptr);

        //===================================================================
        // Comparison Operations
        //===================================================================

        /**
         * @brief Generate comparison operation
         * @param op Operator token kind
         * @param lhs Left operand
         * @param rhs Right operand
         * @param operand_type Type of operands (for signed/unsigned)
         * @return Boolean result (i1)
         */
        llvm::Value *generate_comparison(TokenKind op,
                                         llvm::Value *lhs,
                                         llvm::Value *rhs,
                                         TypeRef operand_type = TypeRef{});

        /**
         * @brief Generate integer comparison
         * @param op Operator token kind
         * @param lhs Left operand
         * @param rhs Right operand
         * @param is_signed Whether operands are signed
         * @return Boolean result (i1)
         */
        llvm::Value *generate_integer_comparison(TokenKind op,
                                                 llvm::Value *lhs,
                                                 llvm::Value *rhs,
                                                 bool is_signed = true);

        /**
         * @brief Generate float comparison
         * @param op Operator token kind
         * @param lhs Left operand
         * @param rhs Right operand
         * @return Boolean result (i1)
         */
        llvm::Value *generate_float_comparison(TokenKind op,
                                               llvm::Value *lhs,
                                               llvm::Value *rhs);

        /**
         * @brief Generate pointer comparison
         * @param op Operator token kind
         * @param lhs Left operand
         * @param rhs Right operand
         * @return Boolean result (i1)
         */
        llvm::Value *generate_pointer_comparison(TokenKind op,
                                                 llvm::Value *lhs,
                                                 llvm::Value *rhs);

        //===================================================================
        // Logical Operations
        //===================================================================

        /**
         * @brief Generate logical AND with short-circuit evaluation
         * @param node Binary expression node (needs AST for short-circuit)
         * @return Boolean result (i1)
         */
        llvm::Value *generate_logical_and(Cryo::BinaryExpressionNode *node);

        /**
         * @brief Generate logical OR with short-circuit evaluation
         * @param node Binary expression node (needs AST for short-circuit)
         * @return Boolean result (i1)
         */
        llvm::Value *generate_logical_or(Cryo::BinaryExpressionNode *node);

        //===================================================================
        // Bitwise Operations
        //===================================================================

        /**
         * @brief Generate bitwise operation
         * @param op Operator token kind
         * @param lhs Left operand
         * @param rhs Right operand
         * @return Result value
         */
        llvm::Value *generate_bitwise(TokenKind op,
                                      llvm::Value *lhs,
                                      llvm::Value *rhs);

        //===================================================================
        // String Operations
        //===================================================================

        /**
         * @brief Generate string concatenation
         * @param lhs Left string operand
         * @param rhs Right string operand
         * @return Concatenated string pointer
         */
        llvm::Value *generate_string_concat(llvm::Value *lhs, llvm::Value *rhs);

        /**
         * @brief Generate string-char concatenation
         * @param str String operand
         * @param chr Character operand
         * @return Concatenated string pointer
         */
        llvm::Value *generate_string_char_concat(llvm::Value *str, llvm::Value *chr);

        /**
         * @brief Generate char-string concatenation
         * @param chr Character operand
         * @param str String operand
         * @return Concatenated string pointer
         */
        llvm::Value *generate_char_string_concat(llvm::Value *chr, llvm::Value *str);

        //===================================================================
        // Unary Operations
        //===================================================================

        /**
         * @brief Generate arithmetic negation (-)
         * @param operand Operand value
         * @param type Operand type
         * @return Negated value
         */
        llvm::Value *generate_negation(llvm::Value *operand, TypeRef type = TypeRef{});

        /**
         * @brief Generate logical NOT (!)
         * @param operand Operand value
         * @return Inverted boolean (i1)
         */
        llvm::Value *generate_logical_not(llvm::Value *operand);

        /**
         * @brief Generate bitwise NOT (~)
         * @param operand Operand value
         * @return Inverted value
         */
        llvm::Value *generate_bitwise_not(llvm::Value *operand);

        /**
         * @brief Generate address-of operation (&)
         * @param operand Operand expression (must be lvalue)
         * @return Pointer to operand
         */
        llvm::Value *generate_address_of(Cryo::ExpressionNode *operand);

        /**
         * @brief Generate dereference operation (*)
         * @param ptr Pointer value
         * @param pointee_type Type being pointed to
         * @return Dereferenced value
         */
        llvm::Value *generate_dereference(llvm::Value *ptr, TypeRef pointee_type = TypeRef{});

        /**
         * @brief Generate pre/post increment (++)
         * @param operand Operand expression (must be lvalue)
         * @param is_prefix Whether prefix (++x) or postfix (x++)
         * @return Result value (new value for prefix, old value for postfix)
         */
        llvm::Value *generate_increment(Cryo::ExpressionNode *operand, bool is_prefix);

        /**
         * @brief Generate pre/post decrement (--)
         * @param operand Operand expression (must be lvalue)
         * @param is_prefix Whether prefix (--x) or postfix (x--)
         * @return Result value (new value for prefix, old value for postfix)
         */
        llvm::Value *generate_decrement(Cryo::ExpressionNode *operand, bool is_prefix);

    private:
        //===================================================================
        // Internal Helpers
        //===================================================================

        MemoryCodegen *_memory = nullptr;
        CallCodegen *_calls = nullptr;

        /**
         * @brief Generate an operand value from an expression
         * @param expr Expression node
         * @return Generated value
         */
        llvm::Value *generate_operand(Cryo::ExpressionNode *expr);

        /**
         * @brief Get the address of an lvalue expression
         * @param expr Expression node (must be lvalue)
         * @return Pointer to the lvalue
         */
        llvm::Value *get_lvalue_address(Cryo::ExpressionNode *expr);

        /**
         * @brief Check if an expression is an lvalue
         * @param expr Expression to check
         * @return true if lvalue
         */
        bool is_lvalue(Cryo::ExpressionNode *expr);

        /**
         * @brief Check if a type is a signed integer
         * @param type Cryo type to check
         * @return true if signed integer
         */
        bool is_signed_integer_type(TypeRef type);

        /**
         * @brief Check if a type is floating point
         * @param type Cryo type to check
         * @return true if floating point
         */
        bool is_float_type(TypeRef type);

        /**
         * @brief Check if a type is a string
         * @param type Cryo type to check
         * @return true if string
         */
        bool is_string_type(TypeRef type);

        /**
         * @brief Ensure operands have compatible types
         * @param lhs Left operand (may be modified)
         * @param rhs Right operand (may be modified)
         * @return true if types are now compatible
         */
        bool ensure_compatible_types(llvm::Value *&lhs, llvm::Value *&rhs);

        /**
         * @brief Get or create string concatenation runtime function
         * @return Function for string concatenation
         */
        llvm::Function *get_string_concat_fn();

        /**
         * @brief Get or create string-char concatenation runtime function
         * @return Function for string-char concatenation
         */
        llvm::Function *get_string_char_concat_fn();

        /**
         * @brief Get increment value for a type
         * @param type LLVM type
         * @return Constant 1 of appropriate type
         */
        llvm::Value *get_increment_value(llvm::Type *type);

        /**
         * @brief Extract the i32 discriminant from an enum value
         * @param val LLVM value (struct, pointer, or already i32)
         * @param enum_type Cryo semantic type (must be EnumType)
         * @return i32 discriminant value, or val unchanged if not an enum
         */
        llvm::Value *extract_enum_discriminant(llvm::Value *val, TypeRef enum_type);

        // Cached function pointers
        llvm::Function *_string_concat_fn = nullptr;
        llvm::Function *_string_char_concat_fn = nullptr;
        llvm::Function *_char_string_concat_fn = nullptr;
    };

} // namespace Cryo::Codegen
