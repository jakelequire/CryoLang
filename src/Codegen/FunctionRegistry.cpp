#include "Codegen/FunctionRegistry.hpp"
#include "AST/SymbolTable.hpp"
#include "AST/Type.hpp"
#include "AST/TypeChecker.hpp"
#include "Utils/Logger.hpp"
#include "Utils/SymbolResolutionManager.hpp"

#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>

#include <iostream>
#include <algorithm>
#include <regex>
#include <set>

namespace Cryo::Codegen
{

    FunctionRegistry::FunctionRegistry(const Cryo::SymbolTable &symbol_table, Cryo::TypeContext &type_context)
        : _symbol_table(symbol_table), _type_context(type_context)
    {
        // Initialize Symbol Resolution Manager
        try
        {
            auto context = std::make_unique<Cryo::SRM::SymbolResolutionContext>(
                &type_context);
            _symbol_resolution_manager = std::make_unique<Cryo::SRM::SymbolResolutionManager>(context.release());
        }
        catch (const std::exception &e)
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN, "Failed to initialize Symbol Resolution Manager in FunctionRegistry: {}", e.what());
            _symbol_resolution_manager = nullptr;
        }

        initialize_pattern_matchers();
    }

    FunctionMetadata FunctionRegistry::get_function_metadata(const std::string &function_name,
                                                             const std::string &resolved_namespace) const
    {
        // Special handling for runtime internal functions that need namespace qualification
        // when called from within the runtime namespace
        std::string qualified_function_name = function_name;
        if (resolved_namespace == "std::Runtime" && function_name.find("::") == std::string::npos)
        {
            // List of runtime functions that should be auto-qualified when called from within runtime namespace
            static const std::set<std::string> runtime_functions = {
                "cryo_alloc", "cryo_memcpy", "cryo_free", "cryo_realloc", "cryo_malloc",
                "cryo_strlen", "cryo_strcmp", "cryo_strcpy", "cryo_strcat", "cryo_runtime_allocate",
                "cryo_runtime_deallocate", "cryo_profile_start", "cryo_profile_end",
                "cryo_throw_exception", "cryo_runtime_initialize"};

            if (runtime_functions.find(function_name) != runtime_functions.end())
            {
                qualified_function_name = "std::Runtime::" + function_name;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Auto-qualifying runtime function '{}' to '{}'",
                          function_name, qualified_function_name);
            }
        }

        // Check cache first
        std::string cache_key = generate_qualified_function_name(resolved_namespace, qualified_function_name);
        auto cache_it = _metadata_cache.find(cache_key);
        if (cache_it != _metadata_cache.end())
        {
            return cache_it->second;
        }

        FunctionMetadata metadata;

        // First, try to classify from symbol table
        metadata = classify_from_symbol_table(qualified_function_name, resolved_namespace);
        if (metadata.category != FunctionCategory::Unknown)
        {
            _metadata_cache[cache_key] = metadata;
            return metadata;
        }

        // Try enum constructor classification before namespace-based classification
        metadata = classify_enum_constructor(qualified_function_name, resolved_namespace);
        if (metadata.category != FunctionCategory::Unknown)
        {
            _metadata_cache[cache_key] = metadata;
            return metadata;
        }

        // Then try namespace-based classification
        metadata = classify_from_namespace(qualified_function_name, resolved_namespace);
        if (metadata.category != FunctionCategory::Unknown)
        {
            _metadata_cache[cache_key] = metadata;
            return metadata;
        }

        // Try intrinsic classification
        metadata = classify_intrinsic_function(qualified_function_name);
        if (metadata.category != FunctionCategory::Unknown)
        {
            _metadata_cache[cache_key] = metadata;
            return metadata;
        }

        // Finally, apply pattern matchers
        for (const auto &matcher : _pattern_matchers)
        {
            metadata = matcher(qualified_function_name);
            if (metadata.category != FunctionCategory::Unknown)
            {
                _metadata_cache[cache_key] = metadata;
                return metadata;
            }
        }

        // Default fallback
        metadata.category = FunctionCategory::IntegerFunction; // Safe default
        metadata.runtime_name = qualified_function_name;
        _metadata_cache[cache_key] = metadata;
        return metadata;
    }

    llvm::Type *FunctionRegistry::get_function_return_type(const std::string &function_name,
                                                           llvm::LLVMContext &llvm_context,
                                                           const std::string &resolved_namespace) const
    {
        FunctionMetadata metadata = get_function_metadata(function_name, resolved_namespace);

        // ADD DEBUGGING: Log the metadata category that was determined
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "FunctionRegistry::get_function_return_type for '{}' in namespace '{}': category={}", 
                  function_name, resolved_namespace, static_cast<int>(metadata.category));

        // SPECIAL HANDLING: Check if we have a symbol with direct type information
        // This handles cases where primitive methods have direct type info but aren't FunctionType
        Cryo::Symbol *symbol = nullptr;
        if (!resolved_namespace.empty())
        {
            symbol = _symbol_table.lookup_namespaced_symbol(resolved_namespace, function_name);
        }
        if (!symbol)
        {
            symbol = _symbol_table.lookup_symbol(function_name);
        }

        if (symbol && symbol->data_type)
        {
            std::string type_name = symbol->data_type->name();
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "FunctionRegistry: Direct type mapping for '{}' with type '{}'", function_name, type_name);

            // Direct type mapping for primitive return types
            if (type_name == "u64")
            {
                return llvm::Type::getInt64Ty(llvm_context);
            }
            else if (type_name == "i64")
            {
                return llvm::Type::getInt64Ty(llvm_context);
            }
            else if (type_name == "u32" || type_name == "i32" || type_name == "int")
            {
                return llvm::Type::getInt32Ty(llvm_context);
            }
            else if (type_name == "u16" || type_name == "i16")
            {
                return llvm::Type::getInt16Ty(llvm_context);
            }
            else if (type_name == "u8" || type_name == "i8")
            {
                return llvm::Type::getInt8Ty(llvm_context);
            }
            else if (type_name == "string")
            {
                return llvm::PointerType::get(llvm_context, 0);
            }
            else if (type_name == "boolean")
            {
                return llvm::Type::getInt1Ty(llvm_context);
            }
            else if (type_name == "void")
            {
                return llvm::Type::getVoidTy(llvm_context);
            }
        }

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
                std::vector<llvm::Type *> fields = {
                    llvm::Type::getInt1Ty(llvm_context),    // discriminant
                    llvm::PointerType::get(llvm_context, 0) // value field
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
        
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "classify_from_symbol_table: '{}' in namespace '{}'", function_name, resolved_namespace);

        // Try to find the function in the symbol table
        Cryo::Symbol *symbol = nullptr;

        if (!resolved_namespace.empty())
        {
            // Try namespaced lookup first
            symbol = _symbol_table.lookup_namespaced_symbol(resolved_namespace, function_name);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Namespaced lookup for '{}' in '{}': {}", function_name, resolved_namespace, symbol ? "FOUND" : "NOT FOUND");
        }

        if (!symbol)
        {
            // Try direct lookup
            symbol = _symbol_table.lookup_symbol(function_name);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Direct lookup for '{}': {}", function_name, symbol ? "FOUND" : "NOT FOUND");
        }

        if (symbol && symbol->kind == Cryo::SymbolKind::Function && symbol->data_type)
        {
            // We found a function symbol with type information
            const FunctionType *func_type = dynamic_cast<const FunctionType *>(symbol->data_type.get());
            if (func_type)
            {
                // Classify based on the return type
                metadata.category = get_category_from_cryo_type(func_type->return_type().get());
                metadata.runtime_name = function_name;
                metadata.namespace_scope = symbol->scope;
                metadata.is_variadic = false; // TODO: Add variadic info to FunctionType if needed

                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Classified '{}' from symbol table as category {}",
                          function_name, static_cast<int>(metadata.category));

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

        // Extract runtime name - for stdlib functions, use the full qualified name
        std::string runtime_name = function_name;

        // For all namespaced functions, use the full qualified name as runtime name
        // This ensures functions called within their own namespace use their fully qualified names
        if (function_name.find("::") != std::string::npos)
        {
            runtime_name = function_name; // Use full qualified name for all namespaced functions
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Using qualified runtime name '{}' for namespaced function",
                      runtime_name);
        }

        // Use symbol table lookup to get actual function information instead of hardcoded patterns
        std::string qualified_name = generate_qualified_function_name(resolved_namespace, function_name);
        auto symbol = _symbol_table.lookup_symbol(qualified_name);
        if (!symbol)
        {
            // Try namespaced lookup
            symbol = _symbol_table.lookup_namespaced_symbol(resolved_namespace, function_name);
        }

        if (symbol && symbol->data_type)
        {
            // Classify function based on actual return type from symbol table
            auto *function_type = dynamic_cast<const FunctionType *>(symbol->data_type.get());
            if (function_type && function_type->return_type())
            {
                auto return_type_kind = function_type->return_type()->kind();
                std::string return_type_name = function_type->return_type()->name();
                
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "classify_from_namespace: function '{}' has return type '{}' (kind: {})", 
                          function_name, return_type_name, static_cast<int>(return_type_kind));
                
                switch (return_type_kind)
                {
                case TypeKind::Integer:
                    metadata.category = FunctionCategory::IntegerFunction;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Classified '{}' as IntegerFunction", function_name);
                    break;
                case TypeKind::Float:
                    metadata.category = FunctionCategory::FloatFunction;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Classified '{}' as FloatFunction", function_name);
                    break;
                case TypeKind::String:
                    metadata.category = FunctionCategory::StringFunction;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Classified '{}' as StringFunction", function_name);
                    break;
                case TypeKind::Boolean:
                    metadata.category = FunctionCategory::BooleanFunction;
                    LOG_ERROR(Cryo::LogComponent::CODEGEN, "CRITICAL: Function '{}' classified as BooleanFunction with return type '{}'", function_name, return_type_name);
                    break;
                case TypeKind::Void:
                    metadata.category = FunctionCategory::VoidFunction;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Classified '{}' as VoidFunction", function_name);
                    break;
                default:
                    metadata.category = FunctionCategory::VoidFunction;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Classified '{}' as VoidFunction (default case)", function_name);
                    break;
                }
            }
            else
            {
                metadata.category = FunctionCategory::IntegerFunction; // Safe fallback to prevent wrong boolean typing
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "No function type found for '{}', using IntegerFunction fallback", function_name);
            }
        }
        else
        {
            // Default to IntegerFunction for unknown functions rather than VoidFunction
            // This prevents functions from being incorrectly typed as BooleanFunction later
            metadata.category = FunctionCategory::IntegerFunction; // Safe fallback
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "No symbol found for '{}', using IntegerFunction fallback", function_name);
        }

        metadata.runtime_name = runtime_name;
        metadata.namespace_scope = resolved_namespace;

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Classified '{}' from namespace '{}' as category {} with runtime name '{}'",
                  function_name, resolved_namespace, static_cast<int>(metadata.category), runtime_name);

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

            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Classified '{}' as intrinsic function", function_name);

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

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Checking enum constructor: {}::{}", enum_name, variant_name);

        // Look up the enum type in the type context
        TypeRef enum_type = _type_context.lookup_enum_type(enum_name);
        if (!enum_type)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "No enum type found for: {}", enum_name);
            return metadata; // Not an enum
        }

        const Cryo::EnumType *cryo_enum_type = dynamic_cast<const Cryo::EnumType *>(enum_type.get());
        if (!cryo_enum_type)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Type is not an enum: {}", enum_name);
            return metadata; // Not a valid enum type
        }

        // Check if the variant exists in the enum
        const auto &variants = cryo_enum_type->variants();
        bool variant_found = std::find(variants.begin(), variants.end(), variant_name) != variants.end();

        if (!variant_found)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Variant {} not found in enum {}", variant_name, enum_name);
            return metadata; // Variant doesn't exist
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Detected enum constructor: {}", function_name);

        // This is an enum constructor
        metadata.category = FunctionCategory::StructFunction;
        metadata.runtime_name = variant_name; // Use just the variant name
        metadata.namespace_scope = enum_name;
        metadata.is_variadic = false;
        metadata.requires_this_pointer = false;
        std::vector<std::string> enum_parts = {enum_name};
        std::string qualified_variant = Cryo::SRM::Utils::build_qualified_name(enum_parts, variant_name);
        metadata.description = "Enum constructor for " + qualified_variant;

        return metadata;
    }

    FunctionCategory FunctionRegistry::get_category_from_cryo_type(const TypeRef cryo_type) const
    {
        if (!cryo_type)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_category_from_cryo_type: NULL type, returning Unknown");
            return FunctionCategory::Unknown;
        }

        // Check for pointer types first, before string name comparison
        if (dynamic_cast<const PointerType *>(cryo_type.get()) != nullptr)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_category_from_cryo_type: Detected PointerType '{}'", cryo_type->name());
            return FunctionCategory::PointerFunction;
        }

        std::string type_name = cryo_type->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_category_from_cryo_type: Categorizing type '{}'", type_name);

        if (type_name == "void")
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Type '{}' categorized as VoidFunction", type_name);
            return FunctionCategory::VoidFunction;
        }
        else if (type_name == "i8" || type_name == "i16" || type_name == "i32" || type_name == "i64" ||
                 type_name == "u8" || type_name == "u16" || type_name == "u32" || type_name == "u64" ||
                 type_name == "int")
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Type '{}' categorized as IntegerFunction", type_name);
            return FunctionCategory::IntegerFunction;
        }
        else if (type_name == "string" || type_name == "char*" || type_name.find("*") != std::string::npos)
        {
            // Handle any pointer type as string/pointer function
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Type '{}' categorized as PointerFunction", type_name);
            return FunctionCategory::PointerFunction;
        }
        else if (type_name == "boolean" || type_name == "bool")
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "CRITICAL: Type '{}' categorized as BooleanFunction - this may cause IR verification issues!", type_name);
            return FunctionCategory::BooleanFunction;
        }
        else if (type_name == "float" || type_name == "double" || type_name == "f32" || type_name == "f64")
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Type '{}' categorized as FloatFunction", type_name);
            return FunctionCategory::FloatFunction;
        }

        // For unknown types, use IntegerFunction as safe default instead of BooleanFunction
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Unknown type '{}', defaulting to IntegerFunction", type_name);
        return FunctionCategory::IntegerFunction;
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

    std::string FunctionRegistry::generate_qualified_function_name(const std::string &resolved_namespace, const std::string &function_name) const
    {
        // If function_name is already fully qualified (contains ::), return it as-is
        if (function_name.find("::") != std::string::npos)
        {
            return function_name;
        }

        if (_symbol_resolution_manager && !resolved_namespace.empty())
        {
            // Use SRM to generate qualified function name with the provided namespace
            std::vector<std::string> namespace_parts = Cryo::SRM::Utils::parse_qualified_name(resolved_namespace).first;
            if (namespace_parts.empty())
            {
                namespace_parts.push_back(resolved_namespace);
            }
            Cryo::SRM::QualifiedIdentifier qualified_id(namespace_parts, function_name, Cryo::SymbolKind::Function);
            return qualified_id.get_qualified_name();
        }
        else
        {
            // Fallback to manual concatenation
            if (resolved_namespace.empty())
            {
                return function_name;
            }
            else
            {
                std::vector<std::string> namespace_parts = {resolved_namespace};
                return Cryo::SRM::Utils::build_qualified_name(namespace_parts, function_name);
            }
        }
    }

} // namespace Cryo::Codegen