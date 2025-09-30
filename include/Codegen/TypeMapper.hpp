#pragma once

#include "AST/Type.hpp"
#include "AST/ASTNode.hpp"
#include "Codegen/LLVMContext.hpp"

#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <optional>

namespace Cryo::Codegen
{
    /**
     * @brief Maps CryoLang types to LLVM IR types
     *
     * The TypeMapper is responsible for converting CryoLang's type system
     * to equivalent LLVM IR types. It handles:
     *
     * - Primitive types (int, float, bool, char, string)
     * - Complex types (arrays, structs, classes, enums)
     * - Generic types and instantiations
     * - Function types and signatures
     * - Pointer and reference types
     * - Memory layout and alignment
     *
     * The mapper maintains a cache of generated types to avoid regeneration
     * and ensure type identity consistency across the compilation unit.
     */
    class TypeMapper
    {
    public:
        //===================================================================
        // Construction
        //===================================================================

        /**
         * @brief Construct type mapper
         * @param context_manager LLVM context manager
         */
        explicit TypeMapper(LLVMContextManager &context_manager);

        ~TypeMapper() = default;

        //===================================================================
        // Core Type Mapping Interface
        //===================================================================

        /**
         * @brief Map CryoLang type to LLVM type
         * @param cryo_type CryoLang type to convert
         * @return Corresponding LLVM type or nullptr if conversion fails
         */
        llvm::Type *map_type(Cryo::Type *cryo_type);

        /**
         * @brief Map CryoLang type to LLVM type by name
         * @param type_name String representation of type
         * @return Corresponding LLVM type or nullptr if not found
         */
        llvm::Type *map_type(const std::string &type_name);

        /**
         * @brief Map function signature to LLVM function type
         * @param return_type Return type
         * @param param_types Parameter types
         * @param is_variadic Whether function accepts variadic arguments
         * @return LLVM function type
         */
        llvm::FunctionType *map_function_type(
            Cryo::Type *return_type,
            const std::vector<Cryo::Type *> &param_types,
            bool is_variadic = false);

        //===================================================================
        // Primitive Type Mapping
        //===================================================================

        /**
         * @brief Get LLVM type for integer
         * @param bit_width Width in bits (8, 16, 32, 64)
         * @param is_signed Whether integer is signed
         * @return LLVM integer type
         */
        llvm::IntegerType *get_integer_type(int bit_width, bool is_signed = true);

        /**
         * @brief Get LLVM type for floating point
         * @param precision Precision (32, 64)
         * @return LLVM floating point type
         */
        llvm::Type *get_float_type(int precision = 64);

        /**
         * @brief Get LLVM type for boolean
         * @return LLVM i1 type
         */
        llvm::IntegerType *get_boolean_type();

        /**
         * @brief Get LLVM type for character
         * @return LLVM i8 type
         */
        llvm::IntegerType *get_char_type();

        /**
         * @brief Get LLVM type for string
         * @return LLVM i8* type (null-terminated C string)
         */
        llvm::PointerType *get_string_type();

        /**
         * @brief Get LLVM void type
         * @return LLVM void type
         */
        llvm::Type *get_void_type();

        //===================================================================
        // Complex Type Mapping
        //===================================================================

        /**
         * @brief Map array type to LLVM array or vector type
         * @param element_type Array element type
         * @param size Array size (0 for dynamic arrays)
         * @param is_dynamic Whether array is dynamically sized
         * @return LLVM array type or pointer for dynamic arrays
         */
        llvm::Type *map_array_type(Cryo::Type *element_type, size_t size, bool is_dynamic = false);

        /**
         * @brief Map struct type to LLVM struct type
         * @param struct_decl Struct declaration node
         * @return LLVM struct type
         */
        llvm::StructType *map_struct_type(Cryo::StructDeclarationNode *struct_decl);

        /**
         * @brief Map class type to LLVM struct type (with vtable if needed)
         * @param class_decl Class declaration node
         * @return LLVM struct type
         */
        llvm::StructType *map_class_type(Cryo::ClassDeclarationNode *class_decl);

        /**
         * @brief Map enum type to LLVM type
         * @param enum_decl Enum declaration node
         * @return LLVM integer or struct type depending on enum style
         */
        llvm::Type *map_enum_type(Cryo::EnumDeclarationNode *enum_decl);

        /**
         * @brief Map generic type instance
         * @param generic_type Generic type declaration
         * @param type_args Type arguments for instantiation
         * @return Instantiated LLVM type
         */
        llvm::Type *map_generic_type(Cryo::Type *generic_type,
                                     const std::vector<Cryo::Type *> &type_args);

        /**
         * @brief Map generic type instantiation from string
         * @param type_name String like "GenericStruct<int>"
         * @return Instantiated LLVM type
         */
        llvm::Type *map_generic_instantiation(const std::string &type_name);

        /**
         * @brief Create generic struct instantiation
         * @param base_name Base generic type name
         * @param type_args Type arguments
         * @param instantiated_name Full instantiated name
         * @return Instantiated LLVM struct type
         */
        llvm::Type *create_generic_struct_instantiation(const std::string &base_name,
                                                        const std::vector<std::string> &type_args,
                                                        const std::string &instantiated_name);

        /**
         * @brief Register field metadata for generic instantiation
         * @param type_name Instantiated type name
         * @param field_name Field name
         * @param field_index Field index
         * @param field_type LLVM field type
         */
        void register_generic_field_metadata(const std::string &type_name,
                                             const std::string &field_name,
                                             int field_index,
                                             llvm::Type *field_type);

        //===================================================================
        // Pointer and Reference Types
        //===================================================================

        /**
         * @brief Map pointer type
         * @param pointee_type Type being pointed to
         * @return LLVM pointer type
         */
        llvm::PointerType *map_pointer_type(Cryo::Type *pointee_type);

        /**
         * @brief Map reference type (implemented as pointer in LLVM)
         * @param referenced_type Type being referenced
         * @return LLVM pointer type
         */
        llvm::PointerType *map_reference_type(Cryo::Type *referenced_type);

        //===================================================================
        // Type Information and Utilities
        //===================================================================

        /**
         * @brief Get size of type in bytes
         * @param llvm_type LLVM type
         * @return Size in bytes
         */
        size_t get_type_size(llvm::Type *llvm_type);

        /**
         * @brief Get alignment of type in bytes
         * @param llvm_type LLVM type
         * @return Alignment in bytes
         */
        size_t get_type_alignment(llvm::Type *llvm_type);

        /**
         * @brief Check if type is signed integer
         * @param type Type to check
         * @return true if signed integer
         */
        bool is_signed_integer(llvm::Type *type);

        /**
         * @brief Check if type is floating point
         * @param type Type to check
         * @return true if floating point
         */
        bool is_floating_point(llvm::Type *type);

        /**
         * @brief Check if type requires memory allocation
         * @param type Type to check
         * @return true if type should be allocated on heap
         */
        bool requires_heap_allocation(llvm::Type *type);

        /**
         * @brief Get default value for type (zero-initialized)
         * @param type LLVM type
         * @return Default constant value
         */
        llvm::Constant *get_default_value(llvm::Type *type);

        //===================================================================
        // Type Cache Management
        //===================================================================

        /**
         * @brief Register named type in cache
         * @param name Type name
         * @param llvm_type LLVM type
         */
        void register_type(const std::string &name, llvm::Type *llvm_type);

        /**
         * @brief Lookup type by name in cache
         * @param name Type name
         * @return LLVM type or nullptr if not found
         */
        llvm::Type *lookup_type(const std::string &name);

        /**
         * @brief Check if type is registered
         * @param name Type name
         * @return true if type exists in cache
         */
        bool has_type(const std::string &name);

        /**
         * @brief Clear type cache
         */
        void clear_cache();

        //===================================================================
        // Field Metadata Management
        //===================================================================

        /**
         * @brief Structure to hold field information
         */
        struct FieldInfo
        {
            llvm::Type *struct_type;
            int field_index;
            llvm::Type *field_type;
            std::string field_name;
        };

        /**
         * @brief Register field metadata for a struct/class type
         * @param type_name Name of the struct/class type
         * @param field_name Name of the field
         * @param field_index Index of the field in the struct
         * @param field_type LLVM type of the field
         */
        void register_field_metadata(const std::string &type_name, const std::string &field_name,
                                     int field_index, llvm::Type *field_type);

        /**
         * @brief Get field information for a given type and field name
         * @param llvm_type LLVM type to search for
         * @param field_name Name of the field
         * @return Optional field information
         */
        std::optional<FieldInfo> get_field_info(llvm::Type *llvm_type, const std::string &field_name);

        /**
         * @brief Get field index by name for a registered type
         * @param type_name Name of the type
         * @param field_name Name of the field
         * @return Field index or -1 if not found
         */
        int get_field_index(const std::string &type_name, const std::string &field_name);

        /**
         * @brief Register all fields from a struct declaration
         * @param struct_decl Struct declaration node
         * @param llvm_struct_type The corresponding LLVM struct type
         */
        void register_struct_fields(Cryo::StructDeclarationNode *struct_decl, llvm::StructType *llvm_struct_type);

        /**
         * @brief Register all fields from a class declaration
         * @param class_decl Class declaration node
         * @param llvm_class_type The corresponding LLVM struct type
         */
        void register_class_fields(Cryo::ClassDeclarationNode *class_decl, llvm::StructType *llvm_class_type);

        //===================================================================
        // Error Handling
        //===================================================================

        /**
         * @brief Check if mapper has errors
         */
        bool has_errors() const { return _has_errors; }

        /**
         * @brief Get last error message
         */
        const std::string &get_last_error() const { return _last_error; }

        /**
         * @brief Clear error state without clearing type caches
         */
        void clear_errors()
        {
            std::cout << "[DEBUG] TypeMapper: Clearing errors (was: " << _has_errors << ")" << std::endl;
            _has_errors = false;
            _last_error.clear();
        }

        //===================================================================
        // Generic Type Definition System
        //===================================================================

        /**
         * @brief Definition of a generic type field
         */
        struct GenericFieldDef
        {
            std::string name;      // Field name (e.g., "elements", "length")
            std::string type_expr; // Type expression (e.g., "ptr<T>", "u64", "T")
            bool is_templated;     // Whether this field uses template parameters
        };

        /**
         * @brief Definition of a generic type
         */
        struct GenericTypeDef
        {
            std::string base_name;               // Base name (e.g., "Array", "Pair")
            int num_type_params;                 // Number of type parameters
            std::vector<GenericFieldDef> fields; // Field definitions
            std::string description;             // Optional description
        };

        /**
         * @brief Register a generic type definition
         * @param def Generic type definition
         */
        void register_generic_type_def(const GenericTypeDef &def);

        /**
         * @brief Initialize built-in generic types (Array, Pair, etc.)
         */
        void initialize_builtin_generic_types();

        /**
         * @brief Create generic type instantiation using registered definitions
         * @param base_name Base generic type name
         * @param type_args Type argument strings
         * @param instantiated_name Full instantiated name
         * @return Instantiated LLVM type
         */
        llvm::Type *create_generic_type_from_def(const std::string &base_name,
                                                 const std::vector<std::string> &type_args,
                                                 const std::string &instantiated_name);

        /**
         * @brief Resolve type expression with template substitution
         * @param type_expr Type expression (e.g., "ptr<T>", "u64")
         * @param type_params_map Mapping from parameter names to concrete types
         * @return Resolved LLVM type
         */
        llvm::Type *resolve_type_expression(const std::string &type_expr,
                                            const std::unordered_map<std::string, std::string> &type_params_map);

    private:
        //===================================================================
        // Private Implementation
        //===================================================================

        LLVMContextManager &_context_manager;

        // Type cache
        std::unordered_map<std::string, llvm::Type *> _type_cache;
        std::unordered_map<Cryo::Type *, llvm::Type *> _cryo_type_cache;

        // Struct type cache for forward declarations
        std::unordered_map<std::string, llvm::StructType *> _struct_cache;

        // Field metadata storage
        std::unordered_map<std::string, std::unordered_map<std::string, FieldInfo>> _field_metadata;
        std::unordered_map<llvm::Type *, std::string> _llvm_type_to_name_map;

        // Registry of generic type definitions
        std::unordered_map<std::string, GenericTypeDef> _generic_type_registry;

        // Error state
        bool _has_errors;
        std::string _last_error;

        //===================================================================
        // Private Methods
        //===================================================================

        /**
         * @brief Map primitive type by kind
         */
        llvm::Type *map_primitive_type(Cryo::TypeKind kind);

        /**
         * @brief Generate struct field types
         */
        std::vector<llvm::Type *> generate_struct_fields(Cryo::StructDeclarationNode *struct_decl);

        /**
         * @brief Generate class field types (including vtable if needed)
         */
        std::vector<llvm::Type *> generate_class_fields(Cryo::ClassDeclarationNode *class_decl);

        /**
         * @brief Resolve forward declared structs
         */
        void resolve_forward_declarations();

        /**
         * @brief Create opaque struct type for forward declaration
         */
        llvm::StructType *create_opaque_struct(const std::string &name);

        /**
         * @brief Create tagged union type for complex enums
         * @param enum_type The enum type to create tagged union for
         * @return Tagged union LLVM struct type
         */
        llvm::Type *create_tagged_union_type(Cryo::EnumType *enum_type);

        /**
         * @brief Create tagged union type with specific payload size
         * @param name Name for the tagged union type
         * @param payload_size Size of the payload union in bytes
         * @return Tagged union LLVM struct type
         */
        llvm::StructType *create_tagged_union_type(const std::string &name, size_t payload_size);

        /**
         * @brief Report type mapping error
         */
        void report_error(const std::string &message);

        /**
         * @brief Parse array type notation from string
         */
        llvm::Type *parse_array_type_from_string(const std::string &name);
    };

    //=======================================================================
    // Utility Functions
    //=======================================================================

    /**
     * @brief Convert CryoLang calling convention to LLVM calling convention
     * @param cryo_cc CryoLang calling convention
     * @return LLVM calling convention
     */
    llvm::CallingConv::ID map_calling_convention(const std::string &cryo_cc);

    /**
     * @brief Get mangled name for generic type instantiation
     * @param base_name Base type name
     * @param type_args Type arguments
     * @return Mangled name
     */
    std::string get_generic_mangled_name(const std::string &base_name,
                                         const std::vector<Cryo::Type *> &type_args);

} // namespace Cryo::Codegen