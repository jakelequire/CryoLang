#pragma once
#include "AST/ASTNode.hpp"
#include "AST/GenericInstantiation.hpp"
#include "AST/TemplateRegistry.hpp"
#include <vector>
#include <unordered_map>
#include <memory>

namespace Cryo
{
    // Forward declarations
    class TemplateRegistry;
    class TypeChecker;
    /**
     * Monomorphization Pass
     *
     * This pass takes generic type templates and generates specialized concrete versions
     * based on the instantiations tracked during type checking. For example:
     *
     * Template: class Array<T> { ... }
     * Usage: new Array<int>(), new Array<string>()
     * Result: Generates Array_int and Array_string concrete classes
     *
     * This happens between type checking (frontend) and code generation (backend).
     */
    class MonomorphizationPass
    {
    public:
        MonomorphizationPass() = default;
        ~MonomorphizationPass() = default;

        /**
         * Perform monomorphization on a program node.
         *
         * @param program The root program node to process
         * @param required_instantiations List of generic instantiations needed
         * @param template_registry Registry containing all available generic templates
         * @param type_checker TypeChecker instance for resolving type strings to Type* objects
         * @return True if monomorphization succeeded, false otherwise
         */
        bool monomorphize(ProgramNode &program,
                          const std::vector<GenericInstantiation> &required_instantiations,
                          const class TemplateRegistry &template_registry,
                          class TypeChecker &type_checker);

    private:
        /**
         * Generate a specialized version of a generic class template.
         *
         * @param template_node The generic template to specialize
         * @param instantiation The specific instantiation to generate
         * @return Specialized class declaration node
         */
        std::unique_ptr<ClassDeclarationNode> specialize_class_template(
            const ClassDeclarationNode &template_node,
            const GenericInstantiation &instantiation);

        /**
         * Generate a specialized version of a generic class template using metadata.
         * This version works with extracted template metadata instead of raw AST pointers.
         *
         * @param metadata The template metadata with parameter names
         * @param instantiation The specific instantiation to generate
         * @return Specialized class declaration node
         */
        std::unique_ptr<ClassDeclarationNode> specialize_class_template_from_metadata(
            const TemplateRegistry::TemplateMetadata &metadata,
            const GenericInstantiation &instantiation);

        /**
         * Generate a specialized version of a generic struct template.
         *
         * @param template_node The generic struct template to specialize
         * @param instantiation The specific instantiation to generate
         * @return Specialized struct declaration node
         */
        std::unique_ptr<StructDeclarationNode> specialize_struct_template(
            const StructDeclarationNode &template_node,
            const GenericInstantiation &instantiation);

        /**
         * Generate a specialized version of a generic struct template using metadata.
         * This version works with extracted template metadata instead of raw AST pointers.
         *
         * @param metadata The template metadata with parameter names
         * @param instantiation The specific instantiation to generate
         * @return Specialized struct declaration node
         */
        std::unique_ptr<StructDeclarationNode> specialize_struct_template_from_metadata(
            const TemplateRegistry::TemplateMetadata &metadata,
            const GenericInstantiation &instantiation);

        /**
         * Generate a specialized version of a generic enum template.
         *
         * @param template_node The generic enum template to specialize
         * @param instantiation The specific instantiation to generate
         * @return Specialized enum declaration node
         */
        std::unique_ptr<EnumDeclarationNode> specialize_enum_template(
            const EnumDeclarationNode &template_node,
            const GenericInstantiation &instantiation);

        /**
         * Generate a specialized version of a generic enum template using metadata.
         * This version works with extracted template metadata instead of raw AST pointers.
         *
         * @param metadata The template metadata with parameter names
         * @param instantiation The specific instantiation to generate
         * @return Specialized enum declaration node
         */
        std::unique_ptr<EnumDeclarationNode> specialize_enum_template_from_metadata(
            const TemplateRegistry::TemplateMetadata &metadata,
            const GenericInstantiation &instantiation);

        /**
         * Specialize methods for a generic enum template.
         * This function looks up methods for the template enum and creates specialized versions
         * with type substitution applied.
         *
         * @param template_node The template enum node with generic parameters
         * @param specialized_name The name of the specialized enum (e.g., "MyResult_int_string")
         * @param type_substitutions Map of type parameter substitutions
         */
        void specialize_enum_methods(
            const EnumDeclarationNode &template_node,
            const std::string &specialized_name,
            const std::unordered_map<std::string, std::string> &type_substitutions);

        /**
         * Create a specialized implementation block for enum methods.
         *
         * @param program The program containing the original implementation block
         * @param instantiation The specific instantiation to generate implementation for
         * @return Specialized implementation block node with concrete method bodies
         */
        std::unique_ptr<ImplementationBlockNode> create_specialized_implementation_block(
            const ProgramNode &program,
            const GenericInstantiation &instantiation);

        /**
         * Clone and substitute type parameters in a method.
         *
         * @param original_method The original method to clone and specialize
         * @param type_substitutions Map from generic parameter to concrete type
         * @return Cloned method with type substitutions applied
         */
        std::unique_ptr<StructMethodNode> clone_and_substitute_method(
            const StructMethodNode &original_method,
            const std::unordered_map<std::string, std::string> &type_substitutions);

        /**
         * Generate a specialized version of a generic function template.
         *
         * @param template_node The generic function template to specialize
         * @param instantiation The specific instantiation to generate
         * @return Specialized function declaration node
         */
        std::unique_ptr<FunctionDeclarationNode> specialize_function_template(
            const FunctionDeclarationNode &template_node,
            const GenericInstantiation &instantiation);

        /**
         * Substitute type parameters in an AST node.
         *
         * @param node The node to process
         * @param type_substitutions Map from generic parameter to concrete type
         */
        void substitute_type_parameters(ASTNode &node,
                                        const std::unordered_map<std::string, std::string> &type_substitutions);

        /**
         * Substitute type parameters in a type string.
         *
         * @param type_string The type string to process (e.g., "T", "Array<T>", "Option<T>")
         * @param type_substitutions Map from generic parameter to concrete type
         * @return Type string with substitutions applied
         */
        std::string substitute_type_in_string(const std::string &type_string,
                                              const std::unordered_map<std::string, std::string> &type_substitutions);

        /**
         * Generate a mangled name for a specialized type.
         *
         * @param base_name The base template name
         * @param concrete_types The concrete type arguments
         * @return Mangled name suitable for LLVM IR
         */
        std::string generate_mangled_name(const std::string &base_name,
                                          const std::vector<std::string> &concrete_types);

        /**
         * Clone a method body with type substitution applied.
         *
         * @param original_body The original method body to clone
         * @param type_substitutions Map from generic parameter to concrete type
         * @return Cloned method body with type substitutions applied
         */
        std::unique_ptr<BlockStatementNode> clone_method_body_with_substitution(
            BlockStatementNode *original_body,
            const std::unordered_map<std::string, std::string> &type_substitutions);

        /**
         * Clone a statement with type substitution applied.
         *
         * @param original_statement The original statement to clone
         * @param type_substitutions Map from generic parameter to concrete type
         * @return Cloned statement with type substitutions applied
         */
        std::unique_ptr<StatementNode> clone_statement_with_substitution(
            StatementNode *original_statement,
            const std::unordered_map<std::string, std::string> &type_substitutions);

        /**
         * Clone an expression with type substitution applied.
         *
         * @param original_expr The original expression to clone
         * @param type_substitutions Map from generic parameter to concrete type
         * @return Cloned expression with type substitutions applied
         */
        std::unique_ptr<ExpressionNode> clone_expression_with_substitution(
            ExpressionNode *original_expr,
            const std::unordered_map<std::string, std::string> &type_substitutions);

        /**
         * Convert string-based type substitutions to Type*-based substitutions.
         *
         * @param string_substitutions Map from generic parameter name to concrete type string
         * @return Map from generic parameter name to concrete Type* object
         */
        std::unordered_map<std::string, std::shared_ptr<Type>> convert_to_type_substitutions(
            const std::unordered_map<std::string, std::string> &string_substitutions);

        /**
         * Substitute generic types in a Type* object with concrete types.
         *
         * @param original_type The original Type* object that may contain generic parameters
         * @param type_substitutions Map from generic parameter name to concrete Type* object
         * @return New Type* object with substitutions applied, or original if no substitution needed
         */
        std::shared_ptr<Type> substitute_type(
            Type *original_type,
            const std::unordered_map<std::string, std::shared_ptr<Type>> &type_substitutions);

        // Track generated specializations to avoid duplicates
        std::unordered_map<std::string, ASTNode *> _generated_specializations;

        // TypeChecker reference for type resolution (set during monomorphize call)
        TypeChecker *_type_checker = nullptr;

        // TemplateRegistry reference for accessing generic parameter names
        const TemplateRegistry *_template_registry = nullptr;
    };
}
