#pragma once

#include "Codegen/ICodegenComponent.hpp"
#include "Codegen/CodegenContext.hpp"
#include "AST/ASTNode.hpp"

#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>
#include <string>

namespace Cryo::Codegen
{
    // Forward declarations
    class MemoryCodegen;
    class TypeCodegen;
    class ControlFlowCodegen;

    /**
     * @brief Handles expression code generation
     *
     * This class centralizes generation for:
     * - Literal expressions (int, float, string, bool)
     * - Identifier expressions (variable references)
     * - Member access expressions
     * - Index expressions (array access)
     * - Cast expressions
     * - Sizeof/alignof expressions
     */
    class ExpressionCodegen : public ICodegenComponent
    {
    public:
        //===================================================================
        // Construction
        //===================================================================

        explicit ExpressionCodegen(CodegenContext &ctx);
        ~ExpressionCodegen() = default;

        /**
         * @brief Set the memory codegen component
         */
        void set_memory_codegen(MemoryCodegen *memory) { _memory = memory; }

        /**
         * @brief Set the type codegen component
         */
        void set_type_codegen(TypeCodegen *types_codegen) { _type_codegen = types_codegen; }

        /**
         * @brief Set the operator codegen component
         */
        void set_operator_codegen(class OperatorCodegen *ops) { _operators = ops; }

        /**
         * @brief Set the call codegen component
         */
        void set_call_codegen(class CallCodegen *calls) { _calls = calls; }

        /**
         * @brief Set the cast codegen component
         */
        void set_cast_codegen(class CastCodegen *casts) { _casts = casts; }

        /**
         * @brief Set the control flow codegen component
         */
        void set_control_flow_codegen(ControlFlowCodegen *control_flow) { _control_flow = control_flow; }

        //===================================================================
        // Literal Expressions
        //===================================================================

        /**
         * @brief Generate integer literal
         * @param value Integer value
         * @param type Target type (determines bit width)
         * @return Constant integer value
         */
        llvm::Value *generate_integer_literal(int64_t value, TypeRef type = TypeRef{});

        /**
         * @brief Generate unsigned integer literal (for large unsigned values like U64_MAX)
         * @param value Unsigned integer value
         * @param type Target type (determines bit width)
         * @return Constant unsigned integer value
         */
        llvm::Value *generate_unsigned_integer_literal(uint64_t value, TypeRef type = TypeRef{});

        /**
         * @brief Generate floating-point literal
         * @param value Float value
         * @param is_double Whether to use double (true) or float (false)
         * @return Constant float value
         */
        llvm::Value *generate_float_literal(double value, bool is_double = true);

        /**
         * @brief Generate boolean literal
         * @param value Boolean value
         * @return Constant i1 value
         */
        llvm::Value *generate_bool_literal(bool value);

        /**
         * @brief Generate string literal
         * @param value String value
         * @return Pointer to string constant
         */
        llvm::Value *generate_string_literal(const std::string &value);

        /**
         * @brief Generate character literal
         * @param value Character value
         * @return Constant i8 value
         */
        llvm::Value *generate_char_literal(char value);

        /**
         * @brief Generate null literal
         * @param type Optional type for typed null
         * @return Null pointer constant
         */
        llvm::Value *generate_null_literal(TypeRef type = TypeRef{});

        /**
         * @brief Generate literal from AST node
         * @param node Literal expression node
         * @return Generated value
         */
        llvm::Value *generate_literal(Cryo::LiteralNode *node);

        /**
         * @brief Generate array literal
         * @param node Array literal node
         * @return Generated array value
         */
        llvm::Value *generate_array_literal(Cryo::ArrayLiteralNode *node);

        /**
         * @brief Generate tuple literal
         * @param node Tuple literal node
         * @return Generated tuple value (as struct)
         */
        llvm::Value *generate_tuple_literal(Cryo::TupleLiteralNode *node);

        /**
         * @brief Generate lambda expression
         * @param node Lambda expression node
         * @return Function pointer to generated lambda
         */
        llvm::Value *generate_lambda(Cryo::LambdaExpressionNode *node);

        /**
         * @brief Generate Array<T> constructor call for array literals
         * @param node Array literal node
         * @param elements Array elements
         * @param elem_type Element type
         * @return Array<T> instance
         */
        llvm::Value *generate_array_constructor_call(Cryo::ArrayLiteralNode *node,
                                                     const std::vector<std::unique_ptr<Cryo::ExpressionNode>> &elements,
                                                     llvm::Type *elem_type);

        /**
         * @brief Generate struct literal
         * @param node Struct literal node
         * @return Generated struct value
         */
        llvm::Value *generate_struct_literal(Cryo::StructLiteralNode *node);

        /**
         * @brief Generate array access expression
         * @param node Array access node
         * @return Element value
         */
        llvm::Value *generate_array_access(Cryo::ArrayAccessNode *node);

        /**
         * @brief Generate new expression (heap allocation)
         * @param node New expression node
         * @return Pointer to allocated object
         */
        llvm::Value *generate_new(Cryo::NewExpressionNode *node);

        /**
         * @brief Generate scope resolution expression
         * @param node Scope resolution node
         * @return Resolved value
         */
        llvm::Value *generate_scope_resolution(Cryo::ScopeResolutionNode *node);

        /**
         * @brief Generate enum variant value from a resolved type
         *
         * Used when GenericExpressionResolutionPass has resolved a generic enum variant
         * (like Option::None) to its concrete type (like Option<Duration>).
         *
         * @param node The scope resolution node
         * @param resolved_type The concrete enum type resolved from context
         * @param scope_name The enum name (e.g., "Option")
         * @param member_name The variant name (e.g., "None")
         * @return The LLVM value for the enum variant, or nullptr if not found
         */
        llvm::Value *generate_enum_variant_from_resolved_type(
            Cryo::ScopeResolutionNode *node,
            TypeRef resolved_type,
            const std::string &scope_name,
            const std::string &member_name);

        //===================================================================
        // Identifier Expressions
        //===================================================================

        /**
         * @brief Generate identifier expression (variable reference)
         * @param node Identifier node
         * @return Variable value (loaded if needed)
         */
        llvm::Value *generate_identifier(Cryo::IdentifierNode *node);

        /**
         * @brief Generate identifier address (for assignment)
         * @param node Identifier node
         * @return Address of variable
         */
        llvm::Value *generate_identifier_address(Cryo::IdentifierNode *node);

        /**
         * @brief Lookup and return variable value by name
         * @param name Variable name
         * @return Variable value or nullptr
         */
        llvm::Value *lookup_variable(const std::string &name);

        //===================================================================
        // Member Access Expressions
        //===================================================================

        /**
         * @brief Generate member access expression
         * @param node Member access node
         * @return Member value
         */
        llvm::Value *generate_member_access(Cryo::MemberAccessNode *node);

        /**
         * @brief Generate member address (for assignment)
         * @param node Member access node
         * @return Address of member
         */
        llvm::Value *generate_member_address(Cryo::MemberAccessNode *node);

        /**
         * @brief Get field index by name from struct type
         * @param struct_type Struct type
         * @param field_name Field name
         * @return Field index, or -1 if not found
         */
        int get_field_index(llvm::StructType *struct_type, const std::string &field_name);

        //===================================================================
        // Index Expressions
        //===================================================================

        /**
         * @brief Generate array index expression
         * @param node Index expression node
         * @return Element value
         */
        llvm::Value *generate_index(Cryo::ArrayAccessNode *node);

        /**
         * @brief Generate index address (for assignment)
         * @param node Index expression node
         * @return Address of element
         */
        llvm::Value *generate_index_address(Cryo::ArrayAccessNode *node);

        //===================================================================
        // Cast Expressions
        //===================================================================

        /**
         * @brief Generate cast expression
         * @param node Cast expression node
         * @return Cast value
         */
        llvm::Value *generate_cast(Cryo::CastExpressionNode *node);

        /**
         * @brief Generate explicit type cast
         * @param value Value to cast
         * @param target_type Target Cryo type
         * @return Cast value
         */
        llvm::Value *generate_cast(llvm::Value *value, TypeRef target_type);

        //===================================================================
        // Sizeof/Alignof
        //===================================================================

        /**
         * @brief Generate sizeof expression
         * @param node Sizeof expression node
         * @return Size value
         */
        llvm::Value *generate_sizeof(Cryo::SizeofExpressionNode *node);
        llvm::Value *generate_alignof(Cryo::AlignofExpressionNode *node);

        /**
         * @brief Generate sizeof for a type
         * @param type Cryo type
         * @return Size in bytes as i64
         */
        llvm::Value *generate_sizeof(TypeRef type);

        /**
         * @brief Generate alignof expression
         * @param type Cryo type
         * @return Alignment in bytes as i64
         */
        llvm::Value *generate_alignof(TypeRef type);

        //===================================================================
        // Address-of and Dereference
        //===================================================================

        /**
         * @brief Generate address-of expression
         * @param operand Operand expression
         * @return Address of operand
         */
        llvm::Value *generate_address_of(Cryo::ExpressionNode *operand);

        /**
         * @brief Generate dereference expression
         * @param operand Pointer operand
         * @param pointee_type Type of pointed-to value
         * @return Dereferenced value
         */
        llvm::Value *generate_dereference(llvm::Value *operand, TypeRef pointee_type);

        //===================================================================
        // Ternary Expression
        //===================================================================

        /**
         * @brief Generate ternary conditional expression
         * @param node Ternary expression node
         * @return Result value
         */
        llvm::Value *generate_ternary(Cryo::TernaryExpressionNode *node);

        /**
         * @brief Generate if-expression (if as a value)
         * @param node If expression node
         * @return Result value
         */
        llvm::Value *generate_if_expression(Cryo::IfExpressionNode *node);

        /**
         * @brief Generate match-expression (match as a value)
         * @param node Match expression node
         * @return Result value
         */
        llvm::Value *generate_match_expression(Cryo::MatchExpressionNode *node);

        //===================================================================
        // Helpers
        //===================================================================

        /**
         * @brief Check if expression is an lvalue
         * @param expr Expression to check
         * @return true if lvalue
         */
        bool is_lvalue(Cryo::ExpressionNode *expr) const;

        /**
         * @brief Generate expression and get result
         * @param expr Expression to generate
         * @return Result value
         */
        llvm::Value *generate(Cryo::ExpressionNode *expr);

    private:
        MemoryCodegen *_memory = nullptr;
        TypeCodegen *_type_codegen = nullptr;
        OperatorCodegen *_operators = nullptr;
        CallCodegen *_calls = nullptr;
        CastCodegen *_casts = nullptr;
        ControlFlowCodegen *_control_flow = nullptr;

        //===================================================================
        // Internal Helpers
        //===================================================================

        /**
         * @brief Get integer type for bit width
         * @param bits Bit width
         * @return Integer type
         */
        llvm::IntegerType *get_int_type(unsigned bits);

        /**
         * @brief Load value if it's a pointer to the expected type
         * @param value Value that might be a pointer
         * @param expected_type Expected result type
         * @return Loaded value or original
         */
        llvm::Value *load_if_pointer(llvm::Value *value, llvm::Type *expected_type);

        /**
         * @brief Get struct type and field info for member access
         * @param object Object expression
         * @param member_name Member name
         * @param out_struct_type Output struct type
         * @param out_field_idx Output field index
         * @return true if successful
         */
        bool resolve_member_info(Cryo::ExpressionNode *object,
                                 const std::string &member_name,
                                 llvm::StructType *&out_struct_type,
                                 unsigned &out_field_idx);

        /// Resolve a TypeRef to the correct mangled struct name for member access.
        /// Unwraps Pointer/Reference and handles InstantiatedType → monomorphized name.
        std::string resolve_struct_type_name(Cryo::TypeRef type);

        /**
         * @brief Wrap a raw i32 discriminant in a tagged union struct
         *
         * When an enum variant (e.g., Option::None) is stored as a raw i32 discriminant
         * but the enum's LLVM type is a tagged union struct { i32, [N x i8] }, this method
         * constructs the full struct with the discriminant at index 0 and zeroed payload.
         *
         * @param discriminant The i32 discriminant value
         * @param struct_type The target tagged union struct type
         * @param name Label prefix for IR instructions
         * @return The loaded struct value, or the original discriminant if wrapping fails
         */
        llvm::Value *wrap_discriminant_in_tagged_union(
            llvm::Value *discriminant, llvm::StructType *struct_type, const std::string &name);

        // String literal cache
        std::unordered_map<std::string, llvm::GlobalVariable *> _string_cache;
    };

} // namespace Cryo::Codegen
