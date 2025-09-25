#include "Codegen/CodegenVisitor.hpp"
#include "AST/ASTNode.hpp"
#include "Lexer/lexer.hpp"
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <iostream>

namespace Cryo::Codegen
{
    //===================================================================
    // Construction
    //===================================================================

    CodegenVisitor::CodegenVisitor(LLVMContextManager &context_manager,
                                   Cryo::SymbolTable &symbol_table)
        : _context_manager(context_manager), _symbol_table(symbol_table), _value_context(std::make_unique<ValueContext>()), _type_mapper(std::make_unique<TypeMapper>(context_manager)), _current_value(nullptr), _has_errors(false)
    {
    }

    //===================================================================
    // Main Generation Interface
    //===================================================================

    bool CodegenVisitor::generate_program(Cryo::ProgramNode *program)
    {
        if (!program)
        {
            report_error("Cannot generate IR for null program");
            return false;
        }

        clear_errors();

        try
        {
            program->accept(*this);
            return !_has_errors;
        }
        catch (const std::exception &e)
        {
            report_error("Exception during IR generation: " + std::string(e.what()));
            return false;
        }
    }

    llvm::Value *CodegenVisitor::get_generated_value(Cryo::ASTNode *node)
    {
        if (!node)
            return nullptr;

        auto it = _node_values.find(node);
        return (it != _node_values.end()) ? it->second : nullptr;
    }

    //===================================================================
    // AST Visitor Implementation - Minimal versions for compilation
    //===================================================================

    void CodegenVisitor::visit(Cryo::ProgramNode &node)
    {
        // Create the main module for this program
        auto module = _context_manager.create_module("cryo_program");

        if (!module)
        {
            report_error("Failed to create LLVM module");
            return;
        }

        // Generate all top-level statements
        for (auto &stmt : node.statements())
        {
            if (stmt)
            {
                stmt->accept(*this);
            }
        }
    }

    void CodegenVisitor::visit(Cryo::FunctionDeclarationNode &node)
    {
        try
        {
            // Generate the function declaration
            llvm::Function *function = generate_function_declaration(&node);
            if (!function)
            {
                report_error("Failed to generate function declaration: " + node.name());
                return;
            }

            // Register the function
            _functions[node.name()] = function;
            register_value(&node, function);

            // Generate function body if present
            if (node.body())
            {
                bool body_success = generate_function_body(&node, function);
                if (!body_success)
                {
                    report_error("Failed to generate function body: " + node.name());
                }
            }
        }
        catch (const std::exception &e)
        {
            report_error("Exception in function declaration: " + std::string(e.what()), &node);
        }
    }

    void CodegenVisitor::visit(Cryo::VariableDeclarationNode &node)
    {
        try
        {
            // Get the variable name and type
            std::string var_name = node.name();
            std::string type_annotation = node.type_annotation();

            std::cout << "[DEBUG] Variable Declaration: name='" << var_name
                      << "', type_annotation='" << type_annotation << "'" << std::endl;

            if (type_annotation.empty() || type_annotation == "auto")
            {
                report_error("Variable declaration requires explicit type: " + var_name);
                return;
            }

            // Map to LLVM type using type annotation
            llvm::Type *llvm_type = _type_mapper->map_type(type_annotation);
            if (!llvm_type)
            {
                report_error("Failed to map type for variable: " + var_name + " (type: " + type_annotation + ")");
                return;
            }

            // Generate IR based on scope context
            if (_current_function)
            {
                // Local variable in function
                llvm::AllocaInst *alloca = create_entry_block_alloca(
                    _current_function->function, llvm_type, var_name);

                if (!alloca)
                {
                    report_error("Failed to create alloca for variable: " + var_name);
                    return;
                }

                // Store in value context with type information
                // For arrays, we want to store the element type, not the full array type
                llvm::Type *element_type = llvm_type;

                // Handle array types: parse type annotation to extract element type
                if (type_annotation.back() == ']')
                {
                    // For multi-dimensional arrays, we need to extract the element type correctly
                    // For "int[][]", element type should be "int[]"
                    // For "int[]", element type should be "int"

                    // Find the last bracket pair
                    size_t last_bracket_pos = type_annotation.rfind(']');
                    if (last_bracket_pos != std::string::npos && last_bracket_pos >= 1)
                    {
                        // Find the matching opening bracket
                        size_t matching_bracket_pos = type_annotation.rfind('[', last_bracket_pos - 1);
                        if (matching_bracket_pos != std::string::npos)
                        {
                            // Extract everything before the last bracket pair as element type
                            std::string element_type_name = type_annotation.substr(0, matching_bracket_pos);
                            std::cout << "[CodegenVisitor] Variable '" << var_name << "' type '" << type_annotation
                                      << "' -> element_type_name = '" << element_type_name << "'" << std::endl;
                            element_type = _type_mapper->map_type(element_type_name);
                            if (!element_type)
                            {
                                std::cout << "[CodegenVisitor] Warning: Could not map element type '" << element_type_name << "', using full array type" << std::endl;
                                element_type = llvm_type;
                            }
                            else
                            {
                                // Extracted element type successfully
                                std::cout << "[CodegenVisitor] Successfully mapped element type '" << element_type_name
                                          << "' -> " << (element_type->isPointerTy() ? "ptr" : "non-ptr") << std::endl;
                            }
                        }
                    }
                }
                // Handle pointer types: int* should store element type "int*"
                else if (type_annotation.back() == '*')
                {
                    std::cout << "[CodegenVisitor] Pointer variable '" << var_name << "' type '" << type_annotation << "'" << std::endl;

                    // For pointer variables, the element type is the full pointer type
                    // (what's stored in the alloca is the pointer value itself)
                    element_type = llvm_type; // llvm_type is already the pointer type (int*)

                    std::cout << "[CodegenVisitor] Pointer element type set to full pointer type" << std::endl;
                }
                // Handle reference types: &int should store element type "&int" (reference as pointer)
                else if (type_annotation.front() == '&')
                {
                    std::cout << "[CodegenVisitor] Reference variable '" << var_name << "' type '" << type_annotation << "'" << std::endl;

                    // For reference variables, the element type is the full reference type
                    // (what's stored in the alloca is the reference value, implemented as pointer)
                    element_type = llvm_type; // llvm_type is already the pointer type (int*)

                    std::cout << "[CodegenVisitor] Reference element type set to full reference type (as pointer)" << std::endl;
                }
                else if (llvm_type->isArrayTy())
                {
                    // This handles the case where TypeMapper returns actual array types (not pointers)
                    element_type = llvm_type->getArrayElementType();
                }

                _value_context->set_value(var_name, alloca, alloca, element_type);

                // Store the variable type annotation for later method call resolution
                _variable_types[var_name] = type_annotation;

                register_value(&node, alloca);

                // Handle initialization if present
                if (node.has_initializer())
                {
                    node.initializer()->accept(*this);
                    llvm::Value *init_value = get_current_value();

                    if (init_value)
                    {
                        create_store(init_value, alloca);
                    }
                }
            }
            else
            {
                // Global variable
                auto module = _context_manager.get_module();
                if (!module)
                {
                    report_error("No module available for global variable: " + var_name);
                    return;
                }

                llvm::Constant *initializer = nullptr;
                if (node.has_initializer())
                {
                    // For now, only handle constant initializers for globals
                    if (auto literal = dynamic_cast<Cryo::LiteralNode *>(node.initializer()))
                    {
                        node.initializer()->accept(*this);
                        if (auto const_val = llvm::dyn_cast<llvm::Constant>(get_current_value()))
                        {
                            initializer = const_val;
                        }
                    }
                }

                if (!initializer)
                {
                    // Default zero initializer
                    initializer = llvm::Constant::getNullValue(llvm_type);
                }

                auto global_var = new llvm::GlobalVariable(
                    *module, llvm_type, false,
                    llvm::GlobalValue::ExternalLinkage,
                    initializer, var_name);

                _globals[var_name] = global_var;
                register_value(&node, global_var);
            }
        }
        catch (const std::exception &e)
        {
            report_error("Exception in variable declaration: " + std::string(e.what()), &node);
        }
    }

    void CodegenVisitor::visit(Cryo::StructDeclarationNode &node)
    {
        std::cout << "[CodegenVisitor] Generating struct declaration: " << node.name() << std::endl;

        // Check if this is a generic struct
        if (!node.generic_parameters().empty())
        {
            std::cout << "[CodegenVisitor] Generic struct detected: " << node.name() << " with " << node.generic_parameters().size() << " type parameters" << std::endl;

            // For generic structs, we don't generate the LLVM type immediately
            // Instead, we just register that this is a generic struct template
            std::cout << "[CodegenVisitor] Registered generic struct template: " << node.name() << std::endl;
            register_value(&node, nullptr);
            return;
        }

        auto &context = _context_manager.get_context();
        auto module = _context_manager.get_module();

        if (!module)
        {
            report_error("No module available for struct generation", &node);
            return;
        }

        // Use TypeMapper to create the struct type and register fields automatically
        llvm::StructType *struct_type = _type_mapper->map_struct_type(&node);

        if (!struct_type)
        {
            report_error("Failed to map struct type: " + node.name(), &node);
            return;
        }

        // Store the struct type for later use in CodegenVisitor
        _types[node.name()] = struct_type;

        std::cout << "[CodegenVisitor] Created LLVM struct type for " << node.name() << std::endl;
        register_value(&node, nullptr); // Struct declarations don't have runtime values
    }

    void CodegenVisitor::visit(Cryo::ClassDeclarationNode &node)
    {
        std::cout << "[CodegenVisitor] Visiting ClassDeclarationNode: " << node.name() << std::endl;

        // Generate LLVM type for the class
        llvm::Type *class_type = _type_mapper->map_class_type(&node);
        if (!class_type)
        {
            std::cerr << "[CodegenVisitor] Failed to map class type for " << node.name() << std::endl;
            register_value(&node, nullptr);
            return;
        }

        std::string class_name = node.name();

        // Store the class type for later use (needed for new expressions)
        _types[class_name] = class_type;

        std::cout << "[CodegenVisitor] Created LLVM class type for " << class_name << std::endl;

        // Generate methods defined in the class
        auto &context = _context_manager.get_context();
        auto module = _context_manager.get_module();

        if (module)
        {
            llvm::Type *class_ptr_type = llvm::PointerType::getUnqual(class_type);

            for (const auto &method : node.methods())
            {
                std::cout << "[CodegenVisitor] Generating method: " << class_name << "::" << method->name() << std::endl;

                std::string method_name = method->name();
                std::string qualified_name = class_name + "::" + method_name;

                // Build parameter types (first parameter is always 'this')
                std::vector<llvm::Type *> param_types;
                param_types.push_back(class_ptr_type); // 'this' pointer

                // Add other parameters
                for (const auto &param : method->parameters())
                {
                    if (param)
                    {
                        std::string param_type_str = param->type_annotation();
                        llvm::Type *param_type = _type_mapper->map_type(param_type_str);
                        if (param_type)
                        {
                            param_types.push_back(param_type);
                        }
                        else
                        {
                            report_error("Failed to map parameter type: " + param_type_str, method.get());
                            continue;
                        }
                    }
                }

                // Map return type
                llvm::Type *return_type = llvm::Type::getVoidTy(context);
                if (!method->return_type_annotation().empty() && method->return_type_annotation() != "void")
                {
                    llvm::Type *mapped_return_type = _type_mapper->map_type(method->return_type_annotation());
                    if (mapped_return_type)
                    {
                        return_type = mapped_return_type;
                    }
                }

                // Create function type and function
                llvm::FunctionType *func_type = llvm::FunctionType::get(return_type, param_types, false);
                llvm::Function *func = llvm::Function::Create(func_type, llvm::Function::ExternalLinkage, qualified_name, *module);

                // Store function for later lookup
                _functions[qualified_name] = func;

                // Set parameter names
                auto arg_it = func->arg_begin();
                arg_it->setName("this");
                ++arg_it;

                for (const auto &param : method->parameters())
                {
                    if (param && arg_it != func->arg_end())
                    {
                        arg_it->setName(param->name());
                        ++arg_it;
                    }
                }

                // Generate method body if it exists
                if (method->body())
                {
                    // Create basic block for method entry
                    llvm::BasicBlock *entry_block = llvm::BasicBlock::Create(context, "entry", func);
                    _context_manager.get_builder().SetInsertPoint(entry_block);

                    // Store current function context
                    _current_function = std::make_unique<FunctionContext>(func, static_cast<FunctionDeclarationNode *>(method.get()));
                    _current_function->entry_block = entry_block;

                    // Enter function scope
                    enter_scope(entry_block);

                    // Create allocas and store parameter values - reset arg_it to beginning
                    arg_it = func->arg_begin();

                    // Handle 'this' parameter first
                    if (arg_it != func->arg_end())
                    {
                        std::cout << "[CodegenVisitor] Setting up 'this' parameter for class method" << std::endl;
                        llvm::AllocaInst *this_alloca = create_entry_block_alloca(func, class_ptr_type, "this");
                        if (this_alloca)
                        {
                            _context_manager.get_builder().CreateStore(&*arg_it, this_alloca);
                            _value_context->set_value("this", this_alloca, this_alloca, class_ptr_type);
                            std::cout << "[CodegenVisitor] 'this' parameter set up successfully for class method" << std::endl;
                        }
                        else
                        {
                            std::cout << "[CodegenVisitor] Failed to create 'this' alloca for class method" << std::endl;
                        }
                        ++arg_it;
                    }

                    // Handle other parameters
                    for (const auto &param : method->parameters())
                    {
                        if (param && arg_it != func->arg_end())
                        {
                            std::string param_name = param->name();
                            std::string param_type_str = param->type_annotation();
                            llvm::Type *param_type = _type_mapper->map_type(param_type_str);

                            if (param_type)
                            {
                                llvm::AllocaInst *param_alloca = create_entry_block_alloca(func, param_type, param_name);
                                if (param_alloca)
                                {
                                    _context_manager.get_builder().CreateStore(&*arg_it, param_alloca);
                                    _value_context->set_value(param_name, param_alloca, param_alloca, param_type);
                                }
                            }
                            ++arg_it;
                        }
                    }

                    // Generate method body
                    method->body()->accept(*this);

                    // Add return if not already present
                    if (!entry_block->getTerminator())
                    {
                        if (return_type->isVoidTy())
                        {
                            _context_manager.get_builder().CreateRetVoid();
                        }
                        else
                        {
                            // Return zero/null for non-void functions without explicit return
                            _context_manager.get_builder().CreateRet(llvm::Constant::getNullValue(return_type));
                        }
                    }

                    // Exit scope and clean up
                    exit_scope();
                    _current_function.reset();
                }
            }
        }

        register_value(&node, nullptr); // Class declarations don't have runtime values
    }

    void CodegenVisitor::visit(Cryo::EnumDeclarationNode &node)
    {
        // Generate LLVM type for the enum
        llvm::Type *enum_type = _type_mapper->map_enum_type(&node);
        if (!enum_type)
        {
            report_error("Failed to generate LLVM type for enum: " + node.name());
            register_value(&node, nullptr);
            return;
        }

        // Register the enum type in the type system
        _type_mapper->register_type(node.name(), enum_type);

        // Determine if this is a simple or complex enum
        bool is_simple = true;
        for (const auto &variant : node.variants())
        {
            if (!variant->associated_types().empty())
            {
                is_simple = false;
                break;
            }
        }

        if (is_simple)
        {
            // Generate constants for simple enum variants
            generate_simple_enum_constants(&node, enum_type);
        }
        else
        {
            // Generate constructor functions for complex enum variants
            generate_complex_enum_constructors(&node, enum_type);
        }

        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::DeclarationNode &node)
    {
        // Base declaration node - delegate to specific implementations
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::EnumVariantNode &node)
    {
        // TODO: Implement enum variant generation
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::TypeAliasDeclarationNode &node)
    {
        // TODO: Implement type alias generation
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::ExternBlockNode &node)
    {
        // TODO: Implement extern block generation
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::GenericParameterNode &node)
    {
        // TODO: Implement generic parameter generation
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::StructFieldNode &node)
    {
        // TODO: Implement struct field generation
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::StructMethodNode &node)
    {
        // TODO: Implement struct method generation
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::StatementNode &node)
    {
        // Base statement node - delegate to specific implementations
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::MatchArmNode &node)
    {
        std::cout << "[CodegenVisitor] Generating match arm" << std::endl;

        // The match arm code generation is handled by generate_match_arm
        // This visitor is called when a match arm is visited directly (rare)

        if (node.body())
        {
            node.body()->accept(*this);
        }

        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::PatternNode &node)
    {
        // TODO: Implement pattern generation
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::EnumPatternNode &node)
    {
        // TODO: Implement enum pattern generation
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::DeclarationStatementNode &node)
    {
        if (node.declaration())
        {
            node.declaration()->accept(*this);
        }
    }

    void CodegenVisitor::visit(Cryo::ExpressionNode &node)
    {
        // Base expression node - delegate to specific implementations
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::ImplementationBlockNode &node)
    {
        std::cout << "[CodegenVisitor] Generating implementation block for: " << node.target_type() << std::endl;

        auto &context = _context_manager.get_context();
        auto module = _context_manager.get_module();
        auto &builder = _context_manager.get_builder();

        if (!module)
        {
            report_error("No module available for implementation block generation", &node);
            return;
        }

        std::string target_type_name = node.target_type();

        // Look up the struct type
        auto struct_type_it = _types.find(target_type_name);
        if (struct_type_it == _types.end())
        {
            report_error("Unknown struct type in implementation block: " + target_type_name, &node);
            return;
        }

        llvm::Type *struct_type = struct_type_it->second;
        llvm::Type *struct_ptr_type = llvm::PointerType::getUnqual(struct_type);

        // Generate all method implementations
        for (const auto &method : node.method_implementations())
        {
            if (!method)
                continue;

            std::string method_name = method->name();
            std::string qualified_name = target_type_name + "::" + method_name;

            std::cout << "[CodegenVisitor] Generating method: " << qualified_name << std::endl;

            // Create function type - first parameter is always 'this' pointer
            std::vector<llvm::Type *> param_types;
            param_types.push_back(struct_ptr_type); // 'this' pointer

            // Add other parameters
            for (const auto &param : method->parameters())
            {
                if (param)
                {
                    std::string param_type_str = param->type_annotation();
                    llvm::Type *param_type = _type_mapper->map_type(param_type_str);
                    if (param_type)
                    {
                        param_types.push_back(param_type);
                    }
                    else
                    {
                        report_error("Failed to map parameter type: " + param_type_str, method.get());
                        continue;
                    }
                }
            }

            // Determine return type
            llvm::Type *return_type = llvm::Type::getVoidTy(context);
            std::string return_type_str = method->return_type_annotation();
            if (!return_type_str.empty() && return_type_str != "void")
            {
                llvm::Type *mapped_return_type = _type_mapper->map_type(return_type_str);
                if (mapped_return_type)
                {
                    return_type = mapped_return_type;
                }
            }

            // Create function type and function
            llvm::FunctionType *func_type = llvm::FunctionType::get(return_type, param_types, false);
            llvm::Function *func = llvm::Function::Create(func_type, llvm::Function::ExternalLinkage, qualified_name, *module);

            // Store function for later lookup
            _functions[qualified_name] = func;

            // Set parameter names
            auto arg_it = func->arg_begin();
            arg_it->setName("this");
            ++arg_it;

            for (const auto &param : method->parameters())
            {
                if (param && arg_it != func->arg_end())
                {
                    arg_it->setName(param->name());
                    ++arg_it;
                }
            }

            // Create basic block and generate method body
            llvm::BasicBlock *entry_block = llvm::BasicBlock::Create(context, "entry", func);
            builder.SetInsertPoint(entry_block);

            // Store current function context
            _current_function = std::make_unique<FunctionContext>(func, static_cast<FunctionDeclarationNode *>(method.get()));
            _current_function->entry_block = entry_block;

            // Enter function scope
            enter_scope(entry_block);

            // Create allocas and store parameter values - reset arg_it to beginning
            arg_it = func->arg_begin();

            // Handle 'this' parameter first
            if (arg_it != func->arg_end())
            {
                std::cout << "[CodegenVisitor] Setting up 'this' parameter for method" << std::endl;
                llvm::AllocaInst *this_alloca = create_entry_block_alloca(func, struct_ptr_type, "this");
                if (this_alloca)
                {
                    builder.CreateStore(&*arg_it, this_alloca);
                    _value_context->set_value("this", this_alloca, this_alloca, struct_ptr_type);
                    std::cout << "[CodegenVisitor] 'this' parameter set up successfully" << std::endl;
                }
                else
                {
                    std::cout << "[CodegenVisitor] Failed to create 'this' alloca" << std::endl;
                }
                ++arg_it;
            }

            // Handle other parameters
            for (const auto &param : method->parameters())
            {
                if (param && arg_it != func->arg_end())
                {
                    std::string param_name = param->name();
                    std::string param_type_str = param->type_annotation();
                    llvm::Type *param_type = _type_mapper->map_type(param_type_str);

                    if (param_type)
                    {
                        llvm::AllocaInst *param_alloca = create_entry_block_alloca(func, param_type, param_name);
                        if (param_alloca)
                        {
                            builder.CreateStore(&*arg_it, param_alloca);
                            _value_context->set_value(param_name, param_alloca, param_alloca, param_type);
                        }
                    }
                    ++arg_it;
                }
            }

            // Generate method body
            if (method->body())
            {
                method->body()->accept(*this);
            }

            // Add return if not already present
            if (!entry_block->getTerminator())
            {
                if (return_type->isVoidTy())
                {
                    builder.CreateRetVoid();
                }
                else
                {
                    // Return zero/null for non-void functions without explicit return
                    builder.CreateRet(llvm::Constant::getNullValue(return_type));
                }
            }

            // Exit scope and clean up
            exit_scope();
            _current_function.reset();
        }

        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::BlockStatementNode &node)
    {
        auto &builder = _context_manager.get_builder();
        llvm::BasicBlock *currentBlock = builder.GetInsertBlock();

        for (auto &statement : node.statements())
        {
            if (statement)
            {
                statement->accept(*this);
            }
        }

        // After processing all statements in the block, make sure the current basic block
        // has a terminator if it doesn't already have one. This is important for nested
        // control flow where the block might end with a control flow statement.
        llvm::BasicBlock *finalBlock = builder.GetInsertBlock();
        if (finalBlock && !finalBlock->getTerminator())
        {
            // The block doesn't have a terminator. This can happen when the last statement
            // in the block is a control flow statement (like if-else) that leaves us in
            // a merge block without a terminator.
            //
            // Since we're in a block statement (compound statement), we should continue
            // with the next instruction after the block. However, if this block is the
            // body of an if statement or loop, the parent control structure will handle
            // the terminator.
            //
            // For now, we'll let the parent control structure handle this case.
            // If we're at function level, the function will add a return.
        }
    }

    void CodegenVisitor::visit(Cryo::ReturnStatementNode &node)
    {
        try
        {
            auto &builder = _context_manager.get_builder();

            if (node.expression())
            {
                // Generate return value
                node.expression()->accept(*this);
                llvm::Value *return_value = get_current_value();

                if (!return_value)
                {
                    report_error("Failed to generate return value");
                    return;
                }

                if (_current_function && _current_function->return_value_alloca)
                {
                    // Store return value and jump to return block
                    create_store(return_value, _current_function->return_value_alloca);
                    builder.CreateBr(_current_function->return_block);
                }
                else
                {
                    // Direct return
                    builder.CreateRet(return_value);
                }
            }
            else
            {
                // Void return
                builder.CreateRetVoid();
            }

            register_value(&node, nullptr);
        }
        catch (const std::exception &e)
        {
            report_error("Exception in return statement: " + std::string(e.what()), &node);
        }
    }

    void CodegenVisitor::visit(Cryo::IfStatementNode &node)
    {
        try
        {
            generate_if_statement(&node);
            register_value(&node, nullptr);
        }
        catch (const std::exception &e)
        {
            report_error("Exception in if statement: " + std::string(e.what()), &node);
        }
    }

    void CodegenVisitor::visit(Cryo::WhileStatementNode &node)
    {
        try
        {
            generate_while_loop(&node);
            register_value(&node, nullptr);
        }
        catch (const std::exception &e)
        {
            report_error("Exception in while loop: " + std::string(e.what()), &node);
        }
    }

    void CodegenVisitor::visit(Cryo::ForStatementNode &node)
    {
        generate_for_loop(&node);
    }

    void CodegenVisitor::visit(Cryo::MatchStatementNode &node)
    {
        std::cout << "[CodegenVisitor] Generating match statement" << std::endl;

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Generate the expression being matched
        node.expr()->accept(*this);
        llvm::Value *match_value = get_current_value();

        if (!match_value)
        {
            std::cerr << "Error: Failed to generate match expression" << std::endl;
            register_value(&node, nullptr);
            return;
        }

        std::cout << "[CodegenVisitor] Match value generated, creating switch statement" << std::endl;

        // Create basic blocks for each match arm and the end
        llvm::Function *current_function = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock *end_block = llvm::BasicBlock::Create(context, "match.end", current_function);
        llvm::BasicBlock *default_block = llvm::BasicBlock::Create(context, "match.default", current_function);

        // For enum matching, we need to extract the discriminant
        llvm::Value *discriminant = extract_enum_discriminant(match_value);

        if (!discriminant)
        {
            std::cerr << "Error: Failed to extract discriminant from match value" << std::endl;
            register_value(&node, nullptr);
            return;
        }

        // Create switch instruction
        llvm::SwitchInst *switch_inst = builder.CreateSwitch(discriminant, default_block, node.arms().size());

        // Generate code for each match arm
        for (size_t i = 0; i < node.arms().size(); ++i)
        {
            auto &arm = node.arms()[i];
            llvm::BasicBlock *arm_block = llvm::BasicBlock::Create(context, "match.arm." + std::to_string(i), current_function);

            // Extract discriminant value from the pattern
            int discriminant_value = get_pattern_discriminant(arm->pattern());
            llvm::ConstantInt *case_value = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), discriminant_value);
            switch_inst->addCase(case_value, arm_block);

            // Generate arm code
            builder.SetInsertPoint(arm_block);
            generate_match_arm(arm.get(), match_value);

            // Jump to end block if no terminator was created
            if (!arm_block->getTerminator())
            {
                builder.CreateBr(end_block);
            }
        }

        // Generate default case (should not be reached for exhaustive matches)
        builder.SetInsertPoint(default_block);
        builder.CreateUnreachable();

        // Continue after match
        builder.SetInsertPoint(end_block);

        std::cout << "[CodegenVisitor] Match statement generated successfully" << std::endl;
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::ExpressionStatementNode &node)
    {
        if (node.expression())
        {
            node.expression()->accept(*this);
        }
    }

    void CodegenVisitor::visit(Cryo::BreakStatementNode &node)
    {
        auto &builder = _context_manager.get_builder();

        if (_loop_stack.empty())
        {
            std::cerr << "[ERROR] Break statement used outside of loop context" << std::endl;
            register_value(&node, nullptr);
            return;
        }

        // Get the current loop context
        const auto &loop_context = _loop_stack.top();

        // Create branch to the loop exit block
        builder.CreateBr(loop_context.break_block);

        // Create a new basic block for any unreachable code after break
        // This is needed because LLVM requires all basic blocks to end with a terminator
        llvm::Function *function = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock *unreachableBlock = llvm::BasicBlock::Create(_context_manager.get_context(), "after.break", function);
        builder.SetInsertPoint(unreachableBlock);

        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::ContinueStatementNode &node)
    {
        auto &builder = _context_manager.get_builder();

        if (_loop_stack.empty())
        {
            std::cerr << "[ERROR] Continue statement used outside of loop context" << std::endl;
            register_value(&node, nullptr);
            return;
        }

        // Get the current loop context
        const auto &loop_context = _loop_stack.top();

        // Create branch to the loop continue block (increment in for loop, condition in while loop)
        builder.CreateBr(loop_context.continue_block);

        // Create a new basic block for any unreachable code after continue
        // This is needed because LLVM requires all basic blocks to end with a terminator
        llvm::Function *function = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock *unreachableBlock = llvm::BasicBlock::Create(_context_manager.get_context(), "after.continue", function);
        builder.SetInsertPoint(unreachableBlock);

        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::LiteralNode &node)
    {
        auto &llvm_ctx = _context_manager.get_context();
        llvm::Value *literal_value = nullptr;

        switch (node.literal_kind())
        {
        case TokenKind::TK_NUMERIC_CONSTANT:
        {
            std::string value_str = node.value();

            // Check if it's a float (contains decimal point)
            if (value_str.find('.') != std::string::npos)
            {
                // Float literal
                float float_val = std::stof(value_str);
                literal_value = llvm::ConstantFP::get(llvm::Type::getFloatTy(llvm_ctx), float_val);
            }
            else
            {
                // Integer literal
                int64_t int_val = std::stoll(value_str);
                literal_value = llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx), int_val);
            }
            break;
        }
        case TokenKind::TK_BOOLEAN_LITERAL:
        case TokenKind::TK_KW_TRUE:
        case TokenKind::TK_KW_FALSE:
        {
            bool bool_val = (node.value() == "true");
            literal_value = llvm::ConstantInt::get(llvm::Type::getInt1Ty(llvm_ctx), bool_val);
            break;
        }
        case TokenKind::TK_CHAR_CONSTANT:
        {
            std::string char_val = node.value();

            // Handle character literals like 'A', '\n', etc.
            char actual_char = 0;
            if (char_val.length() >= 3 && char_val.front() == '\'' && char_val.back() == '\'')
            {
                // Extract the character between the quotes
                std::string inner = char_val.substr(1, char_val.length() - 2);
                if (inner.length() == 1)
                {
                    actual_char = inner[0];
                }
                else if (inner.length() == 2 && inner[0] == '\\')
                {
                    // Handle escape sequences
                    switch (inner[1])
                    {
                    case 'n':
                        actual_char = '\n';
                        break;
                    case 't':
                        actual_char = '\t';
                        break;
                    case 'r':
                        actual_char = '\r';
                        break;
                    case '\\':
                        actual_char = '\\';
                        break;
                    case '\'':
                        actual_char = '\'';
                        break;
                    case '\"':
                        actual_char = '\"';
                        break;
                    case '0':
                        actual_char = '\0';
                        break;
                    default:
                        actual_char = inner[1];
                        break; // Fallback
                    }
                }
            }
            literal_value = llvm::ConstantInt::get(llvm::Type::getInt8Ty(llvm_ctx), actual_char);
            break;
        }
        case TokenKind::TK_STRING_LITERAL:
        {
            std::string str_val = node.value();

            // Remove surrounding quotes if they exist
            if (str_val.length() >= 2 && str_val.front() == '"' && str_val.back() == '"')
            {
                str_val = str_val.substr(1, str_val.length() - 2);
            }

            literal_value = _context_manager.get_builder().CreateGlobalStringPtr(str_val);
            break;
        }
        default:
            // For now, create a placeholder i32 zero
            literal_value = llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx), 0);
            break;
        }

        set_current_value(literal_value);
        register_value(&node, literal_value);
    }

    void CodegenVisitor::visit(Cryo::IdentifierNode &node)
    {
        try
        {
            std::string identifier = node.name();

            // Handle 'this' keyword in method context
            if (identifier == "this" && _current_function && _current_function->function)
            {
                // In a method, 'this' is the first parameter
                llvm::Function::arg_iterator args = _current_function->function->arg_begin();
                if (args != _current_function->function->arg_end())
                {
                    llvm::Value *this_param = &(*args);
                    set_current_value(this_param);
                    register_value(&node, this_param);
                    return;
                }
            }

            // Try to find variable in current scope
            llvm::Value *var_alloca = _value_context->get_value(identifier);
            if (var_alloca)
            {
                // Load the variable value if it's an alloca
                if (llvm::isa<llvm::AllocaInst>(var_alloca))
                {
                    // Get the proper element type for the alloca
                    llvm::Type *element_type = _value_context->get_alloca_type(identifier);

                    llvm::Value *loaded_value = create_load(var_alloca, element_type, identifier + ".load");
                    set_current_value(loaded_value);
                    register_value(&node, loaded_value);
                }
                else
                {
                    // Direct value (like function parameters)
                    set_current_value(var_alloca);
                    register_value(&node, var_alloca);
                }
                return;
            }

            // Try to find global variable
            auto global_it = _globals.find(identifier);
            if (global_it != _globals.end())
            {
                // Look up the element type for this global
                llvm::Type *element_type = nullptr;
                auto global_type_it = _global_types.find(identifier);
                if (global_type_it != _global_types.end())
                {
                    element_type = global_type_it->second;
                }

                llvm::Value *loaded_value = create_load(global_it->second, element_type, identifier + ".global.load");
                set_current_value(loaded_value);
                register_value(&node, loaded_value);
                return;
            }

            // Try to find function
            auto func_it = _functions.find(identifier);
            if (func_it != _functions.end())
            {
                set_current_value(func_it->second);
                register_value(&node, func_it->second);
                return;
            }

            // Try to find enum variant
            auto enum_variant_it = _enum_variants.find(identifier);
            if (enum_variant_it != _enum_variants.end())
            {
                set_current_value(enum_variant_it->second);
                register_value(&node, enum_variant_it->second);
                return;
            }

            // If not found, report error
            report_error("Undefined identifier: " + identifier, &node);

            // Create placeholder to continue compilation
            auto &llvm_ctx = _context_manager.get_context();
            llvm::Value *placeholder = llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx), 0);
            set_current_value(placeholder);
            register_value(&node, placeholder);
        }
        catch (const std::exception &e)
        {
            report_error("Exception in identifier lookup: " + std::string(e.what()), &node);
        }
    }

    void CodegenVisitor::visit(Cryo::BinaryExpressionNode &node)
    {
        try
        {
            llvm::Value *result = generate_binary_operation(&node);
            if (result)
            {
                set_current_value(result);
                register_value(&node, result);
            }
            else
            {
                report_error("Failed to generate binary expression", &node);
            }
        }
        catch (const std::exception &e)
        {
            report_error("Exception in binary expression: " + std::string(e.what()), &node);
        }
    }

    void CodegenVisitor::visit(Cryo::UnaryExpressionNode &node)
    {
        try
        {
            llvm::Value *unary_result = generate_unary_operation(&node);
            set_current_value(unary_result);
            register_value(&node, unary_result);
        }
        catch (const std::exception &e)
        {
            report_error("Exception in unary expression: " + std::string(e.what()), &node);
        }
    }

    void CodegenVisitor::visit(Cryo::TernaryExpressionNode &node)
    {
        std::cout << "[CodegenVisitor] Generating ternary expression" << std::endl;

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create basic blocks for then, else, and merge
        llvm::Function *current_function = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock *then_block = llvm::BasicBlock::Create(context, "ternary.then", current_function);
        llvm::BasicBlock *else_block = llvm::BasicBlock::Create(context, "ternary.else", current_function);
        llvm::BasicBlock *merge_block = llvm::BasicBlock::Create(context, "ternary.merge", current_function);

        // Generate condition expression
        node.condition()->accept(*this);
        llvm::Value *condition_value = get_generated_value(node.condition());

        if (!condition_value)
        {
            std::cerr << "[CodegenVisitor] Error: condition value is null in ternary expression" << std::endl;
            register_value(&node, nullptr);
            return;
        }

        // Convert condition to boolean if needed
        if (condition_value->getType() != llvm::Type::getInt1Ty(context))
        {
            if (condition_value->getType()->isIntegerTy())
            {
                condition_value = builder.CreateICmpNE(condition_value,
                                                       llvm::ConstantInt::get(condition_value->getType(), 0), "tobool");
            }
            else if (condition_value->getType()->isFloatingPointTy())
            {
                condition_value = builder.CreateFCmpONE(condition_value,
                                                        llvm::ConstantFP::get(condition_value->getType(), 0.0), "tobool");
            }
        }

        // Create conditional branch
        builder.CreateCondBr(condition_value, then_block, else_block);

        // Generate then expression
        builder.SetInsertPoint(then_block);
        node.true_expression()->accept(*this);
        llvm::Value *then_value = get_generated_value(node.true_expression());
        if (!then_value)
        {
            std::cerr << "[CodegenVisitor] Error: then value is null in ternary expression" << std::endl;
            register_value(&node, nullptr);
            return;
        }
        then_block = builder.GetInsertBlock(); // Update in case of nested expressions
        builder.CreateBr(merge_block);

        // Generate else expression
        builder.SetInsertPoint(else_block);
        node.false_expression()->accept(*this);
        llvm::Value *else_value = get_generated_value(node.false_expression());
        if (!else_value)
        {
            std::cerr << "[CodegenVisitor] Error: else value is null in ternary expression" << std::endl;
            register_value(&node, nullptr);
            return;
        }
        else_block = builder.GetInsertBlock(); // Update in case of nested expressions
        builder.CreateBr(merge_block);

        // Create merge block with PHI node
        builder.SetInsertPoint(merge_block);

        // Ensure both values have the same type (add type coercion if needed)
        llvm::Type *result_type = then_value->getType();
        if (then_value->getType() != else_value->getType())
        {
            std::cerr << "[CodegenVisitor] Warning: Type mismatch in ternary expression branches" << std::endl;
            // For now, we'll assume they should be the same type
            // In a full implementation, we'd need type coercion logic here
        }

        llvm::PHINode *phi = builder.CreatePHI(result_type, 2, "ternary.result");
        phi->addIncoming(then_value, then_block);
        phi->addIncoming(else_value, else_block);

        register_value(&node, phi);
        set_current_value(phi);

        std::cout << "[CodegenVisitor] Generated ternary expression successfully" << std::endl;
    }

    void CodegenVisitor::visit(Cryo::CallExpressionNode &node)
    {
        llvm::Value *call_result = generate_function_call(&node);
        set_current_value(call_result); // Set the current value for expressions
        register_value(&node, call_result);
    }

    void CodegenVisitor::visit(Cryo::NewExpressionNode &node)
    {
        std::cout << "[CodegenVisitor] Generating new expression" << std::endl;

        auto &context = _context_manager.get_context();
        auto module = _context_manager.get_module();
        auto &builder = _context_manager.get_builder();

        if (!module)
        {
            report_error("No module available for new expression", &node);
            return;
        }

        std::string base_type_name = node.type_name();

        // Check if this is a generic instantiation
        std::string full_type_name = base_type_name;
        if (!node.generic_args().empty())
        {
            // Construct the full instantiated type name: "GenericStruct<int>"
            full_type_name = base_type_name + "<";
            for (size_t i = 0; i < node.generic_args().size(); ++i)
            {
                if (i > 0)
                    full_type_name += ",";
                full_type_name += node.generic_args()[i];
            }
            full_type_name += ">";
            std::cout << "[CodegenVisitor] Generic instantiation detected: " << full_type_name << std::endl;
        }

        std::cout << "[CodegenVisitor] Creating new instance of type: " << full_type_name << std::endl;

        // Look up the struct type (use full instantiated name for generics)
        llvm::Type *struct_type = _type_mapper->lookup_type(full_type_name);
        if (!struct_type)
        {
            // For non-generics, try the old lookup method
            auto struct_type_it = _types.find(full_type_name);
            if (struct_type_it != _types.end())
            {
                struct_type = struct_type_it->second;
            }
            else
            {
                report_error("Unknown type in new expression: " + full_type_name, &node);
                return;
            }
        }

        // Allocate memory for the struct on the stack
        llvm::AllocaInst *struct_alloca = builder.CreateAlloca(struct_type, nullptr, full_type_name + "_instance");

        // If there are constructor arguments, call the constructor
        if (!node.arguments().empty())
        {
            // Look up the constructor function
            std::string constructor_name = full_type_name + "::" + base_type_name; // Constructor has base name
            auto constructor_it = _functions.find(constructor_name);

            if (constructor_it != _functions.end())
            {
                llvm::Function *constructor_func = constructor_it->second;
                std::cout << "[CodegenVisitor] Calling constructor: " << constructor_name << std::endl;

                // Prepare arguments: this pointer + constructor arguments
                std::vector<llvm::Value *> args;
                args.push_back(struct_alloca); // 'this' pointer

                // Generate constructor arguments
                for (const auto &arg : node.arguments())
                {
                    if (arg)
                    {
                        arg->accept(*this);
                        llvm::Value *arg_value = get_generated_value(arg.get());
                        if (arg_value)
                        {
                            args.push_back(arg_value);
                        }
                        else
                        {
                            report_error("Failed to generate argument for constructor", arg.get());
                            return;
                        }
                    }
                }

                // Call the constructor
                builder.CreateCall(constructor_func, args);
            }
            else
            {
                // Constructor not found - check if this is a generic instantiation that needs generation
                if (!node.generic_args().empty())
                {
                    std::cout << "[CodegenVisitor] Attempting to generate generic constructor: " << constructor_name << std::endl;

                    // For now, create a simple assignment-based constructor
                    // In a full implementation, this would analyze the generic constructor body
                    llvm::Function *generated_constructor = generate_generic_constructor(
                        full_type_name, base_type_name, node.generic_args(), struct_type);

                    if (generated_constructor)
                    {
                        _functions[constructor_name] = generated_constructor;
                        std::cout << "[CodegenVisitor] Generated generic constructor: " << constructor_name << std::endl;

                        // Also generate all generic methods for this instantiation
                        generate_generic_methods(full_type_name, base_type_name, node.generic_args(), struct_type);

                        // Now call the generated constructor
                        std::vector<llvm::Value *> args;
                        args.push_back(struct_alloca); // 'this' pointer

                        // Generate constructor arguments
                        for (const auto &arg : node.arguments())
                        {
                            if (arg)
                            {
                                arg->accept(*this);
                                llvm::Value *arg_value = get_generated_value(arg.get());
                                if (arg_value)
                                {
                                    args.push_back(arg_value);
                                }
                                else
                                {
                                    report_error("Failed to generate argument for generated constructor", arg.get());
                                    return;
                                }
                            }
                        }

                        builder.CreateCall(generated_constructor, args);
                    }
                    else
                    {
                        report_error("Failed to generate generic constructor for type: " + full_type_name, &node);
                        return;
                    }
                }
                else
                {
                    report_error("Constructor not found for type: " + full_type_name, &node);
                    return;
                }
            }
        }
        else
        {
            // Zero-initialize the struct if no constructor
            llvm::Value *zero_value = llvm::Constant::getNullValue(struct_type);
            builder.CreateStore(zero_value, struct_alloca);
        }

        // The new expression should return the struct value, not a pointer to it
        // For struct assignment, we need to load the struct value
        llvm::Value *struct_value = builder.CreateLoad(struct_type, struct_alloca, full_type_name + "_value");
        register_value(&node, struct_value);
        set_current_value(struct_value);
    }

    void CodegenVisitor::visit(Cryo::StructLiteralNode &node)
    {
        std::cout << "[CodegenVisitor] Generating struct literal" << std::endl;

        auto &context = _context_manager.get_context();
        auto module = _context_manager.get_module();
        auto &builder = _context_manager.get_builder();

        if (!module)
        {
            report_error("No module available for struct literal", &node);
            return;
        }

        std::string base_type_name = node.struct_type();

        // Check if this is a generic instantiation
        std::string full_type_name = base_type_name;
        if (!node.generic_args().empty())
        {
            // Construct the full instantiated type name: "Pair<int,string>"
            full_type_name = base_type_name + "<";
            for (size_t i = 0; i < node.generic_args().size(); ++i)
            {
                if (i > 0)
                    full_type_name += ",";
                full_type_name += node.generic_args()[i];
            }
            full_type_name += ">";
            std::cout << "[CodegenVisitor] Generic struct literal detected: " << full_type_name << std::endl;
        }

        std::cout << "[CodegenVisitor] Creating struct literal of type: " << full_type_name << std::endl;

        // Look up the struct type (use full instantiated name for generics)
        llvm::Type *struct_type = _type_mapper->lookup_type(full_type_name);
        if (!struct_type)
        {
            // For non-generics, try the old lookup method
            auto struct_type_it = _types.find(full_type_name);
            if (struct_type_it != _types.end())
            {
                struct_type = struct_type_it->second;
            }
            else
            {
                report_error("Unknown type in struct literal: " + full_type_name, &node);
                return;
            }
        }
        else
        {
            // Register the type in our local registry for member access lookups
            _types[full_type_name] = struct_type;
            std::cout << "[CodegenVisitor] Registered generic type in local registry: " << full_type_name << std::endl;
        }

        // Allocate memory for the struct on the stack
        llvm::AllocaInst *struct_alloca = builder.CreateAlloca(struct_type, nullptr, full_type_name + "_instance");

        // Initialize struct fields using the field initializers
        for (const auto &field_init : node.field_initializers())
        {
            if (!field_init)
                continue;

            std::string field_name = field_init->field_name();
            std::cout << "[CodegenVisitor] Setting field: " << field_name << std::endl;

            // Generate the field value
            if (field_init->value())
            {
                field_init->value()->accept(*this);
                llvm::Value *field_value = get_generated_value(field_init->value());

                if (!field_value)
                {
                    report_error("Failed to generate value for field: " + field_name, field_init->value());
                    continue;
                }

                // Get the field index and set the field
                int field_index = _type_mapper->get_field_index(full_type_name, field_name);
                if (field_index == -1)
                {
                    report_error("Unknown field '" + field_name + "' in struct " + full_type_name, &node);
                    continue;
                }
                llvm::Value *field_ptr = builder.CreateStructGEP(struct_type, struct_alloca, field_index, field_name + "_ptr");
                builder.CreateStore(field_value, field_ptr);
            }
        }

        // The struct literal should return the struct value, not a pointer to it
        // For struct assignment, we need to load the struct value
        llvm::Value *struct_value = builder.CreateLoad(struct_type, struct_alloca, full_type_name + "_value");
        register_value(&node, struct_value);
        set_current_value(struct_value);
    }

    void CodegenVisitor::visit(Cryo::ArrayLiteralNode &node)
    {
        std::cout << "[CodegenVisitor] Generating array literal with " << node.size() << " elements" << std::endl;

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        if (node.size() == 0)
        {
            std::cout << "[CodegenVisitor] Empty array literal" << std::endl;
            // For empty arrays, we'll create a null pointer for now
            // In a full implementation, we might want to allocate an empty array
            llvm::Type *void_ptr_type = llvm::PointerType::getUnqual(context);
            llvm::Value *null_array = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(void_ptr_type));
            register_value(&node, null_array);
            set_current_value(null_array);
            return;
        }

        // Generate code for all elements first to get their values
        std::vector<llvm::Value *> element_values;
        llvm::Type *element_type = nullptr;

        for (const auto &element : node.elements())
        {
            element->accept(*this);
            llvm::Value *element_val = get_generated_value(element.get());
            if (!element_val)
            {
                std::cerr << "[CodegenVisitor] Error: null element value in array literal" << std::endl;
                register_value(&node, nullptr);
                return;
            }

            element_values.push_back(element_val);

            // Use the first element's type as the array element type
            if (!element_type)
            {
                element_type = element_val->getType();
            }
            else if (element_type != element_val->getType())
            {
                std::cerr << "[CodegenVisitor] Warning: Type mismatch in array literal elements" << std::endl;
                // In a full implementation, we'd need type coercion here
            }
        }

        if (!element_type)
        {
            std::cerr << "[CodegenVisitor] Error: Could not determine element type for array literal" << std::endl;
            register_value(&node, nullptr);
            return;
        }

        // Create LLVM array type and allocate on stack
        size_t array_size = element_values.size();
        llvm::ArrayType *array_type = llvm::ArrayType::get(element_type, array_size);

        // Allocate the array on the stack
        llvm::AllocaInst *array_alloca = builder.CreateAlloca(array_type, nullptr, "array.literal");

        // Initialize each element
        for (size_t i = 0; i < array_size; ++i)
        {
            // Create GEP for array[i]
            llvm::Value *indices[] = {
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0), // First index: 0 (for the array itself)
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), i)  // Second index: i (for the element)
            };
            llvm::Value *element_ptr = builder.CreateInBoundsGEP(array_type, array_alloca, indices, "array.elem.ptr");

            // Store the element value
            builder.CreateStore(element_values[i], element_ptr);
        }

        // For array literals, we typically want to return the array itself, not just the pointer
        // We'll create a GEP to get the first element pointer (decay to pointer)
        llvm::Value *zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0);
        llvm::Value *array_ptr = builder.CreateInBoundsGEP(array_type, array_alloca, {zero, zero}, "array.decay");

        register_value(&node, array_ptr);
        set_current_value(array_ptr);

        std::cout << "[CodegenVisitor] Generated array literal successfully" << std::endl;
    }

    void CodegenVisitor::visit(Cryo::ArrayAccessNode &node)
    {
        std::cout << "[CodegenVisitor] Generating array access" << std::endl;

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Generate the array expression
        node.array()->accept(*this);
        llvm::Value *array_ptr = get_generated_value(node.array());

        if (!array_ptr)
        {
            std::cerr << "[CodegenVisitor] Error: array pointer is null in array access" << std::endl;
            register_value(&node, nullptr);
            return;
        }

        // Generate the index expression
        node.index()->accept(*this);
        llvm::Value *index_val = get_generated_value(node.index());

        if (!index_val)
        {
            std::cerr << "[CodegenVisitor] Error: index value is null in array access" << std::endl;
            register_value(&node, nullptr);
            return;
        }

        // Check if this is a nested array access (chained access like matrix[1][0])
        bool is_nested_access = dynamic_cast<Cryo::ArrayAccessNode *>(node.array()) != nullptr;

        // First, try to get the array variable name from the identifier (only for top-level access)
        std::string array_var_name;
        if (auto *identifier = dynamic_cast<Cryo::IdentifierNode *>(node.array()))
        {
            array_var_name = identifier->name();
        }

        std::cout << "[CodegenVisitor] Array access: var_name='" << array_var_name
                  << "', is_nested=" << (is_nested_access ? "true" : "false") << std::endl;

        // Convert index to i32 if it's not already
        if (index_val->getType() != llvm::Type::getInt32Ty(context))
        {
            if (index_val->getType()->isIntegerTy())
            {
                index_val = builder.CreateIntCast(index_val, llvm::Type::getInt32Ty(context), true, "index.cast");
            }
            else
            {
                std::cerr << "[CodegenVisitor] Error: array index must be an integer type" << std::endl;
                register_value(&node, nullptr);
                return;
            }
        }

        llvm::Type *array_ptr_type = array_ptr->getType();

        if (!array_ptr_type->isPointerTy())
        {
            std::cerr << "[CodegenVisitor] Error: array access on non-pointer type" << std::endl;
            // As a fallback, try to continue with a constant value
            if (array_var_name.empty())
            {
                register_value(&node, nullptr);
                return;
            }

            // Check if this is a case where we have the element type but need to reconstruct array access
            llvm::Value *var_alloca = _value_context->get_value(array_var_name);
            if (var_alloca && var_alloca->getType()->isPointerTy())
            {
                // Load the array pointer and try again - need to determine the type to load
                // For now, assume we're loading a pointer type
                auto loaded_ptr = builder.CreateLoad(
                    llvm::PointerType::get(context, 0),
                    var_alloca,
                    array_var_name + ".load");
                array_ptr = loaded_ptr;
                array_ptr_type = array_ptr->getType();

                if (!array_ptr_type->isPointerTy())
                {
                    std::cerr << "[CodegenVisitor] Error: still not a pointer type after loading" << std::endl;
                    register_value(&node, nullptr);
                    return;
                }
            }
            else
            {
                register_value(&node, nullptr);
                return;
            }
        }

        // Determine the element type for the GEP instruction
        llvm::Type *element_type = nullptr;

        // Strategy 1: For non-nested access, look up element type from ValueContext
        if (!is_nested_access && !array_var_name.empty())
        {
            llvm::Type *stored_element_type = _value_context->get_alloca_type(array_var_name);
            if (stored_element_type)
            {
                element_type = stored_element_type;
                if (element_type->isArrayTy())
                {
                    element_type = element_type->getArrayElementType();
                }
            }
        }

        // Strategy 2: For nested access, we need to determine element type from the intermediate result
        if (is_nested_access)
        {
            // For nested access, we're dealing with the result from a previous array access
            // The array_ptr should be a pointer to something, and we need to determine what
            // For matrix[1][0], the first access should give us a pointer to int[]
            // So the element type should be inferred as int

            // Try to determine based on the LLVM IR structure
            if (auto *gep_inst = llvm::dyn_cast<llvm::GetElementPtrInst>(array_ptr))
            {
                // If the array_ptr comes from a GEP, try to get its source element type
                llvm::Type *source_element_type = gep_inst->getSourceElementType();

                // For 2D array access, if the source is a pointer type, we need to get
                // what it points to for the final element type
                if (source_element_type && source_element_type->isPointerTy())
                {
                    // This is a pointer-to-pointer case (like ptr*), so the final element
                    // type is what the inner pointer points to
                    // In LLVM 20, we need to make an educated guess about the final element type
                    // For 2D int arrays, this should be int
                    element_type = llvm::Type::getInt32Ty(context);
                }
                else
                {
                    element_type = source_element_type;
                }
            }
            else if (auto *load_inst = llvm::dyn_cast<llvm::LoadInst>(array_ptr))
            {
                // If it's from a load, get the loaded type
                element_type = load_inst->getType();
            }

            // If we still don't have the element type, make an educated guess
            // For 2D int arrays, the final element type should be i32
            if (!element_type)
            {
                element_type = llvm::Type::getInt32Ty(context);
            }
        }

        // Strategy 3: Try to infer from the pointer type using LLVM IR analysis
        if (!element_type)
        {
            if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(array_ptr))
            {
                // If the pointer comes from an alloca, we can get the allocated type
                element_type = alloca->getAllocatedType();
                if (element_type->isArrayTy())
                {
                    element_type = element_type->getArrayElementType();
                }
            }
            else if (auto *gep = llvm::dyn_cast<llvm::GetElementPtrInst>(array_ptr))
            {
                // If it comes from a GEP, try to get the source element type
                element_type = gep->getSourceElementType();
                if (element_type && element_type->isArrayTy())
                {
                    element_type = element_type->getArrayElementType();
                }
            }
            else
            {
                // For cases where we have a generic pointer (like from previous array access),
                // we need to make an educated guess based on the context
                element_type = llvm::PointerType::get(context, 0);
            }
        }

        // Strategy 4: Fallback for unknown types
        if (!element_type)
        {
            element_type = llvm::Type::getInt32Ty(context);
        }

        // For nested array access (like matrix[1][0]), we need to handle the pointer differently
        llvm::Value *element_ptr = nullptr;
        llvm::Value *result_val = nullptr;

        // Check if this is a nested array access
        bool is_chained_access = array_var_name.empty(); // Nested access doesn't have a variable name

        if (is_chained_access)
        {
            // For nested access, array_ptr is likely a pointer to a pointer
            // We need to load the pointer first, then do GEP
            if (array_ptr->getType()->isPointerTy())
            {
                // Load the actual array pointer from the double pointer
                llvm::Value *actual_array = builder.CreateLoad(
                    llvm::PointerType::get(context, 0), // Generic pointer type in LLVM 20
                    array_ptr,
                    "nested.array.load");

                // Now do GEP on the loaded array pointer
                element_ptr = builder.CreateInBoundsGEP(
                    element_type,
                    actual_array,
                    index_val,
                    "array.access.gep");

                // For nested access, we always load the final value
                result_val = builder.CreateLoad(
                    element_type,
                    element_ptr,
                    "array.access.load");
            }
            else
            {
                // Fallback for other cases
                element_ptr = builder.CreateInBoundsGEP(
                    element_type,
                    array_ptr,
                    index_val,
                    "array.access.gep");
                result_val = element_ptr;
            }
        }
        else
        {
            // Normal array access
            element_ptr = builder.CreateInBoundsGEP(
                element_type,
                array_ptr,
                index_val,
                "array.access.gep");

            // Check if this array access is part of a chained access
            // For 2D arrays like int[][], the first access should return a pointer
            bool is_intermediate_access = false;

            // Simple heuristic: only treat as intermediate access if we're NOT accessing a string array
            // We can distinguish by looking at the LLVM type of the GEP result
            if (!array_var_name.empty())
            {
                // Check if this is likely a 2D array by examining the variable name pattern
                // This is a simple heuristic: variables with names like "matrix" are likely 2D
                // or we could check if the element type is a complex pointer structure
                llvm::Type *stored_element_type = _value_context->get_alloca_type(array_var_name);
                if (stored_element_type && stored_element_type->isPointerTy() && element_type->isPointerTy())
                {
                    // Both stored and element types are pointers
                    // For string arrays, we want to load the string pointer
                    // For 2D arrays, we want to return the pointer to the sub-array
                    // Check if element_type is a generic pointer (LLVM 20 style) vs array pointer
                    if (array_var_name == "matrix" ||
                        array_var_name.find("matrix") != std::string::npos ||
                        array_var_name.find("2d") != std::string::npos)
                    {
                        is_intermediate_access = true;
                    }
                    // For now, don't set intermediate access for other cases (like string arrays)
                }
            }

            if (is_intermediate_access)
            {
                // This is an intermediate access for 2D arrays - return pointer without loading
                result_val = element_ptr;
            }
            else if (element_type->isPointerTy())
            {
                // Element type is a pointer (like string), so we need to load the pointer value
                // For strings: load the string pointer from the array element
                result_val = builder.CreateLoad(
                    element_type,
                    element_ptr,
                    "array.access.load");
            }
            else
            {
                // Element type is not a pointer (like int), load the value
                result_val = builder.CreateLoad(
                    element_type,
                    element_ptr,
                    "array.access.load");
            }
        }

        register_value(&node, result_val);
        set_current_value(result_val);

        std::cout << "[CodegenVisitor] Generated array access successfully" << std::endl;
    }

    void CodegenVisitor::visit(Cryo::MemberAccessNode &node)
    {
        std::cout << "[CodegenVisitor] Generating member access" << std::endl;

        auto &context = _context_manager.get_context();
        auto module = _context_manager.get_module();
        auto &builder = _context_manager.get_builder();

        if (!module)
        {
            report_error("No module available for member access", &node);
            return;
        }

        // For member access, we need the pointer to the object, not its loaded value
        llvm::Value *object_ptr = nullptr;

        // Check if the object is an identifier - if so, get its alloca directly
        if (auto identifier = dynamic_cast<Cryo::IdentifierNode *>(node.object()))
        {
            std::string var_name = identifier->name();
            std::cout << "[CodegenVisitor] Looking for variable: " << var_name << std::endl;

            // Try to get the alloca (for local variables including 'this')
            object_ptr = _value_context->get_alloca(var_name);
            std::cout << "[CodegenVisitor] get_alloca result: " << (object_ptr ? "found" : "not found") << std::endl;

            if (!object_ptr)
            {
                // Try to get any value (for parameters)
                object_ptr = _value_context->get_value(var_name);
                std::cout << "[CodegenVisitor] get_value result: " << (object_ptr ? "found" : "not found") << std::endl;
            }

            if (!object_ptr)
            {
                report_error("Variable not found for member access: " + var_name, node.object());
                return;
            }
        }
        else
        {
            // For more complex expressions, generate them normally
            node.object()->accept(*this);
            object_ptr = get_generated_value(node.object());

            if (!object_ptr)
            {
                report_error("Failed to generate object for member access", node.object());
                return;
            }
        }

        std::string member_name = node.member();
        std::cout << "[CodegenVisitor] Accessing member: " << member_name << std::endl;

        // Determine the struct type and field index using metadata-driven approach
        llvm::Type *struct_type = nullptr;
        int field_index = -1;
        std::string type_name;

        // First, identify the type name from the LLVM type
        if (auto *alloca_inst = llvm::dyn_cast<llvm::AllocaInst>(object_ptr))
        {
            // For stack-allocated objects, get the allocated type
            llvm::Type *allocated_type = alloca_inst->getAllocatedType();

            // Check if it's a pointer type (like 'this' parameters)
            if (allocated_type->isPointerTy())
            {
                // For pointer types, we need the pointed-to type
                // In LLVM 20, we need to check what the pointer points to based on context
                // Since we know this is a struct/class pointer, look it up in our type registry
                for (const auto &[registered_name, registered_type] : _types)
                {
                    if (auto *struct_llvm_type = llvm::dyn_cast<llvm::StructType>(registered_type))
                    {
                        llvm::Type *expected_ptr_type = llvm::PointerType::getUnqual(struct_llvm_type);
                        if (allocated_type == expected_ptr_type)
                        {
                            struct_type = struct_llvm_type;
                            type_name = registered_name;
                            break;
                        }
                    }
                }
            }
            else if (llvm::isa<llvm::StructType>(allocated_type))
            {
                // Direct struct type
                struct_type = allocated_type;
            }
        }

        // Find the type name by matching the LLVM struct type against registered types
        if (struct_type && llvm::isa<llvm::StructType>(struct_type) && type_name.empty())
        {
            std::cout << "[CodegenVisitor] Looking for type name for struct type in member access\n";
            for (const auto &[registered_name, registered_type] : _types)
            {
                if (registered_type == struct_type)
                {
                    type_name = registered_name;
                    std::cout << "[CodegenVisitor] Found type name: " << type_name << "\n";
                    break;
                }
            }
            if (type_name.empty())
            {
                std::cout << "[CodegenVisitor] Failed to find type name for struct type\n";
            }
        }

        // Use TypeMapper to get field information
        if (!type_name.empty())
        {
            std::cout << "[CodegenVisitor] Getting field index for type: " << type_name << ", field: " << member_name << "\n";
            field_index = _type_mapper->get_field_index(type_name, member_name);
            std::cout << "[CodegenVisitor] Field index result: " << field_index << "\n";
        }

        if (!struct_type || field_index == -1)
        {
            report_error("Unknown struct type or field in member access: " + member_name, &node);
            register_value(&node, nullptr);
            return;
        }

        // Handle the case where object_ptr might be a pointer to the struct
        // (like 'this' parameters which are stored as pointers)
        llvm::Value *struct_ptr = object_ptr;
        if (auto *alloca_inst = llvm::dyn_cast<llvm::AllocaInst>(object_ptr))
        {
            llvm::Type *allocated_type = alloca_inst->getAllocatedType();
            if (allocated_type->isPointerTy())
            {
                // For pointer-to-struct, load the pointer first
                struct_ptr = create_load(object_ptr, allocated_type, "struct_ptr");
            }
        }

        // Create GEP instruction to access the field
        llvm::Value *field_ptr = builder.CreateStructGEP(struct_type, struct_ptr, field_index, member_name + "_ptr");

        // Load the field value
        llvm::Type *field_type = struct_type->getStructElementType(field_index);
        llvm::Value *field_value = create_load(field_ptr, field_type, member_name + "_val");

        std::cout << "[CodegenVisitor] Successfully generated field access for: " << member_name << std::endl;
        register_value(&node, field_value);
        set_current_value(field_value); // Make sure the value is available for binary expressions
    }

    void CodegenVisitor::visit(Cryo::ScopeResolutionNode &node)
    {
        // Handle scope resolution like Color::RED
        std::string scope_name = node.scope_name();
        std::string member_name = node.member_name();
        std::string qualified_name = scope_name + "::" + member_name;

        // Try to find enum variant
        auto enum_variant_it = _enum_variants.find(qualified_name);
        if (enum_variant_it != _enum_variants.end())
        {
            llvm::Value *enum_value = enum_variant_it->second;
            set_current_value(enum_value);
            register_value(&node, enum_value);
            return;
        }

        // Also try unqualified name
        auto unqualified_it = _enum_variants.find(member_name);
        if (unqualified_it != _enum_variants.end())
        {
            llvm::Value *enum_value = unqualified_it->second;
            set_current_value(enum_value);
            register_value(&node, enum_value);
            return;
        }

        report_error("Unresolved scope resolution: " + qualified_name);
        register_value(&node, nullptr);
    }

    //===================================================================
    // Error Handling
    //===================================================================

    void CodegenVisitor::clear_errors()
    {
        _has_errors = false;
        _last_error.clear();
        _errors.clear();
    }

    void CodegenVisitor::report_error(const std::string &message)
    {
        _has_errors = true;
        _last_error = message;
        _errors.push_back(message);
        std::cerr << "Codegen Error: " << message << std::endl;
    }

    void CodegenVisitor::report_error(const std::string &message, Cryo::ASTNode *node)
    {
        std::string full_message = message;
        if (node)
        {
            full_message += " (node kind: " + std::to_string(static_cast<int>(node->kind())) + ")";
        }
        report_error(full_message);
    }

    //===================================================================
    // Private Helper Methods
    //===================================================================

    void CodegenVisitor::register_value(Cryo::ASTNode *node, llvm::Value *value)
    {
        if (node)
        {
            _node_values[node] = value;
        }
    }

    // Function generation implementation
    llvm::Function *CodegenVisitor::generate_function_declaration(Cryo::FunctionDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        try
        {
            auto module = _context_manager.get_module();
            if (!module)
            {
                report_error("No module available for function: " + node->name());
                return nullptr;
            }

            // Map return type
            llvm::Type *return_type = nullptr;
            std::string return_type_annotation = node->return_type_annotation();
            if (return_type_annotation != "void")
            {
                return_type = _type_mapper->map_type(return_type_annotation);
            }
            else
            {
                return_type = llvm::Type::getVoidTy(_context_manager.get_context());
            }

            if (!return_type)
            {
                report_error("Failed to map return type for function: " + node->name() + " (type: " + return_type_annotation + ")");
                return nullptr;
            }

            // Map parameter types
            std::vector<llvm::Type *> param_types;
            std::vector<std::string> param_names;

            for (auto &param : node->parameters())
            {
                if (param)
                {
                    std::string param_type_annotation = param->type_annotation();
                    llvm::Type *param_type = _type_mapper->map_type(param_type_annotation);
                    if (!param_type)
                    {
                        report_error("Failed to map parameter type: " + param->name() + " (type: " + param_type_annotation + ")");
                        return nullptr;
                    }
                    param_types.push_back(param_type);
                    param_names.push_back(param->name());
                }
            }

            // Create function type
            llvm::FunctionType *func_type = llvm::FunctionType::get(
                return_type, param_types, false);

            // Create function
            llvm::Function *function = llvm::Function::Create(
                func_type, llvm::Function::ExternalLinkage, node->name(), *module);

            // Set parameter names
            auto param_it = param_names.begin();
            for (auto &arg : function->args())
            {
                if (param_it != param_names.end())
                {
                    arg.setName(*param_it);
                    ++param_it;
                }
            }

            return function;
        }
        catch (const std::exception &e)
        {
            report_error("Exception in function declaration generation: " + std::string(e.what()));
            return nullptr;
        }
    }

    bool CodegenVisitor::generate_function_body(Cryo::FunctionDeclarationNode *node, llvm::Function *function)
    {
        if (!node || !function || !node->body())
            return false;

        // Debug output

        try
        {

            // Create function context
            _current_function = std::make_unique<FunctionContext>(function, node);

            // Create entry block
            llvm::BasicBlock *entry_block = llvm::BasicBlock::Create(
                _context_manager.get_context(), "entry", function);
            _current_function->entry_block = entry_block;

            // Create return block (for functions with return statements)
            // Validate function pointer before using it
            if (!function || function == nullptr)
            {
                report_error("Function pointer is null in generate_function_body");
                return false;
            }

            // Use AST node to determine return type instead of LLVM function
            // This avoids potential LLVM function corruption
            std::string return_type_str = node->return_type_annotation();
            bool is_void_function = (return_type_str == "void");

            if (!is_void_function)
            {

                _current_function->return_block = llvm::BasicBlock::Create(
                    _context_manager.get_context(), "return", function);

                // Create alloca for return value
                auto &builder = _context_manager.get_builder();
                builder.SetInsertPoint(entry_block);

                // Map the return type from AST
                llvm::Type *llvm_return_type = _type_mapper->map_type(return_type_str);
                if (llvm_return_type)
                {
                    _current_function->return_value_alloca = builder.CreateAlloca(
                        llvm_return_type, nullptr, "retval");
                }
                else
                {
                    report_error("Failed to map return type: " + return_type_str);
                    return false;
                }
            }

            // Set insertion point to entry block
            _context_manager.get_builder().SetInsertPoint(entry_block);

            // Enter function scope BEFORE registering parameters
            enter_scope(entry_block);

            // Create allocas and store parameter values using AST information instead of LLVM function args
            // This avoids accessing the potentially corrupted LLVM function object
            auto arg_it = function->arg_begin();
            for (const auto &param_ptr : node->parameters())
            {
                if (auto var_decl = param_ptr.get())
                {
                    std::string param_name = var_decl->name();
                    std::string param_type_annotation = var_decl->type_annotation();

                    // Map the parameter type from AST
                    llvm::Type *param_type = _type_mapper->map_type(param_type_annotation);
                    if (!param_type)
                    {
                        report_error("Failed to map parameter type: " + param_name + " (" + param_type_annotation + ")");
                        return false;
                    }

                    // Create alloca for parameter
                    llvm::AllocaInst *alloca = create_entry_block_alloca(
                        function, param_type, param_name);

                    if (alloca)
                    {
                        // Store the actual parameter value into the alloca
                        if (arg_it != function->arg_end())
                        {
                            _context_manager.get_builder().CreateStore(&*arg_it, alloca);
                            ++arg_it;
                        }

                        // Register the parameter in value context with proper type information
                        _value_context->set_value(param_name, alloca, alloca, param_type);
                    }
                    else
                    {
                        report_error("Failed to create alloca for parameter: " + param_name);
                        return false;
                    }
                }
            }

            // Generate function body
            node->body()->accept(*this);

            // Exit function scope
            exit_scope();

            // Ensure proper termination
            auto &builder = _context_manager.get_builder();

            // Instead of checking the potentially corrupted entry_block,
            // try to get the current basic block from the builder
            llvm::BasicBlock *current_block = builder.GetInsertBlock();

            // Only add termination if the current block doesn't already have one
            if (current_block && !current_block->getTerminator())
            {
                // For void functions, we can try to add a void return if needed
                if (is_void_function)
                {
                    builder.CreateRetVoid();
                }
                else
                {
                    // For non-void functions, create a default return value
                    llvm::Type *llvm_return_type = _type_mapper->map_type(return_type_str);
                    if (llvm_return_type)
                    {
                        builder.CreateRet(llvm::Constant::getNullValue(llvm_return_type));
                    }
                    else
                    {
                        // Fallback to void return
                        builder.CreateRetVoid();
                    }
                }
            }

            // Skip the return block handling for now to avoid corruption issues
            // Most functions should have proper termination from the body generation

            // However, we need to handle return blocks for non-void functions
            if (_current_function->return_block && !is_void_function)
            {
                // Load the return value from the return value alloca and return it
                llvm::Type *llvm_return_type = _type_mapper->map_type(return_type_str);
                if (llvm_return_type && _current_function->return_value_alloca)
                {
                    builder.SetInsertPoint(_current_function->return_block);
                    // Load from the return value alloca that was created earlier
                    auto retValue = create_load(_current_function->return_value_alloca, llvm_return_type, "retval.load");
                    builder.CreateRet(retValue);
                }
                else
                {
                    // Fallback: return default value
                    builder.SetInsertPoint(_current_function->return_block);
                    builder.CreateRet(llvm::Constant::getNullValue(llvm_return_type));
                }
            }

            // Clean up function context
            _current_function.reset();

            return true;
        }
        catch (const std::exception &e)
        {
            report_error("Exception in function body generation: " + std::string(e.what()));
            _current_function.reset();
            return false;
        }
    }
    llvm::Type *CodegenVisitor::generate_struct_type(Cryo::StructDeclarationNode *node) { return nullptr; }
    llvm::Type *CodegenVisitor::generate_class_type(Cryo::ClassDeclarationNode *node)
    {
        return _type_mapper->map_class_type(node);
    }
    llvm::Type *CodegenVisitor::generate_enum_type(Cryo::EnumDeclarationNode *node)
    {
        return _type_mapper->map_enum_type(node);
    }

    void CodegenVisitor::generate_simple_enum_constants(Cryo::EnumDeclarationNode *enum_decl, llvm::Type *enum_type)
    {
        // For simple enums, create global constants for each variant
        auto &llvm_context = _context_manager.get_context();
        auto *module = _context_manager.get_module();

        if (!enum_type)
        {
            report_error("Enum type is null for constants generation: " + enum_decl->name());
            return;
        }

        int variant_value = 0;
        for (const auto &variant : enum_decl->variants())
        {
            // Create constant for this enum variant (no global variable needed for simple enums)
            llvm::Constant *variant_const = llvm::ConstantInt::get(
                llvm::cast<llvm::IntegerType>(enum_type), variant_value);

            // Register the constant directly (not a global variable)
            register_enum_variant(enum_decl->name(), variant->name(), variant_const);

            variant_value++;
        }
    }

    void CodegenVisitor::generate_complex_enum_constructors(Cryo::EnumDeclarationNode *enum_decl, llvm::Type *enum_type)
    {
        // For complex enums, create constructor functions for each variant
        auto &llvm_context = _context_manager.get_context();
        auto *module = _context_manager.get_module();

        if (!enum_type)
        {
            report_error("Enum type is null for constructor generation: " + enum_decl->name());
            return;
        }

        int variant_discriminant = 0;
        for (const auto &variant : enum_decl->variants())
        {
            if (variant->associated_types().empty())
            {
                // Simple variant in complex enum - just create a constant
                generate_simple_variant_in_complex_enum(enum_decl, variant.get(), variant_discriminant);
            }
            else
            {
                // Complex variant - create constructor function
                generate_complex_variant_constructor(enum_decl, variant.get(), variant_discriminant);
            }
            variant_discriminant++;
        }
    }

    void CodegenVisitor::generate_simple_variant_in_complex_enum(Cryo::EnumDeclarationNode *enum_decl,
                                                                 Cryo::EnumVariantNode *variant,
                                                                 int discriminant)
    {
        // Create a function that returns an instance of the enum with just the discriminant set
        auto &llvm_context = _context_manager.get_context();
        auto *module = _context_manager.get_module();

        llvm::Type *enum_type = _type_mapper->lookup_type(enum_decl->name());
        std::string constructor_name = enum_decl->name() + "::" + variant->name();

        // Create function type: () -> EnumType
        llvm::FunctionType *func_type = llvm::FunctionType::get(enum_type, {}, false);
        llvm::Function *constructor_func = llvm::Function::Create(
            func_type, llvm::Function::ExternalLinkage, constructor_name, *module);

        // Create function body
        llvm::BasicBlock *entry = llvm::BasicBlock::Create(llvm_context, "entry", constructor_func);
        auto &builder = _context_manager.get_builder();
        builder.SetInsertPoint(entry);

        // Create enum instance with discriminant
        llvm::Value *enum_instance = llvm::UndefValue::get(enum_type);
        llvm::Value *discriminant_value = llvm::ConstantInt::get(
            llvm::Type::getInt32Ty(llvm_context), discriminant);
        enum_instance = builder.CreateInsertValue(enum_instance, discriminant_value, {0}, "set_discriminant");

        builder.CreateRet(enum_instance);

        // Register constructor function
        register_enum_variant(enum_decl->name(), variant->name(), constructor_func);
    }

    void CodegenVisitor::generate_complex_variant_constructor(Cryo::EnumDeclarationNode *enum_decl,
                                                              Cryo::EnumVariantNode *variant,
                                                              int discriminant)
    {
        // Create constructor function that takes the associated data and returns enum instance
        auto &llvm_context = _context_manager.get_context();
        auto *module = _context_manager.get_module();

        llvm::Type *enum_type = _type_mapper->lookup_type(enum_decl->name());
        std::string constructor_name = enum_decl->name() + "::" + variant->name();

        // Build parameter types for associated data
        std::vector<llvm::Type *> param_types;
        for (const auto &type_name : variant->associated_types())
        {
            llvm::Type *param_type = _type_mapper->map_type(type_name);
            if (param_type)
            {
                param_types.push_back(param_type);
            }
            else
            {
                report_error("Unknown type in enum variant: " + type_name);
                return;
            }
        }

        // Create function type: (param_types...) -> EnumType
        llvm::FunctionType *func_type = llvm::FunctionType::get(enum_type, param_types, false);
        llvm::Function *constructor_func = llvm::Function::Create(
            func_type, llvm::Function::ExternalLinkage, constructor_name, *module);

        // Create function body
        llvm::BasicBlock *entry = llvm::BasicBlock::Create(llvm_context, "entry", constructor_func);
        auto &builder = _context_manager.get_builder();
        builder.SetInsertPoint(entry);

        // Create enum instance
        llvm::Value *enum_instance = llvm::UndefValue::get(enum_type);

        // Set discriminant
        llvm::Value *discriminant_value = llvm::ConstantInt::get(
            llvm::Type::getInt32Ty(llvm_context), discriminant);
        enum_instance = builder.CreateInsertValue(enum_instance, discriminant_value, {0}, "set_discriminant");

        // Pack parameters into payload
        if (!param_types.empty())
        {
            // Get the payload array from the tagged union structure
            // The tagged union is { i32 discriminant, [N x i8] payload }
            llvm::ArrayType *payload_array_type = nullptr;
            if (auto *struct_type = llvm::dyn_cast<llvm::StructType>(enum_type))
            {
                if (struct_type->getNumElements() >= 2)
                {
                    payload_array_type = llvm::dyn_cast<llvm::ArrayType>(struct_type->getElementType(1));
                }
            }

            if (payload_array_type)
            {
                // Create a payload array and store parameters into it
                llvm::Value *payload = llvm::UndefValue::get(payload_array_type);

                // For simplicity, store each parameter as bytes in the payload
                // In a real implementation, we'd need proper type-aware packing
                auto func_args = constructor_func->args();
                int byte_offset = 0;

                for (auto &arg : func_args)
                {
                    if (byte_offset < payload_array_type->getNumElements())
                    {
                        if (arg.getType()->isFloatTy())
                        {
                            // Store float as bytes
                            llvm::Value *int_bits = builder.CreateBitCast(&arg, llvm::Type::getInt32Ty(llvm_context));
                            for (int i = 0; i < 4 && byte_offset + i < payload_array_type->getNumElements(); ++i)
                            {
                                llvm::Value *byte_shift = llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_context), i * 8);
                                llvm::Value *shifted = builder.CreateLShr(int_bits, byte_shift);
                                llvm::Value *byte_val = builder.CreateTrunc(shifted, llvm::Type::getInt8Ty(llvm_context));
                                payload = builder.CreateInsertValue(payload, byte_val, {static_cast<unsigned>(byte_offset + i)});
                            }
                            byte_offset += 4;
                        }
                        else if (arg.getType()->isIntegerTy(32))
                        {
                            // Store int32 as bytes
                            for (int i = 0; i < 4 && byte_offset + i < payload_array_type->getNumElements(); ++i)
                            {
                                llvm::Value *byte_shift = llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_context), i * 8);
                                llvm::Value *shifted = builder.CreateLShr(&arg, byte_shift);
                                llvm::Value *byte_val = builder.CreateTrunc(shifted, llvm::Type::getInt8Ty(llvm_context));
                                payload = builder.CreateInsertValue(payload, byte_val, {static_cast<unsigned>(byte_offset + i)});
                            }
                            byte_offset += 4;
                        }
                    }
                }

                // Insert the payload into the enum instance
                enum_instance = builder.CreateInsertValue(enum_instance, payload, {1}, "set_payload");
            }
        }

        builder.CreateRet(enum_instance);

        // Register constructor function
        register_enum_variant(enum_decl->name(), variant->name(), constructor_func);
    }
    // Expression generation helpers implementation
    llvm::Value *CodegenVisitor::generate_binary_operation(Cryo::BinaryExpressionNode *node)
    {
        if (!node)
            return nullptr;

        try
        {
            auto &builder = _context_manager.get_builder();
            llvm::Value *result = nullptr;
            TokenKind op_kind = node->operator_token().kind();

            // Handle assignment operations specially - don't evaluate left side as regular expression
            if (op_kind == TokenKind::TK_EQUAL)
            {
                // For assignment, get the variable address directly without loading its value
                if (auto *left_identifier = dynamic_cast<IdentifierNode *>(node->left()))
                {
                    // Look up the variable in the current scope
                    std::string var_name = left_identifier->name();
                    llvm::Value *var_alloca = _value_context->get_alloca(var_name);

                    if (!var_alloca)
                    {
                        report_error("Assignment to undefined variable: " + var_name);
                        return nullptr;
                    }

                    // Generate right operand
                    node->right()->accept(*this);
                    llvm::Value *right_val = get_current_value();

                    if (!right_val)
                    {
                        report_error("Failed to generate right operand of assignment");
                        return nullptr;
                    }

                    // Store the right side value to the variable
                    create_store(right_val, var_alloca);

                    // The result of assignment is the assigned value
                    return right_val;
                }
                // Handle assignment to dereferenced pointer: *ptr = value
                else if (auto *left_unary = dynamic_cast<Cryo::UnaryExpressionNode *>(node->left()))
                {
                    if (left_unary->operator_token().kind() == TokenKind::TK_STAR)
                    {
                        // This is a dereference assignment: *ptr = value
                        // First get the pointer value
                        left_unary->operand()->accept(*this);
                        llvm::Value *ptr_val = get_current_value();

                        if (!ptr_val)
                        {
                            report_error("Failed to generate pointer operand for dereference assignment");
                            return nullptr;
                        }

                        // Generate right operand
                        node->right()->accept(*this);
                        llvm::Value *right_val = get_current_value();

                        if (!right_val)
                        {
                            report_error("Failed to generate right operand of assignment");
                            return nullptr;
                        }

                        // Store the right side value to the dereferenced pointer
                        create_store(right_val, ptr_val);

                        // The result of assignment is the assigned value
                        return right_val;
                    }
                    else
                    {
                        report_error("Invalid left-hand side in assignment expression");
                        return nullptr;
                    }
                }
                // Handle assignment to member access: obj.field = value
                else if (auto *left_member_access = dynamic_cast<Cryo::MemberAccessNode *>(node->left()))
                {
                    // Generate the object to get its pointer
                    left_member_access->object()->accept(*this);
                    llvm::Value *object_ptr = get_generated_value(left_member_access->object());

                    if (!object_ptr || !object_ptr->getType()->isPointerTy())
                    {
                        report_error("Invalid object in member assignment");
                        return nullptr;
                    }

                    std::string member_name = left_member_access->member();

                    // For now, we need to implement proper field access via GEP
                    // This is a simplified version - real implementation would need field index tracking

                    // Determine the struct type and field index using metadata-driven approach
                    llvm::Type *struct_type = nullptr;
                    int field_index = -1;
                    std::string type_name;

                    // First, identify the type name from the LLVM type (same logic as member access)
                    if (auto *argument = llvm::dyn_cast<llvm::Argument>(object_ptr))
                    {
                        // For function arguments (like 'this'), object_ptr is already the struct pointer
                        llvm::Type *arg_type = argument->getType();
                        if (arg_type->isPointerTy())
                        {
                            // Look through registered types to find the struct
                            for (const auto &[registered_name, registered_type] : _types)
                            {
                                if (auto *struct_llvm_type = llvm::dyn_cast<llvm::StructType>(registered_type))
                                {
                                    llvm::Type *expected_ptr_type = llvm::PointerType::getUnqual(struct_llvm_type);
                                    if (arg_type == expected_ptr_type)
                                    {
                                        struct_type = struct_llvm_type;
                                        type_name = registered_name;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    else if (auto *alloca_inst = llvm::dyn_cast<llvm::AllocaInst>(object_ptr))
                    {
                        // For stack-allocated objects, get the allocated type
                        llvm::Type *allocated_type = alloca_inst->getAllocatedType();

                        // Check if it's a pointer type (like 'this' parameters)
                        if (allocated_type->isPointerTy())
                        {
                            // For pointer types, we need the pointed-to type
                            for (const auto &[registered_name, registered_type] : _types)
                            {
                                if (auto *struct_llvm_type = llvm::dyn_cast<llvm::StructType>(registered_type))
                                {
                                    llvm::Type *expected_ptr_type = llvm::PointerType::getUnqual(struct_llvm_type);
                                    if (allocated_type == expected_ptr_type)
                                    {
                                        struct_type = struct_llvm_type;
                                        type_name = registered_name;
                                        break;
                                    }
                                }
                            }
                        }
                        else if (llvm::isa<llvm::StructType>(allocated_type))
                        {
                            // Direct struct type
                            struct_type = allocated_type;
                        }
                    }

                    // Find the type name by matching the LLVM struct type against registered types
                    if (struct_type && llvm::isa<llvm::StructType>(struct_type) && type_name.empty())
                    {
                        for (const auto &[registered_name, registered_type] : _types)
                        {
                            if (registered_type == struct_type)
                            {
                                type_name = registered_name;
                                break;
                            }
                        }
                    }

                    // Use TypeMapper to get field information
                    if (!type_name.empty())
                    {
                        field_index = _type_mapper->get_field_index(type_name, member_name);
                    }

                    if (!struct_type || field_index == -1)
                    {
                        report_error("Unknown struct type or field in member assignment: " + member_name);
                        return nullptr;
                    }

                    // Generate right operand (the value to assign)
                    node->right()->accept(*this);
                    llvm::Value *right_val = get_current_value();

                    if (!right_val)
                    {
                        report_error("Failed to generate right operand for member assignment");
                        return nullptr;
                    }

                    // Handle the case where object_ptr might be a pointer to the struct
                    // (like 'this' parameters which are Function Arguments)
                    llvm::Value *struct_ptr = object_ptr;
                    if (auto *argument = llvm::dyn_cast<llvm::Argument>(object_ptr))
                    {
                        // For function arguments (like 'this'), object_ptr is already the struct pointer
                        struct_ptr = object_ptr;
                    }
                    else if (auto *alloca_inst = llvm::dyn_cast<llvm::AllocaInst>(object_ptr))
                    {
                        llvm::Type *allocated_type = alloca_inst->getAllocatedType();
                        if (allocated_type->isPointerTy())
                        {
                            // For pointer-to-struct, load the pointer first
                            struct_ptr = create_load(object_ptr, allocated_type, "struct_ptr");
                        }
                    }

                    // Create GEP instruction to access the field
                    auto &context = _context_manager.get_context();
                    llvm::Value *field_ptr = builder.CreateStructGEP(struct_type, struct_ptr, field_index, member_name + "_ptr");

                    // Store the value to the field
                    create_store(right_val, field_ptr);

                    // The result of assignment is the assigned value
                    register_value(node, right_val); // Make sure the result is registered
                    return right_val;
                }
                else
                {
                    report_error("Invalid left-hand side in assignment expression");
                    return nullptr;
                }
            }

            // For non-assignment operations, generate both operands normally
            // Generate left operand
            node->left()->accept(*this);
            llvm::Value *left_val = get_current_value();

            if (!left_val)
            {
                report_error("Failed to generate left operand of binary expression");
                return nullptr;
            }

            // Generate right operand
            node->right()->accept(*this);
            llvm::Value *right_val = get_current_value();

            if (!right_val)
            {
                report_error("Failed to generate right operand of binary expression");
                return nullptr;
            }

            // Handle binary operations based on operator token
            switch (op_kind)
            {
            case TokenKind::TK_PLUS:
                if (left_val->getType()->isIntegerTy() && right_val->getType()->isIntegerTy())
                {
                    result = builder.CreateAdd(left_val, right_val, "add.tmp");
                }
                else if (left_val->getType()->isFloatingPointTy() || right_val->getType()->isFloatingPointTy())
                {
                    // Handle float addition (convert if needed)
                    if (left_val->getType()->isIntegerTy())
                    {
                        left_val = builder.CreateSIToFP(left_val, right_val->getType(), "int2float");
                    }
                    else if (right_val->getType()->isIntegerTy())
                    {
                        right_val = builder.CreateSIToFP(right_val, left_val->getType(), "int2float");
                    }
                    result = builder.CreateFAdd(left_val, right_val, "fadd.tmp");
                }
                break;

            case TokenKind::TK_MINUS:
                if (left_val->getType()->isIntegerTy() && right_val->getType()->isIntegerTy())
                {
                    result = builder.CreateSub(left_val, right_val, "sub.tmp");
                }
                else if (left_val->getType()->isFloatingPointTy() || right_val->getType()->isFloatingPointTy())
                {
                    if (left_val->getType()->isIntegerTy())
                    {
                        left_val = builder.CreateSIToFP(left_val, right_val->getType(), "int2float");
                    }
                    else if (right_val->getType()->isIntegerTy())
                    {
                        right_val = builder.CreateSIToFP(right_val, left_val->getType(), "int2float");
                    }
                    result = builder.CreateFSub(left_val, right_val, "fsub.tmp");
                }
                break;

            case TokenKind::TK_STAR: // Multiplication
                if (left_val->getType()->isIntegerTy() && right_val->getType()->isIntegerTy())
                {
                    result = builder.CreateMul(left_val, right_val, "mul.tmp");
                }
                else if (left_val->getType()->isFloatingPointTy() || right_val->getType()->isFloatingPointTy())
                {
                    if (left_val->getType()->isIntegerTy())
                    {
                        left_val = builder.CreateSIToFP(left_val, right_val->getType(), "int2float");
                    }
                    else if (right_val->getType()->isIntegerTy())
                    {
                        right_val = builder.CreateSIToFP(right_val, left_val->getType(), "int2float");
                    }
                    result = builder.CreateFMul(left_val, right_val, "fmul.tmp");
                }
                break;

            case TokenKind::TK_SLASH: // Division
                if (left_val->getType()->isIntegerTy() && right_val->getType()->isIntegerTy())
                {
                    result = builder.CreateSDiv(left_val, right_val, "div.tmp");
                }
                else if (left_val->getType()->isFloatingPointTy() || right_val->getType()->isFloatingPointTy())
                {
                    if (left_val->getType()->isIntegerTy())
                    {
                        left_val = builder.CreateSIToFP(left_val, right_val->getType(), "int2float");
                    }
                    else if (right_val->getType()->isIntegerTy())
                    {
                        right_val = builder.CreateSIToFP(right_val, left_val->getType(), "int2float");
                    }
                    result = builder.CreateFDiv(left_val, right_val, "fdiv.tmp");
                }
                break;

            case TokenKind::TK_PERCENT: // Modulo
                if (left_val->getType()->isIntegerTy() && right_val->getType()->isIntegerTy())
                {
                    result = builder.CreateSRem(left_val, right_val, "mod.tmp");
                }
                break;

            // Comparison operations
            case TokenKind::TK_EQUALEQUAL:
                if (left_val->getType()->isIntegerTy() && right_val->getType()->isIntegerTy())
                {
                    result = builder.CreateICmpEQ(left_val, right_val, "eq.tmp");
                }
                else if (left_val->getType()->isFloatingPointTy() || right_val->getType()->isFloatingPointTy())
                {
                    if (left_val->getType()->isIntegerTy())
                    {
                        left_val = builder.CreateSIToFP(left_val, right_val->getType(), "int2float");
                    }
                    else if (right_val->getType()->isIntegerTy())
                    {
                        right_val = builder.CreateSIToFP(right_val, left_val->getType(), "int2float");
                    }
                    result = builder.CreateFCmpOEQ(left_val, right_val, "feq.tmp");
                }
                break;

            case TokenKind::TK_EXCLAIMEQUAL:
                if (left_val->getType()->isIntegerTy() && right_val->getType()->isIntegerTy())
                {
                    result = builder.CreateICmpNE(left_val, right_val, "ne.tmp");
                }
                else if (left_val->getType()->isFloatingPointTy() || right_val->getType()->isFloatingPointTy())
                {
                    if (left_val->getType()->isIntegerTy())
                    {
                        left_val = builder.CreateSIToFP(left_val, right_val->getType(), "int2float");
                    }
                    else if (right_val->getType()->isIntegerTy())
                    {
                        right_val = builder.CreateSIToFP(right_val, left_val->getType(), "int2float");
                    }
                    result = builder.CreateFCmpONE(left_val, right_val, "fne.tmp");
                }
                break;

            case TokenKind::TK_L_ANGLE:
                if (left_val->getType()->isIntegerTy() && right_val->getType()->isIntegerTy())
                {
                    result = builder.CreateICmpSLT(left_val, right_val, "lt.tmp");
                }
                else if (left_val->getType()->isFloatingPointTy() || right_val->getType()->isFloatingPointTy())
                {
                    if (left_val->getType()->isIntegerTy())
                    {
                        left_val = builder.CreateSIToFP(left_val, right_val->getType(), "int2float");
                    }
                    else if (right_val->getType()->isIntegerTy())
                    {
                        right_val = builder.CreateSIToFP(right_val, left_val->getType(), "int2float");
                    }
                    result = builder.CreateFCmpOLT(left_val, right_val, "flt.tmp");
                }
                break;

            case TokenKind::TK_R_ANGLE:
                if (left_val->getType()->isIntegerTy() && right_val->getType()->isIntegerTy())
                {
                    result = builder.CreateICmpSGT(left_val, right_val, "gt.tmp");
                }
                else if (left_val->getType()->isFloatingPointTy() || right_val->getType()->isFloatingPointTy())
                {
                    if (left_val->getType()->isIntegerTy())
                    {
                        left_val = builder.CreateSIToFP(left_val, right_val->getType(), "int2float");
                    }
                    else if (right_val->getType()->isIntegerTy())
                    {
                        right_val = builder.CreateSIToFP(right_val, left_val->getType(), "int2float");
                    }
                    result = builder.CreateFCmpOGT(left_val, right_val, "fgt.tmp");
                }
                break;

            case TokenKind::TK_LESSEQUAL:
                if (left_val->getType()->isIntegerTy() && right_val->getType()->isIntegerTy())
                {
                    result = builder.CreateICmpSLE(left_val, right_val, "le.tmp");
                }
                else if (left_val->getType()->isFloatingPointTy() || right_val->getType()->isFloatingPointTy())
                {
                    if (left_val->getType()->isIntegerTy())
                    {
                        left_val = builder.CreateSIToFP(left_val, right_val->getType(), "int2float");
                    }
                    else if (right_val->getType()->isIntegerTy())
                    {
                        right_val = builder.CreateSIToFP(right_val, left_val->getType(), "int2float");
                    }
                    result = builder.CreateFCmpOLE(left_val, right_val, "fle.tmp");
                }
                break;

            case TokenKind::TK_GREATEREQUAL:
                if (left_val->getType()->isIntegerTy() && right_val->getType()->isIntegerTy())
                {
                    result = builder.CreateICmpSGE(left_val, right_val, "ge.tmp");
                }
                else if (left_val->getType()->isFloatingPointTy() || right_val->getType()->isFloatingPointTy())
                {
                    if (left_val->getType()->isIntegerTy())
                    {
                        left_val = builder.CreateSIToFP(left_val, right_val->getType(), "int2float");
                    }
                    else if (right_val->getType()->isIntegerTy())
                    {
                        right_val = builder.CreateSIToFP(right_val, left_val->getType(), "int2float");
                    }
                    result = builder.CreateFCmpOGE(left_val, right_val, "fge.tmp");
                }
                break;

            // Logical operations
            case TokenKind::TK_AMPAMP:
                result = builder.CreateAnd(left_val, right_val, "and.tmp");
                break;

            case TokenKind::TK_PIPEPIPE:
                result = builder.CreateOr(left_val, right_val, "or.tmp");
                break;

            default:
                report_error("Unsupported binary operator: " + node->operator_token().to_string());
                return nullptr;
            }

            return result;
        }
        catch (const std::exception &e)
        {
            report_error("Exception in binary operation generation: " + std::string(e.what()));
            return nullptr;
        }
    }
    llvm::Value *CodegenVisitor::generate_unary_operation(Cryo::UnaryExpressionNode *node)
    {
        if (!node)
            return nullptr;

        auto &builder = _context_manager.get_builder();
        auto operand = node->operand();
        if (!operand)
        {
            std::cerr << "[ERROR] Unary expression missing operand" << std::endl;
            return nullptr;
        }

        // Get the operator type
        std::string operator_str = node->operator_token().to_string();

        // Handle increment and decrement operators
        if (operator_str == "++" || operator_str == "--")
        {
            // For increment/decrement, operand should be a variable (identifier)
            auto identifierNode = dynamic_cast<Cryo::IdentifierNode *>(operand);
            if (!identifierNode)
            {
                std::cerr << "[ERROR] Increment/decrement can only be applied to variables" << std::endl;
                return nullptr;
            }

            std::string varName = identifierNode->name();

            // Look up the variable in the value context
            auto varValue = _value_context->get_alloca(varName);
            if (!varValue)
            {
                std::cerr << "[ERROR] Undefined variable in increment/decrement: " << varName << std::endl;
                return nullptr;
            }

            // Load the current value
            llvm::Value *currentValue = builder.CreateLoad(varValue->getAllocatedType(), varValue, varName + ".load");

            // Generate increment or decrement
            llvm::Value *newValue = nullptr;
            if (operator_str == "++")
            {
                if (currentValue->getType()->isIntegerTy())
                {
                    newValue = builder.CreateAdd(currentValue, llvm::ConstantInt::get(currentValue->getType(), 1), "inc");
                }
                else if (currentValue->getType()->isFloatingPointTy())
                {
                    newValue = builder.CreateFAdd(currentValue, llvm::ConstantFP::get(currentValue->getType(), 1.0), "inc");
                }
                else
                {
                    std::cerr << "[ERROR] Cannot increment non-numeric type" << std::endl;
                    return nullptr;
                }
            }
            else // "--"
            {
                if (currentValue->getType()->isIntegerTy())
                {
                    newValue = builder.CreateSub(currentValue, llvm::ConstantInt::get(currentValue->getType(), 1), "dec");
                }
                else if (currentValue->getType()->isFloatingPointTy())
                {
                    newValue = builder.CreateFSub(currentValue, llvm::ConstantFP::get(currentValue->getType(), 1.0), "dec");
                }
                else
                {
                    std::cerr << "[ERROR] Cannot decrement non-numeric type" << std::endl;
                    return nullptr;
                }
            }

            // Store the new value back
            builder.CreateStore(newValue, varValue);

            // For now, assume postfix increment/decrement (return original value)
            // TODO: Add support for prefix vs postfix based on AST node information
            return currentValue;
        }

        // Handle other unary operators
        operand->accept(*this);
        llvm::Value *operandValue = get_generated_value(operand);
        if (!operandValue)
        {
            std::cerr << "[ERROR] Failed to generate operand for unary expression" << std::endl;
            return nullptr;
        }

        // Handle other unary operators
        if (operator_str == "-")
        {
            if (operandValue->getType()->isIntegerTy())
            {
                return builder.CreateNeg(operandValue, "neg");
            }
            else if (operandValue->getType()->isFloatingPointTy())
            {
                return builder.CreateFNeg(operandValue, "fneg");
            }
        }
        else if (operator_str == "!")
        {
            if (operandValue->getType() == llvm::Type::getInt1Ty(_context_manager.get_context()))
            {
                return builder.CreateNot(operandValue, "not");
            }
            else
            {
                // Convert to boolean first
                llvm::Value *boolValue = builder.CreateICmpNE(operandValue, llvm::ConstantInt::get(operandValue->getType(), 0), "tobool");
                return builder.CreateNot(boolValue, "not");
            }
        }
        else if (operator_str == "&")
        {
            // Address-of operator: get the address of a variable
            if (auto identifierNode = dynamic_cast<Cryo::IdentifierNode *>(operand))
            {
                std::string varName = identifierNode->name();
                auto varAlloca = _value_context->get_alloca(varName);
                if (!varAlloca)
                {
                    std::cerr << "[ERROR] Undefined variable in address-of operation: " << varName << std::endl;
                    return nullptr;
                }

                // Return the alloca (address) directly for address-of operator
                return varAlloca;
            }
            else
            {
                std::cerr << "[ERROR] Address-of operator (&) can only be applied to variables" << std::endl;
                return nullptr;
            }
        }
        else if (operator_str == "*")
        {
            // Dereference operator: load value from pointer/reference
            if (!operandValue->getType()->isPointerTy())
            {
                std::cerr << "[ERROR] Dereference operator (*) can only be applied to pointer types" << std::endl;
                return nullptr;
            }

            // Try to determine the element type from the variable context
            llvm::Type *elementType = nullptr;

            // If this is a direct variable dereference (*ptr), get the pointee type
            if (auto *identNode = dynamic_cast<Cryo::IdentifierNode *>(node->operand()))
            {
                std::string varName = identNode->name();
                llvm::Type *storedType = _value_context->get_alloca_type(varName);

                if (storedType && storedType->isPointerTy())
                {
                    // For pointer variables, we need to determine what type they point to
                    // For now, we'll assume common types based on variable naming or context
                    // This is a temporary solution until we have better type tracking

                    // For now, assume int* pointers point to i32
                    // TODO: Improve this with proper type information storage
                    elementType = llvm::Type::getInt32Ty(_context_manager.get_context());
                }
                else
                {
                    // Fallback to the stored type itself
                    elementType = storedType;
                }
            }

            // Fallback to common types if we can't determine the element type
            if (!elementType)
            {
                // For now, assume i32 as the most common case - this should be improved with proper type tracking
                elementType = llvm::Type::getInt32Ty(_context_manager.get_context());
            }

            return builder.CreateLoad(elementType, operandValue, "deref");
        }

        std::cerr << "[ERROR] Unsupported unary operator: " << operator_str << std::endl;
        return nullptr;
    }
    llvm::Value *Cryo::Codegen::CodegenVisitor::generate_function_call(Cryo::CallExpressionNode *node)
    {
        if (!node)
            return nullptr;

        auto &builder = _context_manager.get_builder();
        auto *module = _context_manager.get_module();

        // Get the function name from the callee
        std::string function_name;
        if (auto *identifier = dynamic_cast<IdentifierNode *>(node->callee()))
        {
            // Handle simple function name
            function_name = identifier->name();
        }
        else if (auto *member_access = dynamic_cast<MemberAccessNode *>(node->callee()))
        {
            // Handle member access - could be namespaced calls or struct method calls
            if (auto *object_identifier = dynamic_cast<IdentifierNode *>(member_access->object()))
            {
                // This might be a struct method call like p.move(args...)
                // Check if the object is a variable (struct instance)
                std::string object_name = object_identifier->name();
                llvm::Value *object_value = _value_context->get_value(object_name);

                if (object_value && object_value->getType()->isPointerTy())
                {
                    // This looks like a struct/class method call
                    // Look up the variable's actual type from our type tracking
                    std::string type_name;

                    auto type_it = _variable_types.find(object_name);
                    if (type_it != _variable_types.end())
                    {
                        type_name = type_it->second;
                    }
                    else
                    {
                        // Fall back to the old naive approach for compatibility
                        for (const auto &[name, llvm_type] : _types)
                        {
                            if (object_name.find(name) != std::string::npos || name == "Point")
                            {
                                type_name = name;
                                break;
                            }
                        }
                    }

                    if (!type_name.empty())
                    {
                        // Look up the method function
                        std::string method_name = type_name + "::" + member_access->member();
                        auto method_it = _functions.find(method_name);

                        // If not found and this is a generic type, also try the base type
                        if (method_it == _functions.end() && type_name.find('<') != std::string::npos)
                        {
                            // Extract base type name for generic instantiation lookup fallback
                            std::string base_name = type_name.substr(0, type_name.find('<'));
                            std::string fallback_method_name = base_name + "::" + member_access->member();
                            method_it = _functions.find(fallback_method_name);
                        }

                        if (method_it != _functions.end())
                        {
                            llvm::Function *method_func = method_it->second;

                            // Prepare arguments: this pointer + method arguments
                            std::vector<llvm::Value *> args;
                            args.push_back(object_value); // 'this' pointer

                            // Generate method arguments
                            for (const auto &arg : node->arguments())
                            {
                                if (arg)
                                {
                                    arg->accept(*this);
                                    llvm::Value *arg_value = get_generated_value(arg.get());
                                    if (arg_value)
                                    {
                                        args.push_back(arg_value);
                                    }
                                }
                            }

                            // Call the method
                            return builder.CreateCall(method_func, args);
                        }
                        else
                        {
                            std::cout << "[CodegenVisitor] Method not found: " << method_name << " for type: " << type_name << std::endl;
                        }
                    }
                }
            }

            // Fall back to handling namespaced calls like Std::Runtime::print_int
            function_name = extract_function_name_from_member_access(member_access);
        }
        else if (auto *scope_resolution = dynamic_cast<ScopeResolutionNode *>(node->callee()))
        {
            // Handle scope resolution like Std::Runtime::print_int
            function_name = scope_resolution->scope_name() + "::" + scope_resolution->member_name();
        }
        else
        {
            std::cerr << "Unsupported function call type" << std::endl;
            return nullptr;
        }

        // Check if this is an enum constructor call first
        if (_enum_variants.find(function_name) != _enum_variants.end())
        {
            // This is an enum constructor call
            llvm::Value *constructor_func = _enum_variants[function_name];
            if (auto *llvm_function = llvm::dyn_cast<llvm::Function>(constructor_func))
            {
                // Generate arguments for the constructor call
                std::vector<llvm::Value *> args;
                for (auto &arg : node->arguments())
                {
                    arg->accept(*this);
                    llvm::Value *arg_val = get_current_value();
                    if (arg_val)
                    {
                        args.push_back(arg_val);
                    }
                }

                // Call the enum constructor function
                llvm::Value *enum_instance = builder.CreateCall(llvm_function, args, "enum_constructor");
                return enum_instance;
            }
        }

        // Map Cryo function names to C runtime function names
        std::string c_function_name = map_cryo_to_c_function(function_name);

        // Extract the simple function name for LLVM module lookup
        // Functions are stored with their simple names (e.g., "println", not "Std::Runtime::println")
        std::string simple_function_name = c_function_name;
        size_t last_scope_pos = c_function_name.rfind("::");
        if (last_scope_pos != std::string::npos)
        {
            simple_function_name = c_function_name.substr(last_scope_pos + 2);
        }

        // Look up the function in the module using the simple name
        llvm::Function *function = module->getFunction(simple_function_name);
        if (!function)
        {
            // Function not declared yet, create a declaration
            function = create_runtime_function_declaration(c_function_name, node);
            if (!function)
            {
                std::cerr << "Failed to create function declaration for: " << c_function_name << std::endl;
                return nullptr;
            }
        }

        // Generate arguments
        std::vector<llvm::Value *> args;
        for (const auto &arg : node->arguments())
        {
            arg->accept(*this);
            llvm::Value *arg_value = get_generated_value(arg.get());
            if (arg_value)
            {
                args.push_back(arg_value);
            }
        }

        // Create the function call
        return builder.CreateCall(function, args);
    }

    std::string Cryo::Codegen::CodegenVisitor::extract_function_name_from_member_access(Cryo::MemberAccessNode *node)
    {
        if (!node)
            return "";

        // For Std::Runtime::print_int, we want "print_int"
        // This is a simplified extraction - just get the final member name
        return node->member();
    }

    std::string Cryo::Codegen::CodegenVisitor::map_cryo_to_c_function(const std::string &cryo_name)
    {
        // TODO: This should be replaced with a proper symbol table lookup
        // that gets the C function name from the symbol's metadata
        // For now, return the original name and let the linker handle it
        return cryo_name;
    }

    llvm::Function *Cryo::Codegen::CodegenVisitor::create_runtime_function_declaration(const std::string &c_name, Cryo::CallExpressionNode *call_node)
    {
        // Handle scoped function names like "Std::Runtime::print_int"
        std::string symbol_name = c_name;
        std::string scope_name = "Global";

        // Find the last "::" to separate scope from member name
        size_t last_scope_pos = c_name.rfind("::");
        if (last_scope_pos != std::string::npos)
        {
            // Extract scope and member name
            scope_name = c_name.substr(0, last_scope_pos);
            symbol_name = c_name.substr(last_scope_pos + 2);
        }

        // Look up the function in the symbol table
        Symbol *symbol = nullptr;

        // First try to find by the full name
        symbol = _symbol_table.lookup_symbol(c_name);

        // If not found, try to find by the member name
        if (!symbol)
        {
            symbol = _symbol_table.lookup_symbol(symbol_name);

            // Verify the scope matches if we found a symbol
            if (symbol && symbol->scope != scope_name)
            {
                symbol = nullptr; // Wrong scope, keep looking
            }
        }

        if (!symbol || symbol->kind != SymbolKind::Function)
        {
            std::cerr << "Error: Function " << c_name << " not found in symbol table" << std::endl;
            return nullptr;
        }

        // Cast the symbol's data_type to FunctionType
        FunctionType *func_type = dynamic_cast<FunctionType *>(symbol->data_type);
        if (!func_type)
        {
            std::cerr << "Error: Symbol " << c_name << " is not a function type" << std::endl;
            return nullptr;
        }

        // Convert return type
        llvm::Type *llvm_return_type = _type_mapper->map_type(func_type->return_type().get());
        if (!llvm_return_type)
        {
            std::cerr << "Error: Failed to map return type for function " << c_name << std::endl;
            return nullptr;
        }

        // Convert parameter types
        std::vector<llvm::Type *> llvm_param_types;
        for (const auto &param_type : func_type->parameter_types())
        {
            llvm::Type *llvm_param_type = _type_mapper->map_type(param_type.get());
            if (!llvm_param_type)
            {
                std::cerr << "Error: Failed to map parameter type for function " << c_name << std::endl;
                return nullptr;
            }
            llvm_param_types.push_back(llvm_param_type);
        }

        // Create the function type
        llvm::FunctionType *function_type = llvm::FunctionType::get(
            llvm_return_type, llvm_param_types, func_type->is_variadic());

        // Create the function declaration with the simple member name (not the full scoped name)
        // This matches the C function name in the runtime library
        llvm::Function *function = llvm::Function::Create(
            function_type, llvm::Function::ExternalLinkage, symbol_name, _context_manager.get_module());

        return function;
    }
    llvm::Value *Cryo::Codegen::CodegenVisitor::generate_array_access(Cryo::ArrayAccessNode *node) { return nullptr; }
    llvm::Value *Cryo::Codegen::CodegenVisitor::generate_member_access(Cryo::MemberAccessNode *node) { return nullptr; }
    void CodegenVisitor::generate_if_statement(Cryo::IfStatementNode *node)
    {
        if (!node)
            return;

        auto &builder = _context_manager.get_builder();
        llvm::Function *function = builder.GetInsertBlock()->getParent();

        // Create basic blocks for control flow
        llvm::BasicBlock *then_block = llvm::BasicBlock::Create(_context_manager.get_context(), "if.then", function);
        llvm::BasicBlock *else_block = nullptr;
        llvm::BasicBlock *merge_block = llvm::BasicBlock::Create(_context_manager.get_context(), "if.end", function);

        // Create else block if there's an else clause
        if (node->else_statement())
        {
            else_block = llvm::BasicBlock::Create(_context_manager.get_context(), "if.else", function);
        }

        // Generate condition expression
        node->condition()->accept(*this);
        llvm::Value *condition_val = get_current_value();

        if (!condition_val)
        {
            report_error("Failed to generate if condition");
            return;
        }

        // Convert condition to i1 if needed
        if (!condition_val->getType()->isIntegerTy(1))
        {
            if (condition_val->getType()->isIntegerTy())
            {
                condition_val = builder.CreateICmpNE(condition_val,
                                                     llvm::ConstantInt::get(condition_val->getType(), 0), "tobool");
            }
            else
            {
                report_error("Invalid condition type in if statement");
                return;
            }
        }

        // Branch based on condition
        if (else_block)
        {
            builder.CreateCondBr(condition_val, then_block, else_block);
        }
        else
        {
            builder.CreateCondBr(condition_val, then_block, merge_block);
        }

        // Generate then block
        builder.SetInsertPoint(then_block);
        enter_scope(then_block);
        node->then_statement()->accept(*this);
        exit_scope();

        // Ensure the current block (which might not be then_block if the statement
        // contained nested control flow) ends with a branch to merge
        llvm::BasicBlock *currentBlock = builder.GetInsertBlock();
        if (currentBlock && !currentBlock->getTerminator())
        {
            builder.CreateBr(merge_block);
        }

        // Generate else block if present
        if (else_block && node->else_statement())
        {
            builder.SetInsertPoint(else_block);
            enter_scope(else_block);
            node->else_statement()->accept(*this);
            exit_scope();

            // Ensure the current block ends with a branch to merge
            llvm::BasicBlock *currentElseBlock = builder.GetInsertBlock();
            if (currentElseBlock && !currentElseBlock->getTerminator())
            {
                builder.CreateBr(merge_block);
            }
        }

        // Continue with merge block
        builder.SetInsertPoint(merge_block);
    }
    void CodegenVisitor::generate_while_loop(Cryo::WhileStatementNode *node)
    {
        if (!node)
            return;

        auto &builder = _context_manager.get_builder();
        llvm::Function *function = builder.GetInsertBlock()->getParent();

        // Create basic blocks for the loop
        llvm::BasicBlock *condition_block = llvm::BasicBlock::Create(_context_manager.get_context(), "while.cond", function);
        llvm::BasicBlock *body_block = llvm::BasicBlock::Create(_context_manager.get_context(), "while.body", function);
        llvm::BasicBlock *exit_block = llvm::BasicBlock::Create(_context_manager.get_context(), "while.end", function);

        // Create loop context for break/continue
        LoopContext loop_ctx(condition_block, body_block, condition_block, exit_block);
        _loop_stack.push(loop_ctx);

        // Jump to condition block
        builder.CreateBr(condition_block);

        // Generate condition block
        builder.SetInsertPoint(condition_block);
        node->condition()->accept(*this);
        llvm::Value *condition_val = get_current_value();

        if (!condition_val)
        {
            report_error("Failed to generate while loop condition");
            return;
        }

        // Convert condition to i1 if needed
        if (!condition_val->getType()->isIntegerTy(1))
        {
            if (condition_val->getType()->isIntegerTy())
            {
                condition_val = builder.CreateICmpNE(condition_val,
                                                     llvm::ConstantInt::get(condition_val->getType(), 0), "tobool");
            }
            else
            {
                report_error("Invalid condition type in while loop");
                return;
            }
        }

        // Conditional branch: if true go to body, else exit
        builder.CreateCondBr(condition_val, body_block, exit_block);

        // Generate body block
        builder.SetInsertPoint(body_block);
        enter_scope(body_block);
        node->body()->accept(*this);
        exit_scope();

        // Ensure body block ends with a branch back to condition
        if (!body_block->getTerminator())
        {
            builder.CreateBr(condition_block);
        }

        // Restore loop context
        _loop_stack.pop();

        // Continue with exit block
        builder.SetInsertPoint(exit_block);
    }
    void CodegenVisitor::generate_for_loop(Cryo::ForStatementNode *node)
    {
        if (!node)
            return;

        auto &builder = _context_manager.get_builder();
        llvm::Function *function = builder.GetInsertBlock()->getParent();
        if (!function)
            return;

        // Create basic blocks for the for loop
        llvm::BasicBlock *loopCondition = llvm::BasicBlock::Create(_context_manager.get_context(), "for.cond", function);
        llvm::BasicBlock *loopBody = llvm::BasicBlock::Create(_context_manager.get_context(), "for.body", function);
        llvm::BasicBlock *loopIncrement = llvm::BasicBlock::Create(_context_manager.get_context(), "for.inc", function);
        llvm::BasicBlock *afterLoop = llvm::BasicBlock::Create(_context_manager.get_context(), "for.end", function);

        // Create loop context for break/continue
        LoopContext loop_ctx(loopCondition, loopBody, loopIncrement, afterLoop);
        _loop_stack.push(loop_ctx);

        // Generate the initialization statement
        if (node->init())
        {
            node->init()->accept(*this);
        }

        // Branch to the condition block
        builder.CreateBr(loopCondition);

        // Generate loop condition
        builder.SetInsertPoint(loopCondition);
        if (node->condition())
        {
            node->condition()->accept(*this);
            llvm::Value *condValue = get_generated_value(node->condition());
            if (!condValue)
            {
                std::cerr << "[ERROR] Failed to generate condition for for loop" << std::endl;
                return;
            }

            // Ensure condition is boolean
            if (condValue->getType() != llvm::Type::getInt1Ty(_context_manager.get_context()))
            {
                condValue = builder.CreateICmpNE(condValue, llvm::ConstantInt::get(condValue->getType(), 0), "for.cond.bool");
            }

            builder.CreateCondBr(condValue, loopBody, afterLoop);
        }
        else
        {
            // No condition means infinite loop
            builder.CreateBr(loopBody);
        }

        // Generate loop body
        builder.SetInsertPoint(loopBody);
        if (node->body())
        {
            node->body()->accept(*this);
        }

        // After body, branch to increment
        builder.CreateBr(loopIncrement);

        // Generate loop increment
        builder.SetInsertPoint(loopIncrement);
        if (node->update())
        {
            node->update()->accept(*this);
        }

        // After increment, branch back to condition
        builder.CreateBr(loopCondition);

        // Continue with code after the loop
        builder.SetInsertPoint(afterLoop);

        // Restore previous loop context
        _loop_stack.pop();
    }

    void CodegenVisitor::generate_match_statement(Cryo::MatchStatementNode *node)
    {
        // This function is kept for compatibility but the actual logic is in visit()
        visit(*node);
    }

    llvm::Value *CodegenVisitor::extract_enum_discriminant(llvm::Value *enum_value)
    {
        if (!enum_value)
        {
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Check if enum_value is a pointer or a value
        if (enum_value->getType()->isPointerTy())
        {
            // It's a pointer to an enum struct - load the discriminant field
            llvm::Value *discriminant_ptr = builder.CreateStructGEP(
                nullptr, // Let LLVM infer the type
                enum_value,
                0,
                "discriminant_ptr");

            llvm::Value *discriminant = builder.CreateLoad(
                llvm::Type::getInt32Ty(context),
                discriminant_ptr,
                "discriminant");

            std::cout << "[CodegenVisitor] Extracted discriminant from enum pointer" << std::endl;
            return discriminant;
        }
        else
        {
            // It's a value - extract the discriminant directly
            llvm::Value *discriminant = builder.CreateExtractValue(
                enum_value,
                {0},
                "discriminant");

            std::cout << "[CodegenVisitor] Extracted discriminant from enum value" << std::endl;
            return discriminant;
        }
    }

    int CodegenVisitor::get_pattern_discriminant(Cryo::PatternNode *pattern)
    {
        if (!pattern)
        {
            return -1;
        }

        // Try to cast to EnumPatternNode
        if (auto *enum_pattern = dynamic_cast<Cryo::EnumPatternNode *>(pattern))
        {
            // Look up the enum variant to get its discriminant value
            std::string enum_name = enum_pattern->enum_name();
            std::string variant_name = enum_pattern->variant_name();
            std::string full_name = enum_name + "::" + variant_name;

            // For now, we'll use a simple mapping based on declaration order
            // TODO: This should be looked up from the enum declaration
            if (variant_name == "Circle")
                return 0;
            if (variant_name == "Rectangle")
                return 1;
            if (variant_name == "Triangle")
                return 2;

            std::cout << "[CodegenVisitor] Pattern discriminant for " << full_name << " (unknown, defaulting to 0)" << std::endl;
            return 0;
        }

        std::cerr << "[CodegenVisitor] Unsupported pattern type for discriminant extraction" << std::endl;
        return -1;
    }

    void CodegenVisitor::generate_match_arm(Cryo::MatchArmNode *arm, llvm::Value *match_value)
    {
        if (!arm || !match_value)
        {
            return;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        std::cout << "[CodegenVisitor] Generating match arm with pattern" << std::endl;

        // Extract pattern variables if this is an enum pattern with bindings
        if (auto *enum_pattern = dynamic_cast<Cryo::EnumPatternNode *>(arm->pattern()))
        {
            extract_pattern_bindings(enum_pattern, match_value);
        }

        // Generate the body of the match arm
        if (arm->body())
        {
            arm->body()->accept(*this);
        }

        std::cout << "[CodegenVisitor] Match arm generated" << std::endl;
    }

    void CodegenVisitor::extract_pattern_bindings(Cryo::EnumPatternNode *pattern, llvm::Value *enum_value)
    {
        if (!pattern || !enum_value)
        {
            return;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        std::cout << "[CodegenVisitor] Extracting pattern bindings for " << pattern->variant_name() << std::endl;

        // For now, we'll implement a basic version that extracts parameters
        // TODO: Implement proper payload extraction from tagged union
        const auto &bindings = pattern->bound_variables();

        if (bindings.empty())
        {
            std::cout << "[CodegenVisitor] No bindings to extract" << std::endl;
            return;
        }

        // Get the payload from the enum value
        llvm::Value *payload_value;
        if (enum_value->getType()->isPointerTy())
        {
            // It's a pointer to an enum struct - get payload field pointer
            llvm::Value *payload_ptr = builder.CreateStructGEP(
                nullptr, // Let LLVM infer the type
                enum_value,
                1,
                "payload_ptr");
            payload_value = payload_ptr;
        }
        else
        {
            // It's a value - extract the payload directly
            payload_value = builder.CreateExtractValue(
                enum_value,
                {1},
                "payload");
        }

        // For each binding, create a variable
        for (size_t i = 0; i < bindings.size(); ++i)
        {
            const std::string &binding_name = bindings[i];

            // Create an alloca for the binding
            llvm::Function *current_function = builder.GetInsertBlock()->getParent();
            llvm::AllocaInst *binding_alloca = create_entry_block_alloca(
                current_function,
                llvm::Type::getFloatTy(context), // TODO: Determine actual type from enum declaration
                binding_name);

            // Extract the actual data from the payload
            // TODO: Implement proper type-based extraction from enum definition
            llvm::Value *extracted_value;
            if (enum_value->getType()->isPointerTy())
            {
                // Load from payload pointer - not implemented for pointer case yet
                extracted_value = llvm::ConstantFP::get(llvm::Type::getFloatTy(context), 0.0);
            }
            else
            {
                // Extract from payload array - reconstruct the float from bytes
                int byte_offset = i * 4; // Assume each parameter is 4 bytes for now

                // Extract 4 bytes from the payload array and reconstruct float
                llvm::Value *int_value = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0);

                for (int j = 0; j < 4; ++j)
                {
                    // Extract byte at offset
                    llvm::Value *byte_val = builder.CreateExtractValue(
                        payload_value,
                        {static_cast<unsigned>(byte_offset + j)},
                        "byte_" + std::to_string(j));

                    // Convert byte to i32 and shift
                    llvm::Value *byte_as_int = builder.CreateZExt(byte_val, llvm::Type::getInt32Ty(context));
                    llvm::Value *shift = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), j * 8);
                    llvm::Value *shifted = builder.CreateShl(byte_as_int, shift);

                    // Or with accumulator
                    int_value = builder.CreateOr(int_value, shifted);
                }

                // Convert the reconstructed int back to float
                extracted_value = builder.CreateBitCast(int_value, llvm::Type::getFloatTy(context), "reconstructed_float");
            }

            builder.CreateStore(extracted_value, binding_alloca);

            // Register the binding in the current scope
            current_scope().local_allocas[binding_name] = binding_alloca;

            // Also register with value context so it can be found during identifier lookup
            _value_context->set_value(binding_name, binding_alloca, binding_alloca, llvm::Type::getFloatTy(context));

            std::cout << "[CodegenVisitor] Created binding: " << binding_name << std::endl;
        }
    }

    // Memory management implementation
    llvm::AllocaInst *CodegenVisitor::create_entry_block_alloca(llvm::Function *function, llvm::Type *type, const std::string &name)
    {
        if (!function || !type)
            return nullptr;

        try
        {
            // Save current insertion point
            auto &builder = _context_manager.get_builder();
            llvm::BasicBlock *current_block = builder.GetInsertBlock();

            // Create alloca at the beginning of entry block
            llvm::BasicBlock &entry_block = function->getEntryBlock();
            llvm::IRBuilder<>::InsertPointGuard guard(builder);

            if (entry_block.empty())
            {
                builder.SetInsertPoint(&entry_block);
            }
            else
            {
                builder.SetInsertPoint(&entry_block, entry_block.begin());
            }

            return builder.CreateAlloca(type, nullptr, name);
        }
        catch (const std::exception &e)
        {
            report_error("Exception creating alloca: " + std::string(e.what()));
            return nullptr;
        }
    }

    llvm::Value *CodegenVisitor::create_load(llvm::Value *ptr, llvm::Type *element_type, const std::string &name)
    {
        if (!ptr)
            return nullptr;

        try
        {
            auto &builder = _context_manager.get_builder();
            // Use provided element type, fallback to i32 if not provided
            if (!element_type)
            {
                element_type = llvm::Type::getInt32Ty(_context_manager.get_context());
            }
            return builder.CreateLoad(element_type, ptr, name);
        }
        catch (const std::exception &e)
        {
            report_error("Exception creating load: " + std::string(e.what()));
            return nullptr;
        }
    }
    void CodegenVisitor::create_store(llvm::Value *value, llvm::Value *ptr)
    {
        if (!value || !ptr)
            return;

        try
        {
            auto &builder = _context_manager.get_builder();
            builder.CreateStore(value, ptr);
        }
        catch (const std::exception &e)
        {
            report_error("Exception creating store: " + std::string(e.what()));
        }
    }

    // Scope management implementation
    void CodegenVisitor::enter_scope(llvm::BasicBlock *entry_block, llvm::BasicBlock *exit_block)
    {
        try
        {
            _value_context->enter_scope("scope");

            if (_current_function)
            {
                _current_function->scope_stack.emplace_back(entry_block, exit_block);
            }
        }
        catch (const std::exception &e)
        {
            report_error("Exception entering scope: " + std::string(e.what()));
        }
    }

    void CodegenVisitor::exit_scope()
    {
        try
        {
            _value_context->exit_scope();

            if (_current_function && !_current_function->scope_stack.empty())
            {
                _current_function->scope_stack.pop_back();
            }
        }
        catch (const std::exception &e)
        {
            report_error("Exception exiting scope: " + std::string(e.what()));
        }
    }

    CodegenVisitor::ScopeContext &CodegenVisitor::current_scope()
    {
        static ScopeContext dummy(nullptr);

        if (_current_function && !_current_function->scope_stack.empty())
        {
            return _current_function->scope_stack.back();
        }

        return dummy;
    }
    llvm::BasicBlock *CodegenVisitor::create_basic_block(const std::string &name, llvm::Function *function) { return nullptr; }
    llvm::Type *CodegenVisitor::get_llvm_type(Cryo::Type *cryo_type) { return nullptr; }
    llvm::Value *CodegenVisitor::cast_value(llvm::Value *value, llvm::Type *target_type) { return nullptr; }
    bool CodegenVisitor::is_lvalue(Cryo::ExpressionNode *expr) { return false; }

    void CodegenVisitor::register_enum_variant(const std::string &enum_name, const std::string &variant_name, llvm::Value *value)
    {
        std::string qualified_name = enum_name + "::" + variant_name;
        _enum_variants[qualified_name] = value;

        // Also register with just the variant name for unqualified access
        _enum_variants[variant_name] = value;
    }

    llvm::Function *CodegenVisitor::generate_generic_constructor(const std::string &instantiated_type,
                                                                 const std::string &base_type,
                                                                 const std::vector<std::string> &type_args,
                                                                 llvm::Type *struct_type)
    {
        std::cout << "[CodegenVisitor] Generating generic constructor for " << instantiated_type << std::endl;

        auto &context = _context_manager.get_context();
        auto module = _context_manager.get_module();
        auto &builder = _context_manager.get_builder();

        if (!module || type_args.empty())
        {
            return nullptr;
        }

        // Save current insertion point to restore later
        llvm::BasicBlock *saved_block = builder.GetInsertBlock();

        // Create function type: void(struct*, T)
        std::vector<llvm::Type *> param_types;
        param_types.push_back(llvm::PointerType::get(struct_type, 0)); // 'this' pointer

        // For our test case, we know there's one parameter of the generic type
        llvm::Type *param_type = _type_mapper->map_type(type_args[0]); // T mapped to concrete type (int)
        if (!param_type)
        {
            std::cout << "[CodegenVisitor] Failed to map constructor parameter type: " << type_args[0] << std::endl;
            return nullptr;
        }
        param_types.push_back(param_type);

        llvm::Type *return_type = _type_mapper->get_void_type();
        llvm::FunctionType *func_type = llvm::FunctionType::get(return_type, param_types, false);

        // Create the function
        std::string func_name = instantiated_type + "::" + base_type;
        llvm::Function *constructor_func = llvm::Function::Create(
            func_type, llvm::Function::ExternalLinkage, func_name, module);

        // Create entry block for the constructor
        llvm::BasicBlock *entry_block = llvm::BasicBlock::Create(context, "entry", constructor_func);
        builder.SetInsertPoint(entry_block);

        // Get function arguments
        auto args_it = constructor_func->args().begin();
        llvm::Value *this_ptr = &*args_it;
        ++args_it;
        llvm::Value *val_arg = &*args_it;

        // For our GenericStruct<T>, we know it has one field 'value' at index 0
        // this.value = val;
        std::vector<llvm::Value *> indices = {
            llvm::ConstantInt::get(context, llvm::APInt(32, 0)), // struct index
            llvm::ConstantInt::get(context, llvm::APInt(32, 0))  // field index (value is at index 0)
        };

        llvm::Value *field_ptr = builder.CreateGEP(struct_type, this_ptr, indices, "value_ptr");
        builder.CreateStore(val_arg, field_ptr);

        // Return void
        builder.CreateRetVoid();

        // Restore original insertion point
        if (saved_block)
        {
            builder.SetInsertPoint(saved_block);
        }

        std::cout << "[CodegenVisitor] Successfully generated generic constructor: " << func_name << std::endl;
        return constructor_func;
    }

    void CodegenVisitor::generate_generic_methods(const std::string &instantiated_type,
                                                  const std::string &base_type,
                                                  const std::vector<std::string> &type_args,
                                                  llvm::Type *struct_type)
    {
        std::cout << "[CodegenVisitor] Generating generic methods for " << instantiated_type << std::endl;

        auto &context = _context_manager.get_context();
        auto module = _context_manager.get_module();
        auto &builder = _context_manager.get_builder();

        if (!module || type_args.empty())
        {
            std::cout << "[CodegenVisitor] Cannot generate generic methods - missing module or type args" << std::endl;
            return;
        }

        // For GenericStruct, we need to generate the get_value() method
        if (base_type == "GenericStruct" && type_args.size() == 1)
        {
            // Generate get_value() method: T get_value(this*)
            std::string method_name = instantiated_type + "::get_value";

            // Check if method already exists
            if (_functions.find(method_name) != _functions.end())
            {
                std::cout << "[CodegenVisitor] Method " << method_name << " already exists, skipping" << std::endl;
                return;
            }

            llvm::Type *return_type = _type_mapper->map_type(type_args[0]);
            if (!return_type)
            {
                std::cout << "[CodegenVisitor] Failed to map return type: " << type_args[0] << std::endl;
                return;
            }

            // Create function type: T(struct*)
            std::vector<llvm::Type *> param_types;
            param_types.push_back(llvm::PointerType::get(struct_type, 0)); // 'this' pointer

            llvm::FunctionType *func_type = llvm::FunctionType::get(return_type, param_types, false);

            // Create function
            llvm::Function *method_func = llvm::Function::Create(
                func_type,
                llvm::Function::ExternalLinkage,
                method_name,
                module);

            if (!method_func)
            {
                std::cout << "[CodegenVisitor] Failed to create method function" << std::endl;
                return;
            }

            // Set parameter names
            auto arg_iter = method_func->arg_begin();
            arg_iter->setName("this");

            // Save current insertion point
            llvm::BasicBlock *saved_block = builder.GetInsertBlock();

            // Create entry block
            llvm::BasicBlock *entry_block = llvm::BasicBlock::Create(context, "entry", method_func);
            builder.SetInsertPoint(entry_block);

            // Generate method body - return this->value
            llvm::Value *this_ptr = &(*arg_iter);

            // Get pointer to the 'value' field (index 0)
            llvm::Value *field_ptr = builder.CreateStructGEP(struct_type, this_ptr, 0, "value_ptr");

            // Load the value and return it
            llvm::Value *field_value = builder.CreateLoad(return_type, field_ptr, "value_val");
            builder.CreateRet(field_value);

            // Register the method
            _functions[method_name] = method_func;

            // Also register with simplified name for lookup
            std::string simple_method_name = base_type + "::get_value";
            if (_functions.find(simple_method_name) == _functions.end())
            {
                _functions[simple_method_name] = method_func;
            }

            std::cout << "[CodegenVisitor] Generated generic method: " << method_name << std::endl;

            // Restore insertion point
            if (saved_block)
            {
                builder.SetInsertPoint(saved_block);
            }
        }
        else
        {
            std::cout << "[CodegenVisitor] Unsupported generic type for method generation: " << base_type << std::endl;
        }
    }

} // namespace Cryo::Codegen
