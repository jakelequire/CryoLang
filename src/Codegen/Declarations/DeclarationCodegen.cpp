#include "Codegen/Declarations/DeclarationCodegen.hpp"
#include "Codegen/Memory/MemoryCodegen.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "Utils/Logger.hpp"

#include <llvm/IR/Verifier.h>

namespace Cryo::Codegen
{
    //===================================================================
    // Construction
    //===================================================================

    DeclarationCodegen::DeclarationCodegen(CodegenContext &ctx)
        : ICodegenComponent(ctx)
    {
    }

    //===================================================================
    // Function Declarations
    //===================================================================

    llvm::Function *DeclarationCodegen::generate_function_declaration(Cryo::FunctionDeclarationNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0633_FUNCTION_BODY_ERROR, "Null function declaration node");
            return nullptr;
        }

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating function declaration: {}", name);

        // Check if already declared
        if (llvm::Function *existing = module()->getFunction(name))
        {
            return existing;
        }

        // Get function type
        llvm::FunctionType *fn_type = get_function_type(node);
        if (!fn_type)
        {
            report_error(ErrorCode::E0633_FUNCTION_BODY_ERROR, node,
                         "Failed to get function type for: " + name);
            return nullptr;
        }

        // Create function
        llvm::GlobalValue::LinkageTypes linkage = get_linkage(node);
        llvm::Function *fn = llvm::Function::Create(fn_type, linkage, name, module());

        // Apply attributes
        apply_function_attributes(fn, node);

        // Name parameters
        generate_parameters(fn, node);

        // Register in context
        ctx().register_function(name, fn);

        return fn;
    }

    llvm::Function *DeclarationCodegen::generate_function_definition(Cryo::FunctionDeclarationNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0633_FUNCTION_BODY_ERROR, "Null function declaration node");
            return nullptr;
        }

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating function definition: {}", name);

        // Get or create declaration
        llvm::Function *fn = module()->getFunction(name);
        if (!fn)
        {
            fn = generate_function_declaration(node);
            if (!fn)
                return nullptr;
        }

        // Skip if already has a body
        if (!fn->empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Function already has body: {}", name);
            return fn;
        }

        // Create entry block
        llvm::BasicBlock *entry_block = llvm::BasicBlock::Create(llvm_ctx(), "entry", fn);
        builder().SetInsertPoint(entry_block);

        // Generate function body
        generate_function_body(fn, node);

        // Verify function
        if (llvm::verifyFunction(*fn, &llvm::errs()))
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "Function verification failed: {}", name);
            fn->eraseFromParent();
            return nullptr;
        }

        return fn;
    }

    llvm::Function *DeclarationCodegen::generate_method_declaration(Cryo::FunctionDeclarationNode *node,
                                                                      const std::string &parent_type)
    {
        if (!node)
            return nullptr;

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating method: {}::{}",
                  parent_type, node->name());

        // Generate qualified method name
        std::string method_name = generate_method_name(parent_type, node->name());

        // Check if already declared
        if (llvm::Function *existing = module()->getFunction(method_name))
        {
            return existing;
        }

        // Get function type with implicit 'this' parameter
        llvm::FunctionType *fn_type = get_function_type(node, true);
        if (!fn_type)
        {
            report_error(ErrorCode::E0633_FUNCTION_BODY_ERROR, node,
                         "Failed to get method type for: " + method_name);
            return nullptr;
        }

        // Create function
        llvm::Function *fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                                     method_name, module());

        // Name parameters (first is 'this')
        auto arg_it = fn->arg_begin();
        arg_it->setName("this");
        ++arg_it;

        // Name remaining parameters
        if (node->parameters().size() > 0)
        {
            for (const auto &param : node->parameters())
            {
                if (arg_it != fn->arg_end())
                {
                    arg_it->setName(param->name());
                    ++arg_it;
                }
            }
        }

        // Register
        ctx().register_function(method_name, fn);

        return fn;
    }

    llvm::Function *DeclarationCodegen::generate_extern_function(Cryo::FunctionDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating extern function: {}", name);

        // Check if already declared
        if (llvm::Function *existing = module()->getFunction(name))
        {
            return existing;
        }

        // Get return type
        llvm::Type *return_type = types().get_type(node->get_resolved_return_type());
        if (!return_type)
        {
            return_type = llvm::Type::getVoidTy(llvm_ctx());
        }

        // Get parameter types
        std::vector<llvm::Type *> param_types;
        bool is_variadic = node->is_variadic();

        if (node->parameters().size() > 0)
        {
            for (const auto &param : node->parameters())
            {
                llvm::Type *param_type = types().get_type(param->get_resolved_type());
                if (param_type)
                {
                    param_types.push_back(param_type);
                }
            }
        }

        // Create function type and declaration
        llvm::FunctionType *fn_type = llvm::FunctionType::get(return_type, param_types, is_variadic);
        llvm::Function *fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                                     name, module());

        ctx().register_function(name, fn);
        return fn;
    }

    //===================================================================
    // Variable Declarations
    //===================================================================

    llvm::AllocaInst *DeclarationCodegen::generate_local_variable(Cryo::VariableDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating local variable: {}", name);

        // Get variable type
        llvm::Type *var_type = get_llvm_type(node->get_resolved_type());
        if (!var_type)
        {
            report_error(ErrorCode::E0634_VARIABLE_INITIALIZATION_ERROR, node,
                         "Unknown type for variable: " + name);
            return nullptr;
        }

        // Create alloca in entry block
        llvm::AllocaInst *alloca = create_entry_alloca(var_type, name);
        if (!alloca)
        {
            report_error(ErrorCode::E0634_VARIABLE_INITIALIZATION_ERROR, node,
                         "Failed to allocate variable: " + name);
            return nullptr;
        }

        // Generate initializer if present
        if (node->initializer())
        {
            CodegenVisitor *visitor = ctx().visitor();
            node->initializer()->accept(*visitor);
            llvm::Value *init_val = get_result();
            if (init_val)
            {
                llvm::Value *cast_val = cast_if_needed(init_val, var_type);
                create_store(cast_val, alloca);
            }
        }

        // Register in value context
        values().set_value(name, nullptr, alloca);

        return alloca;
    }

    llvm::GlobalVariable *DeclarationCodegen::generate_global_variable(Cryo::VariableDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating global variable: {}", name);

        // Check if already exists
        if (llvm::GlobalVariable *existing = module()->getGlobalVariable(name))
        {
            return existing;
        }

        // Get variable type
        llvm::Type *var_type = get_llvm_type(node->get_resolved_type());
        if (!var_type)
        {
            report_error(ErrorCode::E0634_VARIABLE_INITIALIZATION_ERROR, node,
                         "Unknown type for global variable: " + name);
            return nullptr;
        }

        // Generate initializer
        llvm::Constant *initializer = generate_global_initializer(node, var_type);

        // Create global variable
        bool is_constant = !node->is_mutable();
        llvm::GlobalValue::LinkageTypes linkage = get_linkage(node);

        llvm::GlobalVariable *global = new llvm::GlobalVariable(
            *module(),
            var_type,
            is_constant,
            linkage,
            initializer,
            name);

        return global;
    }

    llvm::Constant *DeclarationCodegen::generate_constant(Cryo::VariableDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating constant: {}", name);

        // Generate constant value
        if (node->initializer())
        {
            CodegenVisitor *visitor = ctx().visitor();
            node->initializer()->accept(*visitor);
            llvm::Value *val = get_result();
            if (auto *constant = llvm::dyn_cast<llvm::Constant>(val))
            {
                return constant;
            }
        }

        report_error(ErrorCode::E0634_VARIABLE_INITIALIZATION_ERROR, node,
                     "Constant initializer is not a compile-time constant: " + name);
        return nullptr;
    }

    //===================================================================
    // Type Declarations
    //===================================================================

    llvm::StructType *DeclarationCodegen::generate_struct_declaration(Cryo::StructDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating struct: {}", name);

        // Check if already declared
        if (llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), name))
        {
            return existing;
        }

        // Create opaque struct first (for recursive types)
        llvm::StructType *struct_type = llvm::StructType::create(llvm_ctx(), name);

        // Collect field types
        std::vector<llvm::Type *> field_types;
        for (const auto &field : node->fields())
        {
            llvm::Type *field_type = get_llvm_type(field->get_resolved_type());
            if (field_type)
            {
                field_types.push_back(field_type);
            }
        }

        // Set struct body
        struct_type->setBody(field_types);

        // Register type
        ctx().register_type(name, struct_type);

        return struct_type;
    }

    llvm::StructType *DeclarationCodegen::generate_class_declaration(Cryo::ClassDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating class: {}", name);

        // Check if already declared
        if (llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), name))
        {
            return existing;
        }

        // Create opaque struct
        llvm::StructType *class_type = llvm::StructType::create(llvm_ctx(), name);

        // Collect field types (including inherited fields if any)
        std::vector<llvm::Type *> field_types;

        // Add vtable pointer if class has virtual methods
        // (simplified - full implementation would check for virtual methods)
        // field_types.push_back(llvm::PointerType::get(llvm_ctx(), 0));

        for (const auto &field : node->fields())
        {
            llvm::Type *field_type = get_llvm_type(field->get_resolved_type());
            if (field_type)
            {
                field_types.push_back(field_type);
            }
        }

        // Set class body
        class_type->setBody(field_types);

        // Register type
        ctx().register_type(name, class_type);

        return class_type;
    }

    llvm::Type *DeclarationCodegen::generate_enum_declaration(Cryo::EnumDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating enum: {}", name);

        // Simple enums are just integers
        // Tagged unions would need struct representation
        llvm::Type *enum_type = llvm::Type::getInt32Ty(llvm_ctx());

        // Register type
        ctx().register_type(name, enum_type);

        // Generate variant constants
        int32_t index = 0;
        for (const auto &variant : node->variants())
        {
            std::string variant_name = name + "::" + variant->name();
            llvm::Constant *value = llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), index++);
            // Could store in a constant registry
        }

        return enum_type;
    }

    //===================================================================
    // Helpers
    //===================================================================

    llvm::FunctionType *DeclarationCodegen::get_function_type(Cryo::FunctionDeclarationNode *node,
                                                                bool has_this_param)
    {
        if (!node)
            return nullptr;

        // Get return type
        llvm::Type *return_type = get_llvm_type(node->get_resolved_return_type());
        if (!return_type)
        {
            return_type = llvm::Type::getVoidTy(llvm_ctx());
        }

        // Collect parameter types
        std::vector<llvm::Type *> param_types;

        // Add 'this' pointer for methods
        if (has_this_param)
        {
            param_types.push_back(llvm::PointerType::get(llvm_ctx(), 0));
        }

        // Add regular parameters
        bool is_variadic = node->is_variadic();
        if (node->parameters().size() > 0)
        {
            for (const auto &param : node->parameters())
            {
                llvm::Type *param_type = get_llvm_type(param->get_resolved_type());
                if (param_type)
                {
                    param_types.push_back(param_type);
                }
            }
        }

        return llvm::FunctionType::get(return_type, param_types, is_variadic);
    }

    std::string DeclarationCodegen::mangle_function_name(const std::string &name,
                                                           const std::vector<std::string> &namespace_parts,
                                                           const std::vector<Cryo::Type *> &param_types)
    {
        std::string result;

        // Add namespace prefix
        for (const auto &ns : namespace_parts)
        {
            result += ns + "::";
        }

        result += name;

        // Add parameter type suffix for overloading (simplified)
        if (!param_types.empty())
        {
            result += "(";
            for (size_t i = 0; i < param_types.size(); ++i)
            {
                if (i > 0)
                    result += ",";
                if (param_types[i])
                {
                    result += param_types[i]->to_string();
                }
            }
            result += ")";
        }

        return result;
    }

    std::string DeclarationCodegen::generate_method_name(const std::string &type_name,
                                                           const std::string &method_name,
                                                           const std::vector<Cryo::Type *> &param_types)
    {
        std::string result = type_name + "::" + method_name;

        // Add parameter types for overload resolution
        if (!param_types.empty())
        {
            result += "(";
            for (size_t i = 0; i < param_types.size(); ++i)
            {
                if (i > 0)
                    result += ",";
                if (param_types[i])
                {
                    result += param_types[i]->to_string();
                }
            }
            result += ")";
        }

        return result;
    }

    llvm::Function *DeclarationCodegen::get_or_create_function(const std::string &name,
                                                                 llvm::FunctionType *fn_type,
                                                                 llvm::GlobalValue::LinkageTypes linkage)
    {
        llvm::Function *fn = module()->getFunction(name);
        if (fn)
            return fn;

        fn = llvm::Function::Create(fn_type, linkage, name, module());
        ctx().register_function(name, fn);
        return fn;
    }

    bool DeclarationCodegen::is_function_declared(const std::string &name) const
    {
        return module()->getFunction(name) != nullptr;
    }

    //===================================================================
    // Internal Helpers
    //===================================================================

    void DeclarationCodegen::generate_parameters(llvm::Function *fn,
                                                   Cryo::FunctionDeclarationNode *node,
                                                   unsigned start_idx)
    {
        if (!fn || !node )
            return;

        auto arg_it = fn->arg_begin();
        std::advance(arg_it, start_idx);

        for (const auto &param : node->parameters())
        {
            if (arg_it == fn->arg_end())
                break;

            arg_it->setName(param->name());
            ++arg_it;
        }
    }

    void DeclarationCodegen::generate_function_body(llvm::Function *fn,
                                                      Cryo::FunctionDeclarationNode *node)
    {
        if (!fn || !node || !node->body())
            return;

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Generating body for function: {}", fn->getName().str());

        // Set current function context for variable allocation
        auto fn_ctx = std::make_unique<FunctionContext>(fn, node);
        fn_ctx->entry_block = &fn->getEntryBlock();
        ctx().set_current_function(std::move(fn_ctx));

        // Enter function scope for local variables
        values().enter_scope(fn->getName().str());

        // Allocate space for parameters
        for (auto &arg : fn->args())
        {
            llvm::AllocaInst *alloca = create_entry_alloca(fn, arg.getType(), arg.getName().str());
            create_store(&arg, alloca);
            values().set_value(arg.getName().str(), nullptr, alloca);
        }

        // Generate body statements
        CodegenVisitor *visitor = ctx().visitor();
        node->body()->accept(*visitor);

        // Add implicit return if needed
        llvm::BasicBlock *current_block = builder().GetInsertBlock();
        if (current_block && !current_block->getTerminator())
        {
            if (fn->getReturnType()->isVoidTy())
            {
                builder().CreateRetVoid();
            }
            else
            {
                // Return default value
                llvm::Value *default_val = llvm::Constant::getNullValue(fn->getReturnType());
                builder().CreateRet(default_val);
            }
        }

        // Exit function scope
        values().exit_scope();

        // Clear function context
        ctx().clear_current_function();
    }

    llvm::Function *DeclarationCodegen::generate_default_constructor(const std::string &type_name,
                                                                       llvm::StructType *struct_type)
    {
        std::string ctor_name = type_name + "::init";

        // Check if already exists
        if (llvm::Function *existing = module()->getFunction(ctor_name))
        {
            return existing;
        }

        // Constructor takes 'this' pointer and returns void
        llvm::Type *this_type = llvm::PointerType::get(llvm_ctx(), 0);
        llvm::FunctionType *ctor_type = llvm::FunctionType::get(
            llvm::Type::getVoidTy(llvm_ctx()), {this_type}, false);

        llvm::Function *ctor = llvm::Function::Create(ctor_type, llvm::Function::ExternalLinkage,
                                                       ctor_name, module());

        // Create body
        llvm::BasicBlock *entry = llvm::BasicBlock::Create(llvm_ctx(), "entry", ctor);
        builder().SetInsertPoint(entry);

        // Zero-initialize all fields
        llvm::Value *this_ptr = ctor->arg_begin();
        for (unsigned i = 0; i < struct_type->getNumElements(); ++i)
        {
            llvm::Value *field_ptr = create_struct_gep(struct_type, this_ptr, i, "field." + std::to_string(i));
            llvm::Type *field_type = struct_type->getElementType(i);
            llvm::Value *zero = llvm::Constant::getNullValue(field_type);
            create_store(zero, field_ptr);
        }

        builder().CreateRetVoid();

        ctx().register_function(ctor_name, ctor);
        return ctor;
    }

    llvm::Function *DeclarationCodegen::generate_destructor(const std::string &type_name,
                                                              llvm::StructType *class_type)
    {
        std::string dtor_name = type_name + "::destroy";

        // Check if already exists
        if (llvm::Function *existing = module()->getFunction(dtor_name))
        {
            return existing;
        }

        // Destructor takes 'this' pointer and returns void
        llvm::Type *this_type = llvm::PointerType::get(llvm_ctx(), 0);
        llvm::FunctionType *dtor_type = llvm::FunctionType::get(
            llvm::Type::getVoidTy(llvm_ctx()), {this_type}, false);

        llvm::Function *dtor = llvm::Function::Create(dtor_type, llvm::Function::ExternalLinkage,
                                                       dtor_name, module());

        // Create empty body (subclasses can override)
        llvm::BasicBlock *entry = llvm::BasicBlock::Create(llvm_ctx(), "entry", dtor);
        builder().SetInsertPoint(entry);
        builder().CreateRetVoid();

        ctx().register_function(dtor_name, dtor);
        return dtor;
    }

    llvm::GlobalValue::LinkageTypes DeclarationCodegen::get_linkage(Cryo::ASTNode *node) const
    {
        // Check for public/private visibility
        // For now, default to external linkage
        return llvm::GlobalValue::ExternalLinkage;
    }

    void DeclarationCodegen::apply_function_attributes(llvm::Function *fn,
                                                         Cryo::FunctionDeclarationNode *node)
    {
        if (!fn || !node)
            return;

        // Add common attributes
        fn->addFnAttr(llvm::Attribute::NoUnwind);

        // Check for inline hint
        // fn->addFnAttr(llvm::Attribute::InlineHint);
    }

    llvm::Constant *DeclarationCodegen::generate_global_initializer(Cryo::VariableDeclarationNode *node,
                                                                      llvm::Type *type)
    {
        if (!node || !type)
            return llvm::Constant::getNullValue(type);

        // If there's an initializer, try to evaluate it
        if (node->initializer())
        {
            // For simple cases, we can evaluate at compile time
            // Complex cases would need constant folding
        }

        // Default to zero initializer
        return llvm::Constant::getNullValue(type);
    }

    //===================================================================
    // High-Level Entry Points (called by CodegenVisitor)
    //===================================================================

    llvm::Function *DeclarationCodegen::generate_function(Cryo::FunctionDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        // If function has a body, generate full definition; otherwise just declaration
        if (node->body())
        {
            return generate_function_definition(node);
        }
        else
        {
            return generate_function_declaration(node);
        }
    }

    void DeclarationCodegen::generate_variable(Cryo::VariableDeclarationNode *node)
    {
        if (!node)
            return;

        // Check if we're at global scope or function scope
        llvm::Function *current_fn = builder().GetInsertBlock()
                                         ? builder().GetInsertBlock()->getParent()
                                         : nullptr;

        if (current_fn)
        {
            // Local variable
            generate_local_variable(node);
        }
        else
        {
            // Global variable
            generate_global_variable(node);
        }
    }

    void DeclarationCodegen::generate_method(Cryo::StructMethodNode *node)
    {
        if (!node)
            return;

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating struct method: {}", node->name());

        // Get the parent type name from context
        std::string parent_type = ctx().current_type_name();

        // StructMethodNode IS a FunctionDeclarationNode, use it directly
        generate_method_declaration(node, parent_type);
    }

    void DeclarationCodegen::generate_impl_block(Cryo::ImplementationBlockNode *node)
    {
        if (!node)
            return;

        std::string type_name = node->target_type();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating impl block for {}", type_name);

        // Skip generic type implementations - they should only be instantiated with concrete types
        // Generic types have unresolved type parameters like <T>, <T, E>, etc.
        if (type_name.find('<') != std::string::npos)
        {
            // Check if it contains unresolved generic parameters (single capital letters)
            size_t start = type_name.find('<');
            size_t end = type_name.find('>');
            if (start != std::string::npos && end != std::string::npos && end > start)
            {
                std::string params = type_name.substr(start + 1, end - start - 1);
                // Check for simple generic parameters like T, E, K, V (single uppercase letters)
                // or common patterns like T, E separated by commas
                bool has_generic_param = false;
                size_t pos = 0;
                while (pos < params.length())
                {
                    // Skip whitespace
                    while (pos < params.length() && (params[pos] == ' ' || params[pos] == ','))
                        pos++;
                    if (pos >= params.length())
                        break;

                    // Check if this is a single uppercase letter (generic parameter)
                    if (std::isupper(params[pos]))
                    {
                        size_t param_end = pos + 1;
                        while (param_end < params.length() && std::isalnum(params[param_end]))
                            param_end++;

                        // If it's a single letter or looks like a type parameter, it's generic
                        if (param_end == pos + 1 || (param_end - pos <= 2 && std::isupper(params[pos])))
                        {
                            has_generic_param = true;
                            break;
                        }
                        pos = param_end;
                    }
                    else
                    {
                        // Skip to next separator
                        while (pos < params.length() && params[pos] != ',' && params[pos] != ' ')
                            pos++;
                    }
                }

                if (has_generic_param)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Skipping generic impl block for {} - will be instantiated with concrete types",
                              type_name);
                    return;
                }
            }
        }

        // Set current type context
        std::string previous_type = ctx().current_type_name();
        ctx().set_current_type_name(type_name);

        // Generate each method in the impl block
        for (const auto &method : node->method_implementations())
        {
            // StructMethodNode IS a FunctionDeclarationNode
            Cryo::StructMethodNode *fn_node = method.get();
            if (!fn_node)
                continue;

            // Generate as a method with the type prefix
            if (fn_node->body())
            {
                // Full definition
                std::string method_name = generate_method_name(type_name, fn_node->name());
                llvm::Function *fn = generate_method_declaration(fn_node, type_name);
                if (fn && fn->empty())
                {
                    // Create entry block and generate body
                    llvm::BasicBlock *entry = llvm::BasicBlock::Create(llvm_ctx(), "entry", fn);
                    builder().SetInsertPoint(entry);

                    // Set current function context for variable allocation
                    auto fn_ctx = std::make_unique<FunctionContext>(fn, fn_node);
                    fn_ctx->entry_block = entry;
                    ctx().set_current_function(std::move(fn_ctx));

                    // Enter method scope for local variables
                    values().enter_scope(method_name);

                    // Allocate parameters
                    for (auto &arg : fn->args())
                    {
                        llvm::AllocaInst *alloca = create_entry_alloca(fn, arg.getType(), arg.getName().str());
                        create_store(&arg, alloca);
                        values().set_value(arg.getName().str(), nullptr, alloca);
                    }

                    // Generate body
                    CodegenVisitor *visitor = ctx().visitor();
                    fn_node->body()->accept(*visitor);

                    // Add implicit return if needed
                    llvm::BasicBlock *current_block = builder().GetInsertBlock();
                    if (current_block && !current_block->getTerminator())
                    {
                        if (fn->getReturnType()->isVoidTy())
                        {
                            builder().CreateRetVoid();
                        }
                        else
                        {
                            builder().CreateRet(llvm::Constant::getNullValue(fn->getReturnType()));
                        }
                    }

                    // Exit method scope
                    values().exit_scope();

                    // Clear function context
                    ctx().clear_current_function();
                }
            }
            else
            {
                generate_method_declaration(fn_node, type_name);
            }
        }

        // Restore previous type context
        ctx().set_current_type_name(previous_type);
    }

    void DeclarationCodegen::generate_extern_block(Cryo::ExternBlockNode *node)
    {
        if (!node)
            return;

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating extern block with linkage: {}",
                  node->linkage_type());

        // Generate each function declaration in the extern block
        for (const auto &fn_decl : node->function_declarations())
        {
            if (fn_decl)
            {
                generate_extern_function(fn_decl.get());
            }
        }
    }

    void DeclarationCodegen::pre_register_functions()
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Pre-registering functions from symbol table");

        // This would iterate through the symbol table and pre-declare all functions
        // For now, this is a no-op - functions are declared on first use
    }

    void DeclarationCodegen::import_specialized_methods(const Cryo::TypeChecker &type_checker)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Importing specialized methods");

        // This would import specialized generic methods from the type checker
        // For now, this is a no-op
    }

    void DeclarationCodegen::process_global_variables(Cryo::ASTNode *node)
    {
        if (!node)
            return;

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Processing global variables");

        // Walk the AST and generate global variable declarations
        if (auto *program = dynamic_cast<Cryo::ProgramNode *>(node))
        {
            for (const auto &stmt : program->statements())
            {
                if (auto *var_node = dynamic_cast<Cryo::VariableDeclarationNode *>(stmt.get()))
                {
                    generate_global_variable(var_node);
                }
            }
        }
    }

    void DeclarationCodegen::generate_global_constructors()
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating global constructors");

        // Generate global constructor function that initializes global variables
        // This creates an __init function that LLVM will call at startup

        // For now, this is a no-op - globals are zero-initialized
    }

} // namespace Cryo::Codegen
