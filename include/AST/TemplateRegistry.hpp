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
     * Monomorphizer to find templates from any imported module.
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

        // Struct field types info: used to define struct bodies when processing imported modules
        struct StructFieldInfo
        {
            std::vector<std::string> field_names;
            std::vector<TypeRef> field_types;
            std::vector<std::string> field_type_annotations; // Raw annotation strings for fallback resolution
            std::string source_namespace; // The namespace where the struct was originally defined
        };

        // Method metadata extracted from AST at registration time
        // This persists after AST cleanup and is used for cross-module type checking
        struct MethodMetadata
        {
            std::string name;
            std::string return_type_annotation;
            std::vector<std::string> parameter_type_annotations;
            std::vector<std::string> parameter_names;
            bool is_static = false;

            MethodMetadata() = default;
            MethodMetadata(const std::string &n, const std::string &ret,
                           const std::vector<std::string> &param_types,
                           const std::vector<std::string> &param_names,
                           bool is_static_method = false)
                : name(n), return_type_annotation(ret),
                  parameter_type_annotations(param_types), parameter_names(param_names),
                  is_static(is_static_method) {}
        };

        // Template method metadata: template_name -> list of methods
        // This stores method signatures extracted at registration time
        struct TemplateMethodInfo
        {
            std::string module_namespace;
            std::vector<MethodMetadata> methods;
        };

        // Module-level constant metadata for cross-module generic instantiation
        struct ModuleConstant
        {
            std::string name;
            std::string type_annotation; // e.g., "u8", "u64", "f64"
            uint64_t int_value;
            double float_value;
            bool is_integer;

            ModuleConstant() : int_value(0), float_value(0.0), is_integer(false) {}
            ModuleConstant(const std::string &n, const std::string &type_ann,
                           uint64_t val, bool is_int = true)
                : name(n), type_annotation(type_ann), int_value(val), float_value(0.0), is_integer(is_int) {}
            ModuleConstant(const std::string &n, const std::string &type_ann,
                           double val)
                : name(n), type_annotation(type_ann), int_value(0), float_value(val), is_integer(false) {}
        };

        //===================================================================
        // Module Constants Registry (for cross-module generic instantiation)
        //===================================================================

        /**
         * @brief Register a module-level constant for cross-module generic method access
         * @param module_namespace Namespace of the module defining the constant
         * @param name Constant name (e.g., "BUCKET_EMPTY")
         * @param type_annotation Type annotation string (e.g., "u8")
         * @param value Integer value of the constant
         */
        void register_module_constant(const std::string &module_namespace,
                                      const std::string &name,
                                      const std::string &type_annotation,
                                      uint64_t value);

        /** @brief Register a floating-point module constant */
        void register_module_constant_float(const std::string &module_namespace,
                                            const std::string &name,
                                            const std::string &type_annotation,
                                            double value);

        /**
         * @brief Get all constants for a given module namespace
         * @param module_namespace Namespace to look up
         * @return Pointer to vector of ModuleConstant, or nullptr if none registered
         */
        const std::vector<ModuleConstant> *get_module_constants(const std::string &module_namespace) const;

    private:
        // Map from template base name to template info
        std::unordered_map<std::string, TemplateInfo> _templates;

        // Method return type registry: fully qualified method name -> return type
        // Used for cross-module extern declarations when impl blocks aren't directly accessible
        std::unordered_map<std::string, TypeRef> _method_return_types;

        // Struct field types registry: qualified struct name -> StructFieldInfo
        std::unordered_map<std::string, StructFieldInfo> _struct_field_types;

        // Template method metadata registry: template_name -> TemplateMethodInfo
        // Stores method signatures extracted at registration time, persists after AST cleanup
        std::unordered_map<std::string, TemplateMethodInfo> _template_method_info;

        // Method return type annotations: qualified method name -> return type annotation string
        // Used for cross-module extern declarations when TypeRef resolution fails
        std::unordered_map<std::string, std::string> _method_return_type_annotations;

        // Method static flags: qualified method name -> is_static
        // Used for cross-module extern declarations to know whether to add 'this' parameter
        std::unordered_map<std::string, bool> _method_is_static;

        // Enum implementation blocks: base enum name -> impl block
        // Stores references to implementation blocks for generic enums
        // Used to generate methods during enum instantiation
        std::unordered_map<std::string, ImplementationBlockNode *> _enum_impl_blocks;

        // Struct implementation blocks: base struct name -> impl block
        // Stores references to implementation blocks for generic structs
        // Used to generate methods during struct instantiation
        std::unordered_map<std::string, ImplementationBlockNode *> _struct_impl_blocks;

        // Module-level constants: namespace -> list of constants
        // Used to forward constants to consumer modules during cross-module generic instantiation
        std::unordered_map<std::string, std::vector<ModuleConstant>> _module_constants;

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
        void register_method_return_type(const std::string &qualified_method_name, TypeRef return_type);

        /**
         * @brief Get a method's return type for extern declaration creation
         * @param qualified_method_name Fully qualified method name
         * @return The return type, or nullptr if not registered
         */
        TypeRef get_method_return_type(const std::string &qualified_method_name) const;

        /**
         * @brief Check if a method's return type is registered
         * @param qualified_method_name Fully qualified method name
         * @return true if registered
         */
        bool has_method_return_type(const std::string &qualified_method_name) const;

        /**
         * @brief Register a method's return type annotation string for cross-module lookups
         * @param qualified_method_name Fully qualified method name (e.g., "std::collections::string::String::find")
         * @param return_type_annotation The return type annotation as a string (e.g., "Option<u64>")
         *
         * This is used when TypeRef cannot be resolved during module loading (for complex types).
         * The annotation is later resolved to an LLVM type during codegen when full type context is available.
         */
        void register_method_return_type_annotation(const std::string &qualified_method_name,
                                                    const std::string &return_type_annotation);

        /**
         * @brief Get a method's return type annotation string
         * @param qualified_method_name Fully qualified method name
         * @return The return type annotation, or empty string if not registered
         */
        std::string get_method_return_type_annotation(const std::string &qualified_method_name) const;

        /**
         * @brief Check if a method's return type annotation is registered
         * @param qualified_method_name Fully qualified method name
         * @return true if registered
         */
        bool has_method_return_type_annotation(const std::string &qualified_method_name) const;

        //===================================================================
        // Method Static Flag Registry (for cross-module extern declarations)
        //===================================================================

        /**
         * @brief Register whether a method is static
         * @param qualified_method_name Fully qualified method name (e.g., "std::time::duration::Duration::zero")
         * @param is_static true if the method is static (no 'this' parameter)
         */
        void register_method_is_static(const std::string &qualified_method_name, bool is_static);

        /**
         * @brief Get whether a method is static
         * @param qualified_method_name Fully qualified method name
         * @return Pair of (is_static, found). found is false if the method is not registered.
         */
        std::pair<bool, bool> get_method_is_static(const std::string &qualified_method_name) const;

        //===================================================================
        // Struct Field Types Registry (for cross-module struct definitions)
        //===================================================================

        /**
         * @brief Register a struct's field types for cross-module lookups
         * @param qualified_struct_name Fully qualified struct name (e.g., "std::collections::string::String")
         * @param field_names Names of the struct fields
         * @param field_types Types of the struct fields
         * @param source_namespace The namespace where the struct was originally defined
         */
        void register_struct_field_types(const std::string &qualified_struct_name,
                                         const std::vector<std::string> &field_names,
                                         const std::vector<TypeRef> &field_types,
                                         const std::string &source_namespace = "",
                                         const std::vector<std::string> &field_type_annotations = {});

        /**
         * @brief Get a struct's field types for LLVM struct body definition
         * @param qualified_struct_name Fully qualified struct name
         * @return Pointer to StructFieldInfo, or nullptr if not registered
         */
        const StructFieldInfo *get_struct_field_types(const std::string &qualified_struct_name) const;

        /**
         * @brief Check if a struct's field types are registered
         * @param qualified_struct_name Fully qualified struct name
         * @return true if registered
         */
        bool has_struct_field_types(const std::string &qualified_struct_name) const;

        //===================================================================
        // Template Method Metadata Registry (for cross-module type checking)
        //===================================================================

        /**
         * @brief Get template method info (metadata persists after AST cleanup)
         * @param template_name The template base name
         * @return Pointer to TemplateMethodInfo, or nullptr if not registered
         */
        const TemplateMethodInfo *get_template_method_info(const std::string &template_name) const;

        /**
         * @brief Find a specific method by name in template method metadata
         * @param template_name The template base name
         * @param method_name The method name to find
         * @return Pointer to MethodMetadata, or nullptr if not found
         */
        const MethodMetadata *find_template_method(const std::string &template_name,
                                                   const std::string &method_name) const;

        /**
         * @brief Check if template method info is registered
         * @param template_name The template base name
         * @return true if registered
         */
        bool has_template_method_info(const std::string &template_name) const;

        //===================================================================
        // Enum Implementation Block Registry (for generic enum instantiation)
        //===================================================================

        /**
         * @brief Register an implementation block for a generic enum
         * @param base_enum_name Base name of the generic enum (e.g., "Option", "Result")
         * @param impl_block Pointer to the implementation block node
         * @param module_namespace Namespace where the impl block is defined
         *
         * This allows the monomorphizer to find and generate methods when
         * instantiating generic enums like Option<i64>.
         */
        void register_enum_impl_block(const std::string &base_enum_name,
                                      ImplementationBlockNode *impl_block,
                                      const std::string &module_namespace);

        /**
         * @brief Get the implementation block for a generic enum
         * @param base_enum_name Base name of the generic enum
         * @return Pointer to ImplementationBlockNode, or nullptr if not registered
         */
        ImplementationBlockNode *get_enum_impl_block(const std::string &base_enum_name) const;

        /**
         * @brief Check if an implementation block is registered for a generic enum
         * @param base_enum_name Base name of the generic enum
         * @return true if registered
         */
        bool has_enum_impl_block(const std::string &base_enum_name) const;

        //===================================================================
        // Struct Implementation Block Registry (for generic struct instantiation)
        //===================================================================

        /**
         * @brief Register an implementation block for a generic struct
         * @param base_struct_name Base name of the generic struct (e.g., "Array", "HashMap")
         * @param impl_block Pointer to the implementation block node
         * @param module_namespace Namespace where the impl block is defined
         *
         * This allows the monomorphizer to find and generate methods when
         * instantiating generic structs like Array<i64>.
         */
        void register_struct_impl_block(const std::string &base_struct_name,
                                        ImplementationBlockNode *impl_block,
                                        const std::string &module_namespace);

        /**
         * @brief Get the implementation block for a generic struct
         * @param base_struct_name Base name of the generic struct
         * @return Pointer to ImplementationBlockNode, or nullptr if not registered
         */
        ImplementationBlockNode *get_struct_impl_block(const std::string &base_struct_name) const;

        /**
         * @brief Check if an implementation block is registered for a generic struct
         * @param base_struct_name Base name of the generic struct
         * @return true if registered
         */
        bool has_struct_impl_block(const std::string &base_struct_name) const;

        /**
         * @brief Find the namespace of a type by searching method annotation keys
         * @param type_name Simple type name (e.g., "PathBuf")
         * @param method_name Method name to search for (e.g., "from_cstr")
         * @return The namespace prefix, or empty string if not found
         */
        std::string find_type_namespace_from_methods(const std::string &type_name,
                                                      const std::string &method_name) const;
    };
}