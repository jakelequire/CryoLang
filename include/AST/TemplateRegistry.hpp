#pragma once
#include "AST/ASTNode.hpp"
#include <unordered_map>
#include <vector>
#include <string>

namespace Cryo
{
    /**
     * @brief Registry for tracking generic templates across all modules
     *
     * This registry maintains references to all generic class, enum, and function
     * templates that have been discovered during compilation. It allows the
     * MonomorphizationPass to find templates from any imported module.
     */
    class TemplateRegistry
    {
    public:
        // Template metadata extracted from AST nodes to avoid pointer issues
        struct TemplateMetadata
        {
            std::string name;
            std::vector<std::string> generic_parameter_names;
            std::string module_namespace;
            std::string source_file;

            TemplateMetadata() = default;
            TemplateMetadata(const std::string &n, const std::vector<std::string> &params,
                             const std::string &ns, const std::string &file)
                : name(n), generic_parameter_names(params), module_namespace(ns), source_file(file) {}
        };

        struct TemplateInfo
        {
            ClassDeclarationNode *class_template = nullptr;
            StructDeclarationNode *struct_template = nullptr;
            EnumDeclarationNode *enum_template = nullptr;
            FunctionDeclarationNode *function_template = nullptr;
            TraitDeclarationNode *trait_template = nullptr;
            std::string module_namespace;
            std::string source_file;

            // New: store extracted metadata as backup
            TemplateMetadata metadata;

            TemplateInfo() = default;
            TemplateInfo(ClassDeclarationNode *cls, const std::string &ns, const std::string &file)
                : class_template(cls), module_namespace(ns), source_file(file) {}
            TemplateInfo(StructDeclarationNode *str, const std::string &ns, const std::string &file)
                : struct_template(str), module_namespace(ns), source_file(file) {}
            TemplateInfo(EnumDeclarationNode *enm, const std::string &ns, const std::string &file)
                : enum_template(enm), module_namespace(ns), source_file(file) {}
            TemplateInfo(FunctionDeclarationNode *fn, const std::string &ns, const std::string &file)
                : function_template(fn), module_namespace(ns), source_file(file) {}
            TemplateInfo(TraitDeclarationNode *trait, const std::string &ns, const std::string &file)
                : trait_template(trait), module_namespace(ns), source_file(file) {}
        };

    private:
        // Map from template base name to template info
        std::unordered_map<std::string, TemplateInfo> _templates;

        // Method return type registry: fully qualified method name -> return type
        // Used for cross-module extern declarations when impl blocks aren't directly accessible
        std::unordered_map<std::string, Type *> _method_return_types;

    public:
        TemplateRegistry() = default;
        ~TemplateRegistry() = default;

        /**
         * @brief Register a generic class template
         */
        void register_class_template(const std::string &base_name,
                                     ClassDeclarationNode *template_node,
                                     const std::string &module_namespace,
                                     const std::string &source_file);

        /**
         * @brief Register a generic struct template
         */
        void register_struct_template(const std::string &base_name,
                                      StructDeclarationNode *template_node,
                                      const std::string &module_namespace,
                                      const std::string &source_file);

        /**
         * @brief Register a generic enum template
         */
        void register_enum_template(const std::string &base_name,
                                    EnumDeclarationNode *template_node,
                                    const std::string &module_namespace,
                                    const std::string &source_file);

        /**
         * @brief Register a generic function template
         */
        void register_function_template(const std::string &base_name,
                                        FunctionDeclarationNode *template_node,
                                        const std::string &module_namespace,
                                        const std::string &source_file);

        /**
         * @brief Register a generic trait template
         */
        void register_trait_template(const std::string &base_name,
                                     TraitDeclarationNode *template_node,
                                     const std::string &module_namespace,
                                     const std::string &source_file);

        /**
         * @brief Find a generic template by base name
         */
        const TemplateInfo *find_template(const std::string &base_name) const;

        /**
         * @brief Check if a template exists
         */
        bool has_template(const std::string &base_name) const;

        /**
         * @brief Get all registered templates
         */
        const std::unordered_map<std::string, TemplateInfo> &get_all_templates() const;

        /**
         * @brief Clear all registered templates
         */
        void clear();

        /**
         * @brief Get count of registered templates
         */
        size_t size() const;

        //===================================================================
        // Method Return Type Registry (for cross-module extern declarations)
        //===================================================================

        /**
         * @brief Register a method's return type for cross-module lookups
         * @param qualified_method_name Fully qualified method name (e.g., "std::core::option::Option::is_some")
         * @param return_type The method's return type
         */
        void register_method_return_type(const std::string &qualified_method_name, Type *return_type);

        /**
         * @brief Get a method's return type for extern declaration creation
         * @param qualified_method_name Fully qualified method name
         * @return The return type, or nullptr if not registered
         */
        Type *get_method_return_type(const std::string &qualified_method_name) const;

        /**
         * @brief Check if a method's return type is registered
         * @param qualified_method_name Fully qualified method name
         * @return true if registered
         */
        bool has_method_return_type(const std::string &qualified_method_name) const;
    };
}