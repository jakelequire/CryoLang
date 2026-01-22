#pragma once

#include "Codegen/ICodegenComponent.hpp"
#include "Codegen/CodegenContext.hpp"
#include "AST/ASTNode.hpp"
#include "AST/ASTVisitor.hpp"

#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <string>
#include <vector>
#include <unordered_set>

namespace Cryo::Codegen
{
    // Forward declarations
    class MemoryCodegen;

    /**
     * @brief Handles all function/method/constructor call generation
     *
     * This class replaces the 3,100+ line generate_function_call method
     * from CodegenVisitor with specialized handlers for each call type.
     *
     * Key features:
     * - Call type classification for clean dispatch
     * - Separated logic for each call category
     * - Proper argument preparation and type coercion
     * - Support for variadic functions
     */
    class CallCodegen : public ICodegenComponent
    {
    public:
        //===================================================================
        // Construction
        //===================================================================

        explicit CallCodegen(CodegenContext &ctx);
        ~CallCodegen() = default;

        /**
         * @brief Set the memory codegen component
         */
        void set_memory_codegen(MemoryCodegen *memory) { _memory = memory; }

        //===================================================================
        // Main Entry Point
        //===================================================================

        /**
         * @brief Generate code for a function call expression
         * @param node Call expression AST node
         * @return Generated LLVM value, or nullptr on error
         */
        llvm::Value *generate(Cryo::CallExpressionNode *node);

        //===================================================================
        // Call Classification
        //===================================================================

        /**
         * @brief Classification of call types
         */
        enum class CallKind
        {
            PrimitiveConstructor, // i32(value), f64(value), etc.
            StructConstructor,    // MyStruct(args...)
            ClassConstructor,     // new MyClass(args...)
            EnumVariant,          // Option::Some(value)
            Intrinsic,            // __malloc__(size)
            RuntimeFunction,      // cryo_alloc(...)
            StaticMethod,         // Type::method(args)
            InstanceMethod,       // obj.method(args)
            FreeFunction,         // function(args)
            GenericInstantiation, // Array<int>::new()
            IndirectCall,         // f(args) where f is a function pointer parameter
            Unknown
        };

        /**
         * @brief Classify a call expression
         * @param node Call expression to classify
         * @return Call classification
         */
        CallKind classify_call(Cryo::CallExpressionNode *node);

        //===================================================================
        // Specialized Generators
        //===================================================================

        /**
         * @brief Generate primitive constructor call (i32, f64, etc.)
         * @param node Call expression
         * @param type_name Primitive type name
         * @return Converted value
         */
        llvm::Value *generate_primitive_constructor(Cryo::CallExpressionNode *node,
                                                    const std::string &type_name);

        /**
         * @brief Generate struct constructor call
         * @param node Call expression
         * @param type_name Struct type name
         * @return Pointer to constructed struct
         */
        llvm::Value *generate_struct_constructor(Cryo::CallExpressionNode *node,
                                                 const std::string &type_name);

        /**
         * @brief Generate class constructor call (heap allocation)
         * @param node Call expression
         * @param type_name Class type name
         * @return Pointer to constructed object
         */
        llvm::Value *generate_class_constructor(Cryo::CallExpressionNode *node,
                                                const std::string &type_name);

        /**
         * @brief Generate enum variant constructor
         * @param node Call expression
         * @param enum_name Enum type name
         * @param variant_name Variant name
         * @return Enum value
         */
        llvm::Value *generate_enum_variant(Cryo::CallExpressionNode *node,
                                           const std::string &enum_name,
                                           const std::string &variant_name);

        /**
         * @brief Generate intrinsic function call
         * @param node Call expression
         * @param intrinsic_name Intrinsic name (e.g., "__malloc__")
         * @return Result value
         */
        llvm::Value *generate_intrinsic(Cryo::CallExpressionNode *node,
                                        const std::string &intrinsic_name);

        /**
         * @brief Generate runtime function call
         * @param node Call expression
         * @param function_name Runtime function name
         * @return Result value
         */
        llvm::Value *generate_runtime_call(Cryo::CallExpressionNode *node,
                                           const std::string &function_name);

        /**
         * @brief Generate static method call
         * @param node Call expression
         * @param type_name Type name
         * @param method_name Method name
         * @return Result value
         */
        llvm::Value *generate_static_method(Cryo::CallExpressionNode *node,
                                            const std::string &type_name,
                                            const std::string &method_name);

        /**
         * @brief Generate instance method call
         * @param node Call expression
         * @param callee The member access node representing the method call
         * @param receiver Receiver object value
         * @param method_name Method name
         * @return Result value
         */
        llvm::Value *generate_instance_method(Cryo::CallExpressionNode *node,
                                              Cryo::MemberAccessNode *callee,
                                              llvm::Value *receiver,
                                              const std::string &method_name);

        /**
         * @brief Generate free function call
         * @param node Call expression
         * @param function LLVM function to call
         * @return Result value
         */
        llvm::Value *generate_free_function(Cryo::CallExpressionNode *node,
                                            llvm::Function *function);

        //===================================================================
        // Argument Handling
        //===================================================================

        /**
         * @brief Generate argument values from argument expressions
         * @param args Argument expression nodes
         * @return Vector of generated argument values
         */
        std::vector<llvm::Value *> generate_arguments(
            const std::vector<std::unique_ptr<Cryo::ExpressionNode>> &args);

        /**
         * @brief Prepare an argument value for a parameter type
         * @param arg Argument value
         * @param param_type Expected parameter type
         * @return Prepared argument value
         */
        llvm::Value *prepare_argument(llvm::Value *arg, llvm::Type *param_type);

        /**
         * @brief Check if arguments match parameter types
         * @param args Argument values
         * @param params Parameter types
         * @return true if compatible
         */
        bool check_argument_compatibility(const std::vector<llvm::Value *> &args,
                                          llvm::FunctionType *fn_type);

        //===================================================================
        // Function Resolution
        //===================================================================

        /**
         * @brief Resolve a function by name
         * @param name Function name (may be qualified)
         * @return LLVM function or nullptr if not found
         */
        llvm::Function *resolve_function(const std::string &name);

        /**
         * @brief Resolve a method on a type
         * @param type_name Type name
         * @param method_name Method name
         * @return LLVM function or nullptr if not found
         */
        llvm::Function *resolve_method(const std::string &type_name,
                                       const std::string &method_name);

        /**
         * @brief Resolve a constructor for a type
         * @param type_name Type name
         * @param arg_types Argument types for overload resolution
         * @return LLVM function or nullptr if not found
         */
        llvm::Function *resolve_constructor(const std::string &type_name,
                                            const std::vector<llvm::Type *> &arg_types = {});

        //===================================================================
        // Type Checks
        //===================================================================

        /**
         * @brief Check if a name is a primitive type constructor
         * @param name Name to check
         * @return true if primitive constructor
         */
        bool is_primitive_constructor(const std::string &name) const;

        /**
         * @brief Check if a name is a runtime function
         * @param name Name to check
         * @return true if runtime function
         */
        bool is_runtime_function(const std::string &name) const;

        /**
         * @brief Check if a name is an intrinsic function
         * @param name Name to check
         * @return true if intrinsic
         */
        bool is_intrinsic(const std::string &name) const;

        /**
         * @brief Check if a name is a known struct type
         * @param name Name to check
         * @return true if struct type
         */
        bool is_struct_type(const std::string &name) const;

        /**
         * @brief Check if a name is a known enum type
         * @param name Name to check
         * @return true if enum type
         */
        bool is_enum_type(const std::string &name) const;

        /**
         * @brief Check if a name is a known class type (heap-allocated)
         * @param name Name to check
         * @return true if class type
         */
        bool is_class_type(const std::string &name) const;

    private:
        //===================================================================
        // Internal Helpers
        //===================================================================

        MemoryCodegen *_memory = nullptr;

        /**
         * @brief Extract function name from callee expression
         * @param callee Callee expression
         * @return Function name, or empty string if not identifiable
         */
        std::string extract_function_name(Cryo::ExpressionNode *callee);

        /**
         * @brief Extract type and method from member access
         * @param member Member access node
         * @param out_type Output type name
         * @param out_method Output method name
         * @return true if extraction successful
         */
        bool extract_member_call_info(Cryo::MemberAccessNode *member,
                                      std::string &out_type,
                                      std::string &out_method);

        /**
         * @brief Generate an expression value
         * @param expr Expression to generate
         * @return Generated value
         */
        llvm::Value *generate_expression(Cryo::ExpressionNode *expr);

        /**
         * @brief Generate the address of a member access for use as method receiver
         * @param node Member access node (e.g., this.heap_manager)
         * @return Pointer to the member field
         */
        llvm::Value *generate_member_receiver_address(Cryo::MemberAccessNode *node);

        /**
         * @brief Create function declaration if not exists
         * @param name Function name
         * @param return_type Return type
         * @param param_types Parameter types
         * @param is_variadic Whether function is variadic
         * @return Function declaration
         */
        llvm::Function *get_or_create_function(const std::string &name,
                                               llvm::Type *return_type,
                                               const std::vector<llvm::Type *> &param_types,
                                               bool is_variadic = false);

        /**
         * @brief Qualify a runtime function name
         * @param name Unqualified name
         * @return Qualified name (e.g., "std::Runtime::name")
         */
        std::string qualify_runtime_function(const std::string &name);

        /**
         * @brief Declare a runtime function with proper signature
         * @param unqualified_name Unqualified function name (e.g., "cryo_alloc")
         * @param qualified_name Qualified function name (e.g., "std::Runtime::cryo_alloc")
         * @return Function declaration, or nullptr if unknown function
         */
        llvm::Function *declare_runtime_function(const std::string &unqualified_name,
                                                  const std::string &qualified_name);

        /**
         * @brief Get or create a C library function declaration
         * @param name Function name (e.g., "printf", "sprintf")
         * @return Function declaration, or nullptr if not a known C function
         */
        llvm::Function *get_or_create_c_function(const std::string &name);

        /**
         * @brief Create forward declaration for a function from symbol table lookup
         *
         * Looks up the function in the symbol table using SRM candidates (including
         * imported namespaces) and creates an LLVM function declaration if found.
         * This handles imported stdlib functions that aren't in the LLVM module yet.
         *
         * @param name Function name (may be unqualified)
         * @return Function declaration, or nullptr if not found in symbol table
         */
        llvm::Function *create_forward_declaration_from_symbol(const std::string &name);

        // Static sets for type checking
        static const std::unordered_set<std::string> _primitive_types;
        static const std::unordered_set<std::string> _runtime_functions;
        static const std::unordered_set<std::string> _intrinsic_functions;
    };

} // namespace Cryo::Codegen
