#pragma once
/******************************************************************************
 * Copyright 2025 Jacob LeQuire
 * SPDX-License-Identifier: Apache-2.0
 *
 * TypeMapper - Maps CryoLang types to LLVM IR types
 *
 * This is a focused, simplified type mapper that converts Cryo's type system
 * to LLVM IR types. It maintains a single responsibility: type conversion.
 *
 * Key design principles:
 * - Uses Type* objects directly (no string parsing)
 * - Single cache keyed by Type* for deduplication
 * - Clean dispatch based on TypeKind
 * - Works with the new component architecture
 *****************************************************************************/

#include "AST/Type.hpp"
#include "AST/ASTNode.hpp"
#include "Codegen/LLVMContext.hpp"

#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DataLayout.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <functional>

namespace Cryo::Codegen
{
    /**
     * @brief Callback type for generic type instantiation
     *
     * Takes the base generic name and type arguments, returns the instantiated LLVM type.
     * This allows TypeMapper to delegate generic instantiation to GenericCodegen.
     */
    using GenericInstantiator = std::function<llvm::StructType*(
        const std::string& generic_name,
        const std::vector<Cryo::Type*>& type_args)>;

    /**
     * @brief Maps CryoLang Type* to LLVM Type*
     *
     * Focused type mapper that handles:
     * - Primitive types (void, int, float, bool, char, string)
     * - Compound types (arrays, pointers, references, functions)
     * - User-defined types (structs, classes, enums)
     * - Generic/parameterized types
     *
     * Uses the Cryo type system's Type hierarchy directly without
     * string-based fallbacks.
     */
    class TypeMapper
    {
    public:
        //===================================================================
        // Construction
        //===================================================================

        /**
         * @brief Construct type mapper
         * @param llvm LLVM context manager
         * @param types Cryo type context (required)
         */
        TypeMapper(LLVMContextManager &llvm, Cryo::TypeContext *types);

        ~TypeMapper() = default;

        // Non-copyable
        TypeMapper(const TypeMapper &) = delete;
        TypeMapper &operator=(const TypeMapper &) = delete;

        /**
         * @brief Set the generic instantiator callback
         *
         * This callback is invoked when mapping a ParameterizedType that
         * results in an opaque struct. The callback should instantiate
         * the generic type with the given type arguments.
         *
         * @param instantiator Callback function for generic instantiation
         */
        void set_generic_instantiator(GenericInstantiator instantiator)
        {
            _generic_instantiator = std::move(instantiator);
        }

        //===================================================================
        // Primary Interface
        //===================================================================

        /**
         * @brief Map Cryo type to LLVM type (PRIMARY METHOD)
         * @param cryo_type Cryo type to convert
         * @return LLVM type or nullptr on failure
         */
        llvm::Type *map(Cryo::Type *cryo_type);

        /**
         * @brief Alias for map() - backward compatibility
         */
        llvm::Type *map_type(Cryo::Type *cryo_type) { return map(cryo_type); }

        /**
         * @brief Get LLVM type for Cryo type - alias for map()
         */
        llvm::Type *get_type(Cryo::Type *cryo_type) { return map(cryo_type); }

        /**
         * @brief Get LLVM type by name (looks up in struct cache)
         * @param name Type name
         * @return LLVM type or nullptr
         */
        llvm::Type *get_type(const std::string &name);

        /**
         * @brief Map function signature to LLVM function type
         * @param return_type Return type
         * @param param_types Parameter types
         * @param is_variadic Whether function is variadic
         * @return LLVM function type
         */
        llvm::FunctionType *map_function(
            Cryo::Type *return_type,
            const std::vector<Cryo::Type *> &param_types,
            bool is_variadic = false);

        //===================================================================
        // Primitive Type Accessors
        //===================================================================

        /** @brief Get LLVM void type */
        llvm::Type *void_type();

        /** @brief Get LLVM boolean type (i1) */
        llvm::IntegerType *bool_type();

        /** @brief Get LLVM char type (i8) */
        llvm::IntegerType *char_type();

        /** @brief Get LLVM i8 type */
        llvm::IntegerType *i8_type();

        /** @brief Get LLVM i16 type */
        llvm::IntegerType *i16_type();

        /** @brief Get LLVM i32 type */
        llvm::IntegerType *i32_type();

        /** @brief Get LLVM i64 type */
        llvm::IntegerType *i64_type();

        /** @brief Get LLVM i128 type */
        llvm::IntegerType *i128_type();

        /** @brief Get LLVM integer type by bit width */
        llvm::IntegerType *int_type(unsigned bits);

        /** @brief Get LLVM f32 (float) type */
        llvm::Type *f32_type();

        /** @brief Get LLVM f64 (double) type */
        llvm::Type *f64_type();

        /** @brief Get LLVM string type (i8*) */
        llvm::PointerType *string_type();

        /** @brief Get opaque pointer type */
        llvm::PointerType *ptr_type();

        //===================================================================
        // Type Utilities
        //===================================================================

        /**
         * @brief Get size of LLVM type in bytes
         * @param type LLVM type
         * @return Size in bytes
         */
        uint64_t size_of(llvm::Type *type);

        /**
         * @brief Get alignment of LLVM type in bytes
         * @param type LLVM type
         * @return Alignment in bytes
         */
        uint64_t align_of(llvm::Type *type);

        /**
         * @brief Get null/zero value for type
         * @param type LLVM type
         * @return Null constant
         */
        llvm::Constant *null_value(llvm::Type *type);

        /**
         * @brief Check if type is signed integer
         * @param cryo_type Cryo type to check
         * @return true if signed integer
         */
        bool is_signed(Cryo::Type *cryo_type);

        /**
         * @brief Check if LLVM type is floating point
         * @param type LLVM type
         * @return true if float or double
         */
        bool is_float(llvm::Type *type);

        //===================================================================
        // Struct Type Management
        //===================================================================

        /**
         * @brief Create or get named struct type
         * @param name Struct name
         * @return Opaque struct type (use set_body to define)
         */
        llvm::StructType *get_or_create_struct(const std::string &name);

        /**
         * @brief Register a complete struct type
         * @param name Struct name
         * @param type Complete struct type
         */
        void register_struct(const std::string &name, llvm::StructType *type);

        /**
         * @brief Lookup struct type by name
         * @param name Struct name
         * @return Struct type or nullptr
         */
        llvm::StructType *lookup_struct(const std::string &name);

        /**
         * @brief Check if struct is registered
         * @param name Struct name
         * @return true if exists
         */
        bool has_struct(const std::string &name);

        //===================================================================
        // Cache Management
        //===================================================================

        /**
         * @brief Clear all caches
         */
        void clear_cache();

        /**
         * @brief Register type in cache
         * @param cryo_type Cryo type key
         * @param llvm_type LLVM type value
         */
        void cache_type(Cryo::Type *cryo_type, llvm::Type *llvm_type);

        /**
         * @brief Lookup type in cache
         * @param cryo_type Cryo type key
         * @return LLVM type or nullptr
         */
        llvm::Type *lookup_cached(Cryo::Type *cryo_type);

        //===================================================================
        // Context Access
        //===================================================================

        /** @brief Get LLVM context */
        llvm::LLVMContext &llvm_ctx() { return _llvm.get_context(); }

        /** @brief Get LLVM module */
        llvm::Module *module() { return _llvm.get_module(); }

        /** @brief Get Cryo type context */
        Cryo::TypeContext *type_ctx() { return _types; }

        //===================================================================
        // Error Handling
        //===================================================================

        bool has_errors() const { return _has_error; }
        const std::string &last_error() const { return _last_error; }
        void clear_errors() { _has_error = false; _last_error.clear(); }

    private:
        //===================================================================
        // Type-Specific Mapping Methods
        //===================================================================

        llvm::Type *map_integer(Cryo::IntegerType *type);
        llvm::Type *map_float(Cryo::FloatType *type);
        llvm::Type *map_array(Cryo::ArrayType *type);
        llvm::Type *map_pointer(Cryo::PointerType *type);
        llvm::Type *map_reference(Cryo::ReferenceType *type);
        llvm::Type *map_function_type(Cryo::FunctionType *type);
        llvm::Type *map_struct(Cryo::StructType *type);
        llvm::Type *map_class(Cryo::ClassType *type);
        llvm::Type *map_enum(Cryo::EnumType *type);
        llvm::Type *map_parameterized(Cryo::ParameterizedType *type);
        llvm::Type *map_optional(Cryo::OptionalType *type);

        //===================================================================
        // Enum Helpers
        //===================================================================

        /**
         * @brief Create tagged union type for complex enums
         * @param name Type name
         * @param discriminant_size Discriminant field size
         * @param payload_size Max payload size
         * @return Tagged union struct type
         */
        llvm::StructType *create_tagged_union(
            const std::string &name,
            size_t discriminant_size,
            size_t payload_size);

        /**
         * @brief Calculate payload size for enum variants
         * @param enum_type Enum type
         * @return Max payload size in bytes
         */
        size_t calculate_enum_payload_size(Cryo::EnumType *enum_type);

        //===================================================================
        // Struct/Class Helpers
        //===================================================================

        /**
         * @brief Generate field types for struct from declaration
         * @param decl Struct declaration node
         * @return Vector of field types
         */
        std::vector<llvm::Type *> get_struct_fields(Cryo::StructDeclarationNode *decl);

        /**
         * @brief Generate field types for class from declaration
         * @param decl Class declaration node
         * @return Vector of field types
         */
        std::vector<llvm::Type *> get_class_fields(Cryo::ClassDeclarationNode *decl);

        //===================================================================
        // Error Reporting
        //===================================================================

        void report_error(const std::string &msg);

        //===================================================================
        // Data Members
        //===================================================================

        LLVMContextManager &_llvm;
        Cryo::TypeContext *_types;

        // Type cache - keyed by Cryo Type*
        std::unordered_map<Cryo::Type *, llvm::Type *> _cache;

        // Struct cache - keyed by name for forward declarations
        std::unordered_map<std::string, llvm::StructType *> _struct_cache;

        // Generic instantiator callback - for parameterized types
        GenericInstantiator _generic_instantiator;

        // Error state
        bool _has_error = false;
        std::string _last_error;
    };

    //=======================================================================
    // Utility Functions
    //=======================================================================

    /**
     * @brief Get mangled name for generic type instantiation
     * @param base_name Base type name
     * @param type_args Type arguments
     * @return Mangled name (e.g., "Array_int")
     */
    std::string mangle_generic_name(
        const std::string &base_name,
        const std::vector<Cryo::Type *> &type_args);

} // namespace Cryo::Codegen
