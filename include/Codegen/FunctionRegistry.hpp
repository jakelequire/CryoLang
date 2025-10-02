#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <memory>

// Forward declarations
namespace llvm
{
    class Type;
    class LLVMContext;
}

namespace Cryo
{
    class SymbolTable;
    class TypeContext;
    class Type;
    class FunctionType;
}

namespace Cryo::Codegen
{

    /**
     * @brief Categories of functions based on their return types and behaviors
     */
    enum class FunctionCategory
    {
        Unknown,
        VoidFunction,      // Functions returning void (print, println, etc.)
        IntegerFunction,   // Functions returning integers (get_value, length, etc.)
        StringFunction,    // Functions returning strings
        BooleanFunction,   // Functions returning booleans
        FloatFunction,     // Functions returning floats
        PointerFunction,   // Functions returning pointers (malloc, etc.)
        StructFunction,    // Functions returning structs (enum constructors, etc.)
        IntrinsicFunction, // Compiler intrinsics
        SystemFunction,    // System calls
        RuntimeFunction    // Runtime library functions
    };

    /**
     * @brief Metadata about a function's characteristics
     */
    struct FunctionMetadata
    {
        FunctionCategory category;
        std::string runtime_name;    // The actual C runtime function name
        std::string namespace_scope; // Namespace this function belongs to
        bool is_variadic;
        bool requires_this_pointer; // For method calls
        std::string description;

        FunctionMetadata()
            : category(FunctionCategory::Unknown), is_variadic(false), requires_this_pointer(false) {}
    };

    /**
     * @brief Function pattern matching system to avoid hardcoded function names
     *
     * This registry classifies functions based on their signatures, namespaces,
     * and symbol table information rather than hardcoded string matching.
     */
    class FunctionRegistry
    {
    public:
        /**
         * @brief Initialize the function registry with symbol table and type context
         */
        FunctionRegistry(const Cryo::SymbolTable &symbol_table, Cryo::TypeContext &type_context);

        /**
         * @brief Get function metadata by looking up in symbol table and applying rules
         * @param function_name The function name to classify
         * @param resolved_namespace Optional: resolved namespace context
         * @return Function metadata with inferred category and properties
         */
        FunctionMetadata get_function_metadata(const std::string &function_name,
                                               const std::string &resolved_namespace = "") const;

        /**
         * @brief Get LLVM return type for a function based on its metadata
         * @param function_name The function name
         * @param llvm_context LLVM context for type creation
         * @param resolved_namespace Optional: resolved namespace context
         * @return Appropriate LLVM return type or nullptr if unknown
         */
        llvm::Type *get_function_return_type(const std::string &function_name,
                                             llvm::LLVMContext &llvm_context,
                                             const std::string &resolved_namespace = "") const;

        /**
         * @brief Check if a function is a known runtime function
         * @param function_name The function name to check
         * @return true if this is a known runtime function
         */
        bool is_runtime_function(const std::string &function_name) const;

        /**
         * @brief Register additional runtime function patterns
         * @param pattern_matcher Function that takes a name and returns metadata
         */
        void register_pattern_matcher(std::function<FunctionMetadata(const std::string &)> pattern_matcher);

    private:
        const Cryo::SymbolTable &_symbol_table;
        Cryo::TypeContext &_type_context;

        // Pattern matchers for different function types
        std::vector<std::function<FunctionMetadata(const std::string &)>> _pattern_matchers;

        // Cache for performance
        mutable std::unordered_map<std::string, FunctionMetadata> _metadata_cache;

        /**
         * @brief Initialize built-in pattern matchers
         */
        void initialize_pattern_matchers();

        /**
         * @brief Classify function based on symbol table information
         */
        FunctionMetadata classify_from_symbol_table(const std::string &function_name,
                                                    const std::string &resolved_namespace) const;

        /**
         * @brief Classify function based on namespace patterns
         */
        FunctionMetadata classify_from_namespace(const std::string &function_name,
                                                 const std::string &resolved_namespace) const;

        /**
         * @brief Classify function based on intrinsic patterns
         */
        FunctionMetadata classify_intrinsic_function(const std::string &function_name) const;

        /**
         * @brief Classify function as enum constructor if it matches the pattern
         */
        FunctionMetadata classify_enum_constructor(const std::string &function_name,
                                                   const std::string &resolved_namespace) const;

        /**
         * @brief Get return type category from Cryo type
         */
        FunctionCategory get_category_from_cryo_type(const Cryo::Type *cryo_type) const;

        /**
         * @brief Convert function category to LLVM type
         */
        llvm::Type *category_to_llvm_type(FunctionCategory category, llvm::LLVMContext &context) const;
    };

} // namespace Cryo::Codegen