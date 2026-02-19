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

        // Check if this template is already registered with a more specific namespace
        // Templates from imported modules (with longer/more specific namespaces) take precedence
        auto existing = _templates.find(base_name);
        if (existing != _templates.end())
        {
            // If the existing template has a more specific namespace (contains more ::),
            // don't overwrite it with a less specific one
            size_t existing_depth = std::count(existing->second.module_namespace.begin(),
                                               existing->second.module_namespace.end(), ':');
            size_t new_depth = std::count(module_namespace.begin(), module_namespace.end(), ':');

            if (existing_depth > new_depth)
            {
                LOG_DEBUG(Cryo::LogComponent::AST,
                          "Skipping registration of class template '{}' with namespace '{}' - "
                          "already registered with more specific namespace '{}'",
                          base_name, module_namespace, existing->second.module_namespace);
                return;
            }
        }

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

        // Check if this template is already registered with a more specific namespace
        auto existing = _templates.find(base_name);
        if (existing != _templates.end())
        {
            size_t existing_depth = std::count(existing->second.module_namespace.begin(),
                                               existing->second.module_namespace.end(), ':');
            size_t new_depth = std::count(module_namespace.begin(), module_namespace.end(), ':');

            if (existing_depth > new_depth)
            {
                LOG_DEBUG(Cryo::LogComponent::AST,
                          "Skipping registration of struct template '{}' with namespace '{}' - "
                          "already registered with more specific namespace '{}'",
                          base_name, module_namespace, existing->second.module_namespace);
                return;
            }
        }

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

        // Check if this template is already registered with a more specific namespace
        auto existing = _templates.find(base_name);
        if (existing != _templates.end())
        {
            size_t existing_depth = std::count(existing->second.module_namespace.begin(),
                                               existing->second.module_namespace.end(), ':');
            size_t new_depth = std::count(module_namespace.begin(), module_namespace.end(), ':');

            if (existing_depth > new_depth)
            {
                LOG_DEBUG(Cryo::LogComponent::AST,
                          "Skipping registration of enum template '{}' with namespace '{}' - "
                          "already registered with more specific namespace '{}'",
                          base_name, module_namespace, existing->second.module_namespace);
                return;
            }
        }

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

        // Check if this template is already registered with a more specific namespace
        auto existing = _templates.find(base_name);
        if (existing != _templates.end())
        {
            size_t existing_depth = std::count(existing->second.module_namespace.begin(),
                                               existing->second.module_namespace.end(), ':');
            size_t new_depth = std::count(module_namespace.begin(), module_namespace.end(), ':');

            if (existing_depth > new_depth)
            {
                LOG_DEBUG(Cryo::LogComponent::AST,
                          "Skipping registration of function template '{}' with namespace '{}' - "
                          "already registered with more specific namespace '{}'",
                          base_name, module_namespace, existing->second.module_namespace);
                return;
            }
        }

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

        // Check if this template is already registered with a more specific namespace
        auto existing = _templates.find(base_name);
        if (existing != _templates.end())
        {
            size_t existing_depth = std::count(existing->second.module_namespace.begin(),
                                               existing->second.module_namespace.end(), ':');
            size_t new_depth = std::count(module_namespace.begin(), module_namespace.end(), ':');

            if (existing_depth > new_depth)
            {
                LOG_DEBUG(Cryo::LogComponent::AST,
                          "Skipping registration of trait template '{}' with namespace '{}' - "
                          "already registered with more specific namespace '{}'",
                          base_name, module_namespace, existing->second.module_namespace);
                return;
            }
        }

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

    //===================================================================
    // Enum Implementation Block Registry Implementation
    //===================================================================

    void TemplateRegistry::register_enum_impl_block(const std::string &base_enum_name,
                                                    ImplementationBlockNode *impl_block,
                                                    const std::string &module_namespace)
    {
        if (base_enum_name.empty() || !impl_block)
            return;

        _enum_impl_blocks[base_enum_name] = impl_block;
        LOG_DEBUG(Cryo::LogComponent::AST,
                  "Registered enum impl block: {} (namespace: {}, methods: {})",
                  base_enum_name, module_namespace, impl_block->method_implementations().size());

        // Also extract and store method metadata for cross-module access
        TemplateMethodInfo method_info;
        method_info.module_namespace = module_namespace;

        for (const auto &method : impl_block->method_implementations())
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
            LOG_TRACE(Cryo::LogComponent::AST, "Extracted enum method metadata: {}::{} -> {}",
                      base_enum_name, mm.name, mm.return_type_annotation);
        }

        _template_method_info[base_enum_name] = std::move(method_info);
        LOG_DEBUG(Cryo::LogComponent::AST, "Stored {} method(s) metadata for enum template '{}'",
                  _template_method_info[base_enum_name].methods.size(), base_enum_name);
    }

    ImplementationBlockNode *TemplateRegistry::get_enum_impl_block(const std::string &base_enum_name) const
    {
        auto it = _enum_impl_blocks.find(base_enum_name);
        if (it != _enum_impl_blocks.end())
        {
            LOG_TRACE(Cryo::LogComponent::AST,
                      "Found enum impl block: {} with {} methods",
                      base_enum_name, it->second->method_implementations().size());
            return it->second;
        }
        return nullptr;
    }

    bool TemplateRegistry::has_enum_impl_block(const std::string &base_enum_name) const
    {
        return _enum_impl_blocks.find(base_enum_name) != _enum_impl_blocks.end();
    }

    //===================================================================
    // Struct Implementation Block Registry Implementation
    //===================================================================

    void TemplateRegistry::register_struct_impl_block(const std::string &base_struct_name,
                                                      ImplementationBlockNode *impl_block,
                                                      const std::string &module_namespace)
    {
        if (base_struct_name.empty() || !impl_block)
            return;

        _struct_impl_blocks[base_struct_name] = impl_block;
        LOG_DEBUG(Cryo::LogComponent::AST,
                  "Registered struct impl block: {} (namespace: {}, methods: {})",
                  base_struct_name, module_namespace, impl_block->method_implementations().size());

        // Also extract and store method metadata for cross-module access
        // This merges with any existing method info (e.g., from inline struct methods)
        auto existing_it = _template_method_info.find(base_struct_name);
        if (existing_it != _template_method_info.end())
        {
            // Merge impl block methods into existing method info
            for (const auto &method : impl_block->method_implementations())
            {
                if (!method)
                    continue;

                MethodMetadata mm;
                mm.name = method->name();
                mm.return_type_annotation = method->return_type_annotation() ? method->return_type_annotation()->to_string() : "void";
                mm.is_static = method->is_static();

                const auto &params = method->parameters();
                for (const auto &param : params)
                {
                    if (param)
                    {
                        mm.parameter_names.push_back(param->name());
                        mm.parameter_type_annotations.push_back(param->type_annotation() ? param->type_annotation()->to_string() : "unknown");
                    }
                }

                existing_it->second.methods.push_back(std::move(mm));
                LOG_TRACE(Cryo::LogComponent::AST, "Merged struct impl method metadata: {}::{} -> {}",
                          base_struct_name, mm.name, mm.return_type_annotation);
            }
        }
        else
        {
            // Create new method info entry
            TemplateMethodInfo method_info;
            method_info.module_namespace = module_namespace;

            for (const auto &method : impl_block->method_implementations())
            {
                if (!method)
                    continue;

                MethodMetadata mm;
                mm.name = method->name();
                mm.return_type_annotation = method->return_type_annotation() ? method->return_type_annotation()->to_string() : "void";
                mm.is_static = method->is_static();

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
                LOG_TRACE(Cryo::LogComponent::AST, "Extracted struct impl method metadata: {}::{} -> {}",
                          base_struct_name, mm.name, mm.return_type_annotation);
            }

            _template_method_info[base_struct_name] = std::move(method_info);
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "Stored {} total method(s) metadata for struct template '{}'",
                  _template_method_info[base_struct_name].methods.size(), base_struct_name);
    }

    ImplementationBlockNode *TemplateRegistry::get_struct_impl_block(const std::string &base_struct_name) const
    {
        auto it = _struct_impl_blocks.find(base_struct_name);
        if (it != _struct_impl_blocks.end())
        {
            LOG_TRACE(Cryo::LogComponent::AST,
                      "Found struct impl block: {} with {} methods",
                      base_struct_name, it->second->method_implementations().size());
            return it->second;
        }
        return nullptr;
    }

    bool TemplateRegistry::has_struct_impl_block(const std::string &base_struct_name) const
    {
        return _struct_impl_blocks.find(base_struct_name) != _struct_impl_blocks.end();
    }

    //===================================================================
    // Module Constants Registry Implementation
    //===================================================================

    void TemplateRegistry::register_module_constant(const std::string &module_namespace,
                                                     const std::string &name,
                                                     const std::string &type_annotation,
                                                     uint64_t value)
    {
        if (module_namespace.empty() || name.empty())
            return;

        // Check if already registered (avoid duplicates)
        auto &constants = _module_constants[module_namespace];
        for (const auto &c : constants)
        {
            if (c.name == name)
                return;
        }

        constants.emplace_back(name, type_annotation, value, true);
        LOG_DEBUG(Cryo::LogComponent::AST,
                  "Registered module constant: {}::{} (type: {}, value: {})",
                  module_namespace, name, type_annotation, value);
    }

    const std::vector<TemplateRegistry::ModuleConstant> *TemplateRegistry::get_module_constants(
        const std::string &module_namespace) const
    {
        auto it = _module_constants.find(module_namespace);
        if (it != _module_constants.end() && !it->second.empty())
        {
            return &it->second;
        }
        return nullptr;
    }

    std::string TemplateRegistry::find_type_namespace_from_methods(
        const std::string &type_name, const std::string &method_name) const
    {
        // Look for a key ending with "::TypeName::methodName" in the annotations map
        std::string suffix = "::" + type_name + "::" + method_name;
        for (const auto &[key, _] : _method_return_type_annotations)
        {
            if (key.length() > suffix.length() &&
                key.compare(key.length() - suffix.length(), suffix.length(), suffix) == 0)
            {
                // Extract namespace: everything before "::TypeName::methodName"
                return key.substr(0, key.length() - suffix.length());
            }
        }
        return "";
    }
}