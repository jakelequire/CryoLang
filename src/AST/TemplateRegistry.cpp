#include "AST/TemplateRegistry.hpp"
#include <iostream>

namespace Cryo
{
    void TemplateRegistry::register_class_template(const std::string& base_name, 
                                                   ClassDeclarationNode* template_node,
                                                   const std::string& module_namespace,
                                                   const std::string& source_file)
    {
        if (!template_node) return;

        // Check if it's actually a generic template
        if (template_node->generic_parameters().empty()) {
            return;
        }

        std::cout << "[TemplateRegistry] Registering class template: " << base_name 
                  << " from module: " << module_namespace << std::endl;

        // Extract generic parameter names to avoid pointer issues
        std::vector<std::string> param_names;
        for (const auto& param : template_node->generic_parameters()) {
            if (param) {
                param_names.push_back(param->name());
                std::cout << "[TemplateRegistry] Extracted parameter: " << param->name() << std::endl;
            }
        }

        // Create template info with both pointer and extracted metadata
        TemplateInfo info(template_node, module_namespace, source_file);
        info.metadata = TemplateMetadata(base_name, param_names, module_namespace, source_file);
        
        _templates[base_name] = info;
    }

    void TemplateRegistry::register_struct_template(const std::string& base_name, 
                                                    StructDeclarationNode* template_node,
                                                    const std::string& module_namespace,
                                                    const std::string& source_file)
    {
        if (!template_node) return;

        // Check if it's actually a generic template
        if (template_node->generic_parameters().empty()) {
            return;
        }

        std::cout << "[TemplateRegistry] Registering struct template: " << base_name 
                  << " from module: " << module_namespace << std::endl;

        // Extract parameter names before any potential pointer corruption
        std::vector<std::string> param_names;
        for (const auto& param : template_node->generic_parameters()) {
            param_names.push_back(param->name());
            std::cout << "[TemplateRegistry] Extracted parameter: " << param->name() << std::endl;
        }

        // Create template info with both pointer and extracted metadata
        TemplateInfo info(template_node, module_namespace, source_file);
        info.metadata = TemplateMetadata(base_name, param_names, module_namespace, source_file);
        
        _templates[base_name] = info;
    }

    void TemplateRegistry::register_enum_template(const std::string& base_name, 
                                                 EnumDeclarationNode* template_node,
                                                 const std::string& module_namespace,
                                                 const std::string& source_file)
    {
        if (!template_node) return;

        // Check if it's actually a generic template
        if (template_node->generic_parameters().empty()) {
            return;
        }

        std::cout << "[TemplateRegistry] Registering enum template: " << base_name 
                  << " from module: " << module_namespace << std::endl;

        // Extract generic parameter names to avoid pointer issues
        std::vector<std::string> param_names;
        for (const auto& param : template_node->generic_parameters()) {
            if (param) {
                param_names.push_back(param->name());
                std::cout << "[TemplateRegistry] Extracted parameter: " << param->name() << std::endl;
            }
        }

        // Create template info with both pointer and extracted metadata
        TemplateInfo info(template_node, module_namespace, source_file);
        info.metadata = TemplateMetadata(base_name, param_names, module_namespace, source_file);
        
        _templates[base_name] = info;
    }

    void TemplateRegistry::register_function_template(const std::string& base_name, 
                                                     FunctionDeclarationNode* template_node,
                                                     const std::string& module_namespace,
                                                     const std::string& source_file)
    {
        if (!template_node) return;

        // Check if it's actually a generic template
        if (template_node->generic_parameters().empty()) {
            return;
        }

        std::cout << "[TemplateRegistry] Registering function template: " << base_name 
                  << " from module: " << module_namespace << std::endl;

        _templates[base_name] = TemplateInfo(template_node, module_namespace, source_file);
    }

    const TemplateRegistry::TemplateInfo* TemplateRegistry::find_template(const std::string& base_name) const
    {
        auto it = _templates.find(base_name);
        if (it != _templates.end()) {
            std::cout << "[TemplateRegistry] Found template: " << base_name 
                      << " from module: " << it->second.module_namespace << std::endl;
            return &it->second;
        }
        
        std::cout << "[TemplateRegistry] Template not found: " << base_name << std::endl;
        return nullptr;
    }

    bool TemplateRegistry::has_template(const std::string& base_name) const
    {
        return _templates.find(base_name) != _templates.end();
    }

    const std::unordered_map<std::string, TemplateRegistry::TemplateInfo>& TemplateRegistry::get_all_templates() const
    {
        return _templates;
    }

    void TemplateRegistry::clear()
    {
        _templates.clear();
    }

    size_t TemplateRegistry::size() const
    {
        return _templates.size();
    }
}