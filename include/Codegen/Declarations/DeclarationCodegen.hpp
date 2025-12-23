#pragma once

#include "Codegen/ICodegenComponent.hpp"
#include "Codegen/CodegenContext.hpp"
#include "AST/ASTNode.hpp"

#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <string>
#include <vector>

namespace Cryo
{
    class TypeChecker; // Forward declaration for import_specialized_methods
}

namespace Cryo::Codegen
{
    // Forward declarations
    class MemoryCodegen;
    class TypeCodegen;
    class GenericCodegen;

    /**
     * @brief Handles all declaration code generation
     *
     * This class centralizes the generation of:
     * - Function declarations and definitions
     * - Variable declarations (local and global)
     * - Struct/class declarations
     * - Enum declarations
     * - Type aliases
     *
     * Key features:
     * - Clean separation of declaration vs definition
     * - Proper handling of forward declarations
     * - Support for generic instantiation
     * - Visibility and linkage management
     */
    class DeclarationCodegen : public ICodegenComponent
    {
    public:
        //===================================================================
        // Construction
        //===================================================================

        explicit DeclarationCodegen(CodegenContext &ctx);
        ~DeclarationCodegen() = default;

        /**
         * @brief Set the memory codegen component for allocations
         */
        void set_memory_codegen(MemoryCodegen *memory) { _memory = memory; }

        /**
         * @brief Set the type codegen component
         */
        void set_type_codegen(TypeCodegen *types) { _type_codegen = types; }

        /**
         * @brief Set the generic codegen component
         */
        void set_generic_codegen(class GenericCodegen *generics) { _generics = generics; }

        /**
         * @brief Set pre-registration mode
         */
        void set_pre_registration_mode(bool enabled) { _pre_registration_mode = enabled; }

        //===================================================================
        // High-Level Entry Points (called by visitor)
        //===================================================================

        /**
         * @brief Generate a function (dispatch to declaration or definition)
         * @param node Function declaration node
         * @return Generated function
         */
        llvm::Function *generate_function(Cryo::FunctionDeclarationNode *node);

        /**
         * @brief Generate a variable declaration
         * @param node Variable declaration node
         */
        void generate_variable(Cryo::VariableDeclarationNode *node);

        /**
         * @brief Generate a struct method
         * @param node Method node
         */
        void generate_method(Cryo::StructMethodNode *node);

        /**
         * @brief Generate an implementation block
         * @param node Implementation block node
         */
        void generate_impl_block(Cryo::ImplementationBlockNode *node);

        /**
         * @brief Generate an extern block
         * @param node Extern block node
         */
        void generate_extern_block(Cryo::ExternBlockNode *node);

        /**
         * @brief Pre-register all functions from symbol table
         */
        void pre_register_functions();

        /**
         * @brief Import specialized methods from TypeChecker
         * @param type_checker TypeChecker reference
         */
        void import_specialized_methods(const Cryo::TypeChecker &type_checker);

        /**
         * @brief Process global variables recursively
         * @param node Root AST node
         */
        void process_global_variables(Cryo::ASTNode *node);

        /**
         * @brief Generate global constructor function
         */
        void generate_global_constructors();

        //===================================================================
        // Function Declarations
        //===================================================================

        /**
         * @brief Generate a function declaration (prototype only)
         * @param node Function declaration node
         * @return Generated LLVM function, or nullptr on error
         */
        llvm::Function *generate_function_declaration(Cryo::FunctionDeclarationNode *node);

        /**
         * @brief Generate a complete function definition
         * @param node Function declaration node with body
         * @return Generated LLVM function, or nullptr on error
         */
        llvm::Function *generate_function_definition(Cryo::FunctionDeclarationNode *node);

        /**
         * @brief Generate a method declaration for a struct/class
         * @param node Method declaration node
         * @param parent_type Parent struct/class type name
         * @return Generated LLVM function
         */
        llvm::Function *generate_method_declaration(Cryo::FunctionDeclarationNode *node,
                                                     const std::string &parent_type);

        /**
         * @brief Generate external function declaration
         * @param node External function node
         * @return Generated LLVM function
         */
        llvm::Function *generate_extern_function(Cryo::FunctionDeclarationNode *node);

        //===================================================================
        // Variable Declarations
        //===================================================================

        /**
         * @brief Generate a local variable declaration
         * @param node Variable declaration node
         * @return Alloca instruction for the variable
         */
        llvm::AllocaInst *generate_local_variable(Cryo::VariableDeclarationNode *node);

        /**
         * @brief Generate a global variable declaration
         * @param node Variable declaration node
         * @return Global variable
         */
        llvm::GlobalVariable *generate_global_variable(Cryo::VariableDeclarationNode *node);

        /**
         * @brief Generate a constant declaration
         * @param node Constant declaration node
         * @return Constant value
         */
        llvm::Constant *generate_constant(Cryo::VariableDeclarationNode *node);

        //===================================================================
        // Type Declarations
        //===================================================================

        /**
         * @brief Generate a struct declaration (type only, no methods)
         * @param node Struct declaration node
         * @return Generated struct type
         */
        llvm::StructType *generate_struct_declaration(Cryo::StructDeclarationNode *node);

        /**
         * @brief Generate a class declaration
         * @param node Class declaration node
         * @return Generated struct type (class layout)
         */
        llvm::StructType *generate_class_declaration(Cryo::ClassDeclarationNode *node);

        /**
         * @brief Generate an enum declaration
         * @param node Enum declaration node
         * @return Generated enum type
         */
        llvm::Type *generate_enum_declaration(Cryo::EnumDeclarationNode *node);

        //===================================================================
        // Helpers
        //===================================================================

        /**
         * @brief Generate function type from declaration
         * @param node Function declaration
         * @param has_this_param Whether to add implicit 'this' parameter
         * @return Function type
         */
        llvm::FunctionType *get_function_type(Cryo::FunctionDeclarationNode *node,
                                               bool has_this_param = false);

        /**
         * @brief Generate mangled name for a function
         * @param name Base function name
         * @param namespace_parts Namespace context
         * @param param_types Parameter types for overload disambiguation
         * @return Mangled name
         */
        std::string mangle_function_name(const std::string &name,
                                          const std::vector<std::string> &namespace_parts,
                                          const std::vector<Cryo::Type *> &param_types = {});

        /**
         * @brief Generate method name (Type::method format)
         * @param type_name Type name
         * @param method_name Method name
         * @param param_types Parameter types
         * @return Qualified method name
         */
        std::string generate_method_name(const std::string &type_name,
                                          const std::string &method_name,
                                          const std::vector<Cryo::Type *> &param_types = {});

        /**
         * @brief Get or create a function with given signature
         * @param name Function name
         * @param fn_type Function type
         * @param linkage Linkage type
         * @return Function (existing or newly created)
         */
        llvm::Function *get_or_create_function(const std::string &name,
                                                llvm::FunctionType *fn_type,
                                                llvm::GlobalValue::LinkageTypes linkage =
                                                    llvm::GlobalValue::ExternalLinkage);

        /**
         * @brief Check if a function is already declared
         * @param name Function name
         * @return true if declared
         */
        bool is_function_declared(const std::string &name) const;

    private:
        MemoryCodegen *_memory = nullptr;
        TypeCodegen *_type_codegen = nullptr;
        GenericCodegen *_generics = nullptr;
        bool _pre_registration_mode = false;

        //===================================================================
        // Internal Helpers
        //===================================================================

        /**
         * @brief Generate function parameters
         * @param fn Function to add parameters to
         * @param node Function declaration
         * @param start_idx Starting parameter index (for 'this')
         */
        void generate_parameters(llvm::Function *fn,
                                  Cryo::FunctionDeclarationNode *node,
                                  unsigned start_idx = 0);

        /**
         * @brief Generate function body
         * @param fn Function to generate body for
         * @param node Function declaration with body
         */
        void generate_function_body(llvm::Function *fn,
                                     Cryo::FunctionDeclarationNode *node);

        /**
         * @brief Generate default constructor for struct/class
         * @param type_name Type name
         * @param struct_type Struct type
         * @return Generated constructor function
         */
        llvm::Function *generate_default_constructor(const std::string &type_name,
                                                      llvm::StructType *struct_type);

        /**
         * @brief Generate destructor for class
         * @param type_name Type name
         * @param class_type Class struct type
         * @return Generated destructor function
         */
        llvm::Function *generate_destructor(const std::string &type_name,
                                             llvm::StructType *class_type);

        /**
         * @brief Get linkage type for a declaration
         * @param node Function or variable node
         * @return Appropriate linkage type
         */
        llvm::GlobalValue::LinkageTypes get_linkage(Cryo::ASTNode *node) const;

        /**
         * @brief Apply attributes to function
         * @param fn Function
         * @param node Declaration node for attribute info
         */
        void apply_function_attributes(llvm::Function *fn,
                                        Cryo::FunctionDeclarationNode *node);

        /**
         * @brief Generate global variable initializer
         * @param node Variable declaration
         * @param type Variable type
         * @return Constant initializer
         */
        llvm::Constant *generate_global_initializer(Cryo::VariableDeclarationNode *node,
                                                     llvm::Type *type);
    };

} // namespace Cryo::Codegen
