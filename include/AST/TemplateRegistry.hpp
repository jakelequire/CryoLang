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
            TemplateMetadata(const std::string& n, const std::vector<std::string>& params, 
                           const std::string& ns, const std::string& file)
                : name(n), generic_parameter_names(params), module_namespace(ns), source_file(file) {}
        };

        struct TemplateInfo
        {
            ClassDeclarationNode* class_template = nullptr;
            StructDeclarationNode* struct_template = nullptr;
            EnumDeclarationNode* enum_template = nullptr;
            FunctionDeclarationNode* function_template = nullptr;
            std::string module_namespace;
            std::string source_file;
            
            // New: store extracted metadata as backup
            TemplateMetadata metadata;
            
            TemplateInfo() = default;
            TemplateInfo(ClassDeclarationNode* cls, const std::string& ns, const std::string& file)
                : class_template(cls), module_namespace(ns), source_file(file) {}
            TemplateInfo(StructDeclarationNode* str, const std::string& ns, const std::string& file)
                : struct_template(str), module_namespace(ns), source_file(file) {}
            TemplateInfo(EnumDeclarationNode* enm, const std::string& ns, const std::string& file)
                : enum_template(enm), module_namespace(ns), source_file(file) {}
            TemplateInfo(FunctionDeclarationNode* fn, const std::string& ns, const std::string& file)
                : function_template(fn), module_namespace(ns), source_file(file) {}
        };

    private:
        // Map from template base name to template info
        std::unordered_map<std::string, TemplateInfo> _templates;
        
    public:
        TemplateRegistry() = default;
        ~TemplateRegistry() = default;

        /**
         * @brief Register a generic class template
         */
        void register_class_template(const std::string& base_name, 
                                    ClassDeclarationNode* template_node,
                                    const std::string& module_namespace,
                                    const std::string& source_file);

        /**
         * @brief Register a generic struct template
         */
        void register_struct_template(const std::string& base_name, 
                                     StructDeclarationNode* template_node,
                                     const std::string& module_namespace,
                                     const std::string& source_file);

        /**
         * @brief Register a generic enum template
         */
        void register_enum_template(const std::string& base_name, 
                                   EnumDeclarationNode* template_node,
                                   const std::string& module_namespace,
                                   const std::string& source_file);

        /**
         * @brief Register a generic function template
         */
        void register_function_template(const std::string& base_name, 
                                       FunctionDeclarationNode* template_node,
                                       const std::string& module_namespace,
                                       const std::string& source_file);

        /**
         * @brief Find a generic template by base name
         */
        const TemplateInfo* find_template(const std::string& base_name) const;

        /**
         * @brief Check if a template exists
         */
        bool has_template(const std::string& base_name) const;

        /**
         * @brief Get all registered templates
         */
        const std::unordered_map<std::string, TemplateInfo>& get_all_templates() const;

        /**
         * @brief Clear all registered templates
         */
        void clear();

        /**
         * @brief Get count of registered templates
         */
        size_t size() const;
    };
}