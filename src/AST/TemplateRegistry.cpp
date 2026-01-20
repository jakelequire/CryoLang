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

        // Extract and store method metadata (persists after AST cleanup)
        TemplateMethodInfo method_info;
        method_info.module_namespace = module_namespace;

        const auto &methods = template_node->methods();
        for (const auto &method : methods)
        {
            if (!method)
                continue;

            MethodMetadata mm;
            mm.name = method->name();
            mm.return_type_annotation = method->return_type_annotation() ? method->return_type_annotation()->to_string() : "void";
            mm.is_static = method->is_static();

            // Extract parameter info
            const auto &params = method->parameters();
            for (const auto &param : params)
            {
                if (param)
                {
                    mm.parameter_names.push_back(param->name());
                    mm.parameter_type_annotations.push_back(param->type_annotation() ? param->type_annotation()->to_string() : "unknown");
                }
            }

            method_info.methods.push_back(std::move(mm));
            LOG_TRACE(Cryo::LogComponent::AST, "Extracted method metadata: {}::{} -> {}",
                      base_name, mm.name, mm.return_type_annotation);
        }

        _template_method_info[base_name] = std::move(method_info);
        LOG_DEBUG(Cryo::LogComponent::AST, "Stored {} method(s) metadata for class template '{}'",
                  _template_method_info[base_name].methods.size(), base_name);
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

        // Extract and store method metadata (persists after AST cleanup)
        TemplateMethodInfo method_info;
        method_info.module_namespace = module_namespace;

        const auto &methods = template_node->methods();
        for (const auto &method : methods)
        {
            if (!method)
                continue;

            MethodMetadata mm;
            mm.name = method->name();
            mm.return_type_annotation = method->return_type_annotation() ? method->return_type_annotation()->to_string() : "void";
            mm.is_static = method->is_static();

            // Extract parameter info
            const auto &params = method->parameters();
            for (const auto &param : params)
            {
                if (param)
                {
                    mm.parameter_names.push_back(param->name());
                    mm.parameter_type_annotations.push_back(param->type_annotation() ? param->type_annotation()->to_string() : "unknown");
                }
            }

            method_info.methods.push_back(std::move(mm));
            LOG_TRACE(Cryo::LogComponent::AST, "Extracted method metadata: {}::{} -> {}",
                      base_name, mm.name, mm.return_type_annotation);
        }

        _template_method_info[base_name] = std::move(method_info);
        LOG_DEBUG(Cryo::LogComponent::AST, "Stored {} method(s) metadata for template '{}'",
                  _template_method_info[base_name].methods.size(), base_name);
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

    void TemplateRegistry::register_method_return_type(const std::string &qualified_method_name, TypeRef return_type)
    {
        if (qualified_method_name.empty() || !return_type.is_valid())
            return;

        _method_return_types[qualified_method_name] = return_type;
        LOG_DEBUG(Cryo::LogComponent::AST,
                  "Registered method return type: {} -> {}",
                  qualified_method_name, return_type.get()->display_name());
    }

    TypeRef TemplateRegistry::get_method_return_type(const std::string &qualified_method_name) const
    {
        auto it = _method_return_types.find(qualified_method_name);
        if (it != _method_return_types.end())
        {
            LOG_TRACE(Cryo::LogComponent::AST,
                      "Found method return type: {} -> {}",
                      qualified_method_name, it->second.get()->display_name());
            return it->second;
        }
        return TypeRef{};
    }

    bool TemplateRegistry::has_method_return_type(const std::string &qualified_method_name) const
    {
        return _method_return_types.find(qualified_method_name) != _method_return_types.end();
    }

    void TemplateRegistry::register_method_return_type_annotation(const std::string &qualified_method_name,
                                                                   const std::string &return_type_annotation)
    {
        if (qualified_method_name.empty() || return_type_annotation.empty())
            return;

        _method_return_type_annotations[qualified_method_name] = return_type_annotation;
        LOG_DEBUG(Cryo::LogComponent::AST,
                  "Registered method return type annotation: {} -> {}",
                  qualified_method_name, return_type_annotation);
    }

    std::string TemplateRegistry::get_method_return_type_annotation(const std::string &qualified_method_name) const
    {
        auto it = _method_return_type_annotations.find(qualified_method_name);
        if (it != _method_return_type_annotations.end())
        {
            LOG_TRACE(Cryo::LogComponent::AST,
                      "Found method return type annotation: {} -> {}",
                      qualified_method_name, it->second);
            return it->second;
        }
        return "";
    }

    bool TemplateRegistry::has_method_return_type_annotation(const std::string &qualified_method_name) const
    {
        return _method_return_type_annotations.find(qualified_method_name) != _method_return_type_annotations.end();
    }

    //===================================================================
    // Struct Field Types Registry Implementation
    //===================================================================

    void TemplateRegistry::register_struct_field_types(const std::string &qualified_struct_name,
                                                       const std::vector<std::string> &field_names,
                                                       const std::vector<TypeRef> &field_types,
                                                       const std::string &source_namespace)
    {
        if (qualified_struct_name.empty())
            return;

        StructFieldInfo info;
        info.field_names = field_names;
        info.field_types = field_types;
        info.source_namespace = source_namespace;

        _struct_field_types[qualified_struct_name] = std::move(info);
        LOG_DEBUG(Cryo::LogComponent::AST,
                  "Registered struct field types: {} with {} fields (source_namespace: '{}')",
                  qualified_struct_name, field_names.size(), source_namespace);
    }

    const TemplateRegistry::StructFieldInfo *TemplateRegistry::get_struct_field_types(
        const std::string &qualified_struct_name) const
    {
        auto it = _struct_field_types.find(qualified_struct_name);
        if (it != _struct_field_types.end())
        {
            LOG_TRACE(Cryo::LogComponent::AST,
                      "Found struct field types: {} with {} fields",
                      qualified_struct_name, it->second.field_names.size());
            return &it->second;
        }
        return nullptr;
    }

    bool TemplateRegistry::has_struct_field_types(const std::string &qualified_struct_name) const
    {
        return _struct_field_types.find(qualified_struct_name) != _struct_field_types.end();
    }

    //===================================================================
    // Template Method Metadata Registry Implementation
    //===================================================================

    const TemplateRegistry::TemplateMethodInfo *TemplateRegistry::get_template_method_info(
        const std::string &template_name) const
    {
        auto it = _template_method_info.find(template_name);
        if (it != _template_method_info.end())
        {
            LOG_TRACE(Cryo::LogComponent::AST,
                      "Found template method info: {} with {} methods",
                      template_name, it->second.methods.size());
            return &it->second;
        }
        return nullptr;
    }

    const TemplateRegistry::MethodMetadata *TemplateRegistry::find_template_method(
        const std::string &template_name, const std::string &method_name) const
    {
        auto it = _template_method_info.find(template_name);
        if (it != _template_method_info.end())
        {
            for (const auto &method : it->second.methods)
            {
                if (method.name == method_name)
                {
                    LOG_TRACE(Cryo::LogComponent::AST,
                              "Found method metadata: {}::{} -> {}",
                              template_name, method_name, method.return_type_annotation);
                    return &method;
                }
            }
        }
        return nullptr;
    }

    bool TemplateRegistry::has_template_method_info(const std::string &template_name) const
    {
        return _template_method_info.find(template_name) != _template_method_info.end();
    }

    //===================================================================
    // Method Static Flag Registry Implementation
    //===================================================================

    void TemplateRegistry::register_method_is_static(const std::string &qualified_method_name, bool is_static)
    {
        _method_is_static[qualified_method_name] = is_static;
        LOG_DEBUG(Cryo::LogComponent::AST, "Registered method is_static: {} -> {}",
                  qualified_method_name, is_static);
    }

    std::pair<bool, bool> TemplateRegistry::get_method_is_static(const std::string &qualified_method_name) const
    {
        auto it = _method_is_static.find(qualified_method_name);
        if (it != _method_is_static.end())
        {
            return {it->second, true};
        }
        return {false, false}; // Not found
    }
}