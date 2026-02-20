#pragma once

#include "Codegen/ICodegenComponent.hpp"
#include "Codegen/CodegenContext.hpp"
#include "AST/ASTNode.hpp"

#include <llvm/IR/Type.h>
#include <llvm/IR/Function.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace Cryo::Codegen
{
    // Forward declarations
    class DeclarationCodegen;
    class TypeCodegen;

    /**
     * @brief Handles generic type instantiation and monomorphization
     *
     * This class centralizes:
     * - Generic struct/class instantiation
     * - Generic function monomorphization
     * - Type parameter substitution
     * - Instantiation caching
     *
     * Key features:
     * - Lazy instantiation (on-demand)
     * - Caching to avoid duplicate instantiations
     * - Support for nested generics
     * - Constraint checking (where clauses)
     */
    class GenericCodegen : public ICodegenComponent
    {
    public:
        //===================================================================
        // Construction
        //===================================================================

        explicit GenericCodegen(CodegenContext &ctx);
        ~GenericCodegen() = default;

        /**
         * @brief Set the declaration codegen component
         */
        void set_declaration_codegen(DeclarationCodegen *decls) { _declarations = decls; }

        /**
         * @brief Set the type codegen component
         */
        void set_type_codegen(TypeCodegen *types) { _type_codegen = types; }

        //===================================================================
        // Generic Type Instantiation
        //===================================================================

        /**
         * @brief Instantiate a generic struct type
         * @param generic_name Base generic type name (e.g., "Array")
         * @param type_args Type arguments (e.g., [i32])
         * @return Instantiated struct type
         */
        llvm::StructType *instantiate_struct(const std::string &generic_name,
                                              const std::vector<TypeRef> &type_args);

        /**
         * @brief Instantiate a generic class type
         * @param generic_name Base generic class name
         * @param type_args Type arguments
         * @return Instantiated class type (as struct)
         */
        llvm::StructType *instantiate_class(const std::string &generic_name,
                                             const std::vector<TypeRef> &type_args);

        /**
         * @brief Instantiate a generic enum type
         * @param generic_name Base generic enum name (e.g., "Option")
         * @param type_args Type arguments (e.g., [string])
         * @return Instantiated enum type (as struct for tagged unions, or i32 for simple enums)
         *
         * This function creates the LLVM type for the instantiated enum and
         * registers all variants with the instantiated name (e.g., Option_string::None)
         */
        llvm::Type *instantiate_enum(const std::string &generic_name,
                                      const std::vector<TypeRef> &type_args);

        /**
         * @brief Get or create instantiated type
         * @param generic_name Generic type name
         * @param type_args Type arguments
         * @return Instantiated LLVM type
         */
        llvm::Type *get_instantiated_type(const std::string &generic_name,
                                           const std::vector<TypeRef> &type_args);

        //===================================================================
        // Generic Function Instantiation
        //===================================================================

        /**
         * @brief Instantiate a generic function
         * @param generic_name Base function name
         * @param type_args Type arguments
         * @return Instantiated function
         */
        llvm::Function *instantiate_function(const std::string &generic_name,
                                              const std::vector<TypeRef> &type_args);

        /**
         * @brief Instantiate a generic method
         * @param type_name Parent type name
         * @param method_name Method name
         * @param type_args Type arguments
         * @return Instantiated method function
         */
        llvm::Function *instantiate_method(const std::string &type_name,
                                            const std::string &method_name,
                                            const std::vector<TypeRef> &type_args);

        /**
         * @brief Instantiate a specific method on an already-instantiated type
         *
         * Used for on-demand generation of "doubly generic" methods like map<U>
         * on Maybe<T> when called with concrete type arguments at the call site.
         *
         * @param mangled_type Instantiated type name (e.g., "Maybe_i32")
         * @param generic_base Generic base name (e.g., "Maybe")
         * @param all_type_params Combined type params (e.g., ["T", "U"])
         * @param all_type_args Combined type args (e.g., [i32, i32])
         * @param method The method AST node to generate
         * @param base_namespace Namespace for scope resolution
         * @return Generated LLVM function, or nullptr on failure
         */
        llvm::Function *instantiate_method_for_type(
            const std::string &mangled_type,
            const std::string &generic_base,
            const std::vector<std::string> &all_type_params,
            const std::vector<TypeRef> &all_type_args,
            Cryo::FunctionDeclarationNode *method,
            const std::string &base_namespace);

        //===================================================================
        // Type Parameter Handling
        //===================================================================

        /**
         * @brief Begin type parameter scope
         * @param params Type parameter names
         * @param args Corresponding type arguments
         * @param generic_base Optional: the generic base name (e.g., "Option")
         * @param instantiated_name Optional: the instantiated name (e.g., "Option_voidp")
         *
         * When generic_base and instantiated_name are provided, scope resolutions
         * like "Option::None" will be redirected to "Option_voidp::None".
         */
        void begin_type_params(const std::vector<std::string> &params,
                                const std::vector<TypeRef> &args,
                                const std::string &generic_base = "",
                                const std::string &instantiated_name = "");

        /**
         * @brief End type parameter scope
         */
        void end_type_params();

        /**
         * @brief Resolve a type parameter to its argument
         * @param param_name Type parameter name (e.g., "T")
         * @return Resolved type, or nullptr if not in scope
         */
        TypeRef resolve_type_param(const std::string &param_name);

        /**
         * @brief Substitute type parameters in a type
         * @param type Type potentially containing type parameters
         * @return Type with parameters substituted
         */
        TypeRef substitute_type_params(TypeRef type);

        //===================================================================
        // Name Mangling
        //===================================================================

        /**
         * @brief Generate mangled name for instantiated type
         * @param generic_name Base name
         * @param type_args Type arguments
         * @return Mangled name (e.g., "Array_i32")
         */
        std::string mangle_type_name(const std::string &generic_name,
                                      const std::vector<TypeRef> &type_args);

        /**
         * @brief Generate mangled name for instantiated function
         * @param generic_name Base function name
         * @param type_args Type arguments
         * @return Mangled name
         */
        std::string mangle_function_name(const std::string &generic_name,
                                          const std::vector<TypeRef> &type_args);

        //===================================================================
        // Generic Scope Resolution Support
        //===================================================================

        /**
         * @brief Get the instantiated name for a generic base name
         *
         * When inside a type parameter substitution scope (during method body
         * generation), this returns the instantiated name for scope resolution.
         * For example, if we're generating code for Option<void*>, and the AST
         * contains "Option::None", this will return "Option_voidp" so we can
         * look for "Option_voidp::None".
         *
         * @param generic_base The generic base name (e.g., "Option")
         * @return The instantiated name (e.g., "Option_voidp"), or empty string if not in scope
         */
        std::string get_instantiated_scope_name(const std::string &generic_base) const;

        /**
         * @brief Check if currently inside a type parameter substitution scope
         * @return true if inside a generic instantiation context
         */
        bool in_type_param_scope() const { return !_type_param_stack.empty(); }

        /**
         * @brief Get current type parameter bindings (innermost scope wins)
         * @return Vector of (param_name, arg_display_name) pairs, e.g., [("T", "String")]
         */
        std::vector<std::pair<std::string, std::string>> get_current_type_param_bindings() const;

        /**
         * @brief Substitute type parameters in a type annotation string
         *
         * When inside a type parameter substitution scope, this substitutes
         * type parameters in the annotation string and returns the mangled name.
         * For example, if T -> string, "HashSetEntry<T>" becomes "HashSetEntry_string".
         *
         * This also handles instantiating dependent generic types if they don't exist.
         *
         * @param type_annotation The type annotation string (e.g., "HashSetEntry<T>")
         * @return The substituted/mangled name (e.g., "HashSetEntry_string"), or empty if no substitution needed
         */
        std::string substitute_type_annotation(const std::string &type_annotation);

        //===================================================================
        // Instantiation Cache
        //===================================================================

        /**
         * @brief Check if type instantiation exists
         * @param mangled_name Mangled type name
         * @return true if already instantiated
         */
        bool has_type_instantiation(const std::string &mangled_name) const;

        /**
         * @brief Check if function instantiation exists
         * @param mangled_name Mangled function name
         * @return true if already instantiated
         */
        bool has_function_instantiation(const std::string &mangled_name) const;

        /**
         * @brief Get cached type instantiation
         * @param mangled_name Mangled type name
         * @return Cached type, or nullptr
         */
        llvm::Type *get_cached_type(const std::string &mangled_name);

        /**
         * @brief Get cached function instantiation
         * @param mangled_name Mangled function name
         * @return Cached function, or nullptr
         */
        llvm::Function *get_cached_function(const std::string &mangled_name);

        //===================================================================
        // Generic Definition Lookup
        //===================================================================

        /**
         * @brief Register a generic type definition
         * @param name Generic type name
         * @param node AST node for the generic type
         */
        void register_generic_type(const std::string &name, Cryo::ASTNode *node);

        /**
         * @brief Check if a type is a registered generic template
         * @param name Type name to check
         * @return true if the type is a generic template
         */
        bool is_generic_template(const std::string &name);

        /**
         * @brief Register a generic function definition
         * @param name Generic function name
         * @param node AST node for the generic function
         */
        void register_generic_function(const std::string &name, Cryo::ASTNode *node);

        /**
         * @brief Get generic type definition
         * @param name Generic type name
         * @return AST node, or nullptr if not found
         */
        Cryo::ASTNode *get_generic_type_def(const std::string &name);

        /**
         * @brief Get generic function definition
         * @param name Generic function name
         * @return AST node, or nullptr if not found
         */
        Cryo::ASTNode *get_generic_function_def(const std::string &name);

    private:
        DeclarationCodegen *_declarations = nullptr;
        TypeCodegen *_type_codegen = nullptr;

        //===================================================================
        // Type Parameter Stack
        //===================================================================

        struct TypeParamScope
        {
            std::unordered_map<std::string, TypeRef> bindings;  // T -> void*
            std::string generic_base;      // e.g., "Option" - the template name
            std::string instantiated_name; // e.g., "Option_voidp" - the monomorphized name
        };
        std::vector<TypeParamScope> _type_param_stack;

        //===================================================================
        // Per-module instantiation tracking (reset with each new CodeGenerator)
        //===================================================================

        std::unordered_set<std::string> _generated_struct_methods;
        std::unordered_set<std::string> _generated_enum_methods;
        std::unordered_set<std::string> _structs_being_instantiated;
        std::unordered_set<std::string> _classes_being_instantiated;

        //===================================================================
        // Caches
        //===================================================================

        std::unordered_map<std::string, llvm::Type *> _type_cache;
        std::unordered_map<std::string, llvm::Function *> _function_cache;

        //===================================================================
        // Generic Definitions
        //===================================================================

        std::unordered_map<std::string, Cryo::ASTNode *> _generic_types;
        std::unordered_map<std::string, Cryo::ASTNode *> _generic_functions;

        //===================================================================
        // Internal Helpers
        //===================================================================

        /**
         * @brief Create field types with substitution
         * @param fields Original field definitions
         * @return Vector of substituted LLVM types
         */
        std::vector<llvm::Type *> create_substituted_fields(
            const std::vector<std::unique_ptr<Cryo::StructFieldNode>> &fields);

        /**
         * @brief Create field types from specialized AST (no substitution needed)
         * @param fields Specialized field definitions with concrete types
         * @return Vector of LLVM types
         *
         * This is used when processing a pre-specialized AST from the Monomorphizer.
         * The types are already concrete (T -> Token), so no substitution is needed.
         */
        std::vector<llvm::Type *> create_fields_from_specialized(
            const std::vector<std::unique_ptr<Cryo::StructFieldNode>> &fields);

        /**
         * @brief Create function type with substitution
         * @param node Function declaration
         * @return Substituted function type
         */
        llvm::FunctionType *create_substituted_function_type(Cryo::FunctionDeclarationNode *node);

        /**
         * @brief Ensure all dependent generic types are instantiated
         *
         * This recursively processes a type and triggers instantiation for any
         * InstantiatedType it contains (including through pointers, references,
         * arrays, etc.). This is necessary because LLVM 15+ opaque pointers don't
         * trigger pointee type instantiation.
         *
         * @param type Type to process for dependent instantiations
         */
        void ensure_dependent_types_instantiated(TypeRef type);

        /**
         * @brief Resolve a field's TypeAnnotation to a TypeRef using current type param bindings
         *
         * Similar to DeclarationCodegen::resolve_type_annotation but checks type parameter
         * bindings first (via resolve_type_param) for Named/Primitive kinds. Used as fallback
         * when field->get_resolved_type() returns empty (common for generic template bodies
         * that skip type resolution).
         *
         * @param annotation The field's type annotation from parsing
         * @return Resolved TypeRef, or empty if resolution fails
         */
        TypeRef resolve_field_type_from_annotation(const TypeAnnotation *annotation);

        /**
         * @brief Look up the InstantiatedType for the given generic and set call-site
         *        info on CodegenContext for better error messages.
         */
        void set_instantiation_context_from_type(const std::string &generic_name,
                                                  const std::vector<TypeRef> &type_args,
                                                  const std::string &parent_mangled = "");
    };

} // namespace Cryo::Codegen
