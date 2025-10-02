#include "Codegen/FunctionRegistry.hpp"
#include "AST/SymbolTable.hpp"
#include "AST/Type.hpp"
#include "AST/TypeChecker.hpp"

#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>

#include <iostream>
#include <algorithm>
#include <regex>

namespace Cryo::Codegen
{

    FunctionRegistry::FunctionRegistry(const Cryo::SymbolTable &symbol_table, Cryo::TypeContext &type_context)
        : _symbol_table(symbol_table), _type_context(type_context)
    {
        initialize_pattern_matchers();
    }

    FunctionMetadata FunctionRegistry::get_function_metadata(const std::string &function_name,
                                                             const std::string &resolved_namespace) const
    {
        // Check cache first
        std::string cache_key = resolved_namespace.empty() ? function_name : resolved_namespace + "::" + function_name;
        auto cache_it = _metadata_cache.find(cache_key);
        if (cache_it != _metadata_cache.end())
        {
            return cache_it->second;
        }

        FunctionMetadata metadata;

        // First, try to classify from symbol table
        metadata = classify_from_symbol_table(function_name, resolved_namespace);
        if (metadata.category != FunctionCategory::Unknown)
        {
            _metadata_cache[cache_key] = metadata;
            return metadata;
        }

        // Try enum constructor classification before namespace-based classification
        metadata = classify_enum_constructor(function_name, resolved_namespace);
        if (metadata.category != FunctionCategory::Unknown)
        {
            _metadata_cache[cache_key] = metadata;
            return metadata;
        }

        // Then try namespace-based classification
        metadata = classify_from_namespace(function_name, resolved_namespace);
        if (metadata.category != FunctionCategory::Unknown)
        {
            _metadata_cache[cache_key] = metadata;
            return metadata;
        }

        // Try intrinsic classification
        metadata = classify_intrinsic_function(function_name);
        if (metadata.category != FunctionCategory::Unknown)
        {
            _metadata_cache[cache_key] = metadata;
            return metadata;
        }

        // Finally, apply pattern matchers
        for (const auto &matcher : _pattern_matchers)
        {
            metadata = matcher(function_name);
            if (metadata.category != FunctionCategory::Unknown)
            {
                _metadata_cache[cache_key] = metadata;
                return metadata;
            }
        }

        // Default fallback
        metadata.category = FunctionCategory::IntegerFunction; // Safe default
        metadata.runtime_name = function_name;
        _metadata_cache[cache_key] = metadata;
        return metadata;
    }

    llvm::Type *FunctionRegistry::get_function_return_type(const std::string &function_name,
                                                           llvm::LLVMContext &llvm_context,
                                                           const std::string &resolved_namespace) const
    {
        FunctionMetadata metadata = get_function_metadata(function_name, resolved_namespace);
        
        // Special handling for enum constructors
        if (metadata.category == FunctionCategory::StructFunction)
        {
            // For enum constructors, we need to determine the actual enum type
            size_t scope_pos = function_name.find("::");
            if (scope_pos != std::string::npos)
            {
                std::string enum_name = function_name.substr(0, scope_pos);
                
                // Look up if this is a parameterized enum type
                // For cases like Result::Ok where we need Result<T,E>
                // We can't determine the exact instantiation here, so we use a generic struct type
                
                // Create a simple struct type as placeholder - the actual type will be resolved
                // when the enum constructor is generated
                std::vector<llvm::Type*> fields = {
                    llvm::Type::getInt1Ty(llvm_context),  // discriminant
                    llvm::PointerType::get(llvm_context, 0)  // value field
                };
                
                return llvm::StructType::get(llvm_context, fields);
            }
        }
        
        return category_to_llvm_type(metadata.category, llvm_context);
    }

    bool FunctionRegistry::is_runtime_function(const std::string &function_name) const
    {
        FunctionMetadata metadata = get_function_metadata(function_name);
        return metadata.category == FunctionCategory::RuntimeFunction ||
               metadata.category == FunctionCategory::SystemFunction;
    }

    void FunctionRegistry::register_pattern_matcher(std::function<FunctionMetadata(const std::string &)> pattern_matcher)
    {
        _pattern_matchers.push_back(pattern_matcher);
    }

    void FunctionRegistry::initialize_pattern_matchers()
    {
        // No hardcoded patterns here - this is intentionally empty to force
        // reliance on symbol table and namespace-based classification
    }

    FunctionMetadata FunctionRegistry::classify_from_symbol_table(const std::string &function_name,
                                                                  const std::string &resolved_namespace) const
    {
        FunctionMetadata metadata;

        // Try to find the function in the symbol table
        Cryo::Symbol *symbol = nullptr;

        if (!resolved_namespace.empty())
        {
            // Try namespaced lookup first
            symbol = _symbol_table.lookup_namespaced_symbol(resolved_namespace, function_name);
        }

        if (!symbol)
        {
            // Try direct lookup
            symbol = _symbol_table.lookup_symbol(function_name);
        }

        if (symbol && symbol->kind == Cryo::SymbolKind::Function && symbol->data_type)
        {
            // We found a function symbol with type information
            FunctionType *func_type = dynamic_cast<FunctionType *>(symbol->data_type);
            if (func_type)
            {
                // Classify based on the return type
                metadata.category = get_category_from_cryo_type(func_type->return_type().get());
                metadata.runtime_name = function_name;
                metadata.namespace_scope = symbol->scope;
                metadata.is_variadic = false; // TODO: Add variadic info to FunctionType if needed

                std::cout << "[FunctionRegistry] Classified '" << function_name
                          << "' from symbol table as category " << static_cast<int>(metadata.category) << std::endl;

                return metadata;
            }
        }

        // Check if it's an intrinsic function
        if (symbol && symbol->kind == Cryo::SymbolKind::Intrinsic)
        {
            metadata.category = FunctionCategory::IntrinsicFunction;
            metadata.runtime_name = function_name;
            metadata.namespace_scope = symbol->scope;
            return metadata;
        }

        return metadata; // Returns Unknown category
    }

    FunctionMetadata FunctionRegistry::classify_from_namespace(const std::string &function_name,
                                                               const std::string &resolved_namespace) const
    {
        FunctionMetadata metadata;

        // Extract runtime name - for stdlib functions, use just the function name without namespace
        std::string runtime_name = function_name;

        // Handle namespace-qualified function names like "std::String::_strlen" -> "_strlen"
        if (function_name.find("::") != std::string::npos)
        {
            // Extract the actual function name from the end
            size_t last_scope = function_name.rfind("::");
            if (last_scope != std::string::npos)
            {
                runtime_name = function_name.substr(last_scope + 2);
                std::cout << "[FunctionRegistry] Extracted runtime name '" << runtime_name
                          << "' from qualified name '" << function_name << "'" << std::endl;
            }
        }

        // Use symbol table lookup to get actual function information instead of hardcoded patterns
        std::string qualified_name = resolved_namespace + "::" + function_name;
        auto symbol = _symbol_table.lookup_symbol(qualified_name);
        if (!symbol)
        {
            // Try namespaced lookup
            symbol = _symbol_table.lookup_namespaced_symbol(resolved_namespace, function_name);
        }

        if (symbol && symbol->data_type)
        {
            // Classify function based on actual return type from symbol table
            auto *function_type = dynamic_cast<const FunctionType *>(symbol->data_type);
            if (function_type && function_type->return_type())
            {
                auto return_type_kind = function_type->return_type()->kind();
                switch (return_type_kind)
                {
                case TypeKind::Integer:
                    metadata.category = FunctionCategory::IntegerFunction;
                    break;
                case TypeKind::Float:
                    metadata.category = FunctionCategory::FloatFunction;
                    break;
                case TypeKind::String:
                    metadata.category = FunctionCategory::StringFunction;
                    break;
                case TypeKind::Boolean:
                    metadata.category = FunctionCategory::BooleanFunction;
                    break;
                case TypeKind::Void:
                    metadata.category = FunctionCategory::VoidFunction;
                    break;
                default:
                    metadata.category = FunctionCategory::VoidFunction;
                    break;
                }
            }
            else
            {
                metadata.category = FunctionCategory::VoidFunction; // fallback
            }
        }
        else
        {
            // Fallback for functions not in symbol table
            metadata.category = FunctionCategory::VoidFunction;
        }

        metadata.runtime_name = runtime_name;
        metadata.namespace_scope = resolved_namespace;

        std::cout << "[FunctionRegistry] Classified '" << function_name
                  << "' from namespace '" << resolved_namespace
                  << "' as category " << static_cast<int>(metadata.category)
                  << " with runtime name '" << runtime_name << "'" << std::endl;

        return metadata;
    }

    FunctionMetadata FunctionRegistry::classify_intrinsic_function(const std::string &function_name) const
    {
        FunctionMetadata metadata;

        // Classify intrinsic functions by their naming convention (double underscores)
        if (function_name.length() > 4 &&
            function_name.substr(0, 2) == "__" &&
            function_name.substr(function_name.length() - 2) == "__")
        {

            metadata.category = FunctionCategory::IntrinsicFunction;
            metadata.runtime_name = function_name;
            metadata.namespace_scope = "intrinsic";

            std::cout << "[FunctionRegistry] Classified '" << function_name
                      << "' as intrinsic function" << std::endl;

            return metadata;
        }

        return metadata; // Returns Unknown category
    }

    FunctionMetadata FunctionRegistry::classify_enum_constructor(const std::string &function_name,
                                                                 const std::string &resolved_namespace) const
    {
        FunctionMetadata metadata;

        // Check if this looks like an enum constructor: EnumName::VariantName
        size_t scope_pos = function_name.find("::");
        if (scope_pos == std::string::npos)
        {
            return metadata; // Not a scoped name
        }

        std::string enum_name = function_name.substr(0, scope_pos);
        std::string variant_name = function_name.substr(scope_pos + 2);

        std::cout << "[FunctionRegistry] Checking enum constructor: " << enum_name 
                  << "::" << variant_name << std::endl;

        // Look up the enum type in the type context
        Cryo::Type *enum_type = _type_context.lookup_enum_type(enum_name);
        if (!enum_type)
        {
            std::cout << "[FunctionRegistry] No enum type found for: " << enum_name << std::endl;
            return metadata; // Not an enum
        }

        Cryo::EnumType *cryo_enum_type = dynamic_cast<Cryo::EnumType *>(enum_type);
        if (!cryo_enum_type)
        {
            std::cout << "[FunctionRegistry] Type is not an enum: " << enum_name << std::endl;
            return metadata; // Not a valid enum type
        }

        // Check if the variant exists in the enum
        const auto &variants = cryo_enum_type->variants();
        bool variant_found = std::find(variants.begin(), variants.end(), variant_name) != variants.end();
        
        if (!variant_found)
        {
            std::cout << "[FunctionRegistry] Variant " << variant_name 
                      << " not found in enum " << enum_name << std::endl;
            return metadata; // Variant doesn't exist
        }

        std::cout << "[FunctionRegistry] Detected enum constructor: " << function_name << std::endl;

        // This is an enum constructor
        metadata.category = FunctionCategory::StructFunction;
        metadata.runtime_name = variant_name; // Use just the variant name
        metadata.namespace_scope = enum_name;
        metadata.is_variadic = false;
        metadata.requires_this_pointer = false;
        metadata.description = "Enum constructor for " + enum_name + "::" + variant_name;

        return metadata;
    }

    FunctionCategory FunctionRegistry::get_category_from_cryo_type(const Cryo::Type *cryo_type) const
    {
        if (!cryo_type)
        {
            return FunctionCategory::Unknown;
        }

        std::string type_name = cryo_type->name();

        if (type_name == "void")
        {
            return FunctionCategory::VoidFunction;
        }
        else if (type_name == "i8" || type_name == "i16" || type_name == "i32" || type_name == "i64" ||
                 type_name == "u8" || type_name == "u16" || type_name == "u32" || type_name == "u64" ||
                 type_name == "int")
        {
            return FunctionCategory::IntegerFunction;
        }
        else if (type_name == "string" || type_name == "char*")
        {
            return FunctionCategory::StringFunction;
        }
        else if (type_name == "boolean")
        {
            return FunctionCategory::BooleanFunction;
        }
        else if (dynamic_cast<const PointerType *>(cryo_type) != nullptr)
        {
            return FunctionCategory::PointerFunction;
        }

        return FunctionCategory::IntegerFunction; // Safe default for unknown types
    }

    llvm::Type *FunctionRegistry::category_to_llvm_type(FunctionCategory category, llvm::LLVMContext &context) const
    {
        switch (category)
        {
        case FunctionCategory::VoidFunction:
            return llvm::Type::getVoidTy(context);
        case FunctionCategory::IntegerFunction:
            return llvm::Type::getInt32Ty(context);
        case FunctionCategory::StringFunction:
            return llvm::PointerType::get(context, 0); // char*
        case FunctionCategory::BooleanFunction:
            return llvm::Type::getInt1Ty(context);
        case FunctionCategory::PointerFunction:
            return llvm::PointerType::get(context, 0);
        case FunctionCategory::StructFunction:
            // For enum constructors, we need to look up the actual struct type
            // This is a placeholder - the actual type should be resolved elsewhere
            return llvm::PointerType::get(context, 0); // Safe fallback
        case FunctionCategory::IntrinsicFunction:
        case FunctionCategory::SystemFunction:
        case FunctionCategory::RuntimeFunction:
            return llvm::Type::getInt32Ty(context); // Safe default
        case FunctionCategory::Unknown:
        default:
            return llvm::Type::getInt32Ty(context); // Safe default
        }
    }

} // namespace Cryo::Codegen