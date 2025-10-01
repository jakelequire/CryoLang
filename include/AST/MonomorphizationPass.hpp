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
         * @return True if monomorphization succeeded, false otherwise
         */
        bool monomorphize(ProgramNode& program, 
                         const std::vector<GenericInstantiation>& required_instantiations,
                         const class TemplateRegistry& template_registry);

    private:
        /**
         * Generate a specialized version of a generic class template.
         * 
         * @param template_node The generic template to specialize
         * @param instantiation The specific instantiation to generate
         * @return Specialized class declaration node
         */
        std::unique_ptr<ClassDeclarationNode> specialize_class_template(
            const ClassDeclarationNode& template_node,
            const GenericInstantiation& instantiation);

        /**
         * Generate a specialized version of a generic class template using metadata.
         * This version works with extracted template metadata instead of raw AST pointers.
         * 
         * @param metadata The template metadata with parameter names
         * @param instantiation The specific instantiation to generate
         * @return Specialized class declaration node
         */
        std::unique_ptr<ClassDeclarationNode> specialize_class_template_from_metadata(
            const TemplateRegistry::TemplateMetadata& metadata,
            const GenericInstantiation& instantiation);

        /**
         * Generate a specialized version of a generic struct template.
         * 
         * @param template_node The generic struct template to specialize
         * @param instantiation The specific instantiation to generate
         * @return Specialized struct declaration node
         */
        std::unique_ptr<StructDeclarationNode> specialize_struct_template(
            const StructDeclarationNode& template_node,
            const GenericInstantiation& instantiation);

        /**
         * Generate a specialized version of a generic struct template using metadata.
         * This version works with extracted template metadata instead of raw AST pointers.
         * 
         * @param metadata The template metadata with parameter names
         * @param instantiation The specific instantiation to generate
         * @return Specialized struct declaration node
         */
        std::unique_ptr<StructDeclarationNode> specialize_struct_template_from_metadata(
            const TemplateRegistry::TemplateMetadata& metadata,
            const GenericInstantiation& instantiation);

        /**
         * Generate a specialized version of a generic enum template.
         * 
         * @param template_node The generic enum template to specialize
         * @param instantiation The specific instantiation to generate
         * @return Specialized enum declaration node
         */
        std::unique_ptr<EnumDeclarationNode> specialize_enum_template(
            const EnumDeclarationNode& template_node,
            const GenericInstantiation& instantiation);

        /**
         * Generate a specialized version of a generic enum template using metadata.
         * This version works with extracted template metadata instead of raw AST pointers.
         * 
         * @param metadata The template metadata with parameter names
         * @param instantiation The specific instantiation to generate
         * @return Specialized enum declaration node
         */
        std::unique_ptr<EnumDeclarationNode> specialize_enum_template_from_metadata(
            const TemplateRegistry::TemplateMetadata& metadata,
            const GenericInstantiation& instantiation);

        /**
         * Generate a specialized version of a generic function template.
         * 
         * @param template_node The generic function template to specialize
         * @param instantiation The specific instantiation to generate
         * @return Specialized function declaration node
         */
        std::unique_ptr<FunctionDeclarationNode> specialize_function_template(
            const FunctionDeclarationNode& template_node,
            const GenericInstantiation& instantiation);

        /**
         * Substitute type parameters in an AST node.
         * 
         * @param node The node to process
         * @param type_substitutions Map from generic parameter to concrete type
         */
        void substitute_type_parameters(ASTNode& node, 
                                      const std::unordered_map<std::string, std::string>& type_substitutions);

        /**
         * Generate a mangled name for a specialized type.
         * 
         * @param base_name The base template name
         * @param concrete_types The concrete type arguments
         * @return Mangled name suitable for LLVM IR
         */
        std::string generate_mangled_name(const std::string& base_name, 
                                         const std::vector<std::string>& concrete_types);

        // Track generated specializations to avoid duplicates
        std::unordered_map<std::string, ASTNode*> _generated_specializations;
    };
}