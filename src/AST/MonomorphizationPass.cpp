#include "AST/MonomorphizationPass.hpp"
#include "AST/TemplateRegistry.hpp"
#include "AST/TypeChecker.hpp"
#include "AST/ASTNode.hpp"
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

                // Try to use the corrupted pointer first, fallback to metadata if it fails
                try
                {
                    if (template_info->class_template->generic_parameters().empty() ||
                        !template_info->class_template->generic_parameters()[0])
                    {
                        LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Template node corrupted, using metadata");
                        specialized_node = specialize_class_template_from_metadata(template_info->metadata, instantiation);
                    }
                    else
                    {
                        specialized_node = specialize_class_template(*template_info->class_template, instantiation);
                    }
                }
                catch (...)
                {
                    LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Template node access failed, using metadata");
                    specialized_node = specialize_class_template_from_metadata(template_info->metadata, instantiation);
                }
            }
            else if (template_info->struct_template)
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Specializing struct template: {}", instantiation.base_name);

                // Try to use the corrupted pointer first, fallback to metadata if it fails
                try
                {
                    if (template_info->struct_template->generic_parameters().empty() ||
                        !template_info->struct_template->generic_parameters()[0])
                    {
                        LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Template node corrupted, using metadata");
                        specialized_node = specialize_struct_template_from_metadata(template_info->metadata, instantiation);
                    }
                    else
                    {
                        specialized_node = specialize_struct_template(*template_info->struct_template, instantiation);
                    }
                }
                catch (...)
                {
                    LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Template node access failed, using metadata");
                    specialized_node = specialize_struct_template_from_metadata(template_info->metadata, instantiation);
                }
            }
            else if (template_info->enum_template)
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Specializing enum template: {}", instantiation.base_name);

                // Try to use the corrupted pointer first, fallback to metadata if it fails
                try
                {
                    if (template_info->enum_template->generic_parameters().empty() ||
                        !template_info->enum_template->generic_parameters()[0])
                    {
                        LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Template node corrupted, using metadata");
                        specialized_node = specialize_enum_template_from_metadata(template_info->metadata, instantiation);
                    }
                    else
                    {
                        specialized_node = specialize_enum_template(*template_info->enum_template, instantiation);
                    }
                }
                catch (...)
                {
                    LOG_WARN(Cryo::LogComponent::AST, "MonomorphizationPass: Template node access failed, using metadata");
                    specialized_node = specialize_enum_template_from_metadata(template_info->metadata, instantiation);
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
        auto specialized = std::make_unique<ClassDeclarationNode>(
            template_node.location(),
            generate_mangled_name(instantiation.base_name, instantiation.concrete_types));

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
                auto specialized_method = std::make_unique<StructMethodNode>(
                    method->location(),
                    method->name(),
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
                    }
                }

                specialized->add_method(std::move(specialized_method));
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Added method: {} : {}", method->name(), (substituted_return_type ? substituted_return_type->to_string() : "unknown"));
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
        auto specialized = std::make_unique<StructDeclarationNode>(
            template_node.location(),
            generate_mangled_name(instantiation.base_name, instantiation.concrete_types));

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
                auto specialized_method = std::make_unique<StructMethodNode>(
                    method->location(),
                    method->name(),
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

                specialized->add_method(std::move(specialized_method));
                LOG_DEBUG(Cryo::LogComponent::AST, "MonomorphizationPass: Added method: {} -> {}", method->name(), substituted_return_type);
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
            LOG_TRACE(Cryo::LogComponent::AST, "MonomorphizationPass: generic_params[{}] = {}", i, (void*)param_ptr);
            if (param_ptr)
            {
                LOG_TRACE(Cryo::LogComponent::AST, "MonomorphizationPass: parameter name: {}", param_ptr->name());
            }
        }

        // Validate parameter count
        if (generic_params.size() != instantiation.concrete_types.size())
        {
            std::cerr << "[MonomorphizationPass] ERROR: Parameter count mismatch for "
                      << template_node.name() << std::endl;
            return nullptr;
        }

        std::cout << "[MonomorphizationPass] Creating type substitutions map..." << std::endl;
        // Create type substitutions map
        std::unordered_map<std::string, std::string> type_substitutions;
        for (size_t i = 0; i < generic_params.size(); ++i)
        {
            std::cout << "[MonomorphizationPass] Processing parameter " << i << std::endl;

            if (!generic_params[i])
            {
                std::cerr << "[MonomorphizationPass] ERROR: Generic parameter " << i << " is null!" << std::endl;
                return nullptr;
            }

            std::cout << "[MonomorphizationPass] Getting parameter name..." << std::endl;
            std::string param_name = generic_params[i]->name();
            std::cout << "[MonomorphizationPass] Parameter name: " << param_name << std::endl;

            if (i >= instantiation.concrete_types.size())
            {
                std::cerr << "[MonomorphizationPass] ERROR: Not enough concrete types for parameter " << i << std::endl;
                return nullptr;
            }

            type_substitutions[param_name] = instantiation.concrete_types[i];
            std::cout << "[MonomorphizationPass] Substitution: " << param_name
                      << " -> " << instantiation.concrete_types[i] << std::endl;
        }

        // Clone the template node
        // Note: This is a simplified clone - in practice you'd need a deep copy visitor
        std::cout << "[MonomorphizationPass] About to create specialized enum..." << std::endl;

        std::string mangled_name = generate_mangled_name(instantiation.base_name, instantiation.concrete_types);
        std::cout << "[MonomorphizationPass] Generated mangled name: " << mangled_name << std::endl;

        auto specialized = std::make_unique<EnumDeclarationNode>(
            template_node.location(),
            mangled_name);

        std::cout << "[MonomorphizationPass] Created EnumDeclarationNode successfully" << std::endl;

        // TODO: Implement deep copying of the enum body with type substitution
        // This would involve copying all enum variants with substituted types

        std::cout << "[MonomorphizationPass] Generated specialized enum: "
                  << specialized->name() << std::endl;

        return specialized;
    }

    /* TODO: Fix constructor compilation issue
    std::unique_ptr<FunctionDeclarationNode> MonomorphizationPass::specialize_function_template(
        const FunctionDeclarationNode& template_node,
        const GenericInstantiation& instantiation)
    {
        std::cout << "[MonomorphizationPass] Specializing function "
                  << template_node.name() << " -> " << instantiation.instantiated_name << std::endl;

        const auto& generic_params = template_node.generic_parameters();

        // Validate parameter count
        if (generic_params.size() != instantiation.concrete_types.size())
        {
            std::cerr << "[MonomorphizationPass] ERROR: Parameter count mismatch for "
                      << template_node.name() << std::endl;
            return nullptr;
        }

        // Create type substitutions map
        std::unordered_map<std::string, std::string> type_substitutions;
        for (size_t i = 0; i < generic_params.size(); ++i)
        {
            std::string param_name = generic_params[i]->name();
            type_substitutions[param_name] = instantiation.concrete_types[i];
            std::cout << "[MonomorphizationPass] Substitution: " << param_name
                      << " -> " << instantiation.concrete_types[i] << std::endl;
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

        std::cout << "[MonomorphizationPass] Generated specialized function: "
                  << specialized->name() << std::endl;

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
        std::cout << "[MonomorphizationPass] TODO: Implement type substitution for node" << std::endl;
    }

    std::string MonomorphizationPass::substitute_type_in_string(
        const std::string &type_string,
        const std::unordered_map<std::string, std::string> &type_substitutions)
    {
        std::string result = type_string;

        // Handle array types first (T[] -> int[])
        for (const auto &[generic_param, concrete_type] : type_substitutions)
        {
            std::string array_pattern = generic_param + "[]";
            if (result == array_pattern)
            {
                result = concrete_type + "[]";
                std::cout << "[MonomorphizationPass] Array type substitution: "
                          << array_pattern << " -> " << result << std::endl;
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
                std::cout << "[MonomorphizationPass] Parameterized type substitution: "
                          << generic_param << " -> " << concrete_type << std::endl;
            }
        }

        // Simple substitution - replace exact matches
        for (const auto &[generic_param, concrete_type] : type_substitutions)
        {
            if (result == generic_param)
            {
                result = concrete_type;
                std::cout << "[MonomorphizationPass] Simple type substitution: "
                          << generic_param << " -> " << concrete_type << std::endl;
                break;
            }
        }

        return result;
    }

    std::unique_ptr<EnumDeclarationNode> MonomorphizationPass::specialize_enum_template_from_metadata(
        const TemplateRegistry::TemplateMetadata &metadata,
        const GenericInstantiation &instantiation)
    {
        std::cout << "[MonomorphizationPass] Specializing enum from metadata: "
                  << metadata.name << " -> " << instantiation.instantiated_name << std::endl;

        // Validate parameter count using metadata
        if (metadata.generic_parameter_names.size() != instantiation.concrete_types.size())
        {
            std::cerr << "[MonomorphizationPass] ERROR: Parameter count mismatch for "
                      << metadata.name << " (metadata: " << metadata.generic_parameter_names.size()
                      << ", instantiation: " << instantiation.concrete_types.size() << ")" << std::endl;
            return nullptr;
        }

        std::cout << "[MonomorphizationPass] Creating type substitutions map from metadata..." << std::endl;
        // Create type substitutions map using metadata
        std::unordered_map<std::string, std::string> type_substitutions;
        for (size_t i = 0; i < metadata.generic_parameter_names.size(); ++i)
        {
            const std::string &param_name = metadata.generic_parameter_names[i];
            const std::string &concrete_type = instantiation.concrete_types[i];

            type_substitutions[param_name] = concrete_type;
            std::cout << "[MonomorphizationPass] Substitution: " << param_name
                      << " -> " << concrete_type << std::endl;
        }

        // Create a basic specialized enum (for now, just a stub)
        // TODO: Implement full enum specialization with variants
        std::cout << "[MonomorphizationPass] Creating specialized enum: " << instantiation.instantiated_name << std::endl;

        auto specialized = std::make_unique<EnumDeclarationNode>(
            SourceLocation{}, instantiation.instantiated_name);

        std::cout << "[MonomorphizationPass] Specialized enum created successfully" << std::endl;
        return specialized;
    }

    std::unique_ptr<ClassDeclarationNode> MonomorphizationPass::specialize_class_template_from_metadata(
        const TemplateRegistry::TemplateMetadata &metadata,
        const GenericInstantiation &instantiation)
    {
        std::cout << "[MonomorphizationPass] Specializing class from metadata: "
                  << metadata.name << " -> " << instantiation.instantiated_name << std::endl;

        // Validate parameter count
        if (metadata.generic_parameter_names.size() != instantiation.concrete_types.size())
        {
            std::cerr << "[MonomorphizationPass] ERROR: Parameter count mismatch for "
                      << metadata.name << ". Expected " << metadata.generic_parameter_names.size()
                      << ", got " << instantiation.concrete_types.size() << std::endl;
            return nullptr;
        }

        // Create type substitutions map from metadata
        std::cout << "[MonomorphizationPass] Creating type substitutions map from metadata..." << std::endl;
        std::unordered_map<std::string, std::string> type_substitutions;
        for (size_t i = 0; i < metadata.generic_parameter_names.size(); ++i)
        {
            type_substitutions[metadata.generic_parameter_names[i]] = instantiation.concrete_types[i];
            std::cout << "[MonomorphizationPass] Substitution: "
                      << metadata.generic_parameter_names[i] << " -> " << instantiation.concrete_types[i] << std::endl;
        }

        // Create a basic specialized class (for now, just a stub)
        // TODO: Implement full class specialization with fields and methods
        std::cout << "[MonomorphizationPass] Creating specialized class: " << instantiation.instantiated_name << std::endl;

        auto specialized = std::make_unique<ClassDeclarationNode>(
            SourceLocation{}, instantiation.instantiated_name);

        std::cout << "[MonomorphizationPass] Specialized class created successfully" << std::endl;
        return specialized;
    }

    std::unique_ptr<StructDeclarationNode> MonomorphizationPass::specialize_struct_template_from_metadata(
        const TemplateRegistry::TemplateMetadata &metadata,
        const GenericInstantiation &instantiation)
    {
        std::cout << "[MonomorphizationPass] Specializing struct from metadata: "
                  << metadata.name << " -> " << instantiation.instantiated_name << std::endl;

        // Validate parameter count
        if (metadata.generic_parameter_names.size() != instantiation.concrete_types.size())
        {
            std::cerr << "[MonomorphizationPass] ERROR: Parameter count mismatch for "
                      << metadata.name << ". Expected " << metadata.generic_parameter_names.size()
                      << ", got " << instantiation.concrete_types.size() << std::endl;
            return nullptr;
        }

        // Create type substitutions map from metadata
        std::cout << "[MonomorphizationPass] Creating type substitutions map from metadata..." << std::endl;
        std::unordered_map<std::string, std::string> type_substitutions;
        for (size_t i = 0; i < metadata.generic_parameter_names.size(); ++i)
        {
            type_substitutions[metadata.generic_parameter_names[i]] = instantiation.concrete_types[i];
            std::cout << "[MonomorphizationPass] Substitution: "
                      << metadata.generic_parameter_names[i] << " -> " << instantiation.concrete_types[i] << std::endl;
        }

        // Create a basic specialized struct (for now, just a stub)
        // TODO: Implement full struct specialization with fields and methods
        std::cout << "[MonomorphizationPass] Creating specialized struct: " << instantiation.instantiated_name << std::endl;

        auto specialized = std::make_unique<StructDeclarationNode>(
            SourceLocation{}, instantiation.instantiated_name);

        std::cout << "[MonomorphizationPass] Specialized struct created successfully" << std::endl;
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

        std::cout << "[MonomorphizationPass] Cloning method body with "
                  << original_body->statements().size() << " statements" << std::endl;

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
                    std::cout << "[MonomorphizationPass] WARNING: Failed to clone statement in method body" << std::endl;
                }
            }
        }

        std::cout << "[MonomorphizationPass] Successfully cloned method body with "
                  << cloned_body->statements().size() << " statements" << std::endl;

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

                auto cloned_var = std::make_unique<VariableDeclarationNode>(
                    var_decl->location(),
                    var_decl->name(),
                    nullptr // Will use deprecated string method for now
                );

                // TEMPORARY: Use deprecated string method until this function is converted to Type*
                cloned_var->set_type_annotation(substituted_type);

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

        // Add more statement types as needed
        default:
            std::cout << "[MonomorphizationPass] WARNING: Unsupported statement type for cloning: "
                      << static_cast<int>(original_statement->kind()) << std::endl;
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
            std::cout << "[MonomorphizationPass] WARNING: Unsupported expression type for cloning: "
                      << static_cast<int>(original_expr->kind()) << std::endl;
            // For unsupported expressions, return a simple identifier as fallback
            return std::make_unique<IdentifierNode>(
                NodeKind::Identifier,
                original_expr->location(),
                "UNSUPPORTED_EXPR");
        }

        return nullptr;
    }

    std::unordered_map<std::string, std::shared_ptr<Type>> MonomorphizationPass::convert_to_type_substitutions(
        const std::unordered_map<std::string, std::string> &string_substitutions)
    {
        std::unordered_map<std::string, std::shared_ptr<Type>> type_substitutions;

        if (!_type_checker)
        {
            std::cerr << "[MonomorphizationPass] ERROR: TypeChecker not available for type resolution" << std::endl;
            return type_substitutions;
        }

        for (const auto &[param_name, type_string] : string_substitutions)
        {
            // Try to resolve using public TypeChecker methods first
            ParameterizedType *param_type = _type_checker->resolve_generic_type(type_string);
            if (param_type)
            {
                type_substitutions[param_name] = std::shared_ptr<Type>(param_type, [](Type *)
                                                                       {
                                                                           // Custom deleter that doesn't delete - Type* objects are managed by TypeContext
                                                                       });
            }
            else
            {
                // Fallback: For basic types, we can create them directly
                // This is a temporary approach until we have better type resolution access
                Type *resolved_type = nullptr;

                // Handle basic built-in types
                if (type_string == "int" || type_string == "i32")
                {
                    // Access through TypeChecker is preferred, but as fallback create basic type
                    // Note: This needs to be improved to use proper TypeContext access
                    std::cerr << "[MonomorphizationPass] WARNING: Using fallback type resolution for: "
                              << type_string << std::endl;
                }

                if (resolved_type)
                {
                    type_substitutions[param_name] = std::shared_ptr<Type>(resolved_type, [](Type *)
                                                                           {
                                                                               // Custom deleter that doesn't delete - Type* objects are managed by TypeContext
                                                                           });
                }
                else
                {
                    std::cerr << "[MonomorphizationPass] WARNING: Could not resolve type '"
                              << type_string << "' for parameter '" << param_name << "'" << std::endl;
                }
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

        // If this is a generic type parameter that needs substitution
        if (original_type->kind() == TypeKind::Generic)
        {
            auto it = type_substitutions.find(original_type->name());
            if (it != type_substitutions.end())
            {
                return it->second;
            }
        }

        // If this is a parameterized type, use its built-in substitute method
        if (original_type->kind() == TypeKind::Parameterized)
        {
            auto *param_type = static_cast<ParameterizedType *>(original_type);
            return param_type->substitute(type_substitutions);
        }

        // For other types, return the original (wrapped in shared_ptr)
        return std::shared_ptr<Type>(original_type, [](Type *)
                                     {
                                         // Custom deleter that doesn't delete - Type* objects are managed by TypeContext
                                     });
    }
}