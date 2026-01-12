#include "AST/MonomorphizationPass.hpp"
#include "Utils/SymbolResolutionManager.hpp"
#include "AST/TemplateRegistry.hpp"
#include "AST/TypeChecker.hpp"
#include "AST/ASTNode.hpp"
#include "AST/Type.hpp"
#include "Utils/Logger.hpp"
#include <iostream>
#include <sstream>

namespace Cryo
{
    bool MonomorphizationPass::monomorphize(ProgramNode &program,
                                            const std::vector<GenericInstantiation> &required_instantiations,
                                            const TemplateRegistry &template_registry,
                                            TypeChecker &type_checker)
    {
        // Store TypeChecker reference for type resolution
        _type_checker = &type_checker;

        LOG_INFO(Cryo::LogComponent::AST, "MonomorphizationPass: Starting monomorphization with {} required instantiations", required_instantiations.size());

        bool success = true;

        for (const auto &instantiation : required_instantiations)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Processing instantiation: {}", instantiation.instantiated_name);

            // Check if we already generated this specialization
            if (_generated_specializations.find(instantiation.instantiated_name) != _generated_specializations.end())
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Specialization already exists: {}", instantiation.instantiated_name);
                continue;
            }

            // Find the generic template using the template registry
            const auto *template_info = template_registry.find_template(instantiation.base_name);
            if (!template_info)
            {
                LOG_ERROR(Cryo::LogComponent::AST, "MonomorphizationPass: Cannot find generic template: {}", instantiation.base_name);
                success = false;
                continue;
            }

            std::unique_ptr<ASTNode> specialized_node = nullptr;

            // Handle different template types
            if (template_info->class_template)
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Specializing class template: {}", instantiation.base_name);

                // Prefer metadata path to avoid potentially corrupted pointers
                if (!template_info->metadata.generic_parameter_names.empty())
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Using metadata for class template (safer path)");
                    specialized_node = specialize_class_template_from_metadata(template_info->metadata, instantiation);
                }
                else
                {
                    // Fallback to direct pointer access only if metadata is not available
                    try
                    {
                        if (template_info->class_template->generic_parameters().empty() ||
                            !template_info->class_template->generic_parameters()[0])
                        {
                            LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Template node corrupted, cannot specialize");
                        }
                        else
                        {
                            specialized_node = specialize_class_template(*template_info->class_template, instantiation);
                        }
                    }
                    catch (...)
                    {
                        LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Template node access failed");
                    }
                }
            }
            else if (template_info->struct_template)
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Specializing struct template: {}", instantiation.base_name);

                // Prefer metadata path to avoid potentially corrupted pointers
                // The struct_template pointer may be dangling after AST modifications
                if (!template_info->metadata.generic_parameter_names.empty())
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Using metadata for struct template (safer path)");
                    specialized_node = specialize_struct_template_from_metadata(template_info->metadata, instantiation);
                }
                else
                {
                    // Fallback to direct pointer access only if metadata is not available
                    try
                    {
                        if (template_info->struct_template->generic_parameters().empty() ||
                            !template_info->struct_template->generic_parameters()[0])
                        {
                            LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Template node corrupted, cannot specialize");
                        }
                        else
                        {
                            specialized_node = specialize_struct_template(*template_info->struct_template, instantiation);
                        }
                    }
                    catch (...)
                    {
                        LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Template node access failed");
                    }
                }
            }
            else if (template_info->enum_template)
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Specializing enum template: {}", instantiation.base_name);

                // Prefer metadata path to avoid potentially corrupted pointers
                if (!template_info->metadata.generic_parameter_names.empty())
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Using metadata for enum template (safer path)");
                    specialized_node = specialize_enum_template_from_metadata(template_info->metadata, instantiation);
                }
                else
                {
                    // Fallback to direct pointer access only if metadata is not available
                    try
                    {
                        if (template_info->enum_template->generic_parameters().empty() ||
                            !template_info->enum_template->generic_parameters()[0])
                        {
                            LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Template node corrupted, cannot specialize");
                        }
                        else
                        {
                            specialized_node = specialize_enum_template(*template_info->enum_template, instantiation);
                        }
                    }
                    catch (...)
                    {
                        LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Template node access failed");
                    }
                }
            }
            else if (template_info->function_template)
            {
                LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Function template specialization not yet implemented: {}", instantiation.base_name);
                // TODO: Implement function template specialization
                success = false;
                continue;
            }
            else if (template_info->trait_template)
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Trait template found: {} - traits don't require concrete specialization", instantiation.base_name);
                // Traits don't generate concrete instances, they're used for type checking
                // Skip this instantiation as it doesn't need monomorphization
                continue;
            }
            else
            {
                LOG_ERROR(Cryo::LogComponent::AST, "MonomorphizationPass: Template info contains no valid template: {}", instantiation.base_name);
                success = false;
                continue;
            }
            if (!specialized_node)
            {
                LOG_ERROR(Cryo::LogComponent::AST, "MonomorphizationPass: Failed to specialize template: {}", instantiation.base_name);
                success = false;
                continue;
            }

            // Add specialized node to program
            LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Adding specialized declaration: {}", instantiation.instantiated_name);

            // Store reference before moving
            _generated_specializations[instantiation.instantiated_name] = specialized_node.get();

            // Add to program (this transfers ownership)
            program.add_statement(std::move(specialized_node));

            // CRITICAL: Also create specialized implementation block for enum methods
            // Check if this is an enum template by looking at the template info
            if (template_info && template_info->enum_template)
            {
                auto specialized_impl = create_specialized_implementation_block(program, instantiation);
                if (specialized_impl)
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Adding specialized implementation block: {}", instantiation.instantiated_name);
                    program.add_statement(std::move(specialized_impl));
                }
            }
        }

        LOG_INFO(Cryo::LogComponent::AST, "MonomorphizationPass: Monomorphization completed. Generated {} specializations", _generated_specializations.size());

        return success;
    }

    std::unique_ptr<ClassDeclarationNode> MonomorphizationPass::specialize_class_template(
        const ClassDeclarationNode &template_node,
        const GenericInstantiation &instantiation)
    {
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Specializing {} -> {}", template_node.name(), instantiation.instantiated_name);

        // Create type substitution map (strings for backward compatibility)
        std::unordered_map<std::string, std::string> string_substitutions;
        const auto &generic_params = template_node.generic_parameters();

        if (generic_params.size() != instantiation.concrete_types.size())
        {
            LOG_ERROR(Cryo::LogComponent::AST, "MonomorphizationPass: Parameter count mismatch for {}", template_node.name());
            return nullptr;
        }

        for (size_t i = 0; i < generic_params.size(); ++i)
        {
            // Extract the parameter name from GenericParameterNode
            std::string param_name = generic_params[i]->name();
            string_substitutions[param_name] = instantiation.concrete_types[i];
            LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Substitution: {} -> {}", param_name, instantiation.concrete_types[i]);
        }

        // Convert to Type*-based substitutions for proper type system operations
        auto type_substitutions_map = convert_to_type_substitutions(string_substitutions);

        // Clone the template node
        std::string mangled_class_name = generate_mangled_name(instantiation.base_name, instantiation.concrete_types);
        auto specialized = std::make_unique<ClassDeclarationNode>(
            template_node.location(),
            mangled_class_name);

        // Deep copy fields with Type*-based substitution (no more string manipulation)
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Copying {} fields from template", template_node.fields().size());
        for (const auto &field : template_node.fields())
        {
            if (field)
            {
                // Use the resolved Type* from the field and apply proper substitution
                Type *original_field_type = field->get_resolved_type();
                std::shared_ptr<Type> substituted_type = substitute_type(original_field_type, type_substitutions_map);

                // Create new field with substituted Type* (no string operations)
                auto specialized_field = std::make_unique<StructFieldNode>(
                    field->location(),
                    field->name(),
                    substituted_type.get(), // Convert shared_ptr back to raw pointer for constructor
                    field->visibility());

                specialized->add_field(std::move(specialized_field));
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Added field: {} : {}", field->name(), (substituted_type ? substituted_type->to_string() : "unknown"));
            }
        }

        // Deep copy methods with Type*-based substitution (no more string manipulation)
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Copying {} methods from template", template_node.methods().size());
        for (const auto &method : template_node.methods())
        {
            if (method)
            {
                // Use the resolved Type* from the method return type and apply substitution
                Type *original_return_type = method->get_resolved_return_type();
                std::shared_ptr<Type> substituted_return_type = substitute_type(original_return_type, type_substitutions_map);

                // Create new method with substituted Type* (no string operations)
                // For constructors, update the method name to match the specialized class name
                std::string specialized_method_name = method->name();
                if (method->is_constructor())
                {
                    specialized_method_name = mangled_class_name;
                    LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Updated constructor name from '{}' to '{}'",
                              method->name(), specialized_method_name);
                }

                auto specialized_method = std::make_unique<StructMethodNode>(
                    method->location(),
                    specialized_method_name,
                    substituted_return_type.get(), // Convert shared_ptr back to raw pointer
                    method->visibility(),
                    method->is_constructor(),
                    method->is_static());

                // Copy parameters with Type*-based substitution
                for (const auto &param : method->parameters())
                {
                    if (param)
                    {
                        // Use the resolved Type* from the parameter and apply substitution
                        Type *original_param_type = param->get_resolved_type();
                        std::shared_ptr<Type> substituted_param_type = substitute_type(original_param_type, type_substitutions_map);

                        // Create new parameter with substituted Type*
                        auto specialized_param = std::make_unique<VariableDeclarationNode>(
                            param->location(),
                            param->name(),
                            substituted_param_type.get()); // Convert shared_ptr back to raw pointer

                        specialized_method->add_parameter(std::move(specialized_param));
                    }
                }

                // Copy method body with type substitution (keeping string-based for now)
                if (method->body())
                {
                    auto cloned_body = clone_method_body_with_substitution(method->body(), string_substitutions);
                    if (cloned_body)
                    {
                        specialized_method->set_body(std::move(cloned_body));
                        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Successfully cloned method body for constructor '{}' with {} parameters",
                                  method->name(), method->parameters().size());
                        for (size_t i = 0; i < method->parameters().size(); ++i)
                        {
                            auto param = method->parameters()[i].get();
                            if (param && param->get_resolved_type())
                            {
                                LOG_DEBUG(Cryo::LogComponent::AST, "  Parameter {}: {} of type {}",
                                          i, param->name(), param->get_resolved_type()->to_string());
                            }
                        }
                    }
                    else
                    {
                        LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Failed to clone method body for constructor '{}' with {} parameters",
                                 method->name(), method->parameters().size());
                    }
                }

                // Capture method name before moving the specialized_method
                std::string final_method_name = specialized_method_name;

                specialized->add_method(std::move(specialized_method));
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Added method: {} : {}", method->name(), (substituted_return_type ? substituted_return_type->to_string() : "unknown"));

                // Register the specialized method with TypeChecker for codegen discovery
                if (_type_checker && substituted_return_type)
                {
                    // Create parameter types vector for function type
                    std::vector<Type *> param_types;
                    for (const auto &param : method->parameters())
                    {
                        if (param)
                        {
                            Type *original_param_type = param->get_resolved_type();
                            std::shared_ptr<Type> substituted_param_type = substitute_type(original_param_type, type_substitutions_map);
                            if (substituted_param_type)
                            {
                                param_types.push_back(substituted_param_type.get());
                            }
                        }
                    }

                    // Create function type for the specialized method using TypeContext to ensure proper memory management
                    auto &type_context = _type_checker->get_type_context();
                    Type *function_type = type_context.create_function_type(substituted_return_type.get(), param_types);
                    
                    // Register with TypeChecker using the specialized class name and method name
                    _type_checker->register_specialized_method(mangled_class_name, final_method_name, function_type);
                    LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Registered specialized method: {}::{}", mangled_class_name, final_method_name);
                }
            }
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Generated specialized class: {}", specialized->name());

        return specialized;
    }

    std::unique_ptr<StructDeclarationNode> MonomorphizationPass::specialize_struct_template(
        const StructDeclarationNode &template_node,
        const GenericInstantiation &instantiation)
    {
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Specializing struct {} -> {}", template_node.name(), instantiation.instantiated_name);

        // Create type substitution map
        std::unordered_map<std::string, std::string> type_substitutions;
        const auto &generic_params = template_node.generic_parameters();

        if (generic_params.size() != instantiation.concrete_types.size())
        {
            LOG_ERROR(Cryo::LogComponent::AST, "MonomorphizationPass: Parameter count mismatch for {}", template_node.name());
            return nullptr;
        }

        for (size_t i = 0; i < generic_params.size(); ++i)
        {
            // Extract the parameter name from GenericParameterNode
            std::string param_name = generic_params[i]->name();
            type_substitutions[param_name] = instantiation.concrete_types[i];
            LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Substitution: {} -> {}", param_name, instantiation.concrete_types[i]);
        }

        // Convert to Type*-based substitutions for proper type system operations
        auto type_substitutions_map = convert_to_type_substitutions(type_substitutions);

        // Clone the template node with field substitution
        std::string mangled_struct_name = generate_mangled_name(instantiation.base_name, instantiation.concrete_types);
        auto specialized = std::make_unique<StructDeclarationNode>(
            template_node.location(),
            mangled_struct_name);

        // Copy and substitute fields from the template
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Copying {} fields from template", template_node.fields().size());
        for (const auto &field : template_node.fields())
        {
            if (field)
            {
                // Use the resolved Type* from the field and apply substitution
                Type *original_field_type = field->get_resolved_type();
                std::shared_ptr<Type> substituted_type = substitute_type(original_field_type, type_substitutions_map);

                // Create new field with substituted type
                auto specialized_field = std::make_unique<StructFieldNode>(
                    field->location(),
                    field->name(),
                    substituted_type.get(), // Convert shared_ptr to raw pointer
                    field->visibility());

                specialized->add_field(std::move(specialized_field));
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Added field: {} : {}", field->name(), (substituted_type ? substituted_type->to_string() : "unknown"));
            }
        }

        // Copy methods with type substitution (just like class specialization)
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Copying {} methods from template", template_node.methods().size());
        for (const auto &method : template_node.methods())
        {
            if (method)
            {
                // Substitute return type
                // Use the resolved Type* from the method return type and apply substitution
                Type *original_return_type = method->get_resolved_return_type();
                std::shared_ptr<Type> substituted_return_type = substitute_type(original_return_type, type_substitutions_map);

                // Create new method with substituted return type
                // For constructors, update the method name to match the specialized struct name
                std::string specialized_method_name = method->name();
                if (method->is_constructor())
                {
                    specialized_method_name = mangled_struct_name;
                    LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Updated struct constructor name from '{}' to '{}'",
                              method->name(), specialized_method_name);
                }

                auto specialized_method = std::make_unique<StructMethodNode>(
                    method->location(),
                    specialized_method_name,
                    substituted_return_type.get(), // Convert shared_ptr to raw pointer
                    method->visibility(),
                    method->is_constructor(),
                    false, // is_destructor - default false
                    method->is_static());

                // Copy parameters with type substitution
                for (const auto &param : method->parameters())
                {
                    if (param)
                    {
                        // Use the resolved Type* from the parameter and apply substitution
                        Type *original_param_type = param->get_resolved_type();
                        std::shared_ptr<Type> substituted_param_type = substitute_type(original_param_type, type_substitutions_map);

                        auto specialized_param = std::make_unique<VariableDeclarationNode>(
                            param->location(),
                            param->name(),
                            substituted_param_type.get(), // Convert shared_ptr to raw pointer
                            nullptr,
                            param->is_mutable());

                        specialized_method->add_parameter(std::move(specialized_param));
                    }
                }

                // Clone method body with type substitution
                if (method->body())
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Cloning method body with {} statements", method->body()->statements().size());
                    auto cloned_body = clone_method_body_with_substitution(method->body(), type_substitutions);
                    if (cloned_body)
                    {
                        specialized_method->set_body(std::move(cloned_body));
                        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Successfully cloned method body with {} statements", specialized_method->body()->statements().size());
                        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Copied method body for: {}", method->name());
                    }
                    else
                    {
                        LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Failed to clone method body for: {}", method->name());
                    }
                }

                // Capture method name before moving the specialized_method
                std::string final_method_name = specialized_method_name;

                specialized->add_method(std::move(specialized_method));
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Added method: {} -> {}", method->name(), substituted_return_type);

                // Register the specialized method with TypeChecker for codegen discovery
                if (_type_checker && substituted_return_type)
                {
                    // Create parameter types vector for function type
                    std::vector<Type *> param_types;
                    for (const auto &param : method->parameters())
                    {
                        if (param)
                        {
                            Type *original_param_type = param->get_resolved_type();
                            std::shared_ptr<Type> substituted_param_type = substitute_type(original_param_type, type_substitutions_map);
                            if (substituted_param_type)
                            {
                                param_types.push_back(substituted_param_type.get());
                            }
                        }
                    }

                    // Create function type for the specialized method using TypeContext to ensure proper memory management
                    auto &type_context = _type_checker->get_type_context();
                    Type *function_type = type_context.create_function_type(substituted_return_type.get(), param_types);
                    
                    // Register with TypeChecker using the specialized struct name and method name
                    _type_checker->register_specialized_method(mangled_struct_name, final_method_name, function_type);
                    LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Registered specialized method: {}::{}", mangled_struct_name, final_method_name);
                }
            }
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Generated specialized struct: {} with {} fields and {} methods", specialized->name(), specialized->fields().size(), specialized->methods().size());

        return specialized;
    }

    std::unique_ptr<EnumDeclarationNode> MonomorphizationPass::specialize_enum_template(
        const EnumDeclarationNode &template_node,
        const GenericInstantiation &instantiation)
    {
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Specializing enum {} -> {}", template_node.name(), instantiation.instantiated_name);

        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Getting generic parameters...");
        const auto &generic_params = template_node.generic_parameters();
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Template has {} generic parameters", generic_params.size());
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Instantiation has {} concrete types", instantiation.concrete_types.size());

        // Debug template node pointer and generic parameters
        LOG_TRACE(Cryo::LogComponent::AST, "MonomorphizationPass: template_node name: {}", template_node.name());
        for (size_t i = 0; i < generic_params.size(); ++i)
        {
            auto param_ptr = generic_params[i].get();
            LOG_TRACE(Cryo::LogComponent::AST, "MonomorphizationPass: generic_params[{}] = {}", i, (void *)param_ptr);
            if (param_ptr)
            {
                LOG_TRACE(Cryo::LogComponent::AST, "MonomorphizationPass: parameter name: {}", param_ptr->name());
            }
        }

        // Validate parameter count
        if (generic_params.size() != instantiation.concrete_types.size())
        {
            LOG_ERROR(Cryo::LogComponent::AST, "MonomorphizationPass: Parameter count mismatch for {}", template_node.name());
            return nullptr;
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Creating type substitutions map...");
        // Create type substitutions map
        std::unordered_map<std::string, std::string> type_substitutions;
        for (size_t i = 0; i < generic_params.size(); ++i)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Processing parameter {}", i);

            if (!generic_params[i])
            {
                LOG_ERROR(Cryo::LogComponent::AST, "MonomorphizationPass: Generic parameter {} is null!", i);
                return nullptr;
            }

            LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Getting parameter name...");
            std::string param_name = generic_params[i]->name();
            LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Parameter name: {}", param_name);

            if (i >= instantiation.concrete_types.size())
            {
                LOG_ERROR(Cryo::LogComponent::AST, "MonomorphizationPass: Not enough concrete types for parameter {}", i);
                return nullptr;
            }

            type_substitutions[param_name] = instantiation.concrete_types[i];
            LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Substitution: {} -> {}", param_name, instantiation.concrete_types[i]);
        }

        // Clone the template node
        // Note: This is a simplified clone - in practice you'd need a deep copy visitor
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: About to create specialized enum...");

        std::string mangled_name = generate_mangled_name(instantiation.base_name, instantiation.concrete_types);
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Generated mangled name: {}", mangled_name);

        auto specialized = std::make_unique<EnumDeclarationNode>(
            template_node.location(),
            mangled_name);

        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Created EnumDeclarationNode successfully");

        // Debug: Check the template node variants count
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Template node has {} variants", template_node.variants().size());

        // Copy and substitute enum variants from the template
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Copying {} variants from template", template_node.variants().size());
        for (const auto &variant : template_node.variants())
        {
            if (variant)
            {
                // Clone the variant with type substitution applied to associated types
                std::vector<std::string> substituted_types;
                for (const auto &type_str : variant->associated_types())
                {
                    std::string substituted_type = substitute_type_in_string(type_str, type_substitutions);
                    substituted_types.push_back(substituted_type);
                    LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Substituted variant type: {} -> {}", type_str, substituted_type);
                }

                // Create the specialized variant
                std::unique_ptr<EnumVariantNode> specialized_variant;
                if (substituted_types.empty())
                {
                    // Simple variant (no associated types)
                    specialized_variant = std::make_unique<EnumVariantNode>(
                        variant->location(),
                        variant->name());
                }
                else
                {
                    // Complex variant with associated types
                    specialized_variant = std::make_unique<EnumVariantNode>(
                        variant->location(),
                        variant->name(),
                        std::move(substituted_types));
                }

                specialized->add_variant(std::move(specialized_variant));
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Added variant '{}' to specialized enum", variant->name());
            }
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Generated specialized enum: {}", specialized->name());

        // Register the specialized enum in TypeContext so it can be found during TypeMapper lookup
        if (_type_checker)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Registering specialized enum {} in TypeContext", mangled_name);

            // Collect variant names from the specialized enum
            std::vector<std::string> variant_names;
            for (const auto &variant : specialized->variants())
            {
                if (variant)
                {
                    variant_names.push_back(variant->name());
                    LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Collected variant: {}", variant->name());
                }
            }

            // Register the specialized enum in TypeContext using get_enum_type
            // This will create and register the enum type if it doesn't exist
            auto &type_context = _type_checker->get_type_context();
            Type *specialized_enum_type = type_context.get_enum_type(mangled_name, variant_names, false); // not simple since it's generic

            if (specialized_enum_type)
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Successfully registered specialized enum type: {}", mangled_name);

                // Now specialize any methods from the template enum
                specialize_enum_methods(template_node, mangled_name, type_substitutions);
            }
            else
            {
                LOG_ERROR(Cryo::LogComponent::AST, "MonomorphizationPass: Failed to register specialized enum type: {}", mangled_name);
            }
        }
        else
        {
            LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: No TypeChecker available to register specialized enum");
        }

        return specialized;
    }

    void MonomorphizationPass::specialize_enum_methods(
        const EnumDeclarationNode &template_node,
        const std::string &specialized_name,
        const std::unordered_map<std::string, std::string> &type_substitutions)
    {
        if (!_type_checker)
        {
            LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: No TypeChecker available for method specialization");
            return;
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Specializing methods for enum: {} -> {}", template_node.name(), specialized_name);

        // Create type name for generic template (with angle brackets) using correct parameter order
        std::string generic_type_name = template_node.name() + "<";
        const auto &generic_params = template_node.generic_parameters();
        for (size_t i = 0; i < generic_params.size(); ++i)
        {
            if (i > 0)
                generic_type_name += ",";
            generic_type_name += generic_params[i]->name();
        }
        generic_type_name += ">";

        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Looking up methods for generic type: {}", generic_type_name);

        // Access the struct methods using the new public method
        const auto *methods = _type_checker->get_struct_methods(generic_type_name);

        if (!methods)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: No methods found for generic enum: {}", generic_type_name);
            return;
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Found {} methods for generic enum", methods->size());

        // Convert string substitutions to Type* substitutions
        auto type_substitutions_map = convert_to_type_substitutions(type_substitutions);

        // Create specialized methods with type substitution
        for (const auto &[method_name, method_type] : *methods)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Specializing method: {}", method_name);

            // Get the original function type
            auto original_func_type = dynamic_cast<const FunctionType *>(method_type);
            if (!original_func_type)
            {
                LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Method {} does not have function type", method_name);
                continue;
            }

            // Apply type substitution to return type
            std::shared_ptr<Type> substituted_return_type = substitute_type(original_func_type->return_type().get(), type_substitutions_map);
            if (!substituted_return_type)
            {
                LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Failed to substitute return type for method {}, skipping", method_name);
                continue;
            }
            LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Specialized return type for {}: {} -> {}",
                      method_name,
                      original_func_type->return_type()->to_string(),
                      substituted_return_type->to_string());

            // Apply type substitution to parameter types and add 'this' parameter
            std::vector<Type *> substituted_param_types;
            bool param_substitution_failed = false;

            // Add the specialized enum type as the first parameter ('this' parameter)
            auto &type_context = _type_checker->get_type_context();
            Type *enum_type = type_context.lookup_enum_type(specialized_name);
            if (enum_type)
            {
                // Create pointer to the specialized enum type for 'this' parameter
                Type *enum_ptr_type = type_context.create_pointer_type(enum_type);
                substituted_param_types.push_back(enum_ptr_type);
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Added 'this' parameter for {}: {}",
                          method_name, enum_ptr_type->to_string());
            }

            // Add the original method parameters with type substitution
            for (size_t i = 0; i < original_func_type->parameter_types().size(); i++)
            {
                const auto &param_type = original_func_type->parameter_types()[i];
                std::shared_ptr<Type> substituted_param = substitute_type(param_type.get(), type_substitutions_map);
                if (!substituted_param)
                {
                    LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Failed to substitute param {} for method {}, skipping method", i, method_name);
                    param_substitution_failed = true;
                    break;
                }
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Specialized param {} for {}: {} -> {}",
                          i, method_name,
                          param_type->to_string(),
                          substituted_param->to_string());
                substituted_param_types.push_back(substituted_param.get());
            }

            if (param_substitution_failed)
            {
                continue;
            }

            // Create new function type with substituted types
            Type *specialized_func_type = type_context.create_function_type(
                substituted_return_type.get(),
                substituted_param_types,
                original_func_type->is_variadic());

            LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Created specialized function type for {}: {}",
                      method_name, specialized_func_type->to_string());

            // Register the specialized method using the new public method
            _type_checker->register_specialized_method(specialized_name, method_name, specialized_func_type);

            LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Registered specialized method: {}::{}", specialized_name, method_name);
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Completed method specialization for: {}", specialized_name);
    }

    /* TODO: Fix constructor compilation issue
    std::unique_ptr<FunctionDeclarationNode> MonomorphizationPass::specialize_function_template(
        const FunctionDeclarationNode& template_node,
        const GenericInstantiation& instantiation)
    {
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Specializing function {} -> {}", template_node.name(), instantiation.instantiated_name);

        const auto& generic_params = template_node.generic_parameters();

        // Validate parameter count
        if (generic_params.size() != instantiation.concrete_types.size())
        {
            LOG_ERROR(Cryo::LogComponent::AST, "MonomorphizationPass: Parameter count mismatch for {}", template_node.name());
            return nullptr;
        }

        // Create type substitutions map
        std::unordered_map<std::string, std::string> type_substitutions;
        for (size_t i = 0; i < generic_params.size(); ++i)
        {
            std::string param_name = generic_params[i]->name();
            type_substitutions[param_name] = instantiation.concrete_types[i];
            LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Substitution: {} -> {}", param_name, instantiation.concrete_types[i]);
        }

        // Clone the template node
        // Note: This is a simplified clone - in practice you'd need a deep copy visitor
        auto specialized = std::make_unique<FunctionDeclarationNode>(
            template_node.location(),
            generate_mangled_name(instantiation.base_name, instantiation.concrete_types),
            "void", // return_type - TODO: substitute generic types from template_node.return_type_annotation()
            false   // is_public
        );

        // TODO: Implement deep copying of the function with type substitution
        // This would involve:
        // 1. Copying parameters with substituted types
        // 2. Copying return type with substitution
        // 3. Copying function body with substituted types

        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Generated specialized function: {}", specialized->name());

        return specialized;
    }
    */

    void MonomorphizationPass::substitute_type_parameters(
        ASTNode &node,
        const std::unordered_map<std::string, std::string> &type_substitutions)
    {
        // TODO: Implement recursive type substitution
        // This would involve visiting all nodes and replacing type references

        // For now, this is a placeholder
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: TODO: Implement type substitution for node");
    }

    std::string MonomorphizationPass::substitute_type_in_string(
        const std::string &type_string,
        const std::unordered_map<std::string, std::string> &type_substitutions)
    {
        std::string result = type_string;

        // Handle array types first (T[] -> int[]) using SRM for consistent naming
        for (const auto &[generic_param, concrete_type] : type_substitutions)
        {
            // Use SRM for array type pattern construction
            auto array_id = std::make_unique<Cryo::SRM::TypeIdentifier>(
                std::vector<std::string>{}, generic_param, Cryo::TypeKind::Array);
            std::string array_pattern = generic_param + "[]";
            if (result == array_pattern)
            {
                result = concrete_type + "[]";
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Array type substitution: {} -> {}", array_pattern, result);
                return result;
            }
        }

        // Handle parameterized types (Option<T> -> Option<int>)
        for (const auto &[generic_param, concrete_type] : type_substitutions)
        {
            size_t pos = 0;
            std::string search_pattern = "<" + generic_param + ">";
            std::string replacement = "<" + concrete_type + ">";

            while ((pos = result.find(search_pattern, pos)) != std::string::npos)
            {
                result.replace(pos, search_pattern.length(), replacement);
                pos += replacement.length();
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Parameterized type substitution: {} -> {}", generic_param, concrete_type);
            }
        }

        // Simple substitution - replace exact matches
        for (const auto &[generic_param, concrete_type] : type_substitutions)
        {
            if (result == generic_param)
            {
                result = concrete_type;
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Simple type substitution: {} -> {}", generic_param, concrete_type);
                break;
            }
        }

        return result;
    }

    std::unique_ptr<EnumDeclarationNode> MonomorphizationPass::specialize_enum_template_from_metadata(
        const TemplateRegistry::TemplateMetadata &metadata,
        const GenericInstantiation &instantiation)
    {
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Specializing enum from metadata: {} -> {}", metadata.name, instantiation.instantiated_name);

        // Validate parameter count using metadata
        if (metadata.generic_parameter_names.size() != instantiation.concrete_types.size())
        {
            LOG_ERROR(Cryo::LogComponent::AST, "MonomorphizationPass: Parameter count mismatch for {} (metadata: {}, instantiation: {})", metadata.name, metadata.generic_parameter_names.size(), instantiation.concrete_types.size());
            return nullptr;
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Creating type substitutions map from metadata...");
        // Create type substitutions map using metadata
        std::unordered_map<std::string, std::string> type_substitutions;
        for (size_t i = 0; i < metadata.generic_parameter_names.size(); ++i)
        {
            const std::string &param_name = metadata.generic_parameter_names[i];
            const std::string &concrete_type = instantiation.concrete_types[i];

            type_substitutions[param_name] = concrete_type;
            LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Substitution: {} -> {}", param_name, concrete_type);
        }

        // Create a basic specialized enum (for now, just a stub)
        // TODO: Implement full enum specialization with variants
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Creating specialized enum: {}", instantiation.instantiated_name);

        auto specialized = std::make_unique<EnumDeclarationNode>(
            SourceLocation{}, instantiation.instantiated_name);

        // TODO: For now, we can't copy variants from metadata since we don't have access to the original template node
        // This metadata-based function should ideally be replaced with direct template access
        // But we can at least create the enum structure properly
        LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Metadata-based enum specialization creates empty enum - variants not copied");

        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Specialized enum created successfully");
        return specialized;
    }

    std::unique_ptr<ClassDeclarationNode> MonomorphizationPass::specialize_class_template_from_metadata(
        const TemplateRegistry::TemplateMetadata &metadata,
        const GenericInstantiation &instantiation)
    {
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Specializing class from metadata: {} -> {}", metadata.name, instantiation.instantiated_name);

        // Validate parameter count
        if (metadata.generic_parameter_names.size() != instantiation.concrete_types.size())
        {
            LOG_ERROR(Cryo::LogComponent::AST, "MonomorphizationPass: Parameter count mismatch for {}. Expected {}, got {}", metadata.name, metadata.generic_parameter_names.size(), instantiation.concrete_types.size());
            return nullptr;
        }

        // Create type substitutions map from metadata
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Creating type substitutions map from metadata...");
        std::unordered_map<std::string, std::string> type_substitutions;
        for (size_t i = 0; i < metadata.generic_parameter_names.size(); ++i)
        {
            type_substitutions[metadata.generic_parameter_names[i]] = instantiation.concrete_types[i];
            LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Substitution: {} -> {}", metadata.generic_parameter_names[i], instantiation.concrete_types[i]);
        }

        // Create a basic specialized class (for now, just a stub)
        // TODO: Implement full class specialization with fields and methods
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Creating specialized class: {}", instantiation.instantiated_name);

        auto specialized = std::make_unique<ClassDeclarationNode>(
            SourceLocation{}, instantiation.instantiated_name);

        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Specialized class created successfully");
        return specialized;
    }

    std::unique_ptr<StructDeclarationNode> MonomorphizationPass::specialize_struct_template_from_metadata(
        const TemplateRegistry::TemplateMetadata &metadata,
        const GenericInstantiation &instantiation)
    {
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Specializing struct from metadata: {} -> {}", metadata.name, instantiation.instantiated_name);

        // Validate parameter count
        if (metadata.generic_parameter_names.size() != instantiation.concrete_types.size())
        {
            LOG_ERROR(Cryo::LogComponent::AST, "MonomorphizationPass: Parameter count mismatch for {}. Expected {}, got {}", metadata.name, metadata.generic_parameter_names.size(), instantiation.concrete_types.size());
            return nullptr;
        }

        // Create type substitutions map from metadata
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Creating type substitutions map from metadata...");
        std::unordered_map<std::string, std::string> type_substitutions;
        for (size_t i = 0; i < metadata.generic_parameter_names.size(); ++i)
        {
            type_substitutions[metadata.generic_parameter_names[i]] = instantiation.concrete_types[i];
            LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Substitution: {} -> {}", metadata.generic_parameter_names[i], instantiation.concrete_types[i]);
        }

        // Create a basic specialized struct (for now, just a stub)
        // TODO: Implement full struct specialization with fields and methods
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Creating specialized struct: {}", instantiation.instantiated_name);

        auto specialized = std::make_unique<StructDeclarationNode>(
            SourceLocation{}, instantiation.instantiated_name);

        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Specialized struct created successfully");
        return specialized;
    }

    std::string MonomorphizationPass::generate_mangled_name(
        const std::string &base_name,
        const std::vector<std::string> &concrete_types)
    {
        // Generate a mangled name like Array_int or HashMap_string_int
        std::ostringstream mangled;
        mangled << base_name;

        for (const auto &type : concrete_types)
        {
            mangled << "_" << type;
        }

        return mangled.str();
    }

    std::unique_ptr<BlockStatementNode> MonomorphizationPass::clone_method_body_with_substitution(
        BlockStatementNode *original_body,
        const std::unordered_map<std::string, std::string> &type_substitutions)
    {
        if (!original_body)
        {
            return nullptr;
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Cloning method body with {} statements", original_body->statements().size());

        // Create a new block statement
        auto cloned_body = std::make_unique<BlockStatementNode>(original_body->location());

        // Clone each statement in the body
        for (const auto &statement : original_body->statements())
        {
            if (statement)
            {
                auto cloned_statement = clone_statement_with_substitution(statement.get(), type_substitutions);
                if (cloned_statement)
                {
                    cloned_body->add_statement(std::move(cloned_statement));
                }
                else
                {
                    LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Failed to clone statement in method body");
                }
            }
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Successfully cloned method body with {} statements", cloned_body->statements().size());

        return cloned_body;
    }

    std::unique_ptr<StatementNode> MonomorphizationPass::clone_statement_with_substitution(
        StatementNode *original_statement,
        const std::unordered_map<std::string, std::string> &type_substitutions)
    {
        if (!original_statement)
        {
            return nullptr;
        }

        // For now, we'll implement a basic cloning that handles the most common statement types
        // This is a simplified implementation - a full implementation would need a visitor pattern

        switch (original_statement->kind())
        {
        case NodeKind::DeclarationStatement:
        {
            auto *decl_stmt = static_cast<DeclarationStatementNode *>(original_statement);
            auto *decl = decl_stmt->declaration();

            if (decl && decl->kind() == NodeKind::VariableDeclaration)
            {
                auto *var_decl = static_cast<VariableDeclarationNode *>(decl);

                // Apply type substitution to the variable's type annotation
                std::string original_type = var_decl->type_annotation();
                std::string substituted_type = substitute_type_in_string(original_type, type_substitutions);

                // CRITICAL FIX: Resolve the type properly
                Type *resolved_type = nullptr;
                if (_type_checker)
                {
                    resolved_type = _type_checker->resolve_type_with_generic_context(substituted_type);
                }

                // Copy initializer if present
                std::unique_ptr<ExpressionNode> cloned_initializer = nullptr;
                if (var_decl->initializer())
                {
                    cloned_initializer = clone_expression_with_substitution(var_decl->initializer(), type_substitutions);
                }

                auto cloned_var = std::make_unique<VariableDeclarationNode>(
                    var_decl->location(),
                    var_decl->name(),
                    resolved_type,                          // Properly resolved type
                    std::move(cloned_initializer),          // Cloned initializer
                    var_decl->is_mutable(),                 // Copy mutability
                    var_decl->is_global()                   // Copy global flag
                );

                // Wrap in DeclarationStatementNode
                auto cloned_decl_stmt = std::make_unique<DeclarationStatementNode>(
                    decl_stmt->location(),
                    std::move(cloned_var));

                return std::move(cloned_decl_stmt);
            }
            break;
        }

        case NodeKind::ReturnStatement:
        {
            auto *return_stmt = static_cast<ReturnStatementNode *>(original_statement);
            auto cloned_expr = clone_expression_with_substitution(return_stmt->expression(), type_substitutions);
            auto cloned_return = std::make_unique<ReturnStatementNode>(return_stmt->location(), std::move(cloned_expr));
            return std::move(cloned_return);
        }

        case NodeKind::ExpressionStatement:
        {
            auto *expr_stmt = static_cast<ExpressionStatementNode *>(original_statement);
            if (expr_stmt->expression())
            {
                auto cloned_expr = clone_expression_with_substitution(expr_stmt->expression(), type_substitutions);
                if (cloned_expr)
                {
                    auto cloned_stmt = std::make_unique<ExpressionStatementNode>(
                        expr_stmt->location(),
                        std::move(cloned_expr));
                    return std::move(cloned_stmt);
                }
            }
            break;
        }

        case NodeKind::IfStatement:
        {
            auto *if_stmt = static_cast<IfStatementNode *>(original_statement);

            // Clone condition
            auto cloned_condition = clone_expression_with_substitution(if_stmt->condition(), type_substitutions);
            if (!cloned_condition)
            {
                break;
            }

            // Clone then statement
            auto cloned_then = clone_statement_with_substitution(if_stmt->then_statement(), type_substitutions);
            if (!cloned_then)
            {
                break;
            }

            // Clone else statement (if it exists)
            std::unique_ptr<StatementNode> cloned_else = nullptr;
            if (if_stmt->else_statement())
            {
                cloned_else = clone_statement_with_substitution(if_stmt->else_statement(), type_substitutions);
            }

            return std::make_unique<IfStatementNode>(
                if_stmt->location(),
                std::move(cloned_condition),
                std::move(cloned_then),
                std::move(cloned_else));
        }

        case NodeKind::MatchStatement:
        {
            auto *match_stmt = static_cast<MatchStatementNode *>(original_statement);
            auto location = match_stmt->location();

            // Clone the match expression
            auto cloned_expr = clone_expression_with_substitution(match_stmt->expr(), type_substitutions);
            auto cloned_match = std::make_unique<MatchStatementNode>(location, std::move(cloned_expr));

            // Clone each match arm
            for (const auto &arm : match_stmt->arms())
            {
                auto arm_location = arm->location();

                // Clone the pattern with type substitution
                std::unique_ptr<PatternNode> cloned_pattern;
                if (auto *enum_pattern = dynamic_cast<EnumPatternNode *>(arm->pattern()))
                {
                    // Apply type substitution to the enum name
                    std::string original_enum_name = enum_pattern->enum_name();
                    std::string substituted_enum_name = original_enum_name;

                    // Apply type substitutions to the enum name
                    for (const auto &sub : type_substitutions)
                    {
                        if (original_enum_name == sub.first)
                        {
                            substituted_enum_name = sub.second;
                            break;
                        }
                    }

                    // Create the substituted enum pattern
                    auto substituted_pattern = std::make_unique<EnumPatternNode>(arm_location, substituted_enum_name, enum_pattern->variant_name());

                    // Copy pattern elements (bindings, wildcards, literals)
                    for (const auto &elem : enum_pattern->pattern_elements())
                    {
                        substituted_pattern->add_pattern_element(elem);
                    }

                    cloned_pattern = std::move(substituted_pattern);
                }
                else
                {
                    // For non-enum patterns, create a simple placeholder for now
                    cloned_pattern = std::make_unique<EnumPatternNode>(arm_location, "Placeholder", "Pattern");
                }

                // Clone the arm body
                auto cloned_body = clone_statement_with_substitution(arm->body(), type_substitutions);

                auto cloned_arm = std::make_unique<MatchArmNode>(arm_location, std::move(cloned_pattern), std::move(cloned_body));
                cloned_match->add_arm(std::move(cloned_arm));
            }

            return std::move(cloned_match);
        }

        // Add more statement types as needed
        default:
            LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Unsupported statement type for cloning: {}", static_cast<int>(original_statement->kind()));
            break;
        }

        return nullptr;
    }

    std::unique_ptr<ExpressionNode> MonomorphizationPass::clone_expression_with_substitution(
        ExpressionNode *original_expr,
        const std::unordered_map<std::string, std::string> &type_substitutions)
    {
        if (!original_expr)
        {
            return nullptr;
        }

        // Implement basic expression cloning - this is a simplified version
        switch (original_expr->kind())
        {
        case NodeKind::Literal:
        {
            auto *literal = static_cast<LiteralNode *>(original_expr);
            return std::make_unique<LiteralNode>(
                NodeKind::Literal,
                literal->location(),
                literal->value(),
                literal->literal_kind());
        }

        case NodeKind::Identifier:
        {
            auto *identifier = static_cast<IdentifierNode *>(original_expr);
            return std::make_unique<IdentifierNode>(
                NodeKind::Identifier,
                identifier->location(),
                identifier->name());
        }

        case NodeKind::BinaryExpression:
        {
            auto *binary_expr = static_cast<BinaryExpressionNode *>(original_expr);

            auto cloned_left = clone_expression_with_substitution(binary_expr->left(), type_substitutions);
            auto cloned_right = clone_expression_with_substitution(binary_expr->right(), type_substitutions);

            if (cloned_left && cloned_right)
            {
                return std::make_unique<BinaryExpressionNode>(
                    NodeKind::BinaryExpression,
                    binary_expr->location(),
                    binary_expr->operator_token(),
                    std::move(cloned_left),
                    std::move(cloned_right));
            }
            break;
        }

        case NodeKind::UnaryExpression:
        {
            auto *unary_expr = static_cast<UnaryExpressionNode *>(original_expr);

            auto cloned_operand = clone_expression_with_substitution(unary_expr->operand(), type_substitutions);
            if (cloned_operand)
            {
                return std::make_unique<UnaryExpressionNode>(
                    NodeKind::UnaryExpression,
                    unary_expr->location(),
                    unary_expr->operator_token(),
                    std::move(cloned_operand));
            }
            break;
        }

        case NodeKind::ArrayAccess:
        {
            auto *array_access = static_cast<ArrayAccessNode *>(original_expr);

            auto cloned_array = clone_expression_with_substitution(array_access->array(), type_substitutions);
            auto cloned_index = clone_expression_with_substitution(array_access->index(), type_substitutions);

            if (cloned_array && cloned_index)
            {
                return std::make_unique<ArrayAccessNode>(
                    array_access->location(),
                    std::move(cloned_array),
                    std::move(cloned_index));
            }
            break;
        }

        case NodeKind::MemberAccess:
        {
            auto *member_access = static_cast<MemberAccessNode *>(original_expr);

            auto cloned_object = clone_expression_with_substitution(member_access->object(), type_substitutions);
            if (cloned_object)
            {
                return std::make_unique<MemberAccessNode>(
                    member_access->location(),
                    std::move(cloned_object),
                    member_access->member());
            }
            break;
        }

        case NodeKind::CallExpression:
        {
            auto *call_expr = static_cast<CallExpressionNode *>(original_expr);

            // Clone the function/method being called
            auto cloned_function = clone_expression_with_substitution(call_expr->callee(), type_substitutions);
            if (!cloned_function)
            {
                break;
            }

            // Create new call expression
            auto cloned_call = std::make_unique<CallExpressionNode>(
                call_expr->location(),
                std::move(cloned_function));

            // Clone arguments
            for (const auto &arg : call_expr->arguments())
            {
                if (arg)
                {
                    auto cloned_arg = clone_expression_with_substitution(arg.get(), type_substitutions);
                    if (cloned_arg)
                    {
                        cloned_call->add_argument(std::move(cloned_arg));
                    }
                }
            }

            return std::move(cloned_call);
        }

        case NodeKind::ScopeResolution:
        {
            auto *scope_res = static_cast<ScopeResolutionNode *>(original_expr);

            // For scope resolution (like Option<T>::None), we need to apply type substitutions
            std::string scope_name = scope_res->scope_name();
            std::string member_name = scope_res->member_name();

            // Apply type substitutions to the scope name
            for (const auto &substitution : type_substitutions)
            {
                // Replace template parameter in scope name
                size_t pos = 0;
                while ((pos = scope_name.find(substitution.first, pos)) != std::string::npos)
                {
                    scope_name.replace(pos, substitution.first.length(), substitution.second);
                    pos += substitution.second.length();
                }
            }

            return std::make_unique<ScopeResolutionNode>(
                scope_res->location(),
                scope_name,
                member_name);
        }

        // Add more expression types as needed (simplified for now)
        default:
            LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Unsupported expression type for cloning: {} ({})", static_cast<int>(original_expr->kind()), NodeKindToString(original_expr->kind()));
            // For unsupported expressions, try to return null instead of creating a problematic identifier
            // This allows graceful degradation instead of causing undefined identifier errors
            return nullptr;
        }

        return nullptr;
    }

    std::unordered_map<std::string, std::shared_ptr<Type>> MonomorphizationPass::convert_to_type_substitutions(
        const std::unordered_map<std::string, std::string> &string_substitutions)
    {
        std::unordered_map<std::string, std::shared_ptr<Type>> type_substitutions;

        if (!_type_checker)
        {
            LOG_ERROR(Cryo::LogComponent::AST, "MonomorphizationPass: TypeChecker not available for type resolution");
            return type_substitutions;
        }

        auto &type_context = _type_checker->get_type_context();

        for (const auto &[param_name, type_string] : string_substitutions)
        {
            Type *resolved_type = nullptr;

            // Handle primitive types first
            if (type_string == "int" || type_string == "i32")
            {
                resolved_type = type_context.get_integer_type(IntegerKind::I32, true);
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Resolved primitive type '{}' -> IntegerType", type_string);
            }
            else if (type_string == "i8")
            {
                resolved_type = type_context.get_integer_type(IntegerKind::I8, true);
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Resolved primitive type '{}' -> I8", type_string);
            }
            else if (type_string == "i16")
            {
                resolved_type = type_context.get_integer_type(IntegerKind::I16, true);
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Resolved primitive type '{}' -> I16", type_string);
            }
            else if (type_string == "i64")
            {
                resolved_type = type_context.get_integer_type(IntegerKind::I64, true);
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Resolved primitive type '{}' -> I64", type_string);
            }
            else if (type_string == "u8")
            {
                resolved_type = type_context.get_integer_type(IntegerKind::I8, false);
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Resolved primitive type '{}' -> U8", type_string);
            }
            else if (type_string == "u16")
            {
                resolved_type = type_context.get_integer_type(IntegerKind::I16, false);
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Resolved primitive type '{}' -> U16", type_string);
            }
            else if (type_string == "u32")
            {
                resolved_type = type_context.get_integer_type(IntegerKind::I32, false);
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Resolved primitive type '{}' -> U32", type_string);
            }
            else if (type_string == "u64")
            {
                resolved_type = type_context.get_integer_type(IntegerKind::I64, false);
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Resolved primitive type '{}' -> U64", type_string);
            }
            else if (type_string == "char")
            {
                resolved_type = type_context.get_char_type();
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Resolved primitive type '{}' -> CharType", type_string);
            }
            else if (type_string == "void")
            {
                resolved_type = type_context.get_void_type();
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Resolved primitive type '{}' -> VoidType", type_string);
            }
            else if (type_string == "string")
            {
                resolved_type = type_context.get_string_type();
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Resolved primitive type '{}' -> StringType", type_string);
            }
            else if (type_string == "f32" || type_string == "float")
            {
                resolved_type = type_context.get_float_type(FloatKind::F32);
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Resolved primitive type '{}' -> FloatType", type_string);
            }
            else if (type_string == "f64" || type_string == "double")
            {
                resolved_type = type_context.get_float_type(FloatKind::F64);
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Resolved primitive type '{}' -> DoubleType", type_string);
            }
            else if (type_string == "bool" || type_string == "boolean")
            {
                resolved_type = type_context.get_boolean_type();
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Resolved primitive type '{}' -> BooleanType", type_string);
            }
            else
            {
                // Try to resolve using TypeChecker symbol lookup for user-defined types
                TypedSymbol *symbol = _type_checker->lookup_symbol_in_any_namespace(type_string);
                if (symbol && symbol->type)
                {
                    resolved_type = symbol->type;
                    LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Resolved user-defined type '{}' -> {} (via symbol lookup)", type_string, symbol->type->name());
                }
                else
                {
                    // Fallback to generic type resolution
                    ParameterizedType *param_type = _type_checker->resolve_generic_type(type_string);
                    if (param_type)
                    {
                        resolved_type = param_type;
                        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Resolved complex type '{}' -> ParameterizedType", type_string);
                    }
                    else
                    {
                        LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Could not resolve type '{}' for parameter '{}'", type_string, param_name);
                    }
                }
            }

            if (resolved_type)
            {
                type_substitutions[param_name] = std::shared_ptr<Type>(resolved_type, [](Type *)
                                                                       {
                                                                           // Custom deleter that doesn't delete - Type* objects are managed by TypeContext
                                                                       });
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Successfully added type substitution: '{}' -> '{}'", param_name, type_string);
            }
        }

        return type_substitutions;
    }

    std::shared_ptr<Type> MonomorphizationPass::substitute_type(
        Type *original_type,
        const std::unordered_map<std::string, std::shared_ptr<Type>> &type_substitutions)
    {
        if (!original_type)
        {
            return nullptr;
        }

        std::string type_name = original_type->name();
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: substitute_type called with type '{}' kind={}",
                  type_name, static_cast<int>(original_type->kind()));

        // DEBUG: Print all available substitutions
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Available substitutions:");
        for (const auto &[key, value] : type_substitutions)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "  '{}' -> '{}'", key, value->name());
        }

        // If this is a generic type parameter that needs substitution
        if (original_type->kind() == TypeKind::Generic)
        {
            auto it = type_substitutions.find(type_name);
            if (it != type_substitutions.end())
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Substituting Generic type '{}' -> '{}'",
                          type_name, it->second->name());
                return it->second;
            }
            else
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: No substitution found for Generic type '{}'", type_name);
            }
        }

        // ENHANCED: Also check by name for template parameters (T, E, etc.)
        // Sometimes generic types get resolved as different TypeKinds during parsing
        auto it = type_substitutions.find(type_name);
        if (it != type_substitutions.end())
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Substituting type by name '{}' (kind={}) -> '{}' (kind={})",
                      type_name, static_cast<int>(original_type->kind()),
                      it->second->name(), static_cast<int>(it->second->kind()));
            return it->second;
        }

        // If this is a parameterized type, use its built-in substitute method
        if (original_type->kind() == TypeKind::Parameterized)
        {
            auto *param_type = static_cast<ParameterizedType *>(original_type);
            return param_type->substitute(type_substitutions);
        }

        // For other types, return the original (wrapped in shared_ptr)
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: No substitution needed for type '{}' kind={}",
                  type_name, static_cast<int>(original_type->kind()));
        return std::shared_ptr<Type>(original_type, [](Type *)
                                     {
                                         // Custom deleter that doesn't delete - Type* objects are managed by TypeContext
                                     });
    }

    std::unique_ptr<ImplementationBlockNode> MonomorphizationPass::create_specialized_implementation_block(
        const ProgramNode &program,
        const GenericInstantiation &instantiation)
    {
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Creating specialized implementation block for: {}", instantiation.instantiated_name);

        // Find the original generic implementation block
        ImplementationBlockNode *original_impl = nullptr;
        std::string generic_target_type = instantiation.base_name + "<";

        // Build the generic type pattern to match (e.g., "MyResult<T,E>")
        for (size_t i = 0; i < instantiation.concrete_types.size(); ++i)
        {
            if (i > 0)
                generic_target_type += ",";
            // Use generic parameter names (we need to find these from the original template)
            // For now, use common generic parameter names
            if (i == 0)
                generic_target_type += "T";
            else if (i == 1)
                generic_target_type += "E";
            else
                generic_target_type += "T" + std::to_string(i);
        }
        generic_target_type += ">";

        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Looking for implementation block with target: {}", generic_target_type);

        // Search through all program statements for the matching implementation block
        for (const auto &stmt : program.statements())
        {
            if (auto *impl_block = dynamic_cast<ImplementationBlockNode *>(stmt.get()))
            {
                if (impl_block->target_type() == generic_target_type)
                {
                    original_impl = impl_block;
                    LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Found original implementation block for: {}", generic_target_type);
                    break;
                }
            }
        }

        if (!original_impl)
        {
            LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: No implementation block found for: {}", generic_target_type);
            return nullptr;
        }

        // Create type substitution map
        std::unordered_map<std::string, std::string> type_substitutions;
        for (size_t i = 0; i < instantiation.concrete_types.size(); ++i)
        {
            std::string param_name;
            if (i == 0)
                param_name = "T";
            else if (i == 1)
                param_name = "E";
            else
                param_name = "T" + std::to_string(i);

            type_substitutions[param_name] = instantiation.concrete_types[i];
            LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Implementation substitution: {} -> {}", param_name, instantiation.concrete_types[i]);
        }

        // Create the specialized implementation block with the mangled target type name
        std::string specialized_target = generate_mangled_name(instantiation.base_name, instantiation.concrete_types);
        auto specialized_impl = std::make_unique<ImplementationBlockNode>(SourceLocation{}, specialized_target);

        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Creating specialized implementation for: {}", specialized_target);

        // Clone and specialize each method from the original implementation
        for (const auto &method : original_impl->method_implementations())
        {
            auto specialized_method = clone_and_substitute_method(*method, type_substitutions);
            if (specialized_method)
            {
                specialized_impl->add_method_implementation(std::move(specialized_method));
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Added specialized method: {}", method->name());
            }
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Created specialized implementation block with {} methods", specialized_impl->method_implementations().size());
        return specialized_impl;
    }

    std::unique_ptr<StructMethodNode> MonomorphizationPass::clone_and_substitute_method(
        const StructMethodNode &original_method,
        const std::unordered_map<std::string, std::string> &type_substitutions)
    {
        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Cloning and substituting method: {}", original_method.name());

        // Get the original return type and apply substitution
        Type *original_return_type = original_method.get_resolved_return_type();
        auto type_substitutions_map = convert_to_type_substitutions(type_substitutions);
        std::shared_ptr<Type> substituted_return_type = substitute_type(original_return_type, type_substitutions_map);

        // Create the specialized method with the concrete return type
        auto specialized_method = std::make_unique<StructMethodNode>(
            original_method.location(),
            original_method.name(),
            substituted_return_type.get(),
            original_method.visibility(),
            original_method.is_constructor(),
            original_method.is_destructor(),
            original_method.is_static());

        LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Substituted return type: {} -> {}",
                  (original_return_type ? original_return_type->to_string() : "unknown"),
                  (substituted_return_type ? substituted_return_type->to_string() : "unknown"));

        // Clone and substitute parameters
        for (const auto &param : original_method.parameters())
        {
            Type *original_param_type = param->get_resolved_type();
            std::shared_ptr<Type> substituted_param_type = substitute_type(original_param_type, type_substitutions_map);

            auto specialized_param = std::make_unique<VariableDeclarationNode>(
                param->location(),
                param->name(),
                substituted_param_type.get(),
                nullptr, // initializer
                param->is_mutable());
            specialized_method->add_parameter(std::move(specialized_param));
            LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Substituted parameter {}: {} -> {}",
                      param->name(),
                      (original_param_type ? original_param_type->to_string() : "unknown"),
                      (substituted_param_type ? substituted_param_type->to_string() : "unknown"));
        }

        // Clone the method body if it exists
        if (original_method.body())
        {
            // Use the existing clone method with type substitution
            auto cloned_body = clone_method_body_with_substitution(original_method.body(), type_substitutions);
            if (cloned_body)
            {
                specialized_method->set_body(std::move(cloned_body));
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Successfully cloned method body with {} statements",
                          specialized_method->body()->statements().size());
            }
            else
            {
                LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Failed to clone method body for: {}", original_method.name());
            }
        }

        return specialized_method;
    }
}