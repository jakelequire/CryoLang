#include "Codegen/Declarations/TypeCodegen.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "Codegen/Declarations/GenericCodegen.hpp"
#include "Types/Types.hpp"
#include "Types/TypeAnnotation.hpp"
#include "Types/UserDefinedTypes.hpp"
#include "Utils/Logger.hpp"
#include "AST/TemplateRegistry.hpp"

namespace Cryo::Codegen
{
    // Helper: parse type arguments from an annotation string like "string, int" into TypeRefs.
    // Handles nested generics (e.g., "Array<u8>, int") and pointer modifiers (e.g., "Expr*").
    static std::vector<TypeRef> parse_type_args_for_field(
        const std::string &type_args_str,
        Cryo::SymbolTable &symbols)
    {
        std::vector<TypeRef> type_args;
        if (type_args_str.empty())
            return type_args;

        // Parse comma-separated type arguments, handling nested generics
        std::vector<std::string> arg_names;
        size_t start = 0;
        int depth = 0;
        for (size_t i = 0; i <= type_args_str.length(); ++i)
        {
            if (i == type_args_str.length() || (type_args_str[i] == ',' && depth == 0))
            {
                std::string arg = type_args_str.substr(start, i - start);
                while (!arg.empty() && std::isspace(static_cast<unsigned char>(arg.front())))
                    arg.erase(0, 1);
                while (!arg.empty() && std::isspace(static_cast<unsigned char>(arg.back())))
                    arg.pop_back();
                if (!arg.empty())
                    arg_names.push_back(arg);
                start = i + 1;
            }
            else if (type_args_str[i] == '<')
                depth++;
            else if (type_args_str[i] == '>')
                depth--;
        }

        for (const auto &arg_name : arg_names)
        {
            TypeRef arg_type{};
            std::string base_name = arg_name;

            // Strip pointer modifiers
            int pointer_depth = 0;
            while (!base_name.empty() && base_name.back() == '*')
            {
                base_name.pop_back();
                pointer_depth++;
            }
            while (!base_name.empty() && std::isspace(static_cast<unsigned char>(base_name.back())))
                base_name.pop_back();

            // Try symbol table lookup
            Symbol *type_sym = symbols.lookup_symbol(base_name);
            if (type_sym && type_sym->kind == SymbolKind::Type && type_sym->type.is_valid())
                arg_type = type_sym->type;

            // Try common primitive types
            if (!arg_type.is_valid())
            {
                if (base_name == "int" || base_name == "i32")
                    arg_type = symbols.arena().get_i32();
                else if (base_name == "i8")
                    arg_type = symbols.arena().get_i8();
                else if (base_name == "i16")
                    arg_type = symbols.arena().get_i16();
                else if (base_name == "i64")
                    arg_type = symbols.arena().get_i64();
                else if (base_name == "u8")
                    arg_type = symbols.arena().get_u8();
                else if (base_name == "u16")
                    arg_type = symbols.arena().get_u16();
                else if (base_name == "u32")
                    arg_type = symbols.arena().get_u32();
                else if (base_name == "u64")
                    arg_type = symbols.arena().get_u64();
                else if (base_name == "f32" || base_name == "float")
                    arg_type = symbols.arena().get_f32();
                else if (base_name == "f64" || base_name == "double")
                    arg_type = symbols.arena().get_f64();
                else if (base_name == "string" || base_name == "String")
                    arg_type = symbols.arena().get_string();
                else if (base_name == "bool" || base_name == "boolean")
                    arg_type = symbols.arena().get_bool();
            }

            // Try TypeArena name lookup
            if (!arg_type.is_valid())
                arg_type = symbols.arena().lookup_type_by_name(base_name);

            // Wrap in pointer type(s)
            if (arg_type.is_valid() && pointer_depth > 0)
            {
                for (int p = 0; p < pointer_depth; ++p)
                    arg_type = symbols.arena().get_pointer_to(arg_type);
            }

            if (arg_type.is_valid())
                type_args.push_back(arg_type);
            else
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "parse_type_args_for_field: Could not resolve type argument '{}'", arg_name);
        }
        return type_args;
    }
    // Helper: Create or look up a dynamic Array<T> LLVM struct type from a type annotation.
    // Dynamic arrays are always { ptr, i64, i64 } (elements, length, capacity), regardless
    // of element type. This allows generating the correct LLVM type even when the element
    // type is a cross-module forward reference that hasn't been resolved yet.
    static llvm::StructType *get_or_create_array_struct_from_annotation(
        const Cryo::TypeAnnotation *annotation,
        llvm::LLVMContext &llvm_ctx)
    {
        if (!annotation || annotation->kind != Cryo::TypeAnnotationKind::Array)
            return nullptr;

        // Only handle dynamic arrays (no fixed size)
        if (annotation->array_size.has_value())
            return nullptr;

        // Get element type name from annotation for naming the struct
        std::string elem_name = annotation->inner ? annotation->inner->to_string() : "unknown";
        std::string array_name = "Array<" + elem_name + ">";

        // Check if it already exists
        llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx, array_name);
        if (existing)
            return existing;

        // Create the struct: { ptr (elements), i64 (length), i64 (capacity) }
        std::vector<llvm::Type *> fields = {
            llvm::PointerType::get(llvm_ctx, 0),    // elements: T*
            llvm::Type::getInt64Ty(llvm_ctx),        // length: u64
            llvm::Type::getInt64Ty(llvm_ctx)         // capacity: u64
        };

        return llvm::StructType::create(llvm_ctx, fields, array_name);
    }

    //===================================================================
    // Construction
    //===================================================================

    TypeCodegen::TypeCodegen(CodegenContext &ctx)
        : ICodegenComponent(ctx)
    {
    }

    //===================================================================
    // Type Resolution
    //===================================================================

    llvm::Type *TypeCodegen::resolve_type(TypeRef cryo_type)
    {
        if (!cryo_type)
            return nullptr;

        return types().get_type(cryo_type);
    }

    llvm::Type *TypeCodegen::resolve_type_by_name(const std::string &name)
    {
        // Check context registry first
        if (llvm::Type *type = ctx().get_type(name))
        {
            return type;
        }

        // Check LLVM context for named structs
        if (llvm::StructType *struct_type = llvm::StructType::getTypeByName(llvm_ctx(), name))
        {
            return struct_type;
        }

        // Try type mapper
        return types().get_type(name);
    }

    llvm::Type *TypeCodegen::resolve_generic_type(const std::string &base_type,
                                                  const std::vector<TypeRef> &type_args)
    {
        // Build cache key
        std::string cache_key = base_type + "<";
        for (size_t i = 0; i < type_args.size(); ++i)
        {
            if (i > 0)
                cache_key += ",";
            if (type_args[i])
            {
                cache_key += type_args[i].get()->display_name();
            }
        }
        cache_key += ">";

        // Check cache
        auto it = _generic_cache.find(cache_key);
        if (it != _generic_cache.end())
        {
            return it->second;
        }

        // Look up base generic type
        llvm::Type *base = resolve_type_by_name(base_type);
        if (!base)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "Unknown generic base type: {}", base_type);
            report_error(ErrorCode::E0637_INVALID_GENERIC_INSTANTIATION,
                         "Unknown generic base type: " + base_type);
            return nullptr;
        }

        // For struct types, we need to instantiate with the type arguments
        // This is a simplified version - full generic support would be more complex
        if (auto *struct_type = llvm::dyn_cast<llvm::StructType>(base))
        {
            // Create new struct with substituted types
            std::string instantiated_name = cache_key;
            llvm::StructType *new_type = llvm::StructType::create(llvm_ctx(), instantiated_name);

            // For now, just copy the structure
            // Full implementation would substitute type parameters
            if (!struct_type->isOpaque())
            {
                std::vector<llvm::Type *> elements;
                for (unsigned i = 0; i < struct_type->getNumElements(); ++i)
                {
                    elements.push_back(struct_type->getElementType(i));
                }
                new_type->setBody(elements);
            }

            _generic_cache[cache_key] = new_type;
            return new_type;
        }

        return base;
    }

    //===================================================================
    // Type Casting
    //===================================================================

    llvm::Value *TypeCodegen::cast_value(llvm::Value *value, llvm::Type *target_type,
                                         const std::string &name)
    {
        if (!value || !target_type)
            return value;

        llvm::Type *source_type = value->getType();

        // Same type - no cast needed
        if (source_type == target_type)
        {
            return value;
        }

        std::string cast_name = name.empty() ? "cast" : name;

        // Integer to integer
        if (source_type->isIntegerTy() && target_type->isIntegerTy())
        {
            return convert_integer(value, target_type, true);
        }

        // Float to float
        if (source_type->isFloatingPointTy() && target_type->isFloatingPointTy())
        {
            return convert_float(value, target_type);
        }

        // Integer to float
        if (source_type->isIntegerTy() && target_type->isFloatingPointTy())
        {
            return convert_int_float(value, target_type, true);
        }

        // Float to integer
        if (source_type->isFloatingPointTy() && target_type->isIntegerTy())
        {
            return convert_int_float(value, target_type, true);
        }

        // Pointer conversions
        if (source_type->isPointerTy() && target_type->isPointerTy())
        {
            return convert_pointer(value, target_type);
        }

        // Pointer to integer
        if (source_type->isPointerTy() && target_type->isIntegerTy())
        {
            return builder().CreatePtrToInt(value, target_type, cast_name);
        }

        // Integer to pointer
        if (source_type->isIntegerTy() && target_type->isPointerTy())
        {
            return builder().CreateIntToPtr(value, target_type, cast_name);
        }

        LOG_WARN(Cryo::LogComponent::CODEGEN, "Unable to cast between incompatible types");
        return value;
    }

    llvm::Value *TypeCodegen::convert_integer(llvm::Value *value, llvm::Type *target_type,
                                              bool is_signed)
    {
        if (!value || !target_type)
            return value;

        llvm::Type *source_type = value->getType();
        if (!source_type->isIntegerTy() || !target_type->isIntegerTy())
        {
            return value;
        }

        unsigned src_bits = source_type->getIntegerBitWidth();
        unsigned dst_bits = target_type->getIntegerBitWidth();

        if (dst_bits > src_bits)
        {
            // Extension
            if (is_signed)
            {
                return builder().CreateSExt(value, target_type, "sext");
            }
            else
            {
                return builder().CreateZExt(value, target_type, "zext");
            }
        }
        else if (dst_bits < src_bits)
        {
            // Truncation
            return builder().CreateTrunc(value, target_type, "trunc");
        }

        return value;
    }

    llvm::Value *TypeCodegen::convert_float(llvm::Value *value, llvm::Type *target_type)
    {
        if (!value || !target_type)
            return value;

        llvm::Type *source_type = value->getType();
        if (!source_type->isFloatingPointTy() || !target_type->isFloatingPointTy())
        {
            return value;
        }

        // Get sizes
        bool src_is_double = source_type->isDoubleTy();
        bool dst_is_double = target_type->isDoubleTy();

        if (!src_is_double && dst_is_double)
        {
            // Float to double (extension)
            return builder().CreateFPExt(value, target_type, "fpext");
        }
        else if (src_is_double && !dst_is_double)
        {
            // Double to float (truncation)
            return builder().CreateFPTrunc(value, target_type, "fptrunc");
        }

        return value;
    }

    llvm::Value *TypeCodegen::convert_int_float(llvm::Value *value, llvm::Type *target_type,
                                                bool source_signed)
    {
        if (!value || !target_type)
            return value;

        llvm::Type *source_type = value->getType();

        // Integer to float
        if (source_type->isIntegerTy() && target_type->isFloatingPointTy())
        {
            if (source_signed)
            {
                return builder().CreateSIToFP(value, target_type, "sitofp");
            }
            else
            {
                return builder().CreateUIToFP(value, target_type, "uitofp");
            }
        }

        // Float to integer
        if (source_type->isFloatingPointTy() && target_type->isIntegerTy())
        {
            if (source_signed)
            {
                return builder().CreateFPToSI(value, target_type, "fptosi");
            }
            else
            {
                return builder().CreateFPToUI(value, target_type, "fptoui");
            }
        }

        return value;
    }

    llvm::Value *TypeCodegen::convert_pointer(llvm::Value *value, llvm::Type *target_type)
    {
        if (!value || !target_type)
            return value;

        if (!value->getType()->isPointerTy() || !target_type->isPointerTy())
        {
            return value;
        }

        // With opaque pointers, we can just return the value
        // In older LLVM, we would use CreateBitCast
        return value;
    }

    //===================================================================
    // Type Compatibility
    //===================================================================

    bool TypeCodegen::are_compatible(llvm::Type *from, llvm::Type *to) const
    {
        if (!from || !to)
            return false;

        if (from == to)
            return true;

        // Integer compatibility
        if (from->isIntegerTy() && to->isIntegerTy())
        {
            return true;
        }

        // Float compatibility
        if (from->isFloatingPointTy() && to->isFloatingPointTy())
        {
            return true;
        }

        // Integer <-> Float
        if ((from->isIntegerTy() && to->isFloatingPointTy()) ||
            (from->isFloatingPointTy() && to->isIntegerTy()))
        {
            return true;
        }

        // Pointer compatibility
        if (from->isPointerTy() && to->isPointerTy())
        {
            return true;
        }

        return false;
    }

    bool TypeCodegen::is_lossy_conversion(llvm::Type *from, llvm::Type *to) const
    {
        if (!from || !to)
            return true;

        // Same type - not lossy
        if (from == to)
            return false;

        // Integer truncation
        if (from->isIntegerTy() && to->isIntegerTy())
        {
            return from->getIntegerBitWidth() > to->getIntegerBitWidth();
        }

        // Float truncation
        if (from->isFloatingPointTy() && to->isFloatingPointTy())
        {
            return from->isDoubleTy() && to->isFloatTy();
        }

        // Float to integer is always potentially lossy
        if (from->isFloatingPointTy() && to->isIntegerTy())
        {
            return true;
        }

        return false;
    }

    llvm::Type *TypeCodegen::get_common_type(llvm::Type *lhs, llvm::Type *rhs)
    {
        if (!lhs || !rhs)
            return nullptr;

        if (lhs == rhs)
            return lhs;

        // Both integers - use larger
        if (lhs->isIntegerTy() && rhs->isIntegerTy())
        {
            unsigned lhs_bits = lhs->getIntegerBitWidth();
            unsigned rhs_bits = rhs->getIntegerBitWidth();
            return (lhs_bits >= rhs_bits) ? lhs : rhs;
        }

        // Both floats - use larger
        if (lhs->isFloatingPointTy() && rhs->isFloatingPointTy())
        {
            return (lhs->isDoubleTy() || rhs->isDoubleTy())
                       ? llvm::Type::getDoubleTy(llvm_ctx())
                       : llvm::Type::getFloatTy(llvm_ctx());
        }

        // Mixed int/float - prefer float
        if ((lhs->isIntegerTy() && rhs->isFloatingPointTy()) ||
            (lhs->isFloatingPointTy() && rhs->isIntegerTy()))
        {
            return lhs->isFloatingPointTy() ? lhs : rhs;
        }

        // Both pointers - use first
        if (lhs->isPointerTy() && rhs->isPointerTy())
        {
            return lhs;
        }

        return nullptr;
    }

    //===================================================================
    // Type Layout
    //===================================================================

    uint64_t TypeCodegen::get_type_size(llvm::Type *type) const
    {
        if (!type)
            return 0;

        // Check if the type is sized before computing size
        // Opaque structs (structs without a body) are unsized and would cause LLVM to assert
        if (!type->isSized())
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN,
                     "TypeCodegen::get_type_size called on unsized type, returning 0");
            return 0;
        }

        const llvm::DataLayout &dl = module()->getDataLayout();
        return dl.getTypeAllocSize(type);
    }

    uint64_t TypeCodegen::get_type_alignment(llvm::Type *type) const
    {
        if (!type)
            return 0;

        // Check if the type is sized before computing alignment
        // Opaque structs (structs without a body) are unsized
        if (!type->isSized())
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN,
                     "TypeCodegen::get_type_alignment called on unsized type, returning default alignment");
            return 8; // Default to 8-byte alignment
        }

        const llvm::DataLayout &dl = module()->getDataLayout();
        return dl.getABITypeAlign(type).value();
    }

    uint64_t TypeCodegen::get_field_offset(llvm::StructType *struct_type, unsigned field_index) const
    {
        if (!struct_type || field_index >= struct_type->getNumElements())
            return 0;

        // Check if the struct is opaque (unsized) before computing layout
        if (struct_type->isOpaque())
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN,
                     "TypeCodegen::get_field_offset called on opaque struct, returning 0");
            return 0;
        }

        const llvm::DataLayout &dl = module()->getDataLayout();
        const llvm::StructLayout *layout = dl.getStructLayout(struct_type);
        return layout->getElementOffset(field_index);
    }

    //===================================================================
    // Pointer Operations
    //===================================================================

    llvm::PointerType *TypeCodegen::get_pointer_type(llvm::Type *base_type, unsigned address_space)
    {
        // With opaque pointers in newer LLVM, all pointers are the same type
        return llvm::PointerType::get(llvm_ctx(), address_space);
    }

    llvm::Type *TypeCodegen::get_pointee_type(llvm::Type *ptr_type) const
    {
        // With opaque pointers, we can't get pointee type from the pointer itself
        // The caller must track this separately
        return nullptr;
    }

    bool TypeCodegen::is_pointer_type(llvm::Type *type) const
    {
        return type && type->isPointerTy();
    }

    //===================================================================
    // Array Operations
    //===================================================================

    llvm::ArrayType *TypeCodegen::get_array_type(llvm::Type *element_type, uint64_t size)
    {
        if (!element_type)
            return nullptr;

        return llvm::ArrayType::get(element_type, size);
    }

    llvm::Type *TypeCodegen::get_element_type(llvm::ArrayType *array_type) const
    {
        if (!array_type)
            return nullptr;

        return array_type->getElementType();
    }

    //===================================================================
    // Struct Operations
    //===================================================================

    llvm::StructType *TypeCodegen::get_struct_type(const std::string &name,
                                                   const std::vector<llvm::Type *> &field_types,
                                                   bool packed)
    {
        // Check if already exists
        if (llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), name))
        {
            return existing;
        }

        // Create new struct type
        llvm::StructType *struct_type = llvm::StructType::create(llvm_ctx(), name);
        struct_type->setBody(field_types, packed);

        return struct_type;
    }

    llvm::Type *TypeCodegen::get_field_type(llvm::StructType *struct_type, unsigned field_index) const
    {
        if (!struct_type || field_index >= struct_type->getNumElements())
            return nullptr;

        return struct_type->getElementType(field_index);
    }

    unsigned TypeCodegen::get_num_fields(llvm::StructType *struct_type) const
    {
        if (!struct_type)
            return 0;

        return struct_type->getNumElements();
    }

    //===================================================================
    // Internal Helpers
    //===================================================================

    unsigned TypeCodegen::get_int_bit_width(llvm::Type *type) const
    {
        if (!type || !type->isIntegerTy())
            return 0;

        return type->getIntegerBitWidth();
    }

    bool TypeCodegen::is_floating_point(llvm::Type *type) const
    {
        return type && type->isFloatingPointTy();
    }

    bool TypeCodegen::is_signed_integer(TypeRef cryo_type) const
    {
        if (!cryo_type)
            return true; // Default to signed

        // Check type name for unsigned prefix
        std::string name = cryo_type.get()->display_name();
        return name.empty() || name[0] != 'u';
    }

    //===================================================================
    // High-Level Type Declaration Generation
    //===================================================================

    llvm::StructType *TypeCodegen::generate_struct(Cryo::StructDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        std::string name = node->name();

        // CRITICAL: Do not generate struct types for generic templates
        // Generic templates should only be instantiated when used with concrete types
        if (!node->generic_parameters().empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "TypeCodegen: Skipping generic struct template '{}' with {} type parameters",
                      name, node->generic_parameters().size());
            // Return nullptr - the template will be instantiated with concrete types when used
            return nullptr;
        }

        // Check if any field has an error type - if so, skip generating this struct
        for (const auto &field : node->fields())
        {
            TypeRef field_type = field->get_resolved_type();
            if (field_type.is_error())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "TypeCodegen: Skipping struct '{}' - field '{}' has error type: '{}'",
                          name, field->name(), field_type->display_name());
                // Emit actual error so it shows up in diagnostics
                report_error(ErrorCode::E0644_FIELD_TYPE_ERROR, node,
                             "Struct '" + name + "' field '" + field->name() +
                                 "' has unresolved type: " + field_type->display_name());
                return nullptr;
            }
        }

        llvm::StructType *struct_type = nullptr;

        // Check if already declared
        if (llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), name))
        {
            // If already complete (non-opaque), ensure field names are registered and return
            if (!existing->isOpaque())
            {
                // Even if struct already exists, we need to ensure field names are registered
                // This can happen when struct is generated during pre-registration but fields
                // aren't registered until Pass 1
                if (!node->fields().empty() && ctx().get_struct_field_index(name, node->fields()[0]->name()) < 0)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeCodegen: Registering field names for existing struct: {}", name);
                    std::vector<std::string> field_names;
                    std::vector<TypeRef> cryo_field_type_refs;
                    field_names.reserve(node->fields().size());
                    cryo_field_type_refs.reserve(node->fields().size());
                    for (const auto &field : node->fields())
                    {
                        field_names.push_back(field->name());
                        TypeRef ftype = field->get_resolved_type();
                        cryo_field_type_refs.push_back(ftype.is_valid() && !ftype.is_error() ? ftype : TypeRef{});
                    }
                    ctx().register_struct_fields(name, field_names);

                    // Also register in TemplateRegistry
                    if (auto *template_reg = ctx().template_registry())
                    {
                        std::string source_ns = ctx().namespace_context();
                        std::string qualified_name = source_ns.empty() ? name : source_ns + "::" + name;
                        template_reg->register_struct_field_types(qualified_name, field_names, cryo_field_type_refs, source_ns);
                        if (qualified_name != name)
                        {
                            template_reg->register_struct_field_types(name, field_names, cryo_field_type_refs, source_ns);
                        }
                    }

                    // Also ensure TypeMapper cache has the struct
                    types().register_struct(name, existing);
                    ctx().register_type(name, existing);
                }
                return existing;
            }
            // Otherwise, we'll complete the opaque struct below
            struct_type = existing;
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeCodegen: Completing opaque struct: {}", name);
        }
        else
        {
            // Create opaque struct first (for recursive types)
            struct_type = llvm::StructType::create(llvm_ctx(), name);
        }
        // Collect field types
        std::vector<llvm::Type *> field_types;
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "=== STRUCT_GEN: Mapping {} fields for struct '{}' ===", node->fields().size(), name);
        for (const auto &field : node->fields())
        {
            llvm::Type *field_llvm_type = nullptr;

            TypeRef cryo_field_type = field->get_resolved_type();
            if (cryo_field_type)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "=== STRUCT_GEN: Mapping field '{}' of type '{}' (TypeID={}, kind={}) ===",
                          field->name(), cryo_field_type.get()->display_name(),
                          cryo_field_type.id().id, static_cast<int>(cryo_field_type->kind()));
                // Use the main type mapping method like the old implementation did
                field_llvm_type = types().map(cryo_field_type);
                if (field_llvm_type)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "=== STRUCT_GEN: Field '{}' -> LLVM type ID={} ===",
                              field->name(), field_llvm_type->getTypeID());
                    // Check if the mapped type is opaque
                    if (auto st = llvm::dyn_cast<llvm::StructType>(field_llvm_type))
                    {
                        if (st->isOpaque())
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "=== STRUCT_GEN: WARNING - Field '{}' mapped to OPAQUE struct '{}' ===",
                                      field->name(), st->getName().str());
                        }
                    }
                }
                else
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "=== STRUCT_GEN: Field '{}' mapping returned NULL ===", field->name());
                }
            }
            else
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "=== STRUCT_GEN: Field '{}' has no resolved type ===", field->name());
            }

            // Fallback: try on-demand generic instantiation from annotation
            if (!field_llvm_type && field->has_type_annotation())
            {
                std::string annotation = field->type_annotation()->to_string();
                size_t angle_pos = annotation.find('<');
                if (angle_pos != std::string::npos)
                {
                    size_t close_pos = annotation.rfind('>');
                    std::string base_name = annotation.substr(0, angle_pos);
                    std::string type_args_str = annotation.substr(angle_pos + 1, close_pos - angle_pos - 1);

                    CodegenVisitor *visitor = ctx().visitor();
                    GenericCodegen *gen_codegen = visitor ? visitor->get_generics() : nullptr;

                    if (gen_codegen && gen_codegen->is_generic_template(base_name))
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "=== STRUCT_GEN: Field '{}' attempting on-demand instantiation of '{}' ===",
                                  field->name(), annotation);

                        std::vector<TypeRef> type_args = parse_type_args_for_field(type_args_str, symbols());
                        if (!type_args.empty())
                        {
                            llvm::StructType *instantiated = gen_codegen->instantiate_struct(base_name, type_args);
                            if (instantiated)
                            {
                                field_llvm_type = instantiated;
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "=== STRUCT_GEN: Field '{}' instantiated generic '{}' -> LLVM struct '{}' ===",
                                          field->name(), annotation, instantiated->getName().str());
                            }
                        }
                    }
                }
            }

            // Fallback: try creating Array<T> struct from annotation
            if (!field_llvm_type && field->has_type_annotation())
            {
                auto *ann = field->type_annotation();
                if (ann->kind == Cryo::TypeAnnotationKind::Array)
                {
                    field_llvm_type = get_or_create_array_struct_from_annotation(ann, llvm_ctx());
                }
                else if (ann->kind == Cryo::TypeAnnotationKind::Named)
                {
                    const std::string &ann_name = ann->name;
                    if (ann_name.size() >= 2
                        && ann_name[ann_name.size() - 2] == '['
                        && ann_name[ann_name.size() - 1] == ']')
                    {
                        std::string elem_name = ann_name.substr(0, ann_name.size() - 2);
                        std::string array_name = "Array<" + elem_name + ">";
                        llvm::StructType *existing =
                            llvm::StructType::getTypeByName(llvm_ctx(), array_name);
                        field_llvm_type = existing ? existing
                            : llvm::StructType::create(llvm_ctx(),
                                {llvm::PointerType::get(llvm_ctx(), 0),
                                 llvm::Type::getInt64Ty(llvm_ctx()),
                                 llvm::Type::getInt64Ty(llvm_ctx())},
                                array_name);
                    }
                    else if (!ann_name.empty() && ann_name.back() == '*')
                    {
                        field_llvm_type = llvm::PointerType::get(llvm_ctx(), 0);
                    }
                }
                else if (ann->kind == Cryo::TypeAnnotationKind::Pointer)
                {
                    field_llvm_type = llvm::PointerType::get(llvm_ctx(), 0);
                }
                if (field_llvm_type)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "=== STRUCT_GEN: Field '{}' resolved from annotation '{}' -> LLVM type ===",
                              field->name(), ann->to_string());
                }
            }

            if (field_llvm_type)
            {
                field_types.push_back(field_llvm_type);
            }
            else
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "=== STRUCT_GEN: Field '{}' falling back to i64 ===", field->name());
                field_types.push_back(llvm::Type::getInt64Ty(llvm_ctx()));
            }
        }

        // Set struct body - always set even if empty to mark as non-opaque
        struct_type->setBody(field_types);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "=== STRUCT_TYPE_GEN: Set struct '{}' body with {} fields (isOpaque after: {}) ===",
                  name, field_types.size(), struct_type->isOpaque() ? "YES" : "NO");

        // Register type in both context and TypeMapper cache for consistency
        ctx().register_type(name, struct_type);
        types().register_struct(name, struct_type);

        // Register field names for member access resolution
        std::vector<std::string> field_names;
        std::vector<TypeRef> cryo_field_type_refs; // For TemplateRegistry registration
        field_names.reserve(node->fields().size());
        cryo_field_type_refs.reserve(node->fields().size());
        for (const auto &field : node->fields())
        {
            field_names.push_back(field->name());
            // Collect field types for TemplateRegistry
            TypeRef ftype = field->get_resolved_type();
            if (ftype.is_valid() && !ftype.is_error())
            {
                cryo_field_type_refs.push_back(ftype);
            }
            else
            {
                // Push invalid type to maintain index alignment
                cryo_field_type_refs.push_back(TypeRef{});
            }
        }
        ctx().register_struct_fields(name, field_names);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeCodegen: Registered {} field names for struct {}",
                  field_names.size(), name);

        // Also register field types in TemplateRegistry for nested member access resolution
        if (auto *template_reg = ctx().template_registry())
        {
            // Get the qualified name using current namespace context
            std::string source_ns = ctx().namespace_context();
            std::string qualified_name = source_ns.empty() ? name : source_ns + "::" + name;
            template_reg->register_struct_field_types(qualified_name, field_names, cryo_field_type_refs, source_ns);
            // Also register with simple name for direct lookup
            if (qualified_name != name)
            {
                template_reg->register_struct_field_types(name, field_names, cryo_field_type_refs, source_ns);
            }
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeCodegen: Registered {} field types in TemplateRegistry for struct {} (qualified: {}, source_namespace: {})",
                      cryo_field_type_refs.size(), name, qualified_name, source_ns);
        }

        return struct_type;
    }

    llvm::StructType *TypeCodegen::generate_class(Cryo::ClassDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeCodegen: Generating class: {}", name);

        // CRITICAL: Do not generate class types for generic templates
        // Generic templates should only be instantiated when used with concrete types
        if (!node->generic_parameters().empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "TypeCodegen: Skipping generic class template '{}' with {} type parameters",
                      name, node->generic_parameters().size());
            // Return nullptr - the template will be instantiated with concrete types when used
            return nullptr;
        }

        llvm::StructType *class_type = nullptr;

        // Check if already declared
        if (llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), name))
        {
            // If already complete (non-opaque), ensure field names are registered and return
            if (!existing->isOpaque())
            {
                // Even if class already exists, we need to ensure field names are registered
                // This can happen when class is generated during pre-registration but fields
                // aren't registered until Pass 1.
                // Use ClassType::fields() (includes inherited fields) and account for vtable offset.
                {
                    TypeRef cryo_ref = ctx().symbols().lookup_class_type(name);
                    const Cryo::ClassType *cls = cryo_ref.is_valid()
                        ? dynamic_cast<const Cryo::ClassType *>(cryo_ref.get())
                        : nullptr;

                    // Determine the first real field name to check registration
                    std::string first_field;
                    if (cls && !cls->fields().empty())
                        first_field = cls->fields().front().name;
                    else if (!node->fields().empty())
                        first_field = node->fields()[0]->name();

                    if (!first_field.empty() && ctx().get_struct_field_index(name, first_field) < 0)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeCodegen: Registering field names for existing class: {}", name);

                        // Check if this class has a vtable pointer (first LLVM element)
                        bool has_vtable = false;
                        if (cls && cls->needs_vtable_pointer())
                            has_vtable = true;

                        std::vector<std::string> field_names;
                        if (has_vtable)
                            field_names.push_back("__vtable_ptr"); // placeholder for vtable slot

                        if (cls && !cls->fields().empty())
                        {
                            for (const auto &field : cls->fields())
                                field_names.push_back(field.name);
                        }
                        else
                        {
                            for (const auto &field : node->fields())
                                field_names.push_back(field->name());
                        }
                        ctx().register_struct_fields(name, field_names);
                        types().register_struct(name, existing);
                        ctx().register_type(name, existing);
                    }
                }
                return existing;
            }
            // Otherwise, we'll complete the opaque struct below
            class_type = existing;
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeCodegen: Completing opaque class: {}", name);
        }
        else
        {
            // Create opaque struct
            class_type = llvm::StructType::create(llvm_ctx(), name);
        }

        // Check if this class needs a vtable pointer
        bool needs_vtable = false;
        {
            TypeRef cryo_type = ctx().symbols().lookup_class_type(name);
            // Try qualified name if unqualified lookup fails
            if (!cryo_type.is_valid() && !ctx().namespace_context().empty())
            {
                std::string qualified = ctx().namespace_context() + "::" + name;
                cryo_type = ctx().symbols().lookup_class_type(qualified);
            }
            if (cryo_type.is_valid())
            {
                auto *cryo_class = dynamic_cast<const Cryo::ClassType *>(cryo_type.get());
                if (cryo_class && cryo_class->needs_vtable_pointer())
                {
                    needs_vtable = true;
                }
            }
        }

        // Collect field types — use ClassType fields (includes inherited fields) if available
        std::vector<llvm::Type *> field_types;
        std::vector<std::string> all_field_names; // for registration

        // Add vtable pointer as first element if needed
        if (needs_vtable)
        {
            field_types.push_back(llvm::PointerType::get(llvm_ctx(), 0));
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeCodegen: Added vtable pointer to class '{}'", name);
        }

        // Use the ClassType's resolved fields (which include inherited fields from base classes)
        TypeRef cryo_type_ref = ctx().symbols().lookup_class_type(name);
        if (!cryo_type_ref.is_valid() && !ctx().namespace_context().empty())
        {
            cryo_type_ref = ctx().symbols().lookup_class_type(ctx().namespace_context() + "::" + name);
        }
        const Cryo::ClassType *cryo_class = nullptr;
        if (cryo_type_ref.is_valid())
        {
            cryo_class = dynamic_cast<const Cryo::ClassType *>(cryo_type_ref.get());
        }

        // Prefer ClassType fields (includes inherited fields from base classes),
        // but fall back to AST node fields if ClassType has fewer fields than the AST
        // (can happen when sync pass couldn't resolve all field types and the ClassType
        // was initially populated with an incomplete set).
        bool use_class_type_fields = cryo_class && !cryo_class->fields().empty()
            && cryo_class->fields().size() >= node->fields().size();

        if (use_class_type_fields)
        {
            for (size_t fi = 0; fi < cryo_class->fields().size(); ++fi)
            {
                const auto &field = cryo_class->fields()[fi];
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Class field '{}': type='{}'",
                          field.name, field.type.is_valid() ? field.type->display_name() : "NULL");
                llvm::Type *field_type = field.type.is_valid() ? types().map(field.type) : nullptr;
                if (!field_type)
                {
                    // Try to resolve type from AST annotation when TypeRef is unavailable.
                    // Match by name (not index) because cryo_class->fields() includes inherited
                    // fields while node->fields() does not.
                    for (const auto &ast_field : node->fields())
                    {
                        if (!ast_field || ast_field->name() != field.name
                            || !ast_field->has_type_annotation())
                            continue;

                        auto *ann = ast_field->type_annotation();

                        // Case 1: Properly structured Array annotation
                        if (ann->kind == Cryo::TypeAnnotationKind::Array)
                        {
                            field_type = get_or_create_array_struct_from_annotation(ann, llvm_ctx());
                        }
                        // Case 2: Named annotation whose name ends with "[]" (e.g., "MatchArmNode*[]")
                        // The parser sometimes stores compound types as flat Named strings.
                        else if (ann->kind == Cryo::TypeAnnotationKind::Named)
                        {
                            const std::string &ann_name = ann->name;
                            if (ann_name.size() >= 2
                                && ann_name[ann_name.size() - 2] == '['
                                && ann_name[ann_name.size() - 1] == ']')
                            {
                                // Extract element type name (everything before "[]")
                                std::string elem_name = ann_name.substr(0, ann_name.size() - 2);
                                std::string array_name = "Array<" + elem_name + ">";

                                llvm::StructType *existing =
                                    llvm::StructType::getTypeByName(llvm_ctx(), array_name);
                                if (existing)
                                {
                                    field_type = existing;
                                }
                                else
                                {
                                    field_type = llvm::StructType::create(llvm_ctx(),
                                        {llvm::PointerType::get(llvm_ctx(), 0),
                                         llvm::Type::getInt64Ty(llvm_ctx()),
                                         llvm::Type::getInt64Ty(llvm_ctx())},
                                        array_name);
                                }
                            }
                            // Named annotation that looks like a pointer (e.g., "StatementNode*")
                            else if (!ann_name.empty() && ann_name.back() == '*')
                            {
                                field_type = llvm::PointerType::get(llvm_ctx(), 0);
                            }
                            else
                            {
                                // Plain Named type — use TypeMapper::resolve_and_map which
                                // handles plain structs, mangled generics, and instantiation.
                                llvm::Type *resolved = types().resolve_and_map(ann_name);
                                if (resolved && resolved->isSized())
                                {
                                    field_type = resolved;
                                }
                                else
                                {
                                    // Try arena lookup for enums
                                    TypeRef ref = ctx().symbols().arena().lookup_type_by_name(ann_name);
                                    if (ref.is_valid())
                                        field_type = types().map(ref);
                                    else
                                    {
                                        // Not a struct, not in arena — assume enum (i32)
                                        field_type = llvm::Type::getInt32Ty(llvm_ctx());
                                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                  "Class field '{}': unresolved Named '{}' — assuming enum (i32)",
                                                  field.name, ann_name);
                                    }
                                }
                            }
                        }
                        // Case 3: Pointer annotation — in opaque-pointer mode, all pointers are ptr
                        else if (ann->kind == Cryo::TypeAnnotationKind::Pointer)
                        {
                            field_type = llvm::PointerType::get(llvm_ctx(), 0);
                        }
                        // Case 4: Generic annotation (e.g., HashMap<u32, TypeRef>)
                        // Use TypeMapper::resolve_and_map with the annotation's string form
                        else if (ann->kind == Cryo::TypeAnnotationKind::Generic)
                        {
                            llvm::Type *resolved = types().resolve_and_map(ann->to_string());
                            if (resolved && resolved->isSized())
                            {
                                field_type = resolved;
                            }
                        }

                        if (field_type)
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "Class field '{}' resolved from annotation '{}' -> LLVM type",
                                      field.name, ann->to_string());
                        }
                        break;
                    }
                }
                // If still unresolved, this is likely an inherited field whose type
                // wasn't resolved. Try to get the type from the base class's LLVM struct.
                if (!field_type && !node->base_class().empty())
                {
                    llvm::StructType *base_st =
                        llvm::StructType::getTypeByName(llvm_ctx(), node->base_class());
                    if (base_st && !base_st->isOpaque() && fi < base_st->getNumElements())
                    {
                        field_type = base_st->getElementType(fi);
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Class field '{}' resolved from base '{}' element {} -> LLVM type",
                                  field.name, node->base_class(), fi);
                    }
                }
                if (field_type)
                {
                    field_types.push_back(field_type);
                }
                else
                {
                    field_types.push_back(llvm::Type::getInt64Ty(llvm_ctx()));
                }
                all_field_names.push_back(field.name);
            }
        }
        else
        {
            // Fallback to AST node fields (no inheritance info)
            if (cryo_class && !cryo_class->fields().empty())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                    "TypeCodegen: ClassType '{}' has {} fields but AST has {} - using AST fields",
                    name, cryo_class->fields().size(), node->fields().size());
            }
            for (const auto &field : node->fields())
            {
                TypeRef cryo_field_type = field->get_resolved_type();
                llvm::Type *field_llvm_type = nullptr;
                if (cryo_field_type)
                {
                    field_llvm_type = types().map(cryo_field_type);
                }
                if (!field_llvm_type && field->has_type_annotation())
                {
                    auto *ann = field->type_annotation();
                    if (ann->kind == Cryo::TypeAnnotationKind::Array)
                    {
                        field_llvm_type = get_or_create_array_struct_from_annotation(ann, llvm_ctx());
                    }
                    else if (ann->kind == Cryo::TypeAnnotationKind::Named)
                    {
                        const std::string &ann_name = ann->name;
                        if (ann_name.size() >= 2
                            && ann_name[ann_name.size() - 2] == '['
                            && ann_name[ann_name.size() - 1] == ']')
                        {
                            std::string elem_name = ann_name.substr(0, ann_name.size() - 2);
                            std::string array_name = "Array<" + elem_name + ">";
                            llvm::StructType *existing =
                                llvm::StructType::getTypeByName(llvm_ctx(), array_name);
                            field_llvm_type = existing ? existing
                                : llvm::StructType::create(llvm_ctx(),
                                    {llvm::PointerType::get(llvm_ctx(), 0),
                                     llvm::Type::getInt64Ty(llvm_ctx()),
                                     llvm::Type::getInt64Ty(llvm_ctx())},
                                    array_name);
                        }
                        else if (!ann_name.empty() && ann_name.back() == '*')
                        {
                            field_llvm_type = llvm::PointerType::get(llvm_ctx(), 0);
                        }
                    }
                    else if (ann->kind == Cryo::TypeAnnotationKind::Pointer)
                    {
                        field_llvm_type = llvm::PointerType::get(llvm_ctx(), 0);
                    }
                    // Generic annotation (e.g., HashMap<u32, TypeRef>)
                    else if (ann->kind == Cryo::TypeAnnotationKind::Generic)
                    {
                        llvm::Type *resolved = types().resolve_and_map(ann->to_string());
                        if (resolved && resolved->isSized())
                        {
                            field_llvm_type = resolved;
                        }
                    }
                    if (field_llvm_type)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Class field '{}' resolved from annotation '{}' -> LLVM type",
                                  field->name(), ann->to_string());
                    }
                }
                // If annotation-based resolution failed, try looking up as an enum type.
                // Enums are i32 in Cryo, not i64.
                if (!field_llvm_type && field->has_type_annotation())
                {
                    auto *ann = field->type_annotation();
                    if (ann->kind == Cryo::TypeAnnotationKind::Named)
                    {
                        // Use TypeMapper::resolve_and_map which handles plain structs,
                        // mangled generics, and on-demand instantiation.
                        llvm::Type *resolved = types().resolve_and_map(ann->name);
                        if (resolved && resolved->isSized())
                        {
                            field_llvm_type = resolved;
                        }
                        else
                        {
                            // Try arena lookup for enums
                            TypeRef ref = ctx().symbols().arena().lookup_type_by_name(ann->name);
                            if (ref.is_valid() && ref->kind() == Cryo::TypeKind::Enum)
                            {
                                field_llvm_type = llvm::Type::getInt32Ty(llvm_ctx());
                            }
                            else if (ref.is_valid())
                            {
                                field_llvm_type = types().map(ref);
                            }
                            else
                            {
                                field_llvm_type = llvm::Type::getInt32Ty(llvm_ctx());
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "Class field '{}': unresolved Named type '{}' — "
                                          "assuming enum (i32)",
                                          field->name(), ann->name);
                            }
                        }
                    }
                }
                if (field_llvm_type)
                {
                    field_types.push_back(field_llvm_type);
                }
                else
                {
                    field_types.push_back(llvm::Type::getInt64Ty(llvm_ctx()));
                }
                all_field_names.push_back(field->name());
            }
        }

        // Ensure inherited fields match the base class's actual LLVM layout.
        // The ClassType's inherited fields may be incomplete if populate_type_fields_pass
        // couldn't resolve all base class field types (e.g., cross-module enum types not
        // yet registered when the base class was processed).  The base class's LLVM struct,
        // however, IS correct because its codegen falls back to AST fields.
        // Always rebuild from the base's LLVM type to guarantee correctness.
        if (!node->base_class().empty())
        {
            std::string base_name = node->base_class();
            llvm::StructType *base_struct = llvm::StructType::getTypeByName(llvm_ctx(), base_name);

            // If the base class LLVM struct isn't ready yet (null or opaque), try to
            // resolve its layout from the Cryo type system's ClassType fields.
            // Walk up the inheritance chain until we find an ancestor with fields.
            if (!base_struct || base_struct->isOpaque())
            {
                TypeRef base_cryo_ref = ctx().symbols().lookup_class_type(base_name);
                const Cryo::ClassType *base_cls = base_cryo_ref.is_valid()
                    ? dynamic_cast<const Cryo::ClassType *>(base_cryo_ref.get())
                    : nullptr;

                // Walk up the chain to find the first ancestor with declared fields
                const Cryo::ClassType *field_source = base_cls;
                while (field_source && field_source->fields().empty() && field_source->has_base_class())
                {
                    auto *grandparent = dynamic_cast<const Cryo::ClassType *>(
                        field_source->base_class().get());
                    if (!grandparent) break;
                    field_source = grandparent;
                }

                if (field_source && !field_source->fields().empty())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "TypeCodegen: Base class '{}' LLVM struct not ready, building from ancestor ClassType ({} fields)",
                              base_name, field_source->fields().size());

                    // Create or complete the base struct from ancestor's fields
                    if (!base_struct)
                        base_struct = llvm::StructType::create(llvm_ctx(), base_name);

                    std::vector<llvm::Type *> base_fields;
                    std::vector<std::string> base_field_names;
                    if (field_source->needs_vtable_pointer())
                        base_fields.push_back(llvm::PointerType::get(llvm_ctx(), 0));

                    for (const auto &bf : field_source->fields())
                    {
                        llvm::Type *bft = bf.type.is_valid() ? types().map(bf.type) : nullptr;
                        if (!bft)
                            bft = llvm::Type::getInt64Ty(llvm_ctx()); // fallback
                        base_fields.push_back(bft);
                        base_field_names.push_back(bf.name);
                    }
                    base_struct->setBody(base_fields);
                    ctx().register_type(base_name, base_struct);
                    types().register_struct(base_name, base_struct);
                    unsigned base_vtable_offset = field_source->needs_vtable_pointer() ? 1 : 0;
                    ctx().register_struct_fields(base_name, base_field_names, base_vtable_offset);
                }
            }

            if (base_struct && !base_struct->isOpaque())
            {
                unsigned base_elem_count = base_struct->getNumElements();

                // Identify which fields in field_types are "own" (not inherited).
                // The own fields are those declared directly on this class (node->fields()).
                size_t own_field_count = node->fields().size();
                size_t inherited_count = field_types.size() > own_field_count
                                             ? field_types.size() - own_field_count
                                             : 0;

                // If the number of inherited fields we have doesn't match the base's
                // element count, the ClassType's inherited fields are incomplete or wrong.
                if (inherited_count != base_elem_count)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "TypeCodegen: Class '{}' has {} inherited fields but base '{}' has {} elements - rebuilding from base LLVM type",
                              name, inherited_count, base_name, base_elem_count);

                    // Rebuild: base class LLVM fields + own fields from the end of field_types
                    std::vector<llvm::Type *> full_fields;
                    std::vector<std::string> full_names;

                    // Copy all elements from base struct (correct layout)
                    for (unsigned i = 0; i < base_elem_count; ++i)
                        full_fields.push_back(base_struct->getElementType(i));

                    // Copy base field names from registration
                    auto &base_field_map = ctx().get_struct_field_indices(base_name);
                    if (!base_field_map.empty())
                    {
                        full_names.resize(base_elem_count);
                        for (const auto &[fname, fidx] : base_field_map)
                            if (fidx < base_elem_count)
                                full_names[fidx] = fname;
                    }

                    // Append own fields (the last own_field_count entries in field_types)
                    size_t own_start = field_types.size() - own_field_count;
                    for (size_t i = own_start; i < field_types.size(); ++i)
                        full_fields.push_back(field_types[i]);
                    for (size_t i = all_field_names.size() - own_field_count; i < all_field_names.size(); ++i)
                        full_names.push_back(all_field_names[i]);

                    field_types = std::move(full_fields);
                    all_field_names = std::move(full_names);

                    // Base already accounts for vtable
                    needs_vtable = false;
                }
            }
        }

        // Set class body - always set even if empty to mark as non-opaque
        class_type->setBody(field_types);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeCodegen: Set class {} body with {} fields", name, field_types.size());

        // Register type in both context and TypeMapper cache for consistency
        ctx().register_type(name, class_type);
        types().register_struct(name, class_type);

        // Register field names for member access resolution (already built above)
        std::vector<std::string> field_names = std::move(all_field_names);
        // Legacy fallback path (shouldn't reach here but keep for safety)
        if (field_names.empty())
        {
            for (const auto &field : node->fields())
            {
                field_names.push_back(field->name());
            }
        }
        unsigned field_offset = needs_vtable ? 1 : 0;
        ctx().register_struct_fields(name, field_names, field_offset);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeCodegen: Registered {} field names for class {} (vtable_offset={})",
                  field_names.size(), name, field_offset);

        // Register field types in TemplateRegistry for nested member access resolution
        // (e.g., obj.pointer_field.member requires knowing pointer_field's type)
        if (auto *template_reg = ctx().template_registry())
        {
            std::vector<TypeRef> cryo_field_type_refs;
            std::vector<std::string> field_type_annotations;
            cryo_field_type_refs.reserve(field_names.size());
            field_type_annotations.reserve(field_names.size());

            // Iterate over field_names (which includes inherited + own fields) to ensure
            // the type_refs and annotations vectors have the same size as field_names.
            for (const auto &fname : field_names)
            {
                TypeRef ftype{};
                std::string ann;

                // Try ClassType fields first (may include inherited fields)
                if (cryo_class)
                {
                    for (const auto &cf : cryo_class->fields())
                    {
                        if (cf.name == fname)
                        {
                            ftype = cf.type.is_valid() ? cf.type : TypeRef{};
                            break;
                        }
                    }
                }

                // Try AST node fields for annotation (own fields only)
                for (const auto &ast_field : node->fields())
                {
                    if (ast_field && ast_field->name() == fname)
                    {
                        if (!ftype.is_valid())
                        {
                            TypeRef ast_type = ast_field->get_resolved_type();
                            if (ast_type.is_valid() && !ast_type.is_error())
                                ftype = ast_type;
                        }
                        if (ast_field->has_type_annotation())
                            ann = ast_field->type_annotation()->to_string();
                        break;
                    }
                }

                cryo_field_type_refs.push_back(ftype);
                field_type_annotations.push_back(ann);
            }

            std::string source_ns = ctx().namespace_context();
            std::string qualified_name = source_ns.empty() ? name : source_ns + "::" + name;
            template_reg->register_struct_field_types(qualified_name, field_names, cryo_field_type_refs, source_ns, field_type_annotations);
            if (qualified_name != name)
            {
                template_reg->register_struct_field_types(name, field_names, cryo_field_type_refs, source_ns, field_type_annotations);
            }
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeCodegen: Registered {} field types in TemplateRegistry for class {} (qualified: {})",
                      cryo_field_type_refs.size(), name, qualified_name);
        }

        return class_type;
    }

    llvm::Type *TypeCodegen::generate_enum(Cryo::EnumDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeCodegen: Generating enum: {} with {} variants",
                  name, node->variants().size());

        // Simple enums are just integers
        llvm::Type *enum_type = llvm::Type::getInt32Ty(llvm_ctx());

        // Register type
        ctx().register_type(name, enum_type);

        // Generate variant constants
        int32_t index = 0;
        std::string ns_context = ctx().namespace_context();
        for (const auto &variant : node->variants())
        {
            std::string variant_name = name + "::" + variant->name();
            llvm::Constant *value = llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), index++);

            // Register with simple name (EnumName::Variant)
            ctx().register_enum_variant(variant_name, value);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeCodegen: Registered enum variant: {} = {}",
                      variant_name, index - 1);

            // Also register with fully-qualified name (namespace::EnumName::Variant)
            if (!ns_context.empty())
            {
                std::string qualified_variant = ns_context + "::" + variant_name;
                ctx().register_enum_variant(qualified_variant, value);
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "TypeCodegen: Also registered enum variant as: {}", qualified_variant);
            }
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeCodegen: Finished generating enum: {}", name);
        return enum_type;
    }

    void TypeCodegen::generate_type_alias(Cryo::TypeAliasDeclarationNode *node)
    {
        if (!node)
            return;

        std::string alias_name = node->alias_name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeCodegen: Generating type alias: {}", alias_name);

        // Extract the base type name from the target type annotation for alias resolution
        // e.g., "IoResult<T>" -> "Result<T, IoError>" -> base type "Result"
        if (node->has_target_type_annotation())
        {
            std::string target_str = node->target_type_annotation()->to_string();
            // Extract base type name (before any < or after last ::)
            std::string base_type_name = target_str;

            // Handle qualified names (e.g., "std::core::result::Result<T, E>" -> "Result")
            size_t last_sep = target_str.rfind("::");
            if (last_sep != std::string::npos)
            {
                base_type_name = target_str.substr(last_sep + 2);
            }

            // Remove generic parameters (e.g., "Result<T, IoError>" -> "Result")
            size_t angle_pos = base_type_name.find('<');
            if (angle_pos != std::string::npos)
            {
                base_type_name = base_type_name.substr(0, angle_pos);
            }

            if (!base_type_name.empty())
            {
                ctx().register_type_alias_base(alias_name, base_type_name);
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeCodegen: Registered type alias '{}' -> base type '{}'",
                          alias_name, base_type_name);
            }
        }
        else if (node->has_resolved_target_type())
        {
            // If we have a resolved type, get its name
            TypeRef target_type = node->get_resolved_target_type();
            if (target_type.is_valid())
            {
                std::string base_type_name = target_type->display_name();
                // Remove generic parameters if present
                size_t angle_pos = base_type_name.find('<');
                if (angle_pos != std::string::npos)
                {
                    base_type_name = base_type_name.substr(0, angle_pos);
                }
                ctx().register_type_alias_base(alias_name, base_type_name);
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeCodegen: Registered type alias '{}' -> base type '{}'",
                          alias_name, base_type_name);
            }
        }

        // Get the aliased type
        llvm::Type *aliased_type = nullptr;

        // First try to get from resolved type
        if (node->has_resolved_target_type())
        {
            aliased_type = types().get_type(node->get_resolved_target_type());
        }

        // If not resolved, try to map common primitive type aliases by name
        if (!aliased_type)
        {
            aliased_type = map_primitive_alias(alias_name);
        }

        if (!aliased_type)
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN, "Unknown aliased type for: {}", alias_name);
            return;
        }

        // Register the alias
        ctx().register_type(alias_name, aliased_type);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Registered type: {}", alias_name);
    }

    llvm::Type *TypeCodegen::map_primitive_alias(const std::string &name)
    {
        // Map common primitive type aliases
        if (name == "i1" || name == "bool")
            return llvm::Type::getInt1Ty(llvm_ctx());
        if (name == "i8")
            return llvm::Type::getInt8Ty(llvm_ctx());
        if (name == "i16")
            return llvm::Type::getInt16Ty(llvm_ctx());
        if (name == "i32" || name == "int")
            return llvm::Type::getInt32Ty(llvm_ctx());
        if (name == "i64")
            return llvm::Type::getInt64Ty(llvm_ctx());
        if (name == "i128")
            return llvm::Type::getInt128Ty(llvm_ctx());

        // Unsigned integers (same LLVM type as signed, signedness tracked separately)
        if (name == "u8")
            return llvm::Type::getInt8Ty(llvm_ctx());
        if (name == "u16")
            return llvm::Type::getInt16Ty(llvm_ctx());
        if (name == "u32")
            return llvm::Type::getInt32Ty(llvm_ctx());
        if (name == "u64")
            return llvm::Type::getInt64Ty(llvm_ctx());
        if (name == "u128")
            return llvm::Type::getInt128Ty(llvm_ctx());

        // Floating point
        if (name == "f32" || name == "float")
            return llvm::Type::getFloatTy(llvm_ctx());
        if (name == "f64" || name == "double")
            return llvm::Type::getDoubleTy(llvm_ctx());

        // Void
        if (name == "void")
            return llvm::Type::getVoidTy(llvm_ctx());

        // String (pointer type)
        if (name == "string" || name == "str")
            return llvm::PointerType::get(llvm_ctx(), 0);

        return nullptr;
    }

} // namespace Cryo::Codegen
