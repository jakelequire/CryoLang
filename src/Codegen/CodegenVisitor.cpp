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

                // Store in value context
                _value_context->set_value(var_name, alloca, alloca);
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
        // TODO: Implement struct generation
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::ClassDeclarationNode &node)
    {
        // TODO: Implement class generation
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::EnumDeclarationNode &node)
    {
        // TODO: Implement enum generation
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
        // TODO: Implement match arm generation
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
        // TODO: Implement implementation block generation
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::BlockStatementNode &node)
    {
        for (auto &statement : node.statements())
        {
            if (statement)
            {
                statement->accept(*this);
            }
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
        // TODO: Implement if statement generation
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::WhileStatementNode &node)
    {
        // TODO: Implement while loop generation
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::ForStatementNode &node)
    {
        // TODO: Implement for loop generation
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::MatchStatementNode &node)
    {
        // TODO: Implement match statement generation
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
        // TODO: Implement break statement generation
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::ContinueStatementNode &node)
    {
        // TODO: Implement continue statement generation
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
            // Simple integer for now
            int64_t int_val = std::stoll(node.value());
            literal_value = llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx), int_val);
            break;
        }
        case TokenKind::TK_BOOLEAN_LITERAL:
        {
            bool bool_val = (node.value() == "true");
            literal_value = llvm::ConstantInt::get(llvm::Type::getInt1Ty(llvm_ctx), bool_val);
            break;
        }
        case TokenKind::TK_STRING_LITERAL:
        {
            std::string str_val = node.value();
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

            // Try to find variable in current scope
            llvm::Value *var_alloca = _value_context->get_value(identifier);
            if (var_alloca)
            {
                // Load the variable value if it's an alloca
                if (llvm::isa<llvm::AllocaInst>(var_alloca))
                {
                    llvm::Value *loaded_value = create_load(var_alloca, identifier + ".load");
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
                llvm::Value *loaded_value = create_load(global_it->second, identifier + ".global.load");
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
        // TODO: Implement unary expressions
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::TernaryExpressionNode &node)
    {
        // TODO: Implement ternary expressions
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::CallExpressionNode &node)
    {
        llvm::Value *call_result = generate_function_call(&node);
        register_value(&node, call_result);
    }

    void CodegenVisitor::visit(Cryo::NewExpressionNode &node)
    {
        // TODO: Implement new expressions
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::ArrayLiteralNode &node)
    {
        // TODO: Implement array literals
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::ArrayAccessNode &node)
    {
        // TODO: Implement array access
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::MemberAccessNode &node)
    {
        // TODO: Implement member access
        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::ScopeResolutionNode &node)
    {
        // TODO: Implement scope resolution
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

        try
        {
            // Create function context
            _current_function = std::make_unique<FunctionContext>(function, node);

            // Create entry block
            llvm::BasicBlock *entry_block = llvm::BasicBlock::Create(
                _context_manager.get_context(), "entry", function);
            _current_function->entry_block = entry_block;

            // Create return block (for functions with return statements)
            if (!function->getReturnType()->isVoidTy())
            {
                _current_function->return_block = llvm::BasicBlock::Create(
                    _context_manager.get_context(), "return", function);

                // Create alloca for return value
                auto &builder = _context_manager.get_builder();
                builder.SetInsertPoint(entry_block);
                _current_function->return_value_alloca = builder.CreateAlloca(
                    function->getReturnType(), nullptr, "retval");
            }

            // Set insertion point to entry block
            _context_manager.get_builder().SetInsertPoint(entry_block);

            // Enter function scope BEFORE registering parameters
            enter_scope(entry_block);

            // Create allocas and store parameter values
            auto param_it = node->parameters().begin();
            for (auto &arg : function->args())
            {
                if (param_it != node->parameters().end())
                {
                    if (auto var_decl = param_it->get())
                    {
                        // Use the parameter name from the AST node, not the LLVM argument
                        std::string param_name = var_decl->name();

                        // Set the LLVM argument name to match
                        arg.setName(param_name);

                        // Create alloca for parameter
                        llvm::AllocaInst *alloca = create_entry_block_alloca(
                            function, arg.getType(), param_name);

                        if (alloca)
                        {
                            // Store parameter value
                            create_store(&arg, alloca);

                            // Register in value context using the correct parameter name
                            _value_context->set_value(param_name, alloca, alloca);
                        }
                    }
                    ++param_it;
                }
            }

            // Generate function body
            node->body()->accept(*this);

            // Exit function scope
            exit_scope();

            // Ensure proper termination
            auto &builder = _context_manager.get_builder();

            if (!entry_block->getTerminator())
            {
                if (function->getReturnType()->isVoidTy())
                {
                    builder.SetInsertPoint(entry_block);
                    builder.CreateRetVoid();
                }
                else if (_current_function->return_block && _current_function->return_value_alloca)
                {
                    // Jump to return block
                    builder.SetInsertPoint(entry_block);
                    builder.CreateBr(_current_function->return_block);
                }
                else
                {
                    // Return zero/null value
                    builder.SetInsertPoint(entry_block);
                    builder.CreateRet(llvm::Constant::getNullValue(function->getReturnType()));
                }
            }

            // Also check return block if it exists
            if (_current_function->return_block && !_current_function->return_block->getTerminator())
            {
                // Generate return block terminator
                builder.SetInsertPoint(_current_function->return_block);
                llvm::Value *ret_val = create_load(_current_function->return_value_alloca, "retval");
                builder.CreateRet(ret_val);
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
    llvm::Type *CodegenVisitor::generate_class_type(Cryo::ClassDeclarationNode *node) { return nullptr; }
    llvm::Type *CodegenVisitor::generate_enum_type(Cryo::EnumDeclarationNode *node) { return nullptr; }
    // Expression generation helpers implementation
    llvm::Value *CodegenVisitor::generate_binary_operation(Cryo::BinaryExpressionNode *node)
    {
        if (!node)
            return nullptr;

        try
        {
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

            auto &builder = _context_manager.get_builder();
            llvm::Value *result = nullptr;

            // Get the token kind from the operator token
            TokenKind op_kind = node->operator_token().kind();

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
    llvm::Value *CodegenVisitor::generate_unary_operation(Cryo::UnaryExpressionNode *node) { return nullptr; }
    llvm::Value *CodegenVisitor::generate_function_call(Cryo::CallExpressionNode *node)
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
            // Handle namespaced calls like Std::Runtime::print_int
            // For now, extract the final function name and map to C runtime
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

        // Map Cryo function names to C runtime function names
        std::string c_function_name = map_cryo_to_c_function(function_name);

        // Look up the function in the module
        llvm::Function *function = module->getFunction(c_function_name);
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

    std::string CodegenVisitor::extract_function_name_from_member_access(MemberAccessNode *node)
    {
        if (!node)
            return "";

        // For Std::Runtime::print_int, we want "print_int"
        // This is a simplified extraction - just get the final member name
        return node->member();
    }

    std::string CodegenVisitor::map_cryo_to_c_function(const std::string &cryo_name)
    {
        // Map Cryo runtime function names to C function names
        static std::unordered_map<std::string, std::string> name_map = {
            {"print_int", "cryo_print_int"},
            {"Std::Runtime::print_int", "cryo_print_int"},
            {"print_float", "cryo_print_float"},
            {"Std::Runtime::print_float", "cryo_print_float"},
            {"print_bool", "cryo_print_bool"},
            {"Std::Runtime::print_bool", "cryo_print_bool"},
            {"print_char", "cryo_print_char"},
            {"Std::Runtime::print_char", "cryo_print_char"},
            {"print", "cryo_print"},
            {"Std::Runtime::print", "cryo_print"},
            {"println", "cryo_println"},
            {"Std::Runtime::println", "cryo_println"}};

        auto it = name_map.find(cryo_name);
        if (it != name_map.end())
        {
            return it->second;
        }

        // For non-runtime functions, use the original name
        return cryo_name;
    }

    llvm::Function *CodegenVisitor::create_runtime_function_declaration(const std::string &c_name, CallExpressionNode *call_node)
    {
        auto *module = _context_manager.get_module();
        auto &context = _context_manager.get_context();

        // Create function signatures based on the C runtime functions
        if (c_name == "cryo_print_int")
        {
            // void cryo_print_int(int value)
            llvm::Type *int_type = llvm::Type::getInt32Ty(context);
            llvm::Type *void_type = llvm::Type::getVoidTy(context);
            llvm::FunctionType *func_type = llvm::FunctionType::get(void_type, {int_type}, false);
            return llvm::Function::Create(func_type, llvm::Function::ExternalLinkage, c_name, module);
        }
        else if (c_name == "cryo_print_float")
        {
            // void cryo_print_float(double value)
            llvm::Type *double_type = llvm::Type::getDoubleTy(context);
            llvm::Type *void_type = llvm::Type::getVoidTy(context);
            llvm::FunctionType *func_type = llvm::FunctionType::get(void_type, {double_type}, false);
            return llvm::Function::Create(func_type, llvm::Function::ExternalLinkage, c_name, module);
        }
        // Add more runtime function declarations as needed

        return nullptr;
    }
    llvm::Value *CodegenVisitor::generate_array_access(Cryo::ArrayAccessNode *node) { return nullptr; }
    llvm::Value *CodegenVisitor::generate_member_access(Cryo::MemberAccessNode *node) { return nullptr; }
    void CodegenVisitor::generate_if_statement(Cryo::IfStatementNode *node) {}
    void CodegenVisitor::generate_while_loop(Cryo::WhileStatementNode *node) {}
    void CodegenVisitor::generate_for_loop(Cryo::ForStatementNode *node) {}
    void CodegenVisitor::generate_match_statement(Cryo::MatchStatementNode *node) {}
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

    llvm::Value *CodegenVisitor::create_load(llvm::Value *ptr, const std::string &name)
    {
        if (!ptr)
            return nullptr;

        try
        {
            auto &builder = _context_manager.get_builder();
            // For LLVM 20, we need to provide the element type explicitly
            llvm::Type *element_type = nullptr;
            if (ptr->getType()->isPointerTy())
            {
                // For opaque pointers in LLVM 20, we need to infer the type
                // This is a simplified approach - in practice, we'd track the original type
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

} // namespace Cryo::Codegen
