#pragma once

#include "AST/ASTNode.hpp"
#include "Utils/SymbolResolutionManager.hpp"
#include <memory>

/**
 * @file ASTNodeSRMExtensions.hpp
 * @brief Extensions to AST nodes for Symbol Resolution Manager integration
 * 
 * This header provides SRM integration methods for existing AST nodes without
 * modifying the original node definitions. This approach maintains backward
 * compatibility while adding the new naming capabilities.
 */

namespace Cryo::SRM {

    // Forward declarations
    class SymbolResolutionContext;
    class FunctionIdentifier;
    class TypeIdentifier;
    class QualifiedIdentifier;

    /**
     * @brief SRM extension traits for AST nodes
     * 
     * These templates provide SRM integration methods for AST nodes through
     * trait-based extensions, avoiding modification of existing node classes.
     */
    template<typename NodeType>
    struct SRMNodeExtensions {
        // Default implementation - no extensions
        static_assert(sizeof(NodeType) == 0, "SRM extensions not implemented for this node type");
    };

    /**
     * @brief SRM extensions for FunctionDeclarationNode
     */
    template<>
    struct SRMNodeExtensions<Cryo::FunctionDeclarationNode> {
        
        /**
         * @brief Create a FunctionIdentifier for this function declaration
         */
        static std::unique_ptr<FunctionIdentifier> create_function_identifier(
            const Cryo::FunctionDeclarationNode* node,
            const SymbolResolutionContext* context) {
            
            if (!node || !context) {
                return nullptr;
            }
            
            // Extract parameter types
            std::vector<Cryo::Type*> param_types;
            for (const auto& param : node->parameters()) {
                if (param && param->get_resolved_type()) {
                    param_types.push_back(param->get_resolved_type());
                }
            }
            
            // Determine function type
            FunctionIdentifier::FunctionType func_type = FunctionIdentifier::FunctionType::Regular;
            if (node->is_static()) {
                func_type = FunctionIdentifier::FunctionType::StaticMethod;
            }
            
            return context->create_function_identifier(
                node->name(),
                param_types,
                node->get_resolved_return_type(),
                func_type
            );
        }
        
        /**
         * @brief Get canonical function name using SRM
         */
        static std::string get_canonical_name(
            const Cryo::FunctionDeclarationNode* node,
            const SymbolResolutionContext* context) {
            
            auto identifier = create_function_identifier(node, context);
            return identifier ? identifier->to_string() : node->name();
        }
        
        /**
         * @brief Get LLVM-compatible function name
         */
        static std::string get_llvm_name(
            const Cryo::FunctionDeclarationNode* node,
            const SymbolResolutionContext* context) {
            
            auto identifier = create_function_identifier(node, context);
            return identifier ? identifier->to_llvm_name() : Utils::escape_for_llvm(node->name());
        }
        
        /**
         * @brief Get function signature string for overload resolution
         */
        static std::string get_signature_string(
            const Cryo::FunctionDeclarationNode* node,
            const SymbolResolutionContext* context) {
            
            auto identifier = create_function_identifier(node, context);
            return identifier ? identifier->to_signature_string() : "()";
        }
        
        /**
         * @brief Get overload key for function registry
         */
        static std::string get_overload_key(
            const Cryo::FunctionDeclarationNode* node,
            const SymbolResolutionContext* context) {
            
            auto identifier = create_function_identifier(node, context);
            return identifier ? identifier->to_overload_key() : node->name();
        }
        
        /**
         * @brief Check if this function is compatible with given argument types
         */
        static bool is_compatible_with_arguments(
            const Cryo::FunctionDeclarationNode* node,
            const std::vector<Cryo::Type*>& arg_types,
            const SymbolResolutionContext* context) {
            
            auto identifier = create_function_identifier(node, context);
            return identifier ? identifier->is_compatible_with_arguments(arg_types) : false;
        }
        
        /**
         * @brief Calculate conversion cost for overload resolution
         */
        static int calculate_conversion_cost(
            const Cryo::FunctionDeclarationNode* node,
            const std::vector<Cryo::Type*>& arg_types,
            const SymbolResolutionContext* context) {
            
            auto identifier = create_function_identifier(node, context);
            return identifier ? identifier->calculate_conversion_cost(arg_types) : -1;
        }
    };
    
    /**
     * @brief SRM extensions for StructDeclarationNode
     */
    template<>
    struct SRMNodeExtensions<Cryo::StructDeclarationNode> {
        
        /**
         * @brief Create a TypeIdentifier for this struct declaration
         */
        static std::unique_ptr<TypeIdentifier> create_type_identifier(
            const Cryo::StructDeclarationNode* node,
            const SymbolResolutionContext* context) {
            
            if (!node || !context) {
                return nullptr;
            }
            
            // Extract template parameters if any
            std::vector<Cryo::Type*> template_params;
            // TODO: Extract template parameters from generic_parameters
            
            return context->create_type_identifier(
                node->name(),
                Cryo::TypeKind::Struct,
                template_params
            );
        }
        
        /**
         * @brief Get canonical struct name using SRM
         */
        static std::string get_canonical_name(
            const Cryo::StructDeclarationNode* node,
            const SymbolResolutionContext* context) {
            
            auto identifier = create_type_identifier(node, context);
            return identifier ? identifier->to_canonical_name() : node->name();
        }
        
        /**
         * @brief Get LLVM struct type name
         */
        static std::string get_llvm_struct_name(
            const Cryo::StructDeclarationNode* node,
            const SymbolResolutionContext* context) {
            
            auto identifier = create_type_identifier(node, context);
            return identifier ? identifier->to_llvm_struct_name() : Utils::escape_for_llvm(node->name());
        }
        
        /**
         * @brief Create constructor identifier for this struct
         */
        static std::unique_ptr<FunctionIdentifier> create_constructor_identifier(
            const Cryo::StructDeclarationNode* node,
            const std::vector<Cryo::Type*>& param_types,
            const SymbolResolutionContext* context) {
            
            auto type_id = create_type_identifier(node, context);
            return type_id ? type_id->create_constructor_identifier(param_types) : nullptr;
        }
        
        /**
         * @brief Get constructor name for this struct
         */
        static std::string get_constructor_name(
            const Cryo::StructDeclarationNode* node,
            const std::vector<Cryo::Type*>& param_types,
            const SymbolResolutionContext* context) {
            
            auto constructor_id = create_constructor_identifier(node, param_types, context);
            return constructor_id ? constructor_id->to_constructor_name() : 
                   (node->name() + "::" + node->name());
        }
        
        /**
         * @brief Create method identifier for a struct method
         */
        static std::unique_ptr<FunctionIdentifier> create_method_identifier(
            const Cryo::StructDeclarationNode* node,
            const std::string& method_name,
            const std::vector<Cryo::Type*>& param_types,
            Cryo::Type* return_type,
            bool is_static,
            const SymbolResolutionContext* context) {
            
            auto type_id = create_type_identifier(node, context);
            return type_id ? type_id->create_method_identifier(method_name, param_types, return_type, is_static) : nullptr;
        }
        
        /**
         * @brief Get qualified method name
         */
        static std::string get_method_name(
            const Cryo::StructDeclarationNode* node,
            const std::string& method_name,
            const std::vector<Cryo::Type*>& param_types,
            Cryo::Type* return_type,
            bool is_static,
            const SymbolResolutionContext* context) {
            
            auto method_id = create_method_identifier(node, method_name, param_types, return_type, is_static, context);
            return method_id ? method_id->to_string() : (node->name() + "::" + method_name);
        }
    };
    
    /**
     * @brief SRM extensions for ClassDeclarationNode
     */
    template<>
    struct SRMNodeExtensions<Cryo::ClassDeclarationNode> {
        
        /**
         * @brief Create a TypeIdentifier for this class declaration
         */
        static std::unique_ptr<TypeIdentifier> create_type_identifier(
            const Cryo::ClassDeclarationNode* node,
            const SymbolResolutionContext* context) {
            
            if (!node || !context) {
                return nullptr;
            }
            
            // Extract template parameters if any
            std::vector<Cryo::Type*> template_params;
            // TODO: Extract template parameters from generic_parameters
            
            return context->create_type_identifier(
                node->name(),
                Cryo::TypeKind::Class,
                template_params
            );
        }
        
        /**
         * @brief Get canonical class name using SRM
         */
        static std::string get_canonical_name(
            const Cryo::ClassDeclarationNode* node,
            const SymbolResolutionContext* context) {
            
            auto identifier = create_type_identifier(node, context);
            return identifier ? identifier->to_canonical_name() : node->name();
        }
        
        /**
         * @brief Get LLVM class type name
         */
        static std::string get_llvm_class_name(
            const Cryo::ClassDeclarationNode* node,
            const SymbolResolutionContext* context) {
            
            auto identifier = create_type_identifier(node, context);
            return identifier ? identifier->to_llvm_struct_name() : Utils::escape_for_llvm(node->name());
        }
        
        /**
         * @brief Create constructor identifier for this class
         */
        static std::unique_ptr<FunctionIdentifier> create_constructor_identifier(
            const Cryo::ClassDeclarationNode* node,
            const std::vector<Cryo::Type*>& param_types,
            const SymbolResolutionContext* context) {
            
            auto type_id = create_type_identifier(node, context);
            return type_id ? type_id->create_constructor_identifier(param_types) : nullptr;
        }
        
        /**
         * @brief Create destructor identifier for this class
         */
        static std::unique_ptr<FunctionIdentifier> create_destructor_identifier(
            const Cryo::ClassDeclarationNode* node,
            const SymbolResolutionContext* context) {
            
            auto type_id = create_type_identifier(node, context);
            return type_id ? type_id->create_destructor_identifier() : nullptr;
        }
        
        /**
         * @brief Get constructor name for this class
         */
        static std::string get_constructor_name(
            const Cryo::ClassDeclarationNode* node,
            const std::vector<Cryo::Type*>& param_types,
            const SymbolResolutionContext* context) {
            
            auto constructor_id = create_constructor_identifier(node, param_types, context);
            return constructor_id ? constructor_id->to_constructor_name() : 
                   (node->name() + "::" + node->name());
        }
        
        /**
         * @brief Get destructor name for this class
         */
        static std::string get_destructor_name(
            const Cryo::ClassDeclarationNode* node,
            const SymbolResolutionContext* context) {
            
            auto destructor_id = create_destructor_identifier(node, context);
            return destructor_id ? destructor_id->to_destructor_name() : ("~" + node->name());
        }
        
        /**
         * @brief Create method identifier for a class method
         */
        static std::unique_ptr<FunctionIdentifier> create_method_identifier(
            const Cryo::ClassDeclarationNode* node,
            const std::string& method_name,
            const std::vector<Cryo::Type*>& param_types,
            Cryo::Type* return_type,
            bool is_static,
            const SymbolResolutionContext* context) {
            
            auto type_id = create_type_identifier(node, context);
            return type_id ? type_id->create_method_identifier(method_name, param_types, return_type, is_static) : nullptr;
        }
        
        /**
         * @brief Get qualified method name
         */
        static std::string get_method_name(
            const Cryo::ClassDeclarationNode* node,
            const std::string& method_name,
            const std::vector<Cryo::Type*>& param_types,
            Cryo::Type* return_type,
            bool is_static,
            const SymbolResolutionContext* context) {
            
            auto method_id = create_method_identifier(node, method_name, param_types, return_type, is_static, context);
            return method_id ? method_id->to_string() : (node->name() + "::" + method_name);
        }
    };
    
    /**
     * @brief SRM extensions for CallExpressionNode
     */
    template<>
    struct SRMNodeExtensions<Cryo::CallExpressionNode> {
        
        /**
         * @brief Create function identifier from call expression
         */
        static std::unique_ptr<FunctionIdentifier> create_function_identifier_from_call(
            const Cryo::CallExpressionNode* node,
            const SymbolResolutionContext* context) {
            
            if (!node || !context) {
                return nullptr;
            }
            
            // Extract function name from callee
            std::string function_name;
            if (auto* identifier_node = dynamic_cast<const Cryo::IdentifierNode*>(node->callee())) {
                function_name = identifier_node->name();
            } else if (auto* member_access = dynamic_cast<const Cryo::MemberAccessNode*>(node->callee())) {
                function_name = member_access->member();
            } else {
                return nullptr;
            }
            
            // Extract argument types
            std::vector<Cryo::Type*> arg_types;
            for (const auto& arg : node->arguments()) {
                if (arg && arg->get_resolved_type()) {
                    arg_types.push_back(arg->get_resolved_type());
                }
            }
            
            return context->create_function_identifier(
                function_name,
                arg_types,
                nullptr, // Return type unknown from call site
                FunctionIdentifier::FunctionType::Regular
            );
        }
        
        /**
         * @brief Get function name for resolution
         */
        static std::string get_function_name_for_resolution(
            const Cryo::CallExpressionNode* node,
            const SymbolResolutionContext* context) {
            
            auto identifier = create_function_identifier_from_call(node, context);
            return identifier ? identifier->to_overload_key() : "";
        }
        
        /**
         * @brief Check if this call matches a function signature
         */
        static bool matches_function_signature(
            const Cryo::CallExpressionNode* node,
            const FunctionIdentifier& function_id,
            const SymbolResolutionContext* context) {
            
            auto call_id = create_function_identifier_from_call(node, context);
            if (!call_id) return false;
            
            // Extract argument types for compatibility check
            std::vector<Cryo::Type*> arg_types;
            for (const auto& arg : node->arguments()) {
                if (arg && arg->get_resolved_type()) {
                    arg_types.push_back(arg->get_resolved_type());
                }
            }
            
            return function_id.is_compatible_with_arguments(arg_types);
        }
    };
    
    // Convenience functions for easier usage
    namespace Extensions {
        
        /**
         * @brief Get SRM identifier for any AST node (SFINAE-based)
         */
        template<typename NodeType>
        auto get_srm_identifier(const NodeType* node, const SymbolResolutionContext* context)
            -> decltype(SRMNodeExtensions<NodeType>::create_function_identifier(node, context)) {
            return SRMNodeExtensions<NodeType>::create_function_identifier(node, context);
        }
        
        template<typename NodeType>
        auto get_srm_identifier(const NodeType* node, const SymbolResolutionContext* context)
            -> decltype(SRMNodeExtensions<NodeType>::create_type_identifier(node, context)) {
            return SRMNodeExtensions<NodeType>::create_type_identifier(node, context);
        }
        
        /**
         * @brief Get canonical name for any AST node
         */
        template<typename NodeType>
        std::string get_canonical_name(const NodeType* node, const SymbolResolutionContext* context) {
            return SRMNodeExtensions<NodeType>::get_canonical_name(node, context);
        }
        
        /**
         * @brief Get LLVM name for any AST node
         */
        template<typename NodeType>
        std::string get_llvm_name(const NodeType* node, const SymbolResolutionContext* context) {
            if constexpr (std::is_same_v<NodeType, Cryo::FunctionDeclarationNode>) {
                return SRMNodeExtensions<NodeType>::get_llvm_name(node, context);
            } else if constexpr (std::is_same_v<NodeType, Cryo::StructDeclarationNode>) {
                return SRMNodeExtensions<NodeType>::get_llvm_struct_name(node, context);
            } else if constexpr (std::is_same_v<NodeType, Cryo::ClassDeclarationNode>) {
                return SRMNodeExtensions<NodeType>::get_llvm_class_name(node, context);
            } else {
                return Utils::escape_for_llvm(node->name());
            }
        }
        
        /**
         * @brief Create constructor identifier for struct/class nodes
         */
        template<typename NodeType>
        std::unique_ptr<FunctionIdentifier> create_constructor_identifier(
            const NodeType* node,
            const std::vector<Cryo::Type*>& param_types,
            const SymbolResolutionContext* context) {
            return SRMNodeExtensions<NodeType>::create_constructor_identifier(node, param_types, context);
        }
        
        /**
         * @brief Get constructor name for struct/class nodes
         */
        template<typename NodeType>
        std::string get_constructor_name(
            const NodeType* node,
            const std::vector<Cryo::Type*>& param_types,
            const SymbolResolutionContext* context) {
            return SRMNodeExtensions<NodeType>::get_constructor_name(node, param_types, context);
        }
        
    } // namespace Extensions
    
} // namespace Cryo::SRM

// Convenience macros for cleaner usage in existing code
#define SRM_GET_CANONICAL_NAME(node, context) \
    Cryo::SRM::Extensions::get_canonical_name(node, context)

#define SRM_GET_LLVM_NAME(node, context) \
    Cryo::SRM::Extensions::get_llvm_name(node, context)

#define SRM_GET_CONSTRUCTOR_NAME(node, param_types, context) \
    Cryo::SRM::Extensions::get_constructor_name(node, param_types, context)

#define SRM_CREATE_IDENTIFIER(node, context) \
    Cryo::SRM::Extensions::get_srm_identifier(node, context)