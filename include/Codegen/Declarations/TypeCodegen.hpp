#pragma once

#include "Codegen/ICodegenComponent.hpp"
#include "Codegen/CodegenContext.hpp"

#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Value.h>
#include <string>
#include <unordered_map>

namespace Cryo::Codegen
{
    /**
     * @brief Handles type-related code generation
     *
     * This class centralizes:
     * - Type conversion and casting
     * - Type layout computation
     * - Generic type instantiation
     * - Type compatibility checking
     * - Pointer/reference handling
     */
    class TypeCodegen : public ICodegenComponent
    {
    public:
        //===================================================================
        // Construction
        //===================================================================

        explicit TypeCodegen(CodegenContext &ctx);
        ~TypeCodegen() = default;

        //===================================================================
        // Type Resolution
        //===================================================================

        /**
         * @brief Get LLVM type for a Cryo type
         * @param cryo_type Cryo type
         * @return Corresponding LLVM type
         */
        llvm::Type *resolve_type(Cryo::Type *cryo_type);

        /**
         * @brief Get LLVM type by name
         * @param name Type name
         * @return LLVM type or nullptr
         */
        llvm::Type *resolve_type_by_name(const std::string &name);

        /**
         * @brief Resolve a generic type instantiation
         * @param base_type Base generic type name
         * @param type_args Type arguments
         * @return Instantiated type
         */
        llvm::Type *resolve_generic_type(const std::string &base_type,
                                          const std::vector<Cryo::Type *> &type_args);

        //===================================================================
        // Type Casting
        //===================================================================

        /**
         * @brief Cast a value to a target type
         * @param value Source value
         * @param target_type Target LLVM type
         * @param name Optional name for result
         * @return Cast value
         */
        llvm::Value *cast_value(llvm::Value *value, llvm::Type *target_type,
                                 const std::string &name = "");

        /**
         * @brief Perform integer type conversion
         * @param value Source integer value
         * @param target_type Target integer type
         * @param is_signed Whether source is signed
         * @return Converted value
         */
        llvm::Value *convert_integer(llvm::Value *value, llvm::Type *target_type,
                                      bool is_signed = true);

        /**
         * @brief Perform floating-point type conversion
         * @param value Source float value
         * @param target_type Target float type
         * @return Converted value
         */
        llvm::Value *convert_float(llvm::Value *value, llvm::Type *target_type);

        /**
         * @brief Convert between integer and float
         * @param value Source value
         * @param target_type Target type
         * @param source_signed Whether source integer is signed
         * @return Converted value
         */
        llvm::Value *convert_int_float(llvm::Value *value, llvm::Type *target_type,
                                        bool source_signed = true);

        /**
         * @brief Convert pointer types
         * @param value Source pointer
         * @param target_type Target pointer type
         * @return Bitcast pointer
         */
        llvm::Value *convert_pointer(llvm::Value *value, llvm::Type *target_type);

        //===================================================================
        // Type Compatibility
        //===================================================================

        /**
         * @brief Check if two types are compatible (can be converted)
         * @param from Source type
         * @param to Target type
         * @return true if conversion is possible
         */
        bool are_compatible(llvm::Type *from, llvm::Type *to) const;

        /**
         * @brief Check if conversion would be lossy
         * @param from Source type
         * @param to Target type
         * @return true if conversion may lose information
         */
        bool is_lossy_conversion(llvm::Type *from, llvm::Type *to) const;

        /**
         * @brief Get common type for two types (for binary operations)
         * @param lhs Left type
         * @param rhs Right type
         * @return Common type, or nullptr if incompatible
         */
        llvm::Type *get_common_type(llvm::Type *lhs, llvm::Type *rhs);

        //===================================================================
        // Type Layout
        //===================================================================

        /**
         * @brief Get size of a type in bytes
         * @param type LLVM type
         * @return Size in bytes
         */
        uint64_t get_type_size(llvm::Type *type) const;

        /**
         * @brief Get alignment of a type in bytes
         * @param type LLVM type
         * @return Alignment in bytes
         */
        uint64_t get_type_alignment(llvm::Type *type) const;

        /**
         * @brief Get field offset within a struct
         * @param struct_type Struct type
         * @param field_index Field index
         * @return Offset in bytes
         */
        uint64_t get_field_offset(llvm::StructType *struct_type, unsigned field_index) const;

        //===================================================================
        // Pointer Operations
        //===================================================================

        /**
         * @brief Get pointer type for a base type
         * @param base_type Base type
         * @param address_space Address space (default 0)
         * @return Pointer type
         */
        llvm::PointerType *get_pointer_type(llvm::Type *base_type, unsigned address_space = 0);

        /**
         * @brief Get pointee type from a pointer
         * @param ptr_type Pointer type
         * @return Pointee type (for typed pointers) or nullptr
         */
        llvm::Type *get_pointee_type(llvm::Type *ptr_type) const;

        /**
         * @brief Check if type is a pointer
         * @param type Type to check
         * @return true if pointer type
         */
        bool is_pointer_type(llvm::Type *type) const;

        //===================================================================
        // Array Operations
        //===================================================================

        /**
         * @brief Get array type
         * @param element_type Element type
         * @param size Array size
         * @return Array type
         */
        llvm::ArrayType *get_array_type(llvm::Type *element_type, uint64_t size);

        /**
         * @brief Get element type from array
         * @param array_type Array type
         * @return Element type
         */
        llvm::Type *get_element_type(llvm::ArrayType *array_type) const;

        //===================================================================
        // Struct Operations
        //===================================================================

        /**
         * @brief Create or get a struct type
         * @param name Struct name
         * @param field_types Field types
         * @param packed Whether struct is packed
         * @return Struct type
         */
        llvm::StructType *get_struct_type(const std::string &name,
                                           const std::vector<llvm::Type *> &field_types,
                                           bool packed = false);

        /**
         * @brief Get field type from struct
         * @param struct_type Struct type
         * @param field_index Field index
         * @return Field type
         */
        llvm::Type *get_field_type(llvm::StructType *struct_type, unsigned field_index) const;

        /**
         * @brief Get number of fields in struct
         * @param struct_type Struct type
         * @return Number of fields
         */
        unsigned get_num_fields(llvm::StructType *struct_type) const;

    private:
        //===================================================================
        // Internal Helpers
        //===================================================================

        /**
         * @brief Get integer bit width
         * @param type Integer type
         * @return Bit width, or 0 if not integer
         */
        unsigned get_int_bit_width(llvm::Type *type) const;

        /**
         * @brief Check if type is floating point
         * @param type Type to check
         * @return true if float/double
         */
        bool is_floating_point(llvm::Type *type) const;

        /**
         * @brief Determine if integer type is signed based on Cryo type
         * @param cryo_type Original Cryo type
         * @return true if signed
         */
        bool is_signed_integer(Cryo::Type *cryo_type) const;

        // Cache for generic instantiations
        std::unordered_map<std::string, llvm::Type *> _generic_cache;
    };

} // namespace Cryo::Codegen
