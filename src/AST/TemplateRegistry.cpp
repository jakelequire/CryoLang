#include "AST/TemplateRegistry.hpp"
#include "Utils/Logger.hpp"
#include <iostream>

namespace Cryo
{
    void TemplateRegistry::register_class_template(const std::string &base_name,
                                                   ClassDeclarationNode *template_node,
                                                   const std::string &module_namespace,
                                                   const std::string &source_file)
    {
        if (!template_node)
            return;

        // Check if it's actually a generic template
        if (template_node->generic_parameters().empty())
        {
            return;
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "Registering class template: {} from module: {}", base_name, module_namespace);

        // Extract generic parameter names to avoid pointer issues
        std::vector<std::string> param_names;
        for (const auto &param : template_node->generic_parameters())
        {
            if (param)
            {
                param_names.push_back(param->name());
                LOG_TRACE(Cryo::LogComponent::AST, "Extracted parameter: {}", param->name());
            }
        }

        // Create template info with both pointer and extracted metadata
        TemplateInfo info(template_node, module_namespace, source_file);
        info.metadata = TemplateMetadata(base_name, param_names, module_namespace, source_file);

        _templates[base_name] = info;
    }

    void TemplateRegistry::register_struct_template(const std::string &base_name,
                                                    StructDeclarationNode *template_node,
                                                    const std::string &module_namespace,
                                                    const std::string &source_file)
    {
        if (!template_node)
            return;

        // Check if it's actually a generic template
        if (template_node->generic_parameters().empty())
        {
            return;
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "Registering struct template: {} from module: {}", base_name, module_namespace);

        // Extract parameter names before any potential pointer corruption
        std::vector<std::string> param_names;
        for (const auto &param : template_node->generic_parameters())
        {
            param_names.push_back(param->name());
            LOG_TRACE(Cryo::LogComponent::AST, "Extracted parameter: {}", param->name());
        }

        // Create template info with both pointer and extracted metadata
        TemplateInfo info(template_node, module_namespace, source_file);
        info.metadata = TemplateMetadata(base_name, param_names, module_namespace, source_file);

        _templates[base_name] = info;
    }

    void TemplateRegistry::register_enum_template(const std::string &base_name,
                                                  EnumDeclarationNode *template_node,
                                                  const std::string &module_namespace,
                                                  const std::string &source_file)
    {
        if (!template_node)
            return;

        // Check if it's actually a generic template
        if (template_node->generic_parameters().empty())
        {
            return;
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "Registering enum template: {} from module: {}", base_name, module_namespace);

        // Extract generic parameter names to avoid pointer issues
        std::vector<std::string> param_names;
        for (const auto &param : template_node->generic_parameters())
        {
            if (param)
            {
                param_names.push_back(param->name());
                LOG_TRACE(Cryo::LogComponent::AST, "Extracted parameter: {}", param->name());
            }
        }

        // Create template info with both pointer and extracted metadata
        TemplateInfo info(template_node, module_namespace, source_file);
        info.metadata = TemplateMetadata(base_name, param_names, module_namespace, source_file);

        _templates[base_name] = info;
    }

    void TemplateRegistry::register_function_template(const std::string &base_name,
                                                      FunctionDeclarationNode *template_node,
                                                      const std::string &module_namespace,
                                                      const std::string &source_file)
    {
        if (!template_node)
            return;

        // Check if it's actually a generic template
        if (template_node->generic_parameters().empty())
        {
            return;
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "Registering function template: {} from module: {}", base_name, module_namespace);

        _templates[base_name] = std::move(TemplateInfo(template_node, module_namespace, source_file));
    }

    void TemplateRegistry::register_trait_template(const std::string &base_name,
                                                   TraitDeclarationNode *template_node,
                                                   const std::string &module_namespace,
                                                   const std::string &source_file)
    {
        if (!template_node)
            return;

        // Check if it's actually a generic template
        if (template_node->generic_parameters().empty())
        {
            return;
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "Registering trait template: {} from module: {}", base_name, module_namespace);

        // Extract generic parameter names to avoid pointer issues
        std::vector<std::string> param_names;
        for (const auto &param : template_node->generic_parameters())
        {
            if (param)
            {
                param_names.push_back(param->name());
                LOG_TRACE(Cryo::LogComponent::AST, "Extracted parameter: {}", param->name());
            }
        }

        // Create template info with both pointer and extracted metadata
        TemplateInfo info(template_node, module_namespace, source_file);
        info.metadata = TemplateMetadata(base_name, param_names, module_namespace, source_file);

        _templates[base_name] = info;
    }

    const TemplateRegistry::TemplateInfo *TemplateRegistry::find_template(const std::string &base_name) const
    {
        auto it = _templates.find(base_name);
        if (it != _templates.end())
        {
            LOG_TRACE(Cryo::LogComponent::AST, "Found template: {} from module: {}", base_name, it->second.module_namespace);
            return &it->second;
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "Template not found: {}", base_name);
        return nullptr;
    }

    bool TemplateRegistry::has_template(const std::string &base_name) const
    {
        return _templates.find(base_name) != _templates.end();
    }

    const std::unordered_map<std::string, TemplateRegistry::TemplateInfo> &TemplateRegistry::get_all_templates() const
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

    //===================================================================
    // Method Return Type Registry Implementation
    //===================================================================

    void TemplateRegistry::register_method_return_type(const std::string &qualified_method_name, Type *return_type)
    {
        if (qualified_method_name.empty() || !return_type)
            return;

        _method_return_types[qualified_method_name] = return_type;
        LOG_DEBUG(Cryo::LogComponent::AST,
                  "Registered method return type: {} -> {}",
                  qualified_method_name, return_type->to_string());
    }

    Type *TemplateRegistry::get_method_return_type(const std::string &qualified_method_name) const
    {
        auto it = _method_return_types.find(qualified_method_name);
        if (it != _method_return_types.end())
        {
            LOG_TRACE(Cryo::LogComponent::AST,
                      "Found method return type: {} -> {}",
                      qualified_method_name, it->second->to_string());
            return it->second;
        }
        return nullptr;
    }

    bool TemplateRegistry::has_method_return_type(const std::string &qualified_method_name) const
    {
        return _method_return_types.find(qualified_method_name) != _method_return_types.end();
    }
}