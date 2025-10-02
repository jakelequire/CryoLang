#include "AST/MonomorphizationPass.hpp"
#include "AST/TemplateRegistry.hpp"
#include "AST/ASTNode.hpp"
#include <iostream>
#include <sstream>

namespace Cryo
{
    bool MonomorphizationPass::monomorphize(ProgramNode &program,
                                            const std::vector<GenericInstantiation> &required_instantiations,
                                            const TemplateRegistry &template_registry)
    {
        std::cout << "[MonomorphizationPass] Starting monomorphization with "
                  << required_instantiations.size() << " required instantiations" << std::endl;

        bool success = true;

        for (const auto &instantiation : required_instantiations)
        {
            std::cout << "[MonomorphizationPass] Processing instantiation: "
                      << instantiation.instantiated_name << std::endl;

            // Check if we already generated this specialization
            if (_generated_specializations.find(instantiation.instantiated_name) != _generated_specializations.end())
            {
                std::cout << "[MonomorphizationPass] Specialization already exists: "
                          << instantiation.instantiated_name << std::endl;
                continue;
            }

            // Find the generic template using the template registry
            const auto *template_info = template_registry.find_template(instantiation.base_name);
            if (!template_info)
            {
                std::cerr << "[MonomorphizationPass] ERROR: Cannot find generic template: "
                          << instantiation.base_name << std::endl;
                success = false;
                continue;
            }

            std::unique_ptr<ASTNode> specialized_node = nullptr;

            // Handle different template types
            if (template_info->class_template)
            {
                std::cout << "[MonomorphizationPass] Specializing class template: "
                          << instantiation.base_name << std::endl;

                // Try to use the corrupted pointer first, fallback to metadata if it fails
                try
                {
                    if (template_info->class_template->generic_parameters().empty() ||
                        !template_info->class_template->generic_parameters()[0])
                    {
                        std::cout << "[MonomorphizationPass] Template node corrupted, using metadata" << std::endl;
                        specialized_node = specialize_class_template_from_metadata(template_info->metadata, instantiation);
                    }
                    else
                    {
                        specialized_node = specialize_class_template(*template_info->class_template, instantiation);
                    }
                }
                catch (...)
                {
                    std::cout << "[MonomorphizationPass] Template node access failed, using metadata" << std::endl;
                    specialized_node = specialize_class_template_from_metadata(template_info->metadata, instantiation);
                }
            }
            else if (template_info->struct_template)
            {
                std::cout << "[MonomorphizationPass] Specializing struct template: "
                          << instantiation.base_name << std::endl;

                // Try to use the corrupted pointer first, fallback to metadata if it fails
                try
                {
                    if (template_info->struct_template->generic_parameters().empty() ||
                        !template_info->struct_template->generic_parameters()[0])
                    {
                        std::cout << "[MonomorphizationPass] Template node corrupted, using metadata" << std::endl;
                        specialized_node = specialize_struct_template_from_metadata(template_info->metadata, instantiation);
                    }
                    else
                    {
                        specialized_node = specialize_struct_template(*template_info->struct_template, instantiation);
                    }
                }
                catch (...)
                {
                    std::cout << "[MonomorphizationPass] Template node access failed, using metadata" << std::endl;
                    specialized_node = specialize_struct_template_from_metadata(template_info->metadata, instantiation);
                }
            }
            else if (template_info->enum_template)
            {
                std::cout << "[MonomorphizationPass] Specializing enum template: "
                          << instantiation.base_name << std::endl;

                // Try to use the corrupted pointer first, fallback to metadata if it fails
                try
                {
                    if (template_info->enum_template->generic_parameters().empty() ||
                        !template_info->enum_template->generic_parameters()[0])
                    {
                        std::cout << "[MonomorphizationPass] Template node corrupted, using metadata" << std::endl;
                        specialized_node = specialize_enum_template_from_metadata(template_info->metadata, instantiation);
                    }
                    else
                    {
                        specialized_node = specialize_enum_template(*template_info->enum_template, instantiation);
                    }
                }
                catch (...)
                {
                    std::cout << "[MonomorphizationPass] Template node access failed, using metadata" << std::endl;
                    specialized_node = specialize_enum_template_from_metadata(template_info->metadata, instantiation);
                }
            }
            else if (template_info->function_template)
            {
                std::cout << "[MonomorphizationPass] Function template specialization not yet implemented: "
                          << instantiation.base_name << std::endl;
                // TODO: Implement function template specialization
                success = false;
                continue;
            }
            else if (template_info->trait_template)
            {
                std::cout << "[MonomorphizationPass] Trait template found: "
                          << instantiation.base_name << " - traits don't require concrete specialization" << std::endl;
                // Traits don't generate concrete instances, they're used for type checking
                // Skip this instantiation as it doesn't need monomorphization
                continue;
            }
            else
            {
                std::cerr << "[MonomorphizationPass] ERROR: Template info contains no valid template: "
                          << instantiation.base_name << std::endl;
                success = false;
                continue;
            }
            if (!specialized_node)
            {
                std::cerr << "[MonomorphizationPass] ERROR: Failed to specialize template: "
                          << instantiation.base_name << std::endl;
                success = false;
                continue;
            }

            // Add specialized node to program
            std::cout << "[MonomorphizationPass] Adding specialized declaration: "
                      << instantiation.instantiated_name << std::endl;

            // Store reference before moving
            _generated_specializations[instantiation.instantiated_name] = specialized_node.get();

            // Add to program (this transfers ownership)
            program.add_statement(std::move(specialized_node));
        }

        std::cout << "[MonomorphizationPass] Monomorphization completed. Generated "
                  << _generated_specializations.size() << " specializations" << std::endl;

        return success;
    }

    std::unique_ptr<ClassDeclarationNode> MonomorphizationPass::specialize_class_template(
        const ClassDeclarationNode &template_node,
        const GenericInstantiation &instantiation)
    {
        std::cout << "[MonomorphizationPass] Specializing " << template_node.name()
                  << " -> " << instantiation.instantiated_name << std::endl;

        // Create type substitution map
        std::unordered_map<std::string, std::string> type_substitutions;
        const auto &generic_params = template_node.generic_parameters();

        if (generic_params.size() != instantiation.concrete_types.size())
        {
            std::cerr << "[MonomorphizationPass] ERROR: Parameter count mismatch for "
                      << template_node.name() << std::endl;
            return nullptr;
        }

        for (size_t i = 0; i < generic_params.size(); ++i)
        {
            // Extract the parameter name from GenericParameterNode
            std::string param_name = generic_params[i]->name();
            type_substitutions[param_name] = instantiation.concrete_types[i];
            std::cout << "[MonomorphizationPass] Substitution: " << param_name
                      << " -> " << instantiation.concrete_types[i] << std::endl;
        }

        // Clone the template node
        auto specialized = std::make_unique<ClassDeclarationNode>(
            template_node.location(),
            generate_mangled_name(instantiation.base_name, instantiation.concrete_types));

        // Deep copy fields with type substitution
        std::cout << "[MonomorphizationPass] Copying " << template_node.fields().size() << " fields from template" << std::endl;
        for (const auto &field : template_node.fields())
        {
            if (field)
            {
                std::string original_type = field->type_annotation();
                std::string substituted_type = substitute_type_in_string(original_type, type_substitutions);

                // Create new field with substituted type
                auto specialized_field = std::make_unique<StructFieldNode>(
                    field->location(),
                    field->name(),
                    substituted_type,
                    field->visibility());

                specialized->add_field(std::move(specialized_field));
                std::cout << "[MonomorphizationPass] Added field: " << field->name()
                          << " : " << substituted_type << std::endl;
            }
        }

        // Deep copy methods with type substitution
        std::cout << "[MonomorphizationPass] Copying " << template_node.methods().size() << " methods from template" << std::endl;
        for (const auto &method : template_node.methods())
        {
            if (method)
            {
                // Substitute return type
                std::string original_return_type = method->return_type_annotation();
                std::string substituted_return_type = substitute_type_in_string(original_return_type, type_substitutions);

                // Create new method with substituted return type
                auto specialized_method = std::make_unique<StructMethodNode>(
                    method->location(),
                    method->name(),
                    substituted_return_type,
                    method->visibility(),
                    method->is_constructor(),
                    method->is_static());

                // Copy parameters with type substitution
                for (const auto &param : method->parameters())
                {
                    if (param)
                    {
                        std::string original_param_type = param->type_annotation();
                        std::string substituted_param_type = substitute_type_in_string(original_param_type, type_substitutions);

                        auto specialized_param = std::make_unique<VariableDeclarationNode>(
                            param->location(),
                            param->name(),
                            substituted_param_type);

                        specialized_method->add_parameter(std::move(specialized_param));
                    }
                }

                // Copy method body with type substitution
                if (method->body())
                {
                    auto cloned_body = clone_method_body_with_substitution(method->body(), type_substitutions);
                    if (cloned_body)
                    {
                        specialized_method->set_body(std::move(cloned_body));
                        std::cout << "[MonomorphizationPass] Copied method body for: " << method->name() << std::endl;
                    }
                    else
                    {
                        std::cout << "[MonomorphizationPass] WARNING: Failed to clone method body for: " << method->name() << std::endl;
                    }
                }

                specialized->add_method(std::move(specialized_method));
                std::cout << "[MonomorphizationPass] Added method: " << method->name()
                          << " -> " << substituted_return_type << std::endl;
            }
        }

        std::cout << "[MonomorphizationPass] Generated specialized class: "
                  << specialized->name() << " with " << specialized->fields().size()
                  << " fields and " << specialized->methods().size() << " methods" << std::endl;

        return specialized;
    }

    std::unique_ptr<StructDeclarationNode> MonomorphizationPass::specialize_struct_template(
        const StructDeclarationNode &template_node,
        const GenericInstantiation &instantiation)
    {
        std::cout << "[MonomorphizationPass] Specializing struct " << template_node.name()
                  << " -> " << instantiation.instantiated_name << std::endl;

        // Create type substitution map
        std::unordered_map<std::string, std::string> type_substitutions;
        const auto &generic_params = template_node.generic_parameters();

        if (generic_params.size() != instantiation.concrete_types.size())
        {
            std::cerr << "[MonomorphizationPass] ERROR: Parameter count mismatch for "
                      << template_node.name() << std::endl;
            return nullptr;
        }

        for (size_t i = 0; i < generic_params.size(); ++i)
        {
            // Extract the parameter name from GenericParameterNode
            std::string param_name = generic_params[i]->name();
            type_substitutions[param_name] = instantiation.concrete_types[i];
            std::cout << "[MonomorphizationPass] Substitution: " << param_name
                      << " -> " << instantiation.concrete_types[i] << std::endl;
        }

        // Clone the template node with field substitution
        auto specialized = std::make_unique<StructDeclarationNode>(
            template_node.location(),
            generate_mangled_name(instantiation.base_name, instantiation.concrete_types));

        // Copy and substitute fields from the template
        std::cout << "[MonomorphizationPass] Copying " << template_node.fields().size() << " fields from template" << std::endl;
        for (const auto &field : template_node.fields())
        {
            if (field)
            {
                std::string original_type = field->type_annotation();
                std::string substituted_type = substitute_type_in_string(original_type, type_substitutions);

                // Create new field with substituted type
                auto specialized_field = std::make_unique<StructFieldNode>(
                    field->location(),
                    field->name(),
                    substituted_type,
                    field->visibility());

                specialized->add_field(std::move(specialized_field));
                std::cout << "[MonomorphizationPass] Added field: " << field->name()
                          << " : " << substituted_type << std::endl;
            }
        }

        // Copy methods with type substitution (just like class specialization)
        std::cout << "[MonomorphizationPass] Copying " << template_node.methods().size() << " methods from template" << std::endl;
        for (const auto &method : template_node.methods())
        {
            if (method)
            {
                // Substitute return type
                std::string original_return_type = method->return_type_annotation();
                std::string substituted_return_type = substitute_type_in_string(original_return_type, type_substitutions);

                // Create new method with substituted return type
                auto specialized_method = std::make_unique<StructMethodNode>(
                    method->location(),
                    method->name(),
                    substituted_return_type,
                    method->visibility(),
                    method->is_constructor(),
                    method->is_static());

                // Copy parameters with type substitution
                for (const auto &param : method->parameters())
                {
                    if (param)
                    {
                        std::string original_param_type = param->type_annotation();
                        std::string substituted_param_type = substitute_type_in_string(original_param_type, type_substitutions);

                        auto specialized_param = std::make_unique<VariableDeclarationNode>(
                            param->location(),
                            param->name(),
                            substituted_param_type,
                            nullptr,
                            param->is_mutable());

                        specialized_method->add_parameter(std::move(specialized_param));
                    }
                }

                // Clone method body with type substitution
                if (method->body())
                {
                    std::cout << "[MonomorphizationPass] Cloning method body with " 
                              << method->body()->statements().size() << " statements" << std::endl;
                    auto cloned_body = clone_method_body_with_substitution(method->body(), type_substitutions);
                    if (cloned_body)
                    {
                        specialized_method->set_body(std::move(cloned_body));
                        std::cout << "[MonomorphizationPass] Successfully cloned method body with " 
                                  << specialized_method->body()->statements().size() << " statements" << std::endl;
                        std::cout << "[MonomorphizationPass] Copied method body for: " << method->name() << std::endl;
                    }
                    else
                    {
                        std::cout << "[MonomorphizationPass] WARNING: Failed to clone method body for: " << method->name() << std::endl;
                    }
                }

                specialized->add_method(std::move(specialized_method));
                std::cout << "[MonomorphizationPass] Added method: " << method->name() 
                          << " -> " << substituted_return_type << std::endl;
            }
        }

        std::cout << "[MonomorphizationPass] Generated specialized struct: "
                  << specialized->name() << " with " << specialized->fields().size() 
                  << " fields and " << specialized->methods().size() << " methods" << std::endl;

        return specialized;
    }

    std::unique_ptr<EnumDeclarationNode> MonomorphizationPass::specialize_enum_template(
        const EnumDeclarationNode &template_node,
        const GenericInstantiation &instantiation)
    {
        std::cout << "[MonomorphizationPass] Specializing enum "
                  << template_node.name() << " -> " << instantiation.instantiated_name << std::endl;

        std::cout << "[MonomorphizationPass] Getting generic parameters..." << std::endl;
        const auto &generic_params = template_node.generic_parameters();
        std::cout << "[MonomorphizationPass] Template has " << generic_params.size() << " generic parameters" << std::endl;
        std::cout << "[MonomorphizationPass] Instantiation has " << instantiation.concrete_types.size() << " concrete types" << std::endl;

        // Debug template node pointer and generic parameters
        std::cout << "[DEBUG] MonomorphizationPass: template_node name: " << template_node.name() << std::endl;
        for (size_t i = 0; i < generic_params.size(); ++i)
        {
            auto param_ptr = generic_params[i].get();
            std::cout << "[DEBUG] MonomorphizationPass: generic_params[" << i << "] = " << param_ptr << std::endl;
            if (param_ptr)
            {
                std::cout << "[DEBUG] MonomorphizationPass: parameter name: " << param_ptr->name() << std::endl;
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
                        substituted_type,
                        nullptr  // Skip initializer cloning for now
                    );
                    
                    // Wrap in DeclarationStatementNode
                    auto cloned_decl_stmt = std::make_unique<DeclarationStatementNode>(
                        decl_stmt->location(),
                        std::move(cloned_var)
                    );
                    
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
                            std::move(cloned_expr)
                        );
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
                    std::move(cloned_else)
                );
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
                    literal->literal_kind()
                );
            }
            
            case NodeKind::Identifier:
            {
                auto *identifier = static_cast<IdentifierNode *>(original_expr);
                return std::make_unique<IdentifierNode>(
                    NodeKind::Identifier,
                    identifier->location(), 
                    identifier->name()
                );
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
                        std::move(cloned_right)
                    );
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
                        std::move(cloned_index)
                    );
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
                        member_access->member()
                    );
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
                    std::move(cloned_function)
                );
                
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
                    member_name
                );
            }

            // Add more expression types as needed (simplified for now)
            default:
                std::cout << "[MonomorphizationPass] WARNING: Unsupported expression type for cloning: " 
                          << static_cast<int>(original_expr->kind()) << std::endl;
                // For unsupported expressions, return a simple identifier as fallback
                return std::make_unique<IdentifierNode>(
                    NodeKind::Identifier,
                    original_expr->location(), 
                    "UNSUPPORTED_EXPR"
                );
        }

        return nullptr;
    }
}