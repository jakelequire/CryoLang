#pragma once
/******************************************************************************
 * @file TypeMapper.hpp
 * @brief Maps new Types system types to LLVM IR types
 *
 * TypeMapper is the codegen bridge for the new type system. It converts
 * TypeRef handles to LLVM types for code generation.
 *
 * Key design principles:
 * - Uses TypeRef handles (not raw Type*)
 * - TypeID-based caching for O(1) lookup
 * - Clean dispatch based on new TypeKind enum
 * - No string-based fallbacks
 * - Integrates with GenericRegistry for instantiation
 ******************************************************************************/

#include "Types/TypeID.hpp"
#include "Types/Type.hpp"
#include "Types/TypeArena.hpp"
#include "Types/PrimitiveTypes.hpp"
#include "Types/CompoundTypes.hpp"
#include "Types/UserDefinedTypes.hpp"
#include "Types/GenericTypes.hpp"
#include "Types/ErrorType.hpp"

#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Constants.h>

#include <unordered_map>
#include <string>
#include <vector>
#include <optional>
#include <functional>

namespace Cryo
{
    // Forward declarations
    class GenericRegistry;

    /**************************************************************************
     * @brief Callback for generic type instantiation during codegen
     *
     * This callback is invoked when mapping an InstantiatedType that
     * hasn't been monomorphized yet.
     **************************************************************************/
    using GenericInstantiationCallback = std::function<llvm::StructType *(
        TypeRef base_type,
        const std::vector<TypeRef> &type_args)>;

    /**************************************************************************
     * @brief Maps Types TypeRefs to LLVM Types
     *
     * Provides a clean mapping from the new type system to LLVM IR types.
     * Uses TypeID for efficient caching and supports all type kinds.
     *
     * Usage:
     *   TypeMapper mapper(arena, llvm_ctx, llvm_module);
     *
     *   TypeRef int_type = arena.get_i32();
     *   llvm::Type* llvm_int = mapper.map(int_type);
     **************************************************************************/
    class TypeMapper
    {
    private:
        TypeArena &_arena;
        llvm::LLVMContext &_llvm_ctx;
        llvm::Module *_module;
        GenericRegistry *_generics;

        // Cache: TypeID -> LLVM Type
        std::unordered_map<TypeID, llvm::Type *, TypeID::Hash> _type_cache;

        // Struct cache by name (for forward declarations)
        std::unordered_map<std::string, llvm::StructType *> _struct_cache;

        // Generic instantiation callback
        GenericInstantiationCallback _instantiation_callback;

        // Type parameter resolver for generic instantiation
        std::function<TypeRef(const std::string &)> _type_param_resolver;

        // Generic instantiator callback
        std::function<llvm::StructType *(const std::string &, const std::vector<TypeRef> &)> _generic_instantiator;

        // Error state
        bool _has_error = false;
        std::string _last_error;

    public:
        // ====================================================================
        // Construction
        // ====================================================================

        TypeMapper(TypeArena &arena,
                    llvm::LLVMContext &llvm_ctx,
                    llvm::Module *module = nullptr);

        ~TypeMapper() = default;

        // Non-copyable
        TypeMapper(const TypeMapper &) = delete;
        TypeMapper &operator=(const TypeMapper &) = delete;

        // ====================================================================
        // Configuration
        // ====================================================================

        void set_module(llvm::Module *module) { _module = module; }
        void set_generic_registry(GenericRegistry *reg) { _generics = reg; }
        void set_template_registry(class TemplateRegistry *) {} // Legacy compat - no-op
        void set_instantiation_callback(GenericInstantiationCallback cb)
        {
            _instantiation_callback = std::move(cb);
        }

        /**
         * @brief Set callback for resolving type parameters during instantiation
         */
        using TypeParamResolver = std::function<TypeRef(const std::string &)>;
        void set_type_param_resolver(TypeParamResolver resolver)
        {
            _type_param_resolver = std::move(resolver);
        }
        void clear_type_param_resolver() { _type_param_resolver = nullptr; }

        /**
         * @brief Set callback for generic instantiation
         */
        using GenericInstantiator = std::function<llvm::StructType *(
            const std::string &name,
            const std::vector<TypeRef> &type_args)>;
        void set_generic_instantiator(GenericInstantiator instantiator)
        {
            _generic_instantiator = std::move(instantiator);
        }

        // ====================================================================
        // Primary Interface
        // ====================================================================

        /**
         * @brief Map TypeRef to LLVM type (primary method)
         * @param type TypeRef to convert
         * @return LLVM type or nullptr on failure
         */
        llvm::Type *map(TypeRef type);

        /**
         * @brief Alias for map() for backward compatibility
         */
        llvm::Type *map_type(TypeRef type) { return map(type); }

        /**
         * @brief Get LLVM type by Cryo type (alias for map)
         */
        llvm::Type *get_type(TypeRef type) { return map(type); }

        /**
         * @brief Get LLVM type by name (looks up in struct cache)
         */
        llvm::Type *get_type(const std::string &name);

        /**
         * @brief Resolve type name to TypeRef and map to LLVM type
         */
        llvm::Type *resolve_and_map(const std::string &name);

        /**
         * @brief Map multiple types
         * @param types Vector of TypeRefs
         * @return Vector of LLVM types (nullptr for failures)
         */
        std::vector<llvm::Type *> map_all(const std::vector<TypeRef> &types);

        /**
         * @brief Map function signature to LLVM function type
         * @param return_type Return type
         * @param param_types Parameter types
         * @param is_variadic Whether function is variadic
         * @return LLVM function type
         */
        llvm::FunctionType *map_function(TypeRef return_type,
                                          const std::vector<TypeRef> &param_types,
                                          bool is_variadic = false);

        // ====================================================================
        // Primitive Type Accessors
        // ====================================================================

        llvm::Type *void_type();
        llvm::StructType *unit_type();
        llvm::IntegerType *bool_type();
        llvm::IntegerType *char_type();
        llvm::IntegerType *i8_type();
        llvm::IntegerType *i16_type();
        llvm::IntegerType *i32_type();
        llvm::IntegerType *i64_type();
        llvm::IntegerType *i128_type();
        llvm::IntegerType *int_type(unsigned bits);
        llvm::Type *f32_type();
        llvm::Type *f64_type();
        llvm::PointerType *string_type();
        llvm::PointerType *ptr_type();

        // ====================================================================
        // Type Utilities
        // ====================================================================

        /**
         * @brief Get size of LLVM type in bytes
         */
        uint64_t size_of(llvm::Type *type);

        /**
         * @brief Get alignment of LLVM type in bytes
         */
        uint64_t align_of(llvm::Type *type);

        /**
         * @brief Substitute generic params in a type with concrete type args
         * @param type Type potentially containing generic parameters
         * @param type_args Concrete type arguments to substitute
         * @return Type with generic params substituted with concrete types
         */
        TypeRef substitute_generic_param(TypeRef type,
                                          const std::vector<TypeRef> &type_args);

        /**
         * @brief Get null/zero value for type
         */
        llvm::Constant *null_value(llvm::Type *type);

        /**
         * @brief Check if TypeRef represents a signed integer
         */
        bool is_signed(TypeRef type);

        /**
         * @brief Check if LLVM type is floating point
         */
        bool is_float(llvm::Type *type);

        // ====================================================================
        // Struct Type Management
        // ====================================================================

        /**
         * @brief Get or create named struct type
         * @param name Struct name
         * @return Opaque struct type (use setBody to define)
         */
        llvm::StructType *get_or_create_struct(const std::string &name);

        /**
         * @brief Register a complete struct type
         */
        void register_struct(const std::string &name, llvm::StructType *type);

        /**
         * @brief Lookup struct type by name
         */
        llvm::StructType *lookup_struct(const std::string &name);

        /**
         * @brief Check if struct is registered
         */
        bool has_struct(const std::string &name);

        /**
         * @brief Complete an opaque struct with fields
         */
        void complete_struct(llvm::StructType *st,
                             const std::vector<llvm::Type *> &fields,
                             bool packed = false);

        // ====================================================================
        // Cache Management
        // ====================================================================

        /**
         * @brief Clear all caches
         */
        void clear_cache();

        /**
         * @brief Cache a type mapping
         */
        void cache_type(TypeRef type, llvm::Type *llvm_type);

        /**
         * @brief Lookup type in cache
         */
        llvm::Type *lookup_cached(TypeRef type);

        /**
         * @brief Lookup type in cache by ID
         */
        llvm::Type *lookup_cached(TypeID id);

        // ====================================================================
        // Context Access
        // ====================================================================

        llvm::LLVMContext &llvm_ctx() { return _llvm_ctx; }
        llvm::Module *module() { return _module; }
        TypeArena &arena() { return _arena; }
        const TypeArena &arena() const { return _arena; }
        GenericRegistry *generic_registry() { return _generics; }

        // ====================================================================
        // Error Handling
        // ====================================================================

        bool has_errors() const { return _has_error; }
        const std::string &last_error() const { return _last_error; }
        void clear_errors()
        {
            _has_error = false;
            _last_error.clear();
        }

    private:
        // ====================================================================
        // Type-Specific Mapping Methods
        // ====================================================================

        llvm::Type *map_void(const VoidType *type);
        llvm::Type *map_unit(const UnitType *type);
        llvm::Type *map_bool(const BoolType *type);
        llvm::Type *map_int(const IntType *type);
        llvm::Type *map_float(const FloatType *type);
        llvm::Type *map_char(const CharType *type);
        llvm::Type *map_string(const StringType *type);
        llvm::Type *map_never(const NeverType *type);

        llvm::Type *map_pointer(const PointerType *type);
        llvm::Type *map_reference(const ReferenceType *type);
        llvm::Type *map_array(const ArrayType *type);
        llvm::Type *map_function_type(const FunctionType *type);
        llvm::Type *map_tuple(const TupleType *type);
        llvm::Type *map_optional(const OptionalType *type);

        llvm::Type *map_struct(const StructType *type);
        llvm::Type *map_class(const ClassType *type);
        llvm::Type *map_enum(const EnumType *type);
        llvm::Type *map_trait(const TraitType *type);
        llvm::Type *map_type_alias(const TypeAliasType *type);

        llvm::Type *map_generic_param(const GenericParamType *type);
        llvm::Type *map_bounded_param(const BoundedParamType *type);
        llvm::Type *map_instantiated(const InstantiatedType *type);

        llvm::Type *map_error(const ErrorType *type);

        // ====================================================================
        // Instantiated Type Helpers
        // ====================================================================

        /**
         * @brief Map an instantiated enum type with type arguments
         */
        llvm::Type *map_instantiated_enum(const InstantiatedType *inst,
                                           const EnumType *base_enum,
                                           const std::vector<TypeRef> &type_args);

        /**
         * @brief Map an instantiated struct type with type arguments
         */
        llvm::Type *map_instantiated_struct(const InstantiatedType *inst,
                                             const StructType *base_struct,
                                             const std::vector<TypeRef> &type_args);

        /**
         * @brief Try to parse and instantiate a generic type from string
         * e.g., "Result<Duration,SystemTimeError>" -> proper LLVM type
         */
        llvm::Type *try_instantiate_generic_from_string(const std::string &name);

        /**
         * @brief Directly create LLVM type for well-known generic enums
         * when the base type isn't available as a TypeRef (template-only)
         */
        llvm::Type *create_generic_enum_type_directly(
            const std::string &full_name,
            const std::string &base_name,
            const std::vector<std::string> &arg_names);

        /**
         * @brief Resolve a type name string to a TypeRef
         */
        TypeRef resolve_type_from_string(const std::string &name);

        // ====================================================================
        // Helpers
        // ====================================================================

        /**
         * @brief Create tagged union type for enums with data
         */
        llvm::StructType *create_tagged_union(const std::string &name,
                                               size_t discriminant_size,
                                               size_t payload_size);

        /**
         * @brief Report an error
         */
        void report_error(const std::string &msg);
    };

    /**************************************************************************
     * @brief Generate mangled name for instantiated generic type
     **************************************************************************/
    std::string mangle_instantiation_name(TypeRef base,
                                          const std::vector<TypeRef> &type_args);

} // namespace Cryo
