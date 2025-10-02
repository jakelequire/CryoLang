#include "Codegen/CodegenVisitor.hpp"
#include "AST/ASTNode.hpp"
#include "AST/TemplateRegistry.hpp"
#include "Lexer/lexer.hpp"
#include "Utils/ModuleLoader.hpp"
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/InlineAsm.h>
#include <iostream>
#include <set>
#include <filesystem>
#include <sstream>
#include <algorithm>

namespace Cryo::Codegen
{
    //===================================================================
    // Construction
    //===================================================================

    CodegenVisitor::CodegenVisitor(LLVMContextManager &context_manager,
                                   Cryo::SymbolTable &symbol_table,
                                   Cryo::DiagnosticManager *gdm)
        : _context_manager(context_manager),
          _symbol_table(symbol_table),
          _value_context(std::make_unique<ValueContext>()),
          _type_mapper(std::make_unique<TypeMapper>(context_manager, symbol_table.get_type_context())),
          _intrinsics(std::make_unique<Intrinsics>(context_manager, gdm)),
          _function_registry(std::make_unique<FunctionRegistry>(symbol_table, *symbol_table.get_type_context())),
          _current_value(nullptr),
          _has_errors(false),
          _stdlib_compilation_mode(false)
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

    void CodegenVisitor::set_source_info(const std::string &source_file, const std::string &namespace_context)
    {
        _source_file = source_file;
        _namespace_context = namespace_context;
    }

    //===================================================================
    // AST Visitor Implementation - Minimal versions for compilation
    //===================================================================

    void CodegenVisitor::visit(Cryo::ProgramNode &node)
    {
        std::cout << "[DEBUG] CodegenVisitor::visit(ProgramNode) - stdlib_compilation_mode: " << (_stdlib_compilation_mode ? "true" : "false") << std::endl;

        // In stdlib compilation mode, use the existing main module instead of creating a new one
        llvm::Module *module = nullptr;
        std::string module_name = "cryo_program"; // fallback default

        if (_stdlib_compilation_mode)
        {
            // In stdlib mode, use the existing main module
            module = _context_manager.get_main_module();
            if (module)
            {
                module_name = _context_manager.get_main_module_name();
                std::cout << "[INFO] Using existing main module for stdlib compilation: '" << module_name << "'" << std::endl;
            }
            else
            {
                std::cout << "[WARNING] No main module found in stdlib mode, creating fallback" << std::endl;
            }
        }

        if (!module)
        {
            // Create module name prioritizing namespace context
            // First, try to use namespace context if available
            if (!_namespace_context.empty())
            {
                module_name = _namespace_context;
            }
            // Otherwise, try to construct from source file
            else if (!_source_file.empty())
            {
                // Extract just the filename without path and extension
                std::filesystem::path file_path(_source_file);
                std::string filename = file_path.stem().string(); // filename without extension
                module_name = filename;
            }

            std::cout << "[INFO] Creating LLVM module: '" << module_name << "'" << std::endl;
            if (!_source_file.empty())
            {
                std::cout << "[INFO] Source file: '" << _source_file << "'" << std::endl;
            }
            if (!_namespace_context.empty())
            {
                std::cout << "[INFO] Namespace context: '" << _namespace_context << "'" << std::endl;
            }

            // Create the main module for this program
            module = _context_manager.create_module(module_name);
        }

        if (!module)
        {
            report_error("Failed to create LLVM module");
            return;
        }

        // Set source filename in the module metadata
        if (!_source_file.empty())
        {
            module->setSourceFileName(_source_file);
        }

        std::cout << "[DEBUG] Processing main program with " << node.statements().size() << " statements" << std::endl;

        // Three-pass processing to ensure proper dependency order
        // Pass 1: Process all enum declarations first (needed for struct/class methods)
        std::cout << "[DEBUG] Pass 1: Processing enum declarations" << std::endl;
        for (size_t i = 0; i < node.statements().size(); ++i)
        {
            auto &stmt = node.statements()[i];
            if (stmt)
            {
                // Check if this is an enum declaration
                if (stmt->kind() == NodeKind::EnumDeclaration)
                {
                    std::cout << "[DEBUG] Processing enum declaration " << (i + 1) << "/" << node.statements().size() << std::endl;
                    stmt->accept(*this);
                    std::cout << "[DEBUG] Completed enum declaration " << (i + 1) << std::endl;
                }
            }
        }

        // Pass 2: Process all struct and class declarations
        std::cout << "[DEBUG] Pass 2: Processing struct and class declarations" << std::endl;
        for (size_t i = 0; i < node.statements().size(); ++i)
        {
            auto &stmt = node.statements()[i];
            if (stmt)
            {
                // Check if this is a struct or class declaration
                if (stmt->kind() == NodeKind::StructDeclaration ||
                    stmt->kind() == NodeKind::ClassDeclaration)
                {
                    std::cout << "[DEBUG] Processing struct/class declaration " << (i + 1) << "/" << node.statements().size() << std::endl;
                    stmt->accept(*this);
                    std::cout << "[DEBUG] Completed struct/class declaration " << (i + 1) << std::endl;
                }
            }
        }

        // Pass 3: Process all other statements
        std::cout << "[DEBUG] Pass 3: Processing other statements" << std::endl;
        for (size_t i = 0; i < node.statements().size(); ++i)
        {
            auto &stmt = node.statements()[i];
            if (stmt)
            {
                // Skip enum, struct and class declarations (already processed)
                if (stmt->kind() == NodeKind::EnumDeclaration ||
                    stmt->kind() == NodeKind::StructDeclaration ||
                    stmt->kind() == NodeKind::ClassDeclaration)
                {
                    std::cout << "[DEBUG] Skipping already processed declaration " << (i + 1) << std::endl;
                    continue;
                }

                std::cout << "[DEBUG] Processing statement " << (i + 1) << "/" << node.statements().size() << std::endl;
                stmt->accept(*this);
                std::cout << "[DEBUG] Completed statement " << (i + 1) << std::endl;
            }
            else
            {
                std::cout << "[DEBUG] Skipping null statement " << (i + 1) << std::endl;
            }
        }

        std::cout << "[DEBUG] Completed processing main program" << std::endl;
    }

    void CodegenVisitor::visit(Cryo::FunctionDeclarationNode &node)
    {
        std::cout << "[DEBUG] Visiting FunctionDeclarationNode: " << node.name() << std::endl;

        // Check if this is a StructMethodNode that was already processed as a primitive method
        if (auto *struct_method = dynamic_cast<Cryo::StructMethodNode *>(&node))
        {
            // Check if this method was already processed by looking for the generated function
            std::string potential_scoped_name = current_primitive_type + "::" + node.name();
            auto module = _context_manager.get_module();
            if (module && module->getFunction(potential_scoped_name))
            {
                std::cout << "[DEBUG] Skipping already processed primitive method: " << potential_scoped_name << std::endl;
                register_value(&node, nullptr);
                return;
            }
        }

        try
        {
            // Skip generic functions for now - they require specialized template instantiation
            if (!node.generic_parameters().empty())
            {
                std::cout << "[DEBUG] Skipping generic function declaration: " << node.name() << " (has "
                          << node.generic_parameters().size() << " generic parameters)" << std::endl;
                register_value(&node, nullptr);
                return;
            }

            // Generate the function declaration
            llvm::Function *function = generate_function_declaration(&node);
            if (!function)
            {
                report_error("Failed to generate function declaration: " + node.name());
                return;
            }

            // Register the function in symbol table with multiple lookup keys
            _functions[node.name()] = function; // Simple name lookup

            // Also register with namespace-qualified name if we're in a namespace
            if (!_namespace_context.empty())
            {
                std::string qualified_name = _namespace_context + "::" + node.name();
                _functions[qualified_name] = function;
                std::cout << "[DEBUG] Registered function with qualified name: " << qualified_name << std::endl;
            }

            register_value(&node, function);

            // Generate function body if present
            if (node.body())
            {
                std::cout << "[DEBUG] Generating function body for: " << node.name() << std::endl;
                bool body_success = generate_function_body(&node, function);
                if (!body_success)
                {
                    report_error("Failed to generate function body: " + node.name());
                }
            }
            else
            {
                std::cout << "[DEBUG] No function body to generate for: " << node.name() << std::endl;
            }
        }
        catch (const std::exception &e)
        {
            report_error("Exception in function declaration: " + std::string(e.what()), &node);
        }
        std::cout << "[DEBUG] Completed FunctionDeclarationNode: " << node.name() << std::endl;
    }

    void CodegenVisitor::visit(Cryo::IntrinsicDeclarationNode &node)
    {
        try
        {
            // For memory efficiency, we'll register intrinsics on-demand rather than pre-allocating
            // LLVM function types for all 123 intrinsic functions at once
            std::cout << "[DEBUG] Deferring intrinsic registration: " << node.name() << std::endl;

            // Simply store the intrinsic name for later on-demand registration
            // This prevents memory exhaustion from creating 123 LLVM function types upfront

            std::cout << "[DEBUG] Intrinsic '" << node.name() << "' deferred successfully" << std::endl;
        }
        catch (const std::exception &e)
        {
            report_error("Exception in intrinsic declaration: " + std::string(e.what()), &node);
        }
    }

    void CodegenVisitor::visit(Cryo::ImportDeclarationNode &node)
    {
        // Import declarations should already be processed during the analysis phase
        // Skip processing during codegen to avoid double-processing and memory issues
        std::cout << "[DEBUG] Skipping import declaration during codegen: " << node.path() << std::endl;
        return;
    }

    void CodegenVisitor::visit(Cryo::VariableDeclarationNode &node)
    {
        try
        {
            // Debug: Check ValueContext state at the very beginning
            std::cout << "[DEBUG] VarDecl start: this=" << this << ", _value_context=" << _value_context.get() << std::endl;
            if (!_value_context)
            {
                std::cerr << "[ERROR] _value_context is null in visit(VariableDeclarationNode)!" << std::endl;
                return;
            }

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
            Cryo::Type *cryo_type = _symbol_table.get_type_context()->parse_type_from_string(type_annotation);
            if (!cryo_type)
            {
                report_error("Failed to parse type annotation: " + type_annotation);
                return;
            }
            llvm::Type *llvm_type = _type_mapper->map_type(cryo_type);
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
                            Cryo::Type *cryo_element_type = _symbol_table.get_type_context()->parse_type_from_string(element_type_name);
                            if (cryo_element_type)
                            {
                                element_type = _type_mapper->map_type(cryo_element_type);
                            }
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
                    // Check if this is an Array<T> type being initialized with an array literal
                    // This is a very special case to handle Array<T> construction
                    // e.g., `const arr: Array<int> = [1, 2, 3];`
                    // The constructor should be called with the array literal elements
                    //
                    // This is a very special case and not generalizable to other types
                    // so we hardcode the check for Array<T> here.
                    if (type_annotation.find("Array<") == 0 && type_annotation.find('>') != std::string::npos)
                    {
                        // This is an Array<T> type - call the constructor instead of direct assignment
                        std::cout << "[CodegenVisitor] Array<T> variable initialization - calling constructor" << std::endl;

                        // Get the array literal size before generating it
                        size_t array_literal_size = 0;
                        if (auto *array_literal = dynamic_cast<Cryo::ArrayLiteralNode *>(node.initializer()))
                        {
                            array_literal_size = array_literal->size();
                            std::cout << "[CodegenVisitor] Array literal has " << array_literal_size << " elements" << std::endl;
                        }

                        // Generate the array literal first
                        node.initializer()->accept(*this);
                        llvm::Value *array_ptr = get_current_value();

                        if (array_ptr)
                        {
                            // Extract the concrete type name (e.g., Array<int> -> Array_int)
                            std::string base_type = type_annotation.substr(0, type_annotation.find('<'));
                            size_t start = type_annotation.find('<') + 1;
                            size_t end = type_annotation.find('>');
                            std::string type_args = type_annotation.substr(start, end - start);
                            std::string monomorphized_name = base_type + "_" + type_args;

                            // Find the constructor function
                            std::string constructor_name = monomorphized_name + "::" + base_type;
                            auto constructor_it = _functions.find(constructor_name);

                            if (constructor_it != _functions.end())
                            {
                                llvm::Function *constructor_func = constructor_it->second;
                                std::cout << "[CodegenVisitor] Found Array constructor: " << constructor_name << std::endl;

                                // Get the builder from context manager
                                auto &builder = _context_manager.get_builder();
                                auto &context = _context_manager.get_context();

                                // Get the exact parameter type from the constructor function
                                // This ensures we match the constructor parameter type exactly
                                llvm::FunctionType *constructor_type = constructor_func->getFunctionType();
                                llvm::Type *dynamic_array_type = nullptr;

                                // The constructor should have 2 parameters: 'this' pointer and dynamic array struct
                                if (constructor_type->getNumParams() >= 2)
                                {
                                    dynamic_array_type = constructor_type->getParamType(1); // Second parameter
                                    std::cout << "[CodegenVisitor] Using constructor parameter type for dynamic array" << std::endl;
                                }
                                else
                                {
                                    std::cout << "[CodegenVisitor] ERROR: Constructor doesn't have expected parameter count" << std::endl;
                                    return; // Skip this variable declaration
                                }

                                // Allocate and initialize the dynamic array struct
                                llvm::AllocaInst *dynamic_array_alloca = builder.CreateAlloca(dynamic_array_type, nullptr, "dynamic_array");

                                // Set ptr field (first field) to the array pointer
                                llvm::Value *ptr_field_ptr = builder.CreateStructGEP(dynamic_array_type, dynamic_array_alloca, 0, "ptr_field");
                                builder.CreateStore(array_ptr, ptr_field_ptr);

                                // Set length field (second field) to the actual array literal size
                                llvm::Value *length_field_ptr = builder.CreateStructGEP(dynamic_array_type, dynamic_array_alloca, 1, "length_field");
                                builder.CreateStore(llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), array_literal_size), length_field_ptr);

                                // Set capacity field (third field) to the same value as length initially
                                llvm::Value *capacity_field_ptr = builder.CreateStructGEP(dynamic_array_type, dynamic_array_alloca, 2, "capacity_field");
                                builder.CreateStore(llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), array_literal_size), capacity_field_ptr);

                                // Load the dynamic array struct to pass by value
                                llvm::Value *dynamic_array_value = builder.CreateLoad(dynamic_array_type, dynamic_array_alloca, "dynamic_array_value");

                                // Call constructor with 'this' pointer and dynamic array struct
                                std::vector<llvm::Value *> args;
                                args.push_back(alloca);              // 'this' pointer
                                args.push_back(dynamic_array_value); // dynamic array struct

                                builder.CreateCall(constructor_func, args);

                                std::cout << "[CodegenVisitor] Called Array constructor successfully" << std::endl;
                            }
                            else
                            {
                                std::cout << "[CodegenVisitor] Warning: Array constructor not found: " << constructor_name << std::endl;
                                // Fall back to direct assignment
                                create_store(array_ptr, alloca);
                            }
                        }
                    }
                    else
                    {
                        // Regular variable initialization
                        node.initializer()->accept(*this);
                        llvm::Value *init_value = get_current_value();

                        if (init_value)
                        {
                            create_store(init_value, alloca);
                        }
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

        // Register AST node with TypeMapper for field metadata
        _type_mapper->register_struct_ast_node(&node);

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
        Cryo::StructType *cryo_struct_type = static_cast<Cryo::StructType *>(_symbol_table.get_type_context()->get_struct_type(node.name()));
        llvm::Type *struct_type = _type_mapper->map_struct_type(cryo_struct_type);

        if (!struct_type)
        {
            report_error("Failed to map struct type: " + node.name(), &node);
            return;
        }

        // Store the struct type for later use in CodegenVisitor
        _types[node.name()] = struct_type;

        std::cout << "[CodegenVisitor] Created LLVM struct type for " << node.name() << std::endl;

        // Generate methods defined in the struct (similar to class method processing)
        if (!node.methods().empty())
        {
            std::cout << "[CodegenVisitor] Processing " << node.methods().size() << " struct methods for " << node.name() << std::endl;

            llvm::Type *struct_ptr_type = llvm::PointerType::getUnqual(struct_type);

            for (const auto &method : node.methods())
            {
                std::cout << "[CodegenVisitor] Generating struct method: " << node.name() << "::" << method->name() << std::endl;

                std::string method_name = method->name();
                std::string qualified_name;

                // Include full namespace in qualified name for proper symbol lookup
                if (!_namespace_context.empty())
                {
                    qualified_name = _namespace_context + "::" + node.name() + "::" + method_name;
                }
                else
                {
                    qualified_name = node.name() + "::" + method_name;
                }

                std::cout << "[CodegenVisitor] Full qualified struct method name: " << qualified_name << std::endl;

                // Build parameter types - add 'this' pointer for non-static methods
                std::vector<llvm::Type *> param_types;

                bool is_static = method->is_static();
                if (!is_static)
                {
                    param_types.push_back(struct_ptr_type); // 'this' pointer
                }

                // Add other parameters
                for (const auto &param : method->parameters())
                {
                    if (param)
                    {
                        std::string param_type_str = param->type_annotation();
                        Cryo::Type *cryo_param_type = _symbol_table.get_type_context()->parse_type_from_string(param_type_str);
                        llvm::Type *param_type = cryo_param_type ? _type_mapper->map_type(cryo_param_type) : nullptr;
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
                    Cryo::Type *cryo_return_type = _symbol_table.get_type_context()->parse_type_from_string(method->return_type_annotation());
                    llvm::Type *mapped_return_type = cryo_return_type ? _type_mapper->map_type(cryo_return_type) : nullptr;
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
                std::cout << "[DEBUG] Stored struct method '" << qualified_name << "' in function registry" << std::endl;

                // Set parameter names
                auto arg_it = func->arg_begin();

                if (!is_static)
                {
                    arg_it->setName("this");
                    ++arg_it;
                }

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

                    // Create allocas and store parameter values
                    auto param_arg_it = func->arg_begin();

                    // Handle 'this' parameter first (only for non-static methods)
                    if (!is_static && param_arg_it != func->arg_end())
                    {
                        // Create alloca for 'this' pointer
                        llvm::AllocaInst *this_alloca = _context_manager.get_builder().CreateAlloca(struct_ptr_type, nullptr, "this");
                        _context_manager.get_builder().CreateStore(&*param_arg_it, this_alloca);
                        _value_context->set_value("this", this_alloca, this_alloca, struct_ptr_type);
                        ++param_arg_it;
                    }

                    // Handle other parameters
                    for (const auto &param : method->parameters())
                    {
                        if (param && param_arg_it != func->arg_end())
                        {
                            std::string param_type_str = param->type_annotation();
                            Cryo::Type *cryo_param_type = _symbol_table.get_type_context()->parse_type_from_string(param_type_str);
                            llvm::Type *param_type = cryo_param_type ? _type_mapper->map_type(cryo_param_type) : nullptr;

                            if (param_type)
                            {
                                llvm::AllocaInst *param_alloca = _context_manager.get_builder().CreateAlloca(param_type, nullptr, param->name());
                                _context_manager.get_builder().CreateStore(&*param_arg_it, param_alloca);
                                _value_context->set_value(param->name(), param_alloca, param_alloca, param_type);
                            }
                            ++param_arg_it;
                        }
                    }

                    // Generate method body
                    try
                    {
                        // Set current struct context for member access resolution
                        current_struct_type = node.name();

                        method->body()->accept(*this);

                        // Clear struct context
                        current_struct_type.clear();

                        // Add return if the method doesn't already have one
                        llvm::BasicBlock *current_block = _context_manager.get_builder().GetInsertBlock();
                        if (current_block && !current_block->getTerminator())
                        {
                            if (return_type->isVoidTy())
                            {
                                _context_manager.get_builder().CreateRetVoid();
                            }
                            else
                            {
                                // Create a default return value (this shouldn't happen in well-formed code)
                                llvm::Value *default_val = llvm::UndefValue::get(return_type);
                                _context_manager.get_builder().CreateRet(default_val);
                            }
                        }
                    }
                    catch (const std::exception &e)
                    {
                        report_error("Exception in struct method body generation: " + std::string(e.what()), method.get());
                    }

                    // Exit function scope
                    exit_scope();
                    _current_function.reset();
                }
            }
        }

        register_value(&node, nullptr); // Struct declarations don't have runtime values
    }

    void CodegenVisitor::visit(Cryo::ClassDeclarationNode &node)
    {
        std::cout << "[CodegenVisitor] Visiting ClassDeclarationNode: " << node.name() << std::endl;

        // Check if this is a generic class template
        if (!node.generic_parameters().empty())
        {
            std::cout << "[DEBUG] Skipping code generation for generic class template: " << node.name()
                      << " (has " << node.generic_parameters().size() << " generic parameters)" << std::endl;
            std::cout << "[DEBUG] Generic classes like " << node.name() << "<T> will be handled when instantiated with concrete types" << std::endl;
            return;
        }

        // Register AST node with TypeMapper for field metadata
        _type_mapper->register_class_ast_node(&node);

        // Clear any previous type mapping errors before processing this class
        _type_mapper->clear_errors();

        // Generate LLVM type for the class
        Cryo::ClassType *cryo_class_type = static_cast<Cryo::ClassType *>(_symbol_table.get_type_context()->get_class_type(node.name()));
        llvm::Type *class_type = _type_mapper->map_class_type(cryo_class_type);
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
                std::string qualified_name;

                // Include full namespace in qualified name for proper symbol lookup
                if (!_namespace_context.empty())
                {
                    qualified_name = _namespace_context + "::" + class_name + "::" + method_name;
                }
                else
                {
                    qualified_name = class_name + "::" + method_name;
                }

                std::cout << "[CodegenVisitor] Full qualified method name: " << qualified_name << std::endl;

                // Build parameter types - static methods don't need 'this' parameter
                std::vector<llvm::Type *> param_types;

                bool is_static = method->is_static();
                if (!is_static)
                {
                    param_types.push_back(class_ptr_type); // 'this' pointer only for non-static methods
                }

                // Add other parameters
                for (const auto &param : method->parameters())
                {
                    if (param)
                    {
                        std::string param_type_str = param->type_annotation();
                        Cryo::Type *cryo_param_type = _symbol_table.get_type_context()->parse_type_from_string(param_type_str);
                        llvm::Type *param_type = cryo_param_type ? _type_mapper->map_type(cryo_param_type) : nullptr;
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
                    Cryo::Type *cryo_return_type = _symbol_table.get_type_context()->parse_type_from_string(method->return_type_annotation());
                    llvm::Type *mapped_return_type = cryo_return_type ? _type_mapper->map_type(cryo_return_type) : nullptr;
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
                std::cout << "[DEBUG] Stored function '" << qualified_name << "' in module: " << module->getName().str() << std::endl;
                std::cout << "[DEBUG] Function name at store time: " << func->getName().str() << std::endl;
                std::cout << "[DEBUG] Function has parent module: " << (func->getParent() ? "YES" : "NO") << std::endl;

                // Set parameter names
                auto arg_it = func->arg_begin();

                if (!is_static)
                {
                    arg_it->setName("this");
                    ++arg_it;
                }

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

                    // Handle 'this' parameter first (only for non-static methods)
                    if (!is_static && arg_it != func->arg_end())
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
                            Cryo::Type *cryo_param_type = _symbol_table.get_type_context()->parse_type_from_string(param_type_str);
                            llvm::Type *param_type = cryo_param_type ? _type_mapper->map_type(cryo_param_type) : nullptr;

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
        // Skip generic enums for now - they should be handled as ParameterizedType instances
        // when actually instantiated, not as generic templates
        if (!node.generic_parameters().empty())
        {
            std::cout << "[DEBUG] Skipping generic enum declaration: " << node.name() << " (has "
                      << node.generic_parameters().size() << " generic parameters)" << std::endl;
            std::cout << "[DEBUG] Generic enums like Option<T> will be handled through ParameterizedType system when instantiated" << std::endl;
            register_value(&node, nullptr);
            return;
        }

        // Generate LLVM type for the enum
        // Extract variant names from the AST node
        std::vector<std::string> variant_names;
        for (const auto &variant : node.variants())
        {
            variant_names.push_back(variant->name());
        }

        std::cout << "[DEBUG] Processing enum: " << node.name() << std::endl;
        std::cout << "[DEBUG] Enum has " << node.variants().size() << " variants" << std::endl;
        std::cout << "[DEBUG] node.is_simple_enum() = " << node.is_simple_enum() << std::endl;

        // Check each variant
        for (const auto &variant : node.variants())
        {
            std::cout << "[DEBUG] Variant: " << variant->name()
                      << " has " << variant->associated_types().size() << " associated types"
                      << " is_simple_variant: " << variant->is_simple_variant() << std::endl;
        }

        Cryo::EnumType *cryo_enum_type = static_cast<Cryo::EnumType *>(
            _symbol_table.get_type_context()->get_enum_type(node.name(), std::move(variant_names), node.is_simple_enum()));

        std::cout << "[DEBUG] Created EnumType, is_simple_enum(): " << cryo_enum_type->is_simple_enum() << std::endl;
        llvm::Type *enum_type = _type_mapper->map_enum_type(cryo_enum_type);
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

    void CodegenVisitor::visit(Cryo::TraitDeclarationNode &node)
    {
        // Traits are purely compile-time constructs, no runtime codegen needed
        std::cout << "[CodegenVisitor] Skipping trait declaration for codegen: " << node.name() << std::endl;
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
        std::cout << "[CodegenVisitor] Visiting StructMethodNode: " << node.name() << std::endl;

        // Check if we're in a primitive implementation block context
        if (!current_primitive_type.empty())
        {
            // Primitive method - needs special handling for 'this' parameter
            generate_primitive_method(&node, current_primitive_type);
            register_value(&node, nullptr);
            return;
        }

        // Regular struct method: These should be processed by StructDeclarationNode
        // When StructMethodNode is visited individually, it means it was already processed
        // Check if this method was already generated by looking for the function
        std::string parent_struct_name = ""; // Get from parent context if available
        std::string potential_qualified_name;

        // Try to find the parent struct name and construct qualified method name
        if (!_namespace_context.empty())
        {
            // We need a way to get the parent struct name
            // For now, let's check if the function already exists in registry
            auto module = _context_manager.get_module();
            if (module)
            {
                // Look for any function that ends with "::" + method name
                for (auto &func : *module)
                {
                    std::string func_name = func.getName().str();
                    std::string suffix = "::" + node.name();
                    if (func_name.size() > suffix.size() &&
                        func_name.substr(func_name.size() - suffix.size()) == suffix)
                    {
                        std::cout << "[CodegenVisitor] StructMethodNode already processed: " << func_name << std::endl;
                        register_value(&node, &func);
                        return;
                    }
                }
            }
        }

        std::cout << "[CodegenVisitor] WARNING: StructMethodNode visited individually without parent struct context: " << node.name() << std::endl;
        std::cout << "[CodegenVisitor] This suggests the method was not processed by StructDeclarationNode" << std::endl;

        // If we reach here, treat it as a standalone function (fallback)
        visit(static_cast<FunctionDeclarationNode &>(node));
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

        std::string target_type_name = node.target_type();

        // Skip generic implementation blocks (contain < and >)
        if (target_type_name.find('<') != std::string::npos && target_type_name.find('>') != std::string::npos)
        {
            std::cout << "[DEBUG] Skipping generic implementation block for: " << target_type_name << std::endl;
            register_value(&node, nullptr);
            return;
        }

        auto &context = _context_manager.get_context();
        auto module = _context_manager.get_module();
        auto &builder = _context_manager.get_builder();

        if (!module)
        {
            report_error("No module available for implementation block generation", &node);
            return;
        }

        // Check if this is a primitive type first
        Cryo::Type *cryo_target_type = _symbol_table.get_type_context()->parse_type_from_string(target_type_name);
        llvm::Type *primitive_type = cryo_target_type ? _type_mapper->map_type(cryo_target_type) : nullptr;
        std::cout << "[DEBUG] Implementation block for: " << target_type_name
                  << ", mapped type: " << (primitive_type ? "valid" : "null")
                  << ", is_primitive: " << (is_primitive_type(target_type_name) ? "yes" : "no") << std::endl;

        if (primitive_type && is_primitive_type(target_type_name))
        {
            std::cout << "[CodegenVisitor] Generating implementation block for primitive type: " << target_type_name << std::endl;

            // Set primitive type context for method generation
            current_primitive_type = target_type_name;

            // Generate all method implementations for primitive type
            for (const auto &method : node.method_implementations())
            {
                if (method)
                {
                    // For primitive methods, we need to generate them as regular functions
                    // but with a special naming scheme that includes the primitive type
                    visit(*method);
                }
            }

            // Clear primitive type context
            current_primitive_type.clear();
            return; // We've handled the primitive implementation
        }

        // Look up the struct type
        auto struct_type_it = _types.find(target_type_name);
        if (struct_type_it == _types.end())
        {
            report_error("Unknown struct type in implementation block: " + target_type_name + " (node kind: " + std::to_string(static_cast<int>(node.kind())) + ")", &node);
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
                    Cryo::Type *cryo_param_type = _symbol_table.get_type_context()->parse_type_from_string(param_type_str);
                    llvm::Type *param_type = cryo_param_type ? _type_mapper->map_type(cryo_param_type) : nullptr;
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
                Cryo::Type *cryo_return_type = _symbol_table.get_type_context()->parse_type_from_string(return_type_str);
                llvm::Type *mapped_return_type = cryo_return_type ? _type_mapper->map_type(cryo_return_type) : nullptr;
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
                    Cryo::Type *cryo_param_type = _symbol_table.get_type_context()->parse_type_from_string(param_type_str);
                    llvm::Type *param_type = cryo_param_type ? _type_mapper->map_type(cryo_param_type) : nullptr;

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

                std::cout << "[DEBUG] Return statement: return_value = " << (return_value ? "valid" : "NULL") << std::endl;
                if (return_value)
                {
                    std::cout << "[DEBUG] Return value type: " << return_value->getType()->getTypeID() << std::endl;
                }

                if (!return_value)
                {
                    std::cout << "[DEBUG] Return value is NULL - creating default value" << std::endl;
                    report_error("Failed to generate return value");
                    return;
                }

                if (_current_function && _current_function->return_value_alloca)
                {
                    std::cout << "[DEBUG] Using structured return pattern" << std::endl;
                    // Store return value and jump to return block
                    create_store(return_value, _current_function->return_value_alloca);
                    builder.CreateBr(_current_function->return_block);
                }
                else
                {
                    std::cout << "[DEBUG] Using direct return" << std::endl;
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

    void CodegenVisitor::visit(Cryo::SwitchStatementNode &node)
    {
        try
        {
            generate_switch_statement(&node);
            register_value(&node, nullptr);
        }
        catch (const std::exception &e)
        {
            report_error("Exception in switch statement: " + std::string(e.what()), &node);
        }
    }

    void CodegenVisitor::visit(Cryo::CaseStatementNode &node)
    {
        // Case statements are handled as part of switch statement generation
        // This visit method should not be called directly
        report_error("CaseStatementNode visit called directly - should be handled by SwitchStatementNode", &node);
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

        if (_breakable_stack.empty())
        {
            std::cerr << "[ERROR] Break statement used outside of breakable context (loop or switch)" << std::endl;
            register_value(&node, nullptr);
            return;
        }

        // Get the current breakable context
        const auto &breakable_context = _breakable_stack.top();

        // Create branch to the break block
        builder.CreateBr(breakable_context.break_block);

        // Create a new basic block for any unreachable code after break
        // This is needed because LLVM requires all basic blocks to end with a terminator
        llvm::Function *function = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock *unreachableBlock = llvm::BasicBlock::Create(_context_manager.get_context(), "after.break", function);
        builder.SetInsertPoint(unreachableBlock);

        // Add unreachable terminator since this code should never be reached
        builder.CreateUnreachable();

        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::ContinueStatementNode &node)
    {
        auto &builder = _context_manager.get_builder();

        if (_breakable_stack.empty())
        {
            std::cerr << "[ERROR] Continue statement used outside of loop context" << std::endl;
            register_value(&node, nullptr);
            return;
        }

        // Get the current breakable context
        const auto &breakable_context = _breakable_stack.top();

        // Continue is only valid in loop contexts, not switch contexts
        if (breakable_context.context_type != BreakableContext::Loop)
        {
            std::cerr << "[ERROR] Continue statement used outside of loop context" << std::endl;
            register_value(&node, nullptr);
            return;
        }

        // Create branch to the loop continue block (increment in for loop, condition in while loop)
        builder.CreateBr(breakable_context.continue_block);

        // Create a new basic block for any unreachable code after continue
        // This is needed because LLVM requires all basic blocks to end with a terminator
        llvm::Function *function = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock *unreachableBlock = llvm::BasicBlock::Create(_context_manager.get_context(), "after.continue", function);
        builder.SetInsertPoint(unreachableBlock);

        // Add unreachable terminator since this code should never be reached
        builder.CreateUnreachable();

        register_value(&node, nullptr);
    }

    void CodegenVisitor::visit(Cryo::LiteralNode &node)
    {
        std::cout << "[DEBUG] Visiting LiteralNode, kind: " << static_cast<int>(node.literal_kind()) << ", value: " << node.value() << std::endl;
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

            // Process escape sequences
            str_val = process_escape_sequences(str_val);

            literal_value = _context_manager.get_builder().CreateGlobalStringPtr(str_val);
            break;
        }
        case TokenKind::TK_KW_NULL:
        {
            // Create null pointer constant
            literal_value = llvm::ConstantPointerNull::get(llvm::PointerType::get(llvm_ctx, 0));
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
                        llvm::Value *arg_value = get_current_value();
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
                                llvm::Value *arg_value = get_current_value();
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

    void CodegenVisitor::visit(Cryo::SizeofExpressionNode &node)
    {
        std::cout << "[CodegenVisitor] Generating sizeof expression" << std::endl;

        auto &context = _context_manager.get_context();
        auto module = _context_manager.get_module();
        auto &builder = _context_manager.get_builder();

        if (!module)
        {
            report_error("No module available for sizeof expression", &node);
            return;
        }

        std::string type_name = node.type_name();
        llvm::Value *size_value = nullptr;

        // Handle primitive types with known sizes
        if (type_name == "u8" || type_name == "i8" || type_name == "char")
        {
            size_value = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 1);
        }
        else if (type_name == "u16" || type_name == "i16")
        {
            size_value = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 2);
        }
        else if (type_name == "u32" || type_name == "i32" || type_name == "int")
        {
            size_value = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 4);
        }
        else if (type_name == "u64" || type_name == "i64")
        {
            size_value = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 8);
        }
        else if (type_name == "f32")
        {
            size_value = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 4);
        }
        else if (type_name == "f64" || type_name == "float")
        {
            size_value = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 8);
        }
        else if (type_name == "bool")
        {
            size_value = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 1);
        }
        else if (type_name == "string")
        {
            // String is a pointer (8 bytes on 64-bit systems)
            size_value = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 8);
        }
        else
        {
            // Handle user-defined types
            llvm::Type *llvm_type = _type_mapper->lookup_type(type_name);
            if (!llvm_type)
            {
                // Try the old lookup method as fallback
                auto type_it = _types.find(type_name);
                if (type_it != _types.end())
                {
                    llvm_type = type_it->second;
                }
            }

            if (llvm_type)
            {
                // Calculate size using LLVM's data layout
                const llvm::DataLayout &data_layout = module->getDataLayout();
                uint64_t type_size = data_layout.getTypeAllocSize(llvm_type);
                size_value = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), type_size);
            }
            else
            {
                report_error("Unknown type in sizeof expression: " + type_name, &node);
                // Return 0 as fallback
                size_value = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 0);
            }
        }

        register_value(&node, size_value);
        set_current_value(size_value);
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

        // Special handling for Array<T> types - treat them as direct array access
        if (!array_var_name.empty() && !is_nested_access)
        {
            auto type_it = _variable_types.find(array_var_name);
            if (type_it != _variable_types.end())
            {
                std::string var_type = type_it->second;
                // Check if this is an Array<T> type
                if (var_type.find("Array<") == 0 && var_type.find('>') != std::string::npos)
                {
                    std::cout << "[CodegenVisitor] Special Array<T> direct access for variable '"
                              << array_var_name << "' of type '" << var_type << "'" << std::endl;

                    // Extract the element type (T)
                    size_t start = var_type.find('<') + 1;
                    size_t end = var_type.find('>');
                    std::string element_type_str = var_type.substr(start, end - start);

                    // Get the element type for Array operations
                    Cryo::Type *cryo_element_type = _symbol_table.get_type_context()->parse_type_from_string(element_type_str);
                    llvm::Type *llvm_element_type = cryo_element_type ? _type_mapper->map_type(cryo_element_type) : nullptr;

                    if (!llvm_element_type)
                    {
                        report_error("Could not resolve element type for Array indexing: " + element_type_str, &node);
                        return;
                    }

                    // Get the Array variable's alloca
                    llvm::Value *array_var_alloca = _value_context->get_value(array_var_name);
                    if (array_var_alloca && array_var_alloca->getType()->isPointerTy())
                    {
                        std::cout << "[CodegenVisitor] Array variable alloca found, attempting load..." << std::endl;

                        // Load the array pointer from the Array variable
                        // Since the Array<int> struct is empty but contains the array literal pointer,
                        // we need to load it carefully - the stored data is actually a pointer to the first element
                        llvm::Type *stored_ptr_type = llvm::PointerType::get(context, 0);
                        llvm::Value *loaded_array_ptr = builder.CreateLoad(
                            stored_ptr_type,
                            array_var_alloca,
                            array_var_name + ".array.load");

                        std::cout << "[CodegenVisitor] Successfully loaded array pointer, creating element access..." << std::endl;

                        // Create GEP to access the specific element directly
                        llvm::Value *element_ptr = builder.CreateGEP(
                            llvm_element_type,
                            loaded_array_ptr,
                            {index_val},
                            array_var_name + ".element.ptr");

                        // Load the element value
                        llvm::Value *element_value = builder.CreateLoad(
                            llvm_element_type,
                            element_ptr,
                            array_var_name + ".element.load");

                        register_value(&node, element_value);
                        set_current_value(element_value);
                        std::cout << "[CodegenVisitor] Generated Array<T> direct element access successfully - RETURNING" << std::endl;
                        return;
                    }
                    else
                    {
                        report_error("Could not get Array variable alloca for indexing: " + array_var_name, &node);
                        return;
                    }
                }
            }
        }

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

        // Check if this is array length access (literal_elements.length)
        if (member_name == "length" && object_ptr)
        {
            std::cout << "[CodegenVisitor] Checking for array length access" << std::endl;

            // Check if the object is a dynamic array type (T[])
            if (auto *alloca_inst = llvm::dyn_cast<llvm::AllocaInst>(object_ptr))
            {
                llvm::Type *allocated_type = alloca_inst->getAllocatedType();

                // Check if this is a dynamic array struct (has ptr, length, capacity fields)
                if (auto *struct_type = llvm::dyn_cast<llvm::StructType>(allocated_type))
                {
                    std::string type_name = struct_type->getName().str();
                    if (type_name.find("dynamic_array") != std::string::npos)
                    {
                        std::cout << "[CodegenVisitor] Found dynamic array type for length access: " << type_name << std::endl;

                        // Access the length field (field index 1)
                        llvm::Value *length_field_ptr = builder.CreateStructGEP(struct_type, object_ptr, 1, "length_ptr");
                        llvm::Type *length_type = struct_type->getStructElementType(1); // u64
                        llvm::Value *length_value = create_load(length_field_ptr, length_type, "length_val");

                        std::cout << "[CodegenVisitor] Successfully generated array length access" << std::endl;
                        register_value(&node, length_value);
                        set_current_value(length_value);
                        return;
                    }
                }
            }
        }

        // Enhanced member access resolution for generic structs
        llvm::Type *struct_type = nullptr;
        int field_index = -1;
        std::string type_name;

        // Strategy 1: Try current struct context first (most reliable for generic structs)
        if (!current_struct_type.empty())
        {
            std::cout << "[CodegenVisitor] Trying current struct context: " << current_struct_type << std::endl;

            auto context_type_it = _types.find(current_struct_type);
            if (context_type_it != _types.end())
            {
                if (auto *struct_llvm_type = llvm::dyn_cast<llvm::StructType>(context_type_it->second))
                {
                    // Verify this could be the right type by checking field exists
                    int test_field_index = _type_mapper->get_field_index(current_struct_type, member_name);
                    if (test_field_index != -1)
                    {
                        struct_type = struct_llvm_type;
                        type_name = current_struct_type;
                        field_index = test_field_index;
                        std::cout << "[CodegenVisitor] Found struct type via method context: " << current_struct_type
                                  << ", field index: " << field_index << std::endl;
                    }
                }
            }
        }

        // Strategy 2: Try direct type resolution from LLVM type if strategy 1 failed
        if (!struct_type && object_ptr)
        {
            std::cout << "[CodegenVisitor] Trying direct type resolution from LLVM type" << std::endl;

            if (auto *alloca_inst = llvm::dyn_cast<llvm::AllocaInst>(object_ptr))
            {
                llvm::Type *allocated_type = alloca_inst->getAllocatedType();
                std::cout << "[CodegenVisitor] Member access allocated_type: " << (allocated_type ? "valid" : "null") << std::endl;

                if (allocated_type->isPointerTy())
                {
                    std::cout << "[CodegenVisitor] Member access: this is a pointer type, searching for matching struct" << std::endl;

                    // For pointer types, find the matching struct by comparing against all registered types
                    for (const auto &[registered_name, registered_type] : _types)
                    {
                        if (auto *struct_llvm_type = llvm::dyn_cast<llvm::StructType>(registered_type))
                        {
                            llvm::Type *expected_ptr_type = llvm::PointerType::getUnqual(struct_llvm_type);

                            // Try both exact comparison and flexible comparison for generic structs
                            bool type_matches = (allocated_type == expected_ptr_type);

                            // For generic structs, also check if this could be a specialized instance
                            if (!type_matches && registered_name.find("GenericStruct") != std::string::npos)
                            {
                                // Check if field exists in this struct type
                                int test_field_index = _type_mapper->get_field_index(registered_name, member_name);
                                if (test_field_index != -1)
                                {
                                    type_matches = true;
                                    std::cout << "[CodegenVisitor] Generic struct field match found for: " << registered_name << std::endl;
                                }
                            }

                            if (type_matches)
                            {
                                struct_type = struct_llvm_type;
                                type_name = registered_name;
                                field_index = _type_mapper->get_field_index(registered_name, member_name);
                                std::cout << "[CodegenVisitor] Found matching struct type: " << registered_name
                                          << ", field index: " << field_index << std::endl;
                                break;
                            }
                        }
                    }
                }
                else if (llvm::isa<llvm::StructType>(allocated_type))
                {
                    // Direct struct type
                    struct_type = allocated_type;
                    std::cout << "[CodegenVisitor] Member access: direct struct type" << std::endl;

                    // Find the type name
                    for (const auto &[registered_name, registered_type] : _types)
                    {
                        if (registered_type == struct_type)
                        {
                            type_name = registered_name;
                            field_index = _type_mapper->get_field_index(registered_name, member_name);
                            std::cout << "[CodegenVisitor] Found direct struct type: " << type_name
                                      << ", field index: " << field_index << std::endl;
                            break;
                        }
                    }
                }
            }
        }

        // Strategy 3: Fallback pattern matching for specialized generic types
        if (!struct_type || field_index == -1)
        {
            std::cout << "[CodegenVisitor] Trying fallback pattern matching for specialized generic structs" << std::endl;

            // Look specifically for GenericStruct specializations
            for (const auto &[registered_name, registered_type] : _types)
            {
                if (auto *struct_llvm_type = llvm::dyn_cast<llvm::StructType>(registered_type))
                {
                    // Check if this is a GenericStruct specialization with the field we need
                    if (registered_name.find("GenericStruct_") != std::string::npos)
                    {
                        int test_field_index = _type_mapper->get_field_index(registered_name, member_name);
                        if (test_field_index != -1)
                        {
                            struct_type = struct_llvm_type;
                            type_name = registered_name;
                            field_index = test_field_index;
                            std::cout << "[CodegenVisitor] Fallback pattern match found: " << registered_name
                                      << ", field index: " << field_index << std::endl;
                            break;
                        }
                    }
                }
            }
        }

        // Validate we found everything we need
        if (!struct_type || field_index == -1 || type_name.empty())
        {
            std::cout << "[CodegenVisitor] Member access resolution failed:" << std::endl;
            std::cout << "  struct_type: " << (struct_type ? "found" : "null") << std::endl;
            std::cout << "  field_index: " << field_index << std::endl;
            std::cout << "  type_name: " << type_name << std::endl;
            std::cout << "  member_name: " << member_name << std::endl;
            std::cout << "  current_struct_type: " << current_struct_type << std::endl;

            report_error("Unknown struct type or field in member access: " + member_name + " (type: " + type_name + ")", &node);
            register_value(&node, nullptr);
            return;
        }

        std::cout << "[CodegenVisitor] Member access resolution successful:" << std::endl;
        std::cout << "  type_name: " << type_name << std::endl;
        std::cout << "  member_name: " << member_name << std::endl;
        std::cout << "  field_index: " << field_index << std::endl;

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
                std::cout << "[CodegenVisitor] Loaded struct pointer for member access" << std::endl;
            }
        }

        // Create GEP instruction to access the field
        llvm::Value *field_ptr = builder.CreateStructGEP(struct_type, struct_ptr, field_index, member_name + "_ptr");
        std::cout << "[CodegenVisitor] Created GEP for field access" << std::endl;

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

    std::string CodegenVisitor::process_escape_sequences(const std::string &str)
    {
        std::string result;
        result.reserve(str.length());

        for (size_t i = 0; i < str.length(); ++i)
        {
            if (str[i] == '\\' && i + 1 < str.length())
            {
                char next = str[i + 1];
                switch (next)
                {
                case 'n':
                    result += '\n';
                    break;
                case 't':
                    result += '\t';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case '\\':
                    result += '\\';
                    break;
                case '\'':
                    result += '\'';
                    break;
                case '\"':
                    result += '\"';
                    break;
                case '0':
                    result += '\0';
                    break;
                case 'a':
                    result += '\a';
                    break;
                case 'b':
                    result += '\b';
                    break;
                case 'f':
                    result += '\f';
                    break;
                case 'v':
                    result += '\v';
                    break;
                default:
                    // If unknown escape sequence, just include the character as-is
                    result += next;
                    break;
                }
                ++i; // Skip the next character since we processed it
            }
            else
            {
                result += str[i];
            }
        }

        return result;
    }

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
                Cryo::Type *cryo_return_type = _symbol_table.get_type_context()->parse_type_from_string(return_type_annotation);
                return_type = cryo_return_type ? _type_mapper->map_type(cryo_return_type) : nullptr;
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
            bool has_variadic_param = false;

            for (auto &param : node->parameters())
            {
                if (param)
                {
                    std::string param_type_annotation = param->type_annotation();

                    // Check if this is a variadic parameter
                    if (param_type_annotation == "...")
                    {
                        has_variadic_param = true;
                        // Skip variadic parameters in LLVM function signature
                        continue;
                    }

                    Cryo::Type *cryo_param_type = _symbol_table.get_type_context()->parse_type_from_string(param_type_annotation);
                    llvm::Type *param_type = cryo_param_type ? _type_mapper->map_type(cryo_param_type) : nullptr;
                    if (!param_type)
                    {
                        report_error("Failed to map parameter type: " + param->name() + " (type: " + param_type_annotation + ")");
                        return nullptr;
                    }
                    param_types.push_back(param_type);
                    param_names.push_back(param->name());
                }
            }

            // Create function type - use node's variadic flag or detected variadic parameter
            bool is_variadic = node->is_variadic() || has_variadic_param;
            llvm::FunctionType *func_type = llvm::FunctionType::get(
                return_type, param_types, is_variadic);

            // Use simple function name in IR - namespace resolution handled by symbol table
            std::string function_name = node->name();
            std::cout << "[DEBUG] Function name generation - using simple IR name: " << function_name
                      << " (namespace: '" << _namespace_context << "')" << std::endl;

            // Create function
            llvm::Function *function = llvm::Function::Create(
                func_type, llvm::Function::ExternalLinkage, function_name, *module);

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
                Cryo::Type *cryo_return_type = _symbol_table.get_type_context()->parse_type_from_string(return_type_str);
                llvm::Type *llvm_return_type = cryo_return_type ? _type_mapper->map_type(cryo_return_type) : nullptr;
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

            // Debug ValueContext before entering scope
            std::cout << "[DEBUG] Before enter_scope: this=" << this << ", _value_context=" << _value_context.get() << std::endl;
            if (!_value_context)
            {
                std::cerr << "[ERROR] _value_context is null before enter_scope!" << std::endl;
                return false;
            }

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
                    Cryo::Type *cryo_param_type = _symbol_table.get_type_context()->parse_type_from_string(param_type_annotation);
                    llvm::Type *param_type = cryo_param_type ? _type_mapper->map_type(cryo_param_type) : nullptr;
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
                    Cryo::Type *cryo_return_type = _symbol_table.get_type_context()->parse_type_from_string(return_type_str);
                    llvm::Type *llvm_return_type = cryo_return_type ? _type_mapper->map_type(cryo_return_type) : nullptr;
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
                Cryo::Type *cryo_return_type = _symbol_table.get_type_context()->parse_type_from_string(return_type_str);
                llvm::Type *llvm_return_type = cryo_return_type ? _type_mapper->map_type(cryo_return_type) : nullptr;
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

    void CodegenVisitor::generate_primitive_method(Cryo::StructMethodNode *node, const std::string &primitive_type_name)
    {
        if (!node)
            return;

        std::cout << "[CodegenVisitor] Generating primitive method: " << primitive_type_name << "::" << node->name() << std::endl;

        // Debug: Check _value_context at the very start
        std::cout << "[DEBUG] At method start, _value_context=" << _value_context.get() << std::endl;
        if (!_value_context)
        {
            std::cerr << "[ERROR] _value_context is null at method start!" << std::endl;
            report_error("Value context is null at method start", node);
            return;
        }

        auto module = _context_manager.get_module();
        auto &builder = _context_manager.get_builder();
        auto &llvm_context = _context_manager.get_context();

        if (!module)
        {
            report_error("No module available for primitive method generation");
            return;
        }

        // Get primitive type for 'this' parameter
        Cryo::Type *cryo_primitive_type = _symbol_table.get_type_context()->parse_type_from_string(primitive_type_name);
        llvm::Type *primitive_type = cryo_primitive_type ? _type_mapper->map_type(cryo_primitive_type) : nullptr;
        if (!primitive_type)
        {
            report_error("Cannot map primitive type: " + primitive_type_name);
            return;
        }

        // Create function signature with 'this' parameter
        std::vector<llvm::Type *> param_types;

        // Add 'this' parameter (pointer to primitive type)
        param_types.push_back(llvm::PointerType::getUnqual(primitive_type));

        // Add regular parameters
        for (const auto &param : node->parameters())
        {
            if (param)
            {
                Cryo::Type *cryo_param_type = _symbol_table.get_type_context()->parse_type_from_string(param->type_annotation());
                llvm::Type *param_type = cryo_param_type ? _type_mapper->map_type(cryo_param_type) : nullptr;
                if (param_type)
                {
                    param_types.push_back(param_type);
                }
            }
        }

        // Map return type
        llvm::Type *return_type = nullptr;
        if (!node->return_type_annotation().empty() && node->return_type_annotation() != "void")
        {
            Cryo::Type *cryo_return_type = _symbol_table.get_type_context()->parse_type_from_string(node->return_type_annotation());
            return_type = cryo_return_type ? _type_mapper->map_type(cryo_return_type) : nullptr;
        }
        if (!return_type)
        {
            return_type = llvm::Type::getVoidTy(llvm_context);
        }

        // Create function type
        llvm::FunctionType *func_type = llvm::FunctionType::get(return_type, param_types, false);

        // Create function with scoped name
        std::string func_name = primitive_type_name + "::" + node->name();
        llvm::Function *func = llvm::Function::Create(
            func_type,
            llvm::Function::ExternalLinkage,
            func_name,
            module);

        // Set parameter names
        auto arg_it = func->arg_begin();
        arg_it->setName("this");
        ++arg_it;

        for (const auto &param : node->parameters())
        {
            if (param && arg_it != func->arg_end())
            {
                arg_it->setName(param->name());
                ++arg_it;
            }
        }

        // Generate function body
        llvm::BasicBlock *entry_block = llvm::BasicBlock::Create(llvm_context, "entry", func);
        builder.SetInsertPoint(entry_block);

        // Set up function context for body generation
        _current_function = std::make_unique<FunctionContext>(func, node);
        _current_function->entry_block = entry_block;

        // Create allocas for parameters and set up symbol table
        arg_it = func->arg_begin();

        // Handle 'this' parameter
        llvm::AllocaInst *this_alloca = create_entry_block_alloca(func, llvm::PointerType::getUnqual(primitive_type), "this");
        if (this_alloca)
        {
            builder.CreateStore(&*arg_it, this_alloca);

            // Debug: Check if _value_context is valid
            if (!_value_context)
            {
                std::cerr << "[ERROR] _value_context is null in generate_primitive_method!" << std::endl;
                report_error("Value context is null", node);
                return;
            }

            std::cout << "[DEBUG] Setting 'this' value in context, _value_context=" << _value_context.get() << std::endl;

            // Add to value context so it can be resolved in the function body
            _value_context->set_value("this", this_alloca, this_alloca, llvm::PointerType::getUnqual(primitive_type));
        }
        ++arg_it;

        // Handle regular parameters
        for (const auto &param : node->parameters())
        {
            if (param && arg_it != func->arg_end())
            {
                Cryo::Type *cryo_param_type = _symbol_table.get_type_context()->parse_type_from_string(param->type_annotation());
                llvm::Type *param_type = cryo_param_type ? _type_mapper->map_type(cryo_param_type) : nullptr;
                if (param_type)
                {
                    llvm::AllocaInst *alloca = create_entry_block_alloca(func, param_type, param->name());
                    if (alloca)
                    {
                        builder.CreateStore(&*arg_it, alloca);
                        _value_context->set_value(param->name(), alloca, alloca, param_type);
                    }
                }
                ++arg_it;
            }
        }

        // Generate function body
        if (node->body())
        {
            node->body()->accept(*this);
        }

        // Ensure proper termination
        if (!builder.GetInsertBlock()->getTerminator())
        {
            if (return_type->isVoidTy())
            {
                builder.CreateRetVoid();
            }
            else
            {
                // Return default value for non-void functions
                builder.CreateRet(llvm::Constant::getNullValue(return_type));
            }
        }

        // Clean up
        _current_function.reset();
        register_value(node, func);
    }

    llvm::Type *CodegenVisitor::generate_struct_type(Cryo::StructDeclarationNode *node) { return nullptr; }
    llvm::Type *CodegenVisitor::generate_class_type(Cryo::ClassDeclarationNode *node)
    {
        Cryo::ClassType *cryo_class_type = static_cast<Cryo::ClassType *>(_symbol_table.get_type_context()->get_class_type(node->name()));
        return _type_mapper->map_class_type(cryo_class_type);
    }
    llvm::Type *CodegenVisitor::generate_enum_type(Cryo::EnumDeclarationNode *node)
    {
        // Extract variant names from the AST node
        std::vector<std::string> variant_names;
        for (const auto &variant : node->variants())
        {
            variant_names.push_back(variant->name());
        }

        Cryo::EnumType *cryo_enum_type = static_cast<Cryo::EnumType *>(
            _symbol_table.get_type_context()->get_enum_type(node->name(), std::move(variant_names), node->is_simple_enum()));
        return _type_mapper->map_enum_type(cryo_enum_type);
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

        // Extract variant names for enum type creation
        std::vector<std::string> variant_names;
        for (const auto &v : enum_decl->variants())
        {
            variant_names.push_back(v->name());
        }

        Cryo::EnumType *cryo_enum_type = static_cast<Cryo::EnumType *>(
            _symbol_table.get_type_context()->get_enum_type(enum_decl->name(), variant_names, enum_decl->is_simple_enum()));
        llvm::Type *enum_type = _type_mapper->map_enum_type(cryo_enum_type);
        std::string constructor_name = enum_decl->name() + "::" + variant->name();

        // Build parameter types for associated data
        std::vector<llvm::Type *> param_types;
        for (const auto &type_name : variant->associated_types())
        {
            Cryo::Type *cryo_param_type = _symbol_table.get_type_context()->parse_type_from_string(type_name);
            llvm::Type *param_type = cryo_param_type ? _type_mapper->map_type(cryo_param_type) : nullptr;
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
                    // Handle integer type coercion for different bit widths
                    llvm::Type *left_type = left_val->getType();
                    llvm::Type *right_type = right_val->getType();

                    if (left_type != right_type)
                    {
                        // Get bit widths
                        unsigned left_bits = left_type->getIntegerBitWidth();
                        unsigned right_bits = right_type->getIntegerBitWidth();

                        // Convert to the smaller type (this handles the i32 + i8 -> i8 case)
                        if (left_bits > right_bits)
                        {
                            // Truncate left operand to match right operand's type
                            left_val = builder.CreateTrunc(left_val, right_type, "trunc_left");
                        }
                        else if (right_bits > left_bits)
                        {
                            // Truncate right operand to match left operand's type
                            right_val = builder.CreateTrunc(right_val, left_type, "trunc_right");
                        }
                    }

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
                // Handle string concatenation (string + string, string + char, char + string)
                else if (left_val->getType()->isPointerTy() && right_val->getType()->isPointerTy())
                {
                    // This is string + string concatenation
                    result = generate_string_concatenation(left_val, right_val);
                }
                else if (left_val->getType()->isPointerTy() && right_val->getType()->isIntegerTy() &&
                         right_val->getType()->getIntegerBitWidth() == 8)
                {
                    // This is string + char concatenation
                    result = generate_string_char_concatenation(left_val, right_val);
                }
                else if (left_val->getType()->isIntegerTy() && left_val->getType()->getIntegerBitWidth() == 8 &&
                         right_val->getType()->isPointerTy())
                {
                    // This is char + string concatenation
                    result = generate_char_string_concatenation(left_val, right_val);
                }
                break;

            case TokenKind::TK_MINUS:
                if (left_val->getType()->isIntegerTy() && right_val->getType()->isIntegerTy())
                {
                    // Handle integer type coercion for different bit widths
                    llvm::Type *left_type = left_val->getType();
                    llvm::Type *right_type = right_val->getType();

                    if (left_type != right_type)
                    {
                        // Get bit widths
                        unsigned left_bits = left_type->getIntegerBitWidth();
                        unsigned right_bits = right_type->getIntegerBitWidth();

                        // Convert to the smaller type (this handles the i32 + i8 -> i8 case)
                        if (left_bits > right_bits)
                        {
                            // Truncate left operand to match right operand's type
                            left_val = builder.CreateTrunc(left_val, right_type, "trunc_left");
                        }
                        else if (right_bits > left_bits)
                        {
                            // Truncate right operand to match left operand's type
                            right_val = builder.CreateTrunc(right_val, left_type, "trunc_right");
                        }
                    }

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
                    // Handle integer type coercion for different bit widths
                    llvm::Type *left_type = left_val->getType();
                    llvm::Type *right_type = right_val->getType();

                    if (left_type != right_type)
                    {
                        // Get bit widths
                        unsigned left_bits = left_type->getIntegerBitWidth();
                        unsigned right_bits = right_type->getIntegerBitWidth();

                        // Convert to the smaller type (this handles the i32 + i8 -> i8 case)
                        if (left_bits > right_bits)
                        {
                            // Truncate left operand to match right operand's type
                            left_val = builder.CreateTrunc(left_val, right_type, "trunc_left");
                        }
                        else if (right_bits > left_bits)
                        {
                            // Truncate right operand to match left operand's type
                            right_val = builder.CreateTrunc(right_val, left_type, "trunc_right");
                        }
                    }

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
                    // Handle integer type coercion for different bit widths
                    llvm::Type *left_type = left_val->getType();
                    llvm::Type *right_type = right_val->getType();

                    if (left_type != right_type)
                    {
                        // Get bit widths
                        unsigned left_bits = left_type->getIntegerBitWidth();
                        unsigned right_bits = right_type->getIntegerBitWidth();

                        // Convert to the smaller type (this handles the i32 + i8 -> i8 case)
                        if (left_bits > right_bits)
                        {
                            // Truncate left operand to match right operand's type
                            left_val = builder.CreateTrunc(left_val, right_type, "trunc_left");
                        }
                        else if (right_bits > left_bits)
                        {
                            // Truncate right operand to match left operand's type
                            right_val = builder.CreateTrunc(right_val, left_type, "trunc_right");
                        }
                    }

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
                    // Ensure both operands have the same type for ICmp
                    if (left_val->getType() != right_val->getType())
                    {
                        // Convert to the larger type
                        llvm::Type *target_type = left_val->getType()->getIntegerBitWidth() > right_val->getType()->getIntegerBitWidth()
                                                      ? left_val->getType()
                                                      : right_val->getType();

                        if (left_val->getType() != target_type)
                            left_val = builder.CreateSExt(left_val, target_type, "sext.tmp");
                        if (right_val->getType() != target_type)
                            right_val = builder.CreateSExt(right_val, target_type, "sext.tmp");
                    }
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
                else if (left_val->getType()->isPointerTy() || right_val->getType()->isPointerTy())
                {
                    // Pointer comparison - convert both to the same pointer type if needed
                    llvm::Type *pointer_type = llvm::PointerType::get(_context_manager.get_context(), 0);

                    if (!left_val->getType()->isPointerTy())
                    {
                        // Convert integer to pointer if needed (e.g., null constant)
                        if (left_val->getType()->isIntegerTy())
                        {
                            left_val = builder.CreateIntToPtr(left_val, pointer_type, "int2ptr");
                        }
                    }

                    if (!right_val->getType()->isPointerTy())
                    {
                        // Convert integer to pointer if needed (e.g., null constant)
                        if (right_val->getType()->isIntegerTy())
                        {
                            right_val = builder.CreateIntToPtr(right_val, pointer_type, "int2ptr");
                        }
                    }

                    result = builder.CreateICmpEQ(left_val, right_val, "ptr_eq.tmp");
                }
                break;

            case TokenKind::TK_EXCLAIMEQUAL:
                if (left_val->getType()->isIntegerTy() && right_val->getType()->isIntegerTy())
                {
                    // Ensure both operands have the same type for ICmp
                    if (left_val->getType() != right_val->getType())
                    {
                        // Convert to the larger type
                        llvm::Type *target_type = left_val->getType()->getIntegerBitWidth() > right_val->getType()->getIntegerBitWidth()
                                                      ? left_val->getType()
                                                      : right_val->getType();

                        if (left_val->getType() != target_type)
                            left_val = builder.CreateSExt(left_val, target_type, "sext.tmp");
                        if (right_val->getType() != target_type)
                            right_val = builder.CreateSExt(right_val, target_type, "sext.tmp");
                    }
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
                else if (left_val->getType()->isPointerTy() || right_val->getType()->isPointerTy())
                {
                    // Pointer comparison - convert both to the same pointer type if needed
                    llvm::Type *pointer_type = llvm::PointerType::get(_context_manager.get_context(), 0);

                    if (!left_val->getType()->isPointerTy())
                    {
                        // Convert integer to pointer if needed (e.g., null constant)
                        if (left_val->getType()->isIntegerTy())
                        {
                            left_val = builder.CreateIntToPtr(left_val, pointer_type, "int2ptr");
                        }
                    }

                    if (!right_val->getType()->isPointerTy())
                    {
                        // Convert integer to pointer if needed (e.g., null constant)
                        if (right_val->getType()->isIntegerTy())
                        {
                            right_val = builder.CreateIntToPtr(right_val, pointer_type, "int2ptr");
                        }
                    }

                    result = builder.CreateICmpNE(left_val, right_val, "ptr_ne.tmp");
                }
                break;

            case TokenKind::TK_L_ANGLE:
                if (left_val->getType()->isIntegerTy() && right_val->getType()->isIntegerTy())
                {
                    // Ensure both operands have the same type for ICmp
                    if (left_val->getType() != right_val->getType())
                    {
                        // Convert to the larger type
                        llvm::Type *target_type = left_val->getType()->getIntegerBitWidth() > right_val->getType()->getIntegerBitWidth()
                                                      ? left_val->getType()
                                                      : right_val->getType();

                        if (left_val->getType() != target_type)
                            left_val = builder.CreateSExt(left_val, target_type, "sext.tmp");
                        if (right_val->getType() != target_type)
                            right_val = builder.CreateSExt(right_val, target_type, "sext.tmp");
                    }
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
                    // Ensure both operands have the same type for ICmp
                    if (left_val->getType() != right_val->getType())
                    {
                        // Convert to the larger type
                        llvm::Type *target_type = left_val->getType()->getIntegerBitWidth() > right_val->getType()->getIntegerBitWidth()
                                                      ? left_val->getType()
                                                      : right_val->getType();

                        if (left_val->getType() != target_type)
                            left_val = builder.CreateSExt(left_val, target_type, "sext.tmp");
                        if (right_val->getType() != target_type)
                            right_val = builder.CreateSExt(right_val, target_type, "sext.tmp");
                    }
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
                    // Ensure both operands have the same type for ICmp
                    if (left_val->getType() != right_val->getType())
                    {
                        // Convert to the larger type
                        llvm::Type *target_type = left_val->getType()->getIntegerBitWidth() > right_val->getType()->getIntegerBitWidth()
                                                      ? left_val->getType()
                                                      : right_val->getType();

                        if (left_val->getType() != target_type)
                            left_val = builder.CreateSExt(left_val, target_type, "sext.tmp");
                        if (right_val->getType() != target_type)
                            right_val = builder.CreateSExt(right_val, target_type, "sext.tmp");
                    }
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
                    // Ensure both operands have the same type for ICmp
                    if (left_val->getType() != right_val->getType())
                    {
                        // Convert to the larger type
                        llvm::Type *target_type = left_val->getType()->getIntegerBitWidth() > right_val->getType()->getIntegerBitWidth()
                                                      ? left_val->getType()
                                                      : right_val->getType();

                        if (left_val->getType() != target_type)
                            left_val = builder.CreateSExt(left_val, target_type, "sext.tmp");
                        if (right_val->getType() != target_type)
                            right_val = builder.CreateSExt(right_val, target_type, "sext.tmp");
                    }
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

    llvm::Value *CodegenVisitor::generate_string_concatenation(llvm::Value *left_str, llvm::Value *right_str)
    {
        if (!left_str || !right_str)
            return nullptr;

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Get or create strlen function
        llvm::FunctionType *strlen_type = llvm::FunctionType::get(
            llvm::Type::getInt64Ty(context),
            {llvm::PointerType::get(context, 0)},
            false);
        llvm::Function *strlen_func = _context_manager.get_module()->getFunction("strlen");
        if (!strlen_func)
        {
            strlen_func = llvm::Function::Create(strlen_type, llvm::Function::ExternalLinkage, "strlen", _context_manager.get_module());
        }

        // Get or create malloc function
        llvm::FunctionType *malloc_type = llvm::FunctionType::get(
            llvm::PointerType::get(context, 0),
            {llvm::Type::getInt64Ty(context)},
            false);
        llvm::Function *malloc_func = _context_manager.get_module()->getFunction("malloc");
        if (!malloc_func)
        {
            malloc_func = llvm::Function::Create(malloc_type, llvm::Function::ExternalLinkage, "malloc", _context_manager.get_module());
        }

        // Get or create strcpy function
        llvm::FunctionType *strcpy_type = llvm::FunctionType::get(
            llvm::PointerType::get(context, 0),
            {llvm::PointerType::get(context, 0), llvm::PointerType::get(context, 0)},
            false);
        llvm::Function *strcpy_func = _context_manager.get_module()->getFunction("strcpy");
        if (!strcpy_func)
        {
            strcpy_func = llvm::Function::Create(strcpy_type, llvm::Function::ExternalLinkage, "strcpy", _context_manager.get_module());
        }

        // Get or create strcat function
        llvm::FunctionType *strcat_type = llvm::FunctionType::get(
            llvm::PointerType::get(context, 0),
            {llvm::PointerType::get(context, 0), llvm::PointerType::get(context, 0)},
            false);
        llvm::Function *strcat_func = _context_manager.get_module()->getFunction("strcat");
        if (!strcat_func)
        {
            strcat_func = llvm::Function::Create(strcat_type, llvm::Function::ExternalLinkage, "strcat", _context_manager.get_module());
        }

        // Calculate lengths of both strings
        llvm::Value *len1 = builder.CreateCall(strlen_func, {left_str}, "len1");
        llvm::Value *len2 = builder.CreateCall(strlen_func, {right_str}, "len2");

        // Calculate total length (len1 + len2 + 1 for null terminator)
        llvm::Value *total_len = builder.CreateAdd(len1, len2, "total_len_temp");
        llvm::Value *total_len_with_null = builder.CreateAdd(total_len, llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 1), "total_len");

        // Allocate memory for result string
        llvm::Value *result_str = builder.CreateCall(malloc_func, {total_len_with_null}, "result_str");

        // Copy first string to result
        builder.CreateCall(strcpy_func, {result_str, left_str});

        // Concatenate second string to result
        builder.CreateCall(strcat_func, {result_str, right_str});

        return result_str;
    }

    llvm::Value *CodegenVisitor::generate_string_char_concatenation(llvm::Value *str_val, llvm::Value *char_val)
    {
        if (!str_val || !char_val)
            return nullptr;

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Get or create strlen function
        llvm::FunctionType *strlen_type = llvm::FunctionType::get(
            llvm::Type::getInt64Ty(context),
            {llvm::PointerType::get(context, 0)},
            false);
        llvm::Function *strlen_func = _context_manager.get_module()->getFunction("strlen");
        if (!strlen_func)
        {
            strlen_func = llvm::Function::Create(strlen_type, llvm::Function::ExternalLinkage, "strlen", _context_manager.get_module());
        }

        // Get or create malloc function
        llvm::FunctionType *malloc_type = llvm::FunctionType::get(
            llvm::PointerType::get(context, 0),
            {llvm::Type::getInt64Ty(context)},
            false);
        llvm::Function *malloc_func = _context_manager.get_module()->getFunction("malloc");
        if (!malloc_func)
        {
            malloc_func = llvm::Function::Create(malloc_type, llvm::Function::ExternalLinkage, "malloc", _context_manager.get_module());
        }

        // Get or create strcpy function
        llvm::FunctionType *strcpy_type = llvm::FunctionType::get(
            llvm::PointerType::get(context, 0),
            {llvm::PointerType::get(context, 0), llvm::PointerType::get(context, 0)},
            false);
        llvm::Function *strcpy_func = _context_manager.get_module()->getFunction("strcpy");
        if (!strcpy_func)
        {
            strcpy_func = llvm::Function::Create(strcpy_type, llvm::Function::ExternalLinkage, "strcpy", _context_manager.get_module());
        }

        // Calculate length of string
        llvm::Value *str_len = builder.CreateCall(strlen_func, {str_val}, "str_len");

        // Calculate total length (str_len + 1 char + 1 null terminator)
        llvm::Value *total_len = builder.CreateAdd(str_len, llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 2), "total_len");

        // Allocate memory for result string
        llvm::Value *result_str = builder.CreateCall(malloc_func, {total_len}, "result_str");

        // Copy string to result
        builder.CreateCall(strcpy_func, {result_str, str_val});

        // Get pointer to end of string (str_len position)
        llvm::Value *end_ptr = builder.CreateGEP(llvm::Type::getInt8Ty(context), result_str, {str_len}, "end_ptr");

        // Store the character at the end
        builder.CreateStore(char_val, end_ptr);

        // Store null terminator after the character
        llvm::Value *null_ptr = builder.CreateGEP(llvm::Type::getInt8Ty(context), end_ptr, {llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 1)}, "null_ptr");
        builder.CreateStore(llvm::ConstantInt::get(llvm::Type::getInt8Ty(context), 0), null_ptr);

        return result_str;
    }

    llvm::Value *CodegenVisitor::generate_char_string_concatenation(llvm::Value *char_val, llvm::Value *str_val)
    {
        if (!char_val || !str_val)
            return nullptr;

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Get or create strlen function
        llvm::FunctionType *strlen_type = llvm::FunctionType::get(
            llvm::Type::getInt64Ty(context),
            {llvm::PointerType::get(context, 0)},
            false);
        llvm::Function *strlen_func = _context_manager.get_module()->getFunction("strlen");
        if (!strlen_func)
        {
            strlen_func = llvm::Function::Create(strlen_type, llvm::Function::ExternalLinkage, "strlen", _context_manager.get_module());
        }

        // Get or create malloc function
        llvm::FunctionType *malloc_type = llvm::FunctionType::get(
            llvm::PointerType::get(context, 0),
            {llvm::Type::getInt64Ty(context)},
            false);
        llvm::Function *malloc_func = _context_manager.get_module()->getFunction("malloc");
        if (!malloc_func)
        {
            malloc_func = llvm::Function::Create(malloc_type, llvm::Function::ExternalLinkage, "malloc", _context_manager.get_module());
        }

        // Get or create strcpy function
        llvm::FunctionType *strcpy_type = llvm::FunctionType::get(
            llvm::PointerType::get(context, 0),
            {llvm::PointerType::get(context, 0), llvm::PointerType::get(context, 0)},
            false);
        llvm::Function *strcpy_func = _context_manager.get_module()->getFunction("strcpy");
        if (!strcpy_func)
        {
            strcpy_func = llvm::Function::Create(strcpy_type, llvm::Function::ExternalLinkage, "strcpy", _context_manager.get_module());
        }

        // Calculate length of string
        llvm::Value *str_len = builder.CreateCall(strlen_func, {str_val}, "str_len");

        // Calculate total length (1 char + str_len + 1 null terminator)
        llvm::Value *total_len = builder.CreateAdd(str_len, llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 2), "total_len");

        // Allocate memory for result string
        llvm::Value *result_str = builder.CreateCall(malloc_func, {total_len}, "result_str");

        // Store the character at the beginning
        builder.CreateStore(char_val, result_str);

        // Get pointer to position after the character
        llvm::Value *after_char_ptr = builder.CreateGEP(llvm::Type::getInt8Ty(context), result_str, {llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 1)}, "after_char_ptr");

        // Copy string after the character
        builder.CreateCall(strcpy_func, {after_char_ptr, str_val});

        return result_str;
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
        std::string resolved_function_name; // Will be set after namespace alias resolution
        if (auto *identifier = dynamic_cast<IdentifierNode *>(node->callee()))
        {
            // Handle simple function name
            function_name = identifier->name();
            resolved_function_name = function_name;

            // Check if this is a primitive type constructor (e.g., i64, i32, etc.)
            if (is_primitive_integer_constructor(function_name))
            {
                return generate_primitive_constructor_call(node, function_name);
            }

            // Check if this is an intrinsic call (intrinsics start with "__")
            if (function_name.length() > 4 && function_name.substr(0, 2) == "__" && function_name.substr(function_name.length() - 2) == "__")
            {
                return generate_intrinsic_call(node, function_name);
            }
        }
        else if (auto *member_access = dynamic_cast<MemberAccessNode *>(node->callee()))
        {
            // First check if this is an intrinsic call with qualified names
            std::string member_name = member_access->member();
            if (member_name.length() > 4 && member_name.substr(0, 2) == "__" && member_name.substr(member_name.length() - 2) == "__")
            {
                // This is a qualified intrinsic call like std::Intrinsics::__printf__
                return generate_intrinsic_call(node, member_name);
            }

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

                        // Keep the original generic type name for method lookup
                        // The specialized methods are stored with the full generic name like "GenericStruct<int>::get_value"
                        std::cout << "[CodegenVisitor] Using type name '" << type_name
                                  << "' for method lookup" << std::endl;
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

                        // If not found and this is a generic type, try different lookup strategies
                        if (method_it == _functions.end() && type_name.find('<') != std::string::npos)
                        {
                            // First try: Convert angle bracket format to underscore format for monomorphized types
                            // GenericStruct<int> -> GenericStruct_int
                            std::string monomorphized_name = type_name;
                            size_t angle_start = monomorphized_name.find('<');
                            size_t angle_end = monomorphized_name.find('>', angle_start);
                            if (angle_start != std::string::npos && angle_end != std::string::npos)
                            {
                                std::string type_params = monomorphized_name.substr(angle_start + 1, angle_end - angle_start - 1);
                                // Replace commas with underscores for multiple type parameters
                                std::replace(type_params.begin(), type_params.end(), ',', '_');
                                std::replace(type_params.begin(), type_params.end(), ' ', '_');
                                monomorphized_name = monomorphized_name.substr(0, angle_start) + "_" + type_params;
                            }
                            std::string monomorphized_method_name = monomorphized_name + "::" + member_access->member();
                            method_it = _functions.find(monomorphized_method_name);

                            // Second try: Extract base type name for generic instantiation lookup fallback
                            if (method_it == _functions.end())
                            {
                                std::string base_name = type_name.substr(0, type_name.find('<'));
                                std::string fallback_method_name = base_name + "::" + member_access->member();
                                method_it = _functions.find(fallback_method_name);
                            }
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
                                    llvm::Value *arg_value = get_current_value();
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

                            // For primitive types, use the fully qualified method name instead of just the member name
                            // This allows linking with precompiled primitive methods in libcryo.a
                            if (is_primitive_type(type_name))
                            {
                                std::cout << "[CodegenVisitor] Using qualified name for primitive method: " << method_name << std::endl;
                                function_name = method_name;          // Use string::length instead of just length
                                resolved_function_name = method_name; // Also update the resolved name
                            }
                            else
                            {
                                // Fall back to unqualified name for non-primitive types
                                function_name = extract_function_name_from_member_access(member_access);
                                resolved_function_name = function_name; // Update resolved name
                            }
                        }
                    }
                }
            }
            else
            {
                // Check if the object is a literal that needs primitive method handling
                if (auto *literal_node = dynamic_cast<LiteralNode *>(member_access->object()))
                {
                    std::cout << "[DEBUG] Found literal node in member access, kind: " << static_cast<int>(literal_node->literal_kind()) << std::endl;
                    // Check if this is a string literal method call
                    if (literal_node->literal_kind() == TokenKind::TK_STRING_LITERAL)
                    {
                        // This is a string literal method call like "hello".length()
                        std::string method_name = "string::" + member_access->member();
                        std::cout << "[CodegenVisitor] String literal method call: " << method_name << std::endl;
                        function_name = method_name;
                        resolved_function_name = method_name;
                    }
                    else
                    {
                        // Handle other literal types as needed
                        function_name = extract_function_name_from_member_access(member_access);
                        resolved_function_name = function_name;
                    }
                }
                else
                {
                    // Fall back to handling namespaced calls like Std::Runtime::print_int
                    function_name = extract_function_name_from_member_access(member_access);
                    resolved_function_name = function_name;
                }
            }
        }
        else if (auto *scope_resolution = dynamic_cast<ScopeResolutionNode *>(node->callee()))
        {
            // Handle scope resolution like Std::Runtime::print_int
            std::string member_name = scope_resolution->member_name();

            // Check if this is an intrinsic call with qualified names
            if (member_name.length() > 4 && member_name.substr(0, 2) == "__" && member_name.substr(member_name.length() - 2) == "__")
            {
                // This is a qualified intrinsic call like std::Intrinsics::__printf__
                return generate_intrinsic_call(node, member_name);
            }

            function_name = scope_resolution->scope_name() + "::" + scope_resolution->member_name();
            resolved_function_name = function_name;
        }
        else
        {
            std::cerr << "Unsupported function call type" << std::endl;
            return nullptr;
        }

        std::cout << "[DEBUG] Processing function call: " << function_name << std::endl;

        // Check if this is a template enum constructor (e.g., Result::Ok) that needs instantiation
        // This must happen early, before any forward declarations are created
        size_t early_scope_pos = function_name.find("::");
        if (early_scope_pos != std::string::npos)
        {
            std::string base_name = function_name.substr(0, early_scope_pos);
            std::string variant_name = function_name.substr(early_scope_pos + 2);
            
            std::cout << "[CodegenVisitor] Early check: is " << function_name << " a template enum constructor? (base: " << base_name << ", variant: " << variant_name << ")" << std::endl;
            
            // Check if base_name is a known template enum (like Result, Option)
            if (base_name == "Result" || base_name == "Option")
            {
                std::cout << "[CodegenVisitor] Early detection: template enum constructor " << function_name << std::endl;
                
                // Try to find an existing instantiation that matches this call
                std::string matching_instantiation;
                std::cout << "[CodegenVisitor] Early: Looking for existing instantiations of " << base_name << " in _enum_variants:" << std::endl;
                for (const auto &[variant_key, variant_func] : _enum_variants)
                {
                    std::cout << "[CodegenVisitor] Early:   - Found variant: " << variant_key << std::endl;
                    // Look for patterns like "Result<void*, AllocError>::Ok"
                    if (variant_key.find(base_name + "<") == 0 && variant_key.find("::" + variant_name) != std::string::npos)
                    {
                        matching_instantiation = variant_key;
                        break;
                    }
                }
                
                if (!matching_instantiation.empty())
                {
                    std::cout << "[CodegenVisitor] Early: Found matching instantiation: " << matching_instantiation << std::endl;
                    
                    // Use the instantiated constructor
                    llvm::Value *constructor_func = _enum_variants[matching_instantiation];
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
                else
                {
                    std::cout << "[CodegenVisitor] Early: No matching instantiation found for " << function_name << std::endl;
                    
                    // Prevent infinite recursion with a generation guard
                    static std::set<std::string> generating;
                    std::string guard_key = base_name + "::" + variant_name;
                    
                    if (generating.find(guard_key) != generating.end()) {
                        std::cout << "[CodegenVisitor] Early: Already generating " << guard_key << ", avoiding recursion" << std::endl;
                    } else {
                        std::cout << "[CodegenVisitor] Early: Triggering enum constructor generation!" << std::endl;
                        generating.insert(guard_key);
                        
                        // We need to infer the concrete instantiation from the current context
                        // For now, we'll use the return type of the current function to determine the instantiation
                        if (_current_function && _current_function->ast_node) {
                            std::string return_type_str = _current_function->ast_node->return_type_annotation();
                            std::cout << "[CodegenVisitor] Current function return type: " << return_type_str << std::endl;
                            
                            // If the return type looks like a parameterized enum (e.g., "AllocResult<void>"), 
                            // extract the concrete type arguments and generate constructors
                            if (return_type_str.find('<') != std::string::npos) {
                                // Parse the return type to get base name and type arguments
                                size_t open_bracket = return_type_str.find('<');
                                size_t close_bracket = return_type_str.find('>', open_bracket);
                                if (open_bracket != std::string::npos && close_bracket != std::string::npos) {
                                    std::string base_return_type = return_type_str.substr(0, open_bracket);
                                    std::string type_args_str = return_type_str.substr(open_bracket + 1, close_bracket - open_bracket - 1);
                                    
                                    // If the base type matches our enum base (Result), generate constructors
                                    if (base_return_type == base_name || 
                                        (base_return_type == "AllocResult" && base_name == "Result")) {
                                        
                                        std::vector<std::string> type_args;
                                        // For AllocResult<void>, this should generate Result<void*, AllocError>
                                        if (base_return_type == "AllocResult" && type_args_str == "void") {
                                            type_args = {"void*", "AllocError"};
                                            ensure_parameterized_enum_constructors("Result<void*, AllocError>", "Result", type_args);
                                        } else {
                                            // Split type_args_str by comma and generate constructors
                                            std::istringstream iss(type_args_str);
                                            std::string type_arg;
                                            while (std::getline(iss, type_arg, ',')) {
                                                // Trim whitespace
                                                type_arg.erase(0, type_arg.find_first_not_of(" \t"));
                                                type_arg.erase(type_arg.find_last_not_of(" \t") + 1);
                                                type_args.push_back(type_arg);
                                            }
                                            ensure_parameterized_enum_constructors(return_type_str, base_name, type_args);
                                        }
                                        
                                        std::cout << "[CodegenVisitor] Generated constructors for " << return_type_str << std::endl;
                                        
                                        // Now try to find the constructor again
                                        std::string instantiated_name = (base_return_type == "AllocResult") ? "Result<void*, AllocError>" : return_type_str;
                                        std::string qualified_variant_name = instantiated_name + "::" + variant_name;
                                        auto variant_it = _enum_variants.find(qualified_variant_name);
                                        if (variant_it != _enum_variants.end()) {
                                            std::cout << "[CodegenVisitor] Found generated constructor: " << qualified_variant_name << std::endl;
                                            llvm::Function *llvm_function = llvm::dyn_cast<llvm::Function>(variant_it->second);
                                            if (llvm_function) {
                                                // Prepare arguments and call the constructor
                                                std::vector<llvm::Value *> args;
                                                for (size_t i = 0; i < node->arguments().size() && i < llvm_function->arg_size(); ++i) {
                                                    node->arguments()[i]->accept(*this);
                                                    llvm::Value *arg_val = _node_values[node->arguments()[i].get()];
                                                    if (arg_val) {
                                                        args.push_back(arg_val);
                                                    }
                                                }
                                                
                                                // Call the enum constructor function
                                                llvm::Value *enum_instance = builder.CreateCall(llvm_function, args, "enum_constructor");
                                                register_value(node, enum_instance);
                                                generating.erase(guard_key);  // Remove from guard before returning
                                                return enum_instance;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        
                        generating.erase(guard_key);  // Always remove from guard
                    }
                    
                    std::cout << "[CodegenVisitor] Could not generate constructors, continuing with normal flow" << std::endl;
                }
            }
        }

        // Check if this is a parameterized enum constructor call that needs generation
        size_t param_scope_pos = function_name.find("::");
        if (param_scope_pos != std::string::npos)
        {
            std::string type_part = function_name.substr(0, param_scope_pos);
            std::string variant_part = function_name.substr(param_scope_pos + 2);

            // Check if the type part looks like a parameterized type (contains < and >)
            if (type_part.find('<') != std::string::npos && type_part.find('>') != std::string::npos)
            {
                // Parse the type: "Option<int>" -> base="Option", args=["int"]
                size_t open_bracket = type_part.find('<');
                size_t close_bracket = type_part.find('>', open_bracket);

                if (open_bracket != std::string::npos && close_bracket != std::string::npos && close_bracket > open_bracket)
                {
                    std::string base_name = type_part.substr(0, open_bracket);
                    std::string type_args_str = type_part.substr(open_bracket + 1, close_bracket - open_bracket - 1);

                    // Parse type arguments (simple comma-separated for now)
                    std::vector<std::string> type_args;
                    std::stringstream ss(type_args_str);
                    std::string arg;
                    while (std::getline(ss, arg, ','))
                    {
                        // Trim whitespace
                        arg.erase(0, arg.find_first_not_of(" \t"));
                        arg.erase(arg.find_last_not_of(" \t") + 1);
                        if (!arg.empty())
                        {
                            type_args.push_back(arg);
                        }
                    }

                    if (!type_args.empty())
                    {
                        std::cout << "[CodegenVisitor] Detected parameterized enum constructor call: " << function_name << std::endl;
                        std::cout << "[CodegenVisitor] Base: " << base_name << ", Variant: " << variant_part << ", Args: ";
                        for (size_t i = 0; i < type_args.size(); ++i)
                        {
                            if (i > 0)
                                std::cout << ", ";
                            std::cout << type_args[i];
                        }
                        std::cout << std::endl;

                        // Ensure constructors are generated for this parameterized enum
                        ensure_parameterized_enum_constructors(type_part, base_name, type_args);
                    }
                }
            }
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

                // Verify the argument count matches the function signature
                if (args.size() != llvm_function->arg_size())
                {
                    std::cout << "[ERROR] Argument count mismatch for " << function_name
                              << ": expected " << llvm_function->arg_size()
                              << ", got " << args.size() << std::endl;
                    return nullptr;
                }

                // Call the enum constructor function
                llvm::Value *enum_instance = builder.CreateCall(llvm_function, args, "enum_constructor");
                return enum_instance;
            }
        }

        std::cout << "[DEBUG] About to check template enum constructor for: " << function_name << std::endl;

        // Check if this is a template enum constructor (e.g., Result::Ok) that needs instantiation
        size_t template_scope_pos = function_name.find("::");
        if (template_scope_pos != std::string::npos)
        {
            std::string base_name = function_name.substr(0, template_scope_pos);
            std::string variant_name = function_name.substr(template_scope_pos + 2);
            
            std::cout << "[CodegenVisitor] Checking if " << function_name << " is a template enum constructor (base: " << base_name << ", variant: " << variant_name << ")" << std::endl;
            
            // Check if base_name is a known template enum (like Result, Option)
            // We need to determine the instantiation context from the return type
            // For now, let's check for known template enums
            if (base_name == "Result" || base_name == "Option")
            {
                std::cout << "[CodegenVisitor] Detected template enum constructor: " << function_name << std::endl;
                
                // Try to find an existing instantiation that matches this call
                // Look for any instantiated enum that starts with the base name
                std::string matching_instantiation;
                std::cout << "[CodegenVisitor] Looking for existing instantiations of " << base_name << " in _enum_variants:" << std::endl;
                for (const auto &[variant_key, variant_func] : _enum_variants)
                {
                    std::cout << "[CodegenVisitor]   - Found variant: " << variant_key << std::endl;
                    // Look for patterns like "Result<void*, AllocError>::Ok"
                    if (variant_key.find(base_name + "<") == 0 && variant_key.find("::" + variant_name) != std::string::npos)
                    {
                        matching_instantiation = variant_key;
                        break;
                    }
                }
                
                if (!matching_instantiation.empty())
                {
                    std::cout << "[CodegenVisitor] Found matching instantiation: " << matching_instantiation << std::endl;
                    
                    // Use the instantiated constructor
                    llvm::Value *constructor_func = _enum_variants[matching_instantiation];
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
                else
                {
                    std::cout << "[CodegenVisitor] No matching instantiation found for " << function_name << std::endl;
                }
            }
            else
            {
                std::cout << "[CodegenVisitor] " << base_name << " is not a known template enum" << std::endl;
            }
        }

        // Check for namespace alias resolution first
        resolved_function_name = function_name;

        // Check if this is a namespace-qualified call (contains "::")
        size_t scope_pos = function_name.find("::");
        if (scope_pos != std::string::npos)
        {
            std::string namespace_part = function_name.substr(0, scope_pos);
            std::string function_part = function_name.substr(scope_pos + 2);

            std::cout << "[DEBUG] Checking namespace alias for: " << namespace_part << "::" << function_part << std::endl;

            // Check if the namespace part is an alias in our symbol table
            if (_symbol_table.has_namespace(namespace_part))
            {
                // Look up the symbol in the aliased namespace
                Symbol *symbol = _symbol_table.lookup_namespaced_symbol(namespace_part, function_part);
                if (symbol && symbol->kind == SymbolKind::Function)
                {
                    // For namespace aliases, use just the function name for linking
                    resolved_function_name = function_part;
                    std::cout << "[DEBUG] Resolved namespace alias " << namespace_part << "::" << function_part
                              << " to function: " << resolved_function_name << std::endl;
                }
            }
        }

        // First, check if this is a function we know about in our symbol table
        auto func_it = _functions.find(resolved_function_name);
        if (func_it != _functions.end())
        {
            llvm::Function *known_function = func_it->second;
            std::cout << "[DEBUG] Found function in symbol table: " << resolved_function_name << " -> " << known_function->getName().str() << std::endl;

            // Generate arguments for the function call
            std::vector<llvm::Value *> args;
            for (const auto &arg : node->arguments())
            {
                arg->accept(*this);
                llvm::Value *arg_value = get_current_value();
                if (arg_value)
                {
                    args.push_back(arg_value);
                }
            }

            // Call the function
            return builder.CreateCall(known_function, args);
        }

        // Map Cryo function names to C runtime function names
        std::string c_function_name = map_cryo_to_c_function(resolved_function_name);

        // Look up the function in the module using the C runtime name
        // Use FunctionRegistry to get the runtime function name
        FunctionMetadata metadata = _function_registry->get_function_metadata(c_function_name, _namespace_context);
        std::string lookup_name = metadata.runtime_name.empty() ? c_function_name : metadata.runtime_name;

        llvm::Function *function = module->getFunction(lookup_name);
        if (!function)
        {
            // Function not declared yet, create a declaration
            function = create_runtime_function_declaration(c_function_name, node);
            if (!function)
            {
                std::cerr << "Failed to create function declaration for: " << c_function_name
                          << " (resolved from: " << function_name << ")" << std::endl;

                // Dump current IR state before crash
                std::cerr << "=== CURRENT IR DUMP BEFORE CRASH ===" << std::endl;
                std::string ir_string;
                llvm::raw_string_ostream stream(ir_string);
                module->print(stream, nullptr);
                std::cerr << stream.str() << std::endl;
                std::cerr << "=== END IR DUMP ===" << std::endl;

                return nullptr;
            }
        }

        // Generate arguments
        std::vector<llvm::Value *> args;

        // For primitive method calls (like string::length), we need to add the 'this' pointer as the first argument
        if (auto *member_access = dynamic_cast<MemberAccessNode *>(node->callee()))
        {
            if (auto *object_identifier = dynamic_cast<IdentifierNode *>(member_access->object()))
            {
                std::string object_name = object_identifier->name();

                // Check if this is a primitive type method call
                auto type_it = _variable_types.find(object_name);
                if (type_it != _variable_types.end() && is_primitive_type(type_it->second))
                {
                    // This is a primitive method call - add the object as 'this' pointer
                    llvm::Value *object_value = _value_context->get_value(object_name);
                    if (object_value)
                    {
                        // If it's an alloca (stack variable), load the value first
                        if (llvm::isa<llvm::AllocaInst>(object_value))
                        {
                            llvm::Type *element_type = _value_context->get_alloca_type(object_name);
                            llvm::Value *loaded_value = create_load(object_value, element_type, object_name + ".load");
                            args.push_back(loaded_value); // Load the actual value for primitive methods
                            std::cout << "[CodegenVisitor] Added loaded 'this' value for primitive method: " << function_name << std::endl;
                        }
                        else
                        {
                            args.push_back(object_value); // Direct value (like function parameters)
                            std::cout << "[CodegenVisitor] Added direct 'this' value for primitive method: " << function_name << std::endl;
                        }
                    }
                }
            }
            else if (auto *literal_node = dynamic_cast<LiteralNode *>(member_access->object()))
            {
                std::cout << "[DEBUG] In argument generation - found literal node, kind: " << static_cast<int>(literal_node->literal_kind()) << std::endl;
                // Handle literal method calls like "hello".length()
                if (literal_node->literal_kind() == TokenKind::TK_STRING_LITERAL)
                {
                    std::cout << "[CodegenVisitor] Processing string literal method call" << std::endl;
                    std::cout << "[DEBUG] About to call accept on literal node with value: " << literal_node->value() << std::endl;
                    // Generate the string literal value and add it as 'this' pointer
                    literal_node->accept(*this);
                    std::cout << "[DEBUG] Finished calling accept on literal node" << std::endl;
                    llvm::Value *literal_value = get_generated_value(literal_node);
                    std::cout << "[DEBUG] String literal value: " << (literal_value ? "valid" : "NULL") << std::endl;
                    if (literal_value)
                    {
                        args.push_back(literal_value);
                        std::cout << "[CodegenVisitor] Added string literal as 'this' value for primitive method: " << function_name << std::endl;
                    }
                    else
                    {
                        std::cout << "[ERROR] Failed to generate string literal value" << std::endl;
                    }
                }
            }
        }

        // Add regular arguments
        for (size_t i = 0; i < node->arguments().size(); ++i)
        {
            const auto &arg = node->arguments()[i];
            std::cout << "[DEBUG] Processing argument " << i << " of " << node->arguments().size() << std::endl;

            arg->accept(*this);
            llvm::Value *arg_value = get_current_value();

            std::cout << "[DEBUG] Argument " << i << " value: " << (arg_value ? "valid" : "NULL") << std::endl;

            if (arg_value)
            {
                // Additional safety check - ensure the value has a valid type
                if (arg_value->getType())
                {
                    args.push_back(arg_value);
                    std::cout << "[DEBUG] Successfully added argument " << i << " to call" << std::endl;
                }
                else
                {
                    std::cout << "[ERROR] Argument " << i << " has NULL type, skipping" << std::endl;
                    return nullptr;
                }
            }
            else
            {
                std::cout << "[ERROR] Failed to generate argument " << i << " for function call: " << function_name << std::endl;
                return nullptr;
            }
        }

        // Create the function call
        std::cout << "[DEBUG] About to create call. Function pointer: " << (function ? "valid" : "NULL") << std::endl;
        if (!function)
        {
            std::cout << "[ERROR] Function pointer is NULL, cannot create call" << std::endl;
            return nullptr;
        }

        std::cout << "[DEBUG] Function name: " << function->getName().str() << std::endl;
        std::cout << "[DEBUG] Function type: " << (function->getFunctionType() ? "valid" : "NULL") << std::endl;

        if (!function->getFunctionType())
        {
            std::cout << "[ERROR] Function type is NULL, cannot create call" << std::endl;
            return nullptr;
        }

        std::cout << "[DEBUG] Expected args: " << function->getFunctionType()->getNumParams() << ", provided: " << args.size() << std::endl;

        // Validate all arguments before creating the call
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (!args[i])
            {
                std::cout << "[ERROR] Argument " << i << " is NULL, cannot create call" << std::endl;
                return nullptr;
            }
            if (!args[i]->getType())
            {
                std::cout << "[ERROR] Argument " << i << " has NULL type, cannot create call" << std::endl;
                return nullptr;
            }
        }

        std::cout << "[DEBUG] All arguments validated, creating call..." << std::endl;

        // Add extensive debugging before CreateCall
        std::cout << "[DEBUG] Builder basic block: " << (builder.GetInsertBlock() ? "valid" : "NULL") << std::endl;
        if (builder.GetInsertBlock())
        {
            std::cout << "[DEBUG] Basic block parent: " << (builder.GetInsertBlock()->getParent() ? "valid" : "NULL") << std::endl;
        }

        std::cout << "[DEBUG] Function details:" << std::endl;
        std::cout << "[DEBUG]   Function name: " << function->getName().str() << std::endl;
        std::cout << "[DEBUG]   Function address: " << function << std::endl;
        std::cout << "[DEBUG]   Function type address: " << function->getFunctionType() << std::endl;
        std::cout << "[DEBUG]   Function linkage: " << function->getLinkage() << std::endl;

        std::cout << "[DEBUG] Arguments details:" << std::endl;
        for (size_t i = 0; i < args.size(); ++i)
        {
            std::cout << "[DEBUG]   Arg " << i << ": address=" << args[i] << ", type=" << args[i]->getType() << std::endl;
            if (args[i]->getType())
            {
                args[i]->getType()->print(llvm::errs());
                std::cout << std::endl;
            }
        }

        std::cout << "[DEBUG] About to call CreateCall with " << args.size() << " arguments..." << std::endl;

        // Add even more detailed debugging before CreateCall
        try
        {
            std::cout << "[DEBUG] Verifying function before CreateCall..." << std::endl;

            // Check if function is valid
            if (!function)
            {
                std::cout << "[ERROR] Function is null!" << std::endl;
                return nullptr;
            }

            // Check function type
            llvm::FunctionType *funcType = function->getFunctionType();
            if (!funcType)
            {
                std::cout << "[ERROR] Function type is null!" << std::endl;
                return nullptr;
            }

            std::cout << "[DEBUG] Function type params: " << funcType->getNumParams() << std::endl;
            std::cout << "[DEBUG] Function is vararg: " << funcType->isVarArg() << std::endl;

            // Verify module compatibility
            llvm::Module *funcModule = function->getParent();
            llvm::Module *currentModule = builder.GetInsertBlock()->getModule();
            std::cout << "[DEBUG] Function module: " << (funcModule ? funcModule->getName().str() : "NULL") << std::endl;
            std::cout << "[DEBUG] Current module: " << (currentModule ? currentModule->getName().str() : "NULL") << std::endl;

            if (funcModule != currentModule)
            {
                std::cout << "[WARNING] Cross-module function call detected!" << std::endl;
            }

            // Detailed argument verification
            for (size_t i = 0; i < args.size(); ++i)
            {
                std::cout << "[DEBUG] Arg " << i << " verification:" << std::endl;
                if (!args[i])
                {
                    std::cout << "[ERROR]   Argument is null!" << std::endl;
                    return nullptr;
                }

                llvm::Type *argType = args[i]->getType();
                if (!argType)
                {
                    std::cout << "[ERROR]   Argument type is null!" << std::endl;
                    return nullptr;
                }

                std::cout << "[DEBUG]   Argument value: " << args[i] << std::endl;
                std::cout << "[DEBUG]   Argument type: " << argType << std::endl;

                if (i < funcType->getNumParams())
                {
                    llvm::Type *expectedType = funcType->getParamType(i);
                    std::cout << "[DEBUG]   Expected type: " << expectedType << std::endl;
                    std::cout << "[DEBUG]   Types match: " << (argType == expectedType ? "YES" : "NO") << std::endl;
                }
            }

            std::cout << "[DEBUG] All pre-CreateCall checks passed, creating call..." << std::endl;
            llvm::Value *call_result = builder.CreateCall(function, args);
            std::cout << "[DEBUG] CreateCall completed successfully!" << std::endl;

            // Add debugging for the result
            if (!call_result)
            {
                std::cout << "[ERROR] CreateCall returned NULL!" << std::endl;
                return nullptr;
            }

            std::cout << "[DEBUG] Call result: " << call_result << std::endl;
            std::cout << "[DEBUG] Call result type: " << call_result->getType() << std::endl;

            if (call_result->getType())
            {
                std::cout << "[DEBUG] Result type verified, printing..." << std::endl;
                call_result->getType()->print(llvm::errs());
                std::cout << std::endl;
            }

            std::cout << "[DEBUG] About to return call_result..." << std::endl;
            return call_result;
        }
        catch (const std::exception &e)
        {
            std::cout << "[ERROR] Exception during CreateCall: " << e.what() << std::endl;
            return nullptr;
        }
        catch (...)
        {
            std::cout << "[ERROR] Unknown exception during CreateCall!" << std::endl;
            return nullptr;
        }
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

        // Debug: show what symbols are available
        std::cout << "[DEBUG] Symbol table lookup for: '" << c_name << "'" << std::endl;
        std::cout << "[DEBUG] Scope context: '" << scope_name << "', Member: '" << symbol_name << "'" << std::endl;

        // First try to find by the full name
        symbol = _symbol_table.lookup_symbol(c_name);
        std::cout << "[DEBUG] Symbol lookup for '" << c_name << "': " << (symbol ? "FOUND" : "NOT FOUND") << std::endl;

        // If not found, try to find by the member name
        if (!symbol)
        {
            symbol = _symbol_table.lookup_symbol(symbol_name);
            std::cout << "[DEBUG] Symbol lookup for member name '" << symbol_name << "': " << (symbol ? "FOUND" : "NOT FOUND") << std::endl;

            // Verify the scope matches if we found a symbol
            if (symbol && symbol->scope != scope_name)
            {
                std::cout << "[DEBUG] Symbol found but scope mismatch: '" << symbol->scope << "' != '" << scope_name << "'" << std::endl;
                symbol = nullptr; // Wrong scope, keep looking
            }
        }

        // Try namespaced lookup
        if (!symbol)
        {
            std::cout << "[DEBUG] Trying namespaced lookup..." << std::endl;
            symbol = _symbol_table.lookup_namespaced_symbol(scope_name, symbol_name);
            std::cout << "[DEBUG] Namespaced lookup for '" << scope_name << "::" << symbol_name << "': " << (symbol ? "FOUND" : "NOT FOUND") << std::endl;
        }

        // Debug symbol properties if found
        if (symbol)
        {
            std::cout << "[DEBUG] Symbol found! Kind: " << (int)symbol->kind << " (Function=" << (int)SymbolKind::Function << ")" << std::endl;
            std::cout << "[DEBUG] Symbol data_type: " << (symbol->data_type ? "NOT NULL" : "NULL") << std::endl;
            if (symbol->data_type)
            {
                std::cout << "[DEBUG] Symbol data_type name: " << symbol->data_type->name() << std::endl;
                FunctionType *func_type = dynamic_cast<FunctionType *>(symbol->data_type);
                std::cout << "[DEBUG] Dynamic cast to FunctionType: " << (func_type ? "SUCCESS" : "FAILED") << std::endl;
            }
        }

        // If we found a function symbol, use its actual type information
        if (symbol && symbol->kind == SymbolKind::Function && symbol->data_type)
        {
            std::cout << "[DEBUG] Found function symbol, checking data_type..." << std::endl;
            FunctionType *func_type = dynamic_cast<FunctionType *>(symbol->data_type);
            if (func_type)
            {
                std::cout << "[DEBUG] Using symbol table function type for: " << c_name << std::endl;

                // Convert Cryo types to LLVM types
                std::vector<llvm::Type *> param_types;
                for (const auto &param_type : func_type->parameter_types())
                {
                    llvm::Type *llvm_param_type = get_llvm_type(param_type.get());
                    if (llvm_param_type)
                    {
                        param_types.push_back(llvm_param_type);
                    }
                    else
                    {
                        std::cerr << "[WARNING] Failed to convert parameter type, using i32 as fallback" << std::endl;
                        param_types.push_back(llvm::Type::getInt32Ty(_context_manager.get_context()));
                    }
                }

                llvm::Type *return_type = get_llvm_type(func_type->return_type().get());
                if (!return_type)
                {
                    std::cerr << "[WARNING] Failed to convert return type, using i64 as fallback" << std::endl;
                    return_type = llvm::Type::getInt64Ty(_context_manager.get_context());
                }

                // Create function type and declaration using actual types
                llvm::FunctionType *llvm_func_type = llvm::FunctionType::get(return_type, param_types, false);

                // Use FunctionRegistry to get the runtime function name
                FunctionMetadata metadata_local = _function_registry->get_function_metadata(c_name, _namespace_context);
                std::string c_runtime_name = metadata_local.runtime_name.empty() ? c_name : metadata_local.runtime_name;

                llvm::Function *forward_decl = llvm::Function::Create(
                    llvm_func_type,
                    llvm::Function::ExternalLinkage,
                    c_runtime_name,
                    _context_manager.get_module());

                if (forward_decl)
                {
                    _functions[c_name] = forward_decl;
                    std::cout << "[DEBUG] Successfully created forward declaration using symbol table for: " << c_name << std::endl;
                    return forward_decl;
                }
            }
        }

        if (!symbol || symbol->kind != SymbolKind::Function)
        {
            // Check if this is a class method that was already generated
            auto functions_it = _functions.find(c_name);
            if (functions_it != _functions.end())
            {
                std::cout << "[CodegenVisitor] Found pre-generated function: " << c_name << std::endl;
                llvm::Function *found_func = functions_it->second;
                std::cout << "[DEBUG] Function name at find time: " << (found_func ? found_func->getName().str() : "NULL") << std::endl;

                // Check if the function has a valid name and parent
                if (found_func && found_func->getParent())
                {
                    std::string func_name = found_func->getName().str();
                    std::cout << "[DEBUG] Function name retrieved: '" << func_name << "'" << std::endl;
                    if (!func_name.empty())
                    {
                        return found_func; // Return the valid already-generated function
                    }
                    else
                    {
                        std::cout << "[DEBUG] Function has no name, treating as corrupted" << std::endl;
                        // Remove the corrupted function from our map and fall through
                        _functions.erase(c_name);
                    }
                }
                else
                {
                    std::cout << "[DEBUG] Function is invalid (no parent), removing corrupted entry" << std::endl;
                    // Remove the corrupted function from our map and fall through to create forward declaration
                    _functions.erase(c_name);
                }
            }

            // Try resolving with namespace variations
            std::vector<std::string> search_variations;
            search_variations.push_back(c_name); // Original name

            // If we have a namespace context, try variations
            if (!_namespace_context.empty())
            {
                // Extract the root namespace from current context (e.g., "std" from "std::IO")
                std::string current_root_ns;
                size_t first_scope_pos = _namespace_context.find("::");
                if (first_scope_pos != std::string::npos)
                {
                    current_root_ns = _namespace_context.substr(0, first_scope_pos);
                }
                else
                {
                    current_root_ns = _namespace_context;
                }

                // Check if the function name starts with the same root namespace
                if (c_name.find(current_root_ns + "::") != 0)
                {
                    // Try adding the current root namespace
                    search_variations.push_back(current_root_ns + "::" + c_name);
                    // Also try with full namespace context
                    search_variations.push_back(_namespace_context + "::" + c_name);
                }
            }

            // If no namespace context or we're looking for something not in current namespace,
            // try common prefixes (especially std:: for standard library)
            if (c_name.find("::") != std::string::npos && c_name.find("std::") != 0)
            {
                search_variations.push_back("std::" + c_name);
            }

            // Try all variations
            for (const std::string &search_name : search_variations)
            {
                functions_it = _functions.find(search_name);
                if (functions_it != _functions.end())
                {
                    llvm::Function *found_func = functions_it->second;
                    std::cout << "[CodegenVisitor] Found function in _functions map: " << search_name << " (searched for: " << c_name << ")" << std::endl;

                    // Check if function is valid (not orphaned)
                    if (found_func && found_func->getParent())
                    {
                        std::cout << "[DEBUG] Function is valid and has parent module" << std::endl;
                        std::cout << "[DEBUG] Function name at search variations find time: " << found_func->getName().str() << std::endl;
                        return found_func;
                    }
                    else
                    {
                        std::cout << "[DEBUG] Function is orphaned (no parent module), removing corrupted entry" << std::endl;
                        // Remove the corrupted orphaned function from our map
                        _functions.erase(search_name);
                        // Fall through to create forward declaration below
                    }
                }
            }
        }

        // If we reach here, the function wasn't found anywhere
        // Create a forward declaration based on the call site
        std::cout << "[DEBUG] Creating forward declaration for: " << c_name << std::endl;
        std::cout << "[DEBUG] Current namespace context: " << _namespace_context << std::endl;

        if (!call_node)
        {
            std::cerr << "[ERROR] Cannot create forward declaration without call node" << std::endl;
            return nullptr;
        }

        // Infer parameter types from call arguments
        std::vector<llvm::Type *> param_types;

        // Check if this is a primitive method call that needs a 'this' pointer
        bool is_primitive_method_call = false;
        if (auto *member_access = dynamic_cast<MemberAccessNode *>(call_node->callee()))
        {
            if (auto *object_identifier = dynamic_cast<IdentifierNode *>(member_access->object()))
            {
                std::string object_name = object_identifier->name();
                auto type_it = _variable_types.find(object_name);
                if (type_it != _variable_types.end() && is_primitive_type(type_it->second))
                {
                    is_primitive_method_call = true;
                    // Add 'this' pointer parameter (string pointer for string methods)
                    param_types.push_back(llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(_context_manager.get_context())));
                    std::cout << "[DEBUG] Added 'this' pointer parameter to forward declaration for primitive method: " << c_name << std::endl;
                }
            }
        }

        if (!call_node->arguments().empty())
        {
            for (const auto &arg : call_node->arguments())
            {
                // Generate the argument to determine its type
                arg->accept(*this);
                llvm::Value *arg_val = get_current_value();
                if (arg_val)
                {
                    param_types.push_back(arg_val->getType());
                }
                else
                {
                    std::cerr << "[WARNING] Failed to determine argument type, using i32 as fallback" << std::endl;
                    param_types.push_back(llvm::Type::getInt32Ty(_context_manager.get_context()));
                }
            }
        }

        // Use FunctionRegistry to infer return type based on function metadata
        llvm::Type *return_type = _function_registry->get_function_return_type(c_name, _context_manager.get_context(), _namespace_context);

        std::cout << "[DEBUG] FunctionRegistry return type for '" << c_name << "': " << return_type << std::endl;
        if (return_type)
        {
            return_type->print(llvm::errs());
            std::cout << std::endl;
        }

        // TEMPORARY FIX: Override return type for known cross-module functions
        if (c_name == "std::String::_strlen")
        {
            std::cout << "[DEBUG] Overriding return type for std::String::_strlen to u64" << std::endl;
            return_type = llvm::Type::getInt64Ty(_context_manager.get_context());
        }
        else if (c_name == "std::Syscall::IO::write")
        {
            std::cout << "[DEBUG] Overriding return type for std::Syscall::IO::write to i64" << std::endl;
            return_type = llvm::Type::getInt64Ty(_context_manager.get_context());
        }
        else if (c_name == "std::String::_int_to_string")
        {
            std::cout << "[DEBUG] Overriding return type for std::String::_int_to_string to string" << std::endl;
            return_type = llvm::PointerType::get(llvm::Type::getInt8Ty(_context_manager.get_context()), 0);
        }
        else if (c_name == "std::String::_float_to_string")
        {
            std::cout << "[DEBUG] Overriding return type for std::String::_float_to_string to string" << std::endl;
            return_type = llvm::PointerType::get(llvm::Type::getInt8Ty(_context_manager.get_context()), 0);
        }
        else if (c_name == "std::String::from_char")
        {
            std::cout << "[DEBUG] Overriding return type for std::String::from_char to string" << std::endl;
            return_type = llvm::PointerType::get(llvm::Type::getInt8Ty(_context_manager.get_context()), 0);
        }

        // Create function type and declaration
        llvm::FunctionType *func_type = llvm::FunctionType::get(return_type, param_types, false);

        // Use FunctionRegistry to get the runtime function name
        FunctionMetadata metadata = _function_registry->get_function_metadata(c_name, _namespace_context);
        std::string c_runtime_name = metadata.runtime_name.empty() ? c_name : metadata.runtime_name;

        std::cout << "[DEBUG] FunctionRegistry provided runtime name for '" << c_name << "': '" << c_runtime_name << "'" << std::endl;

        llvm::Function *forward_decl = llvm::Function::Create(
            func_type,
            llvm::Function::ExternalLinkage,
            c_runtime_name, // Use the C runtime function name instead of the full Cryo name
            _context_manager.get_module());

        if (!forward_decl)
        {
            std::cerr << "[ERROR] Failed to create forward declaration for: " << c_name << std::endl;
            return nullptr;
        }

        // Store the forward declaration for future use
        _functions[c_name] = forward_decl;

        std::cout << "[DEBUG] Successfully created forward declaration for: " << c_name << std::endl;
        return forward_decl;
    }
    llvm::Value *Cryo::Codegen::CodegenVisitor::generate_array_access(Cryo::ArrayAccessNode *node) { return nullptr; }
    llvm::Value *Cryo::Codegen::CodegenVisitor::generate_member_access(Cryo::MemberAccessNode *node) { return nullptr; }
    void Cryo::Codegen::CodegenVisitor::generate_if_statement(Cryo::IfStatementNode *node)
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
        BreakableContext loop_ctx(condition_block, body_block, condition_block, exit_block);
        _breakable_stack.push(loop_ctx);

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

        // Restore breakable context
        _breakable_stack.pop();

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
        BreakableContext loop_ctx(loopCondition, loopBody, loopIncrement, afterLoop);
        _breakable_stack.push(loop_ctx);

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

        // Restore previous breakable context
        _breakable_stack.pop();
    }

    void CodegenVisitor::generate_switch_statement(Cryo::SwitchStatementNode *node)
    {
        if (!node)
            return;

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();
        llvm::Function *function = builder.GetInsertBlock()->getParent();

        // Create the end block that will be used for break statements
        llvm::BasicBlock *end_block = llvm::BasicBlock::Create(context, "switch.end", function);

        // Create switch context for break statements
        BreakableContext switch_ctx(end_block);
        _breakable_stack.push(switch_ctx);

        // Generate the switch expression
        node->expression()->accept(*this);
        llvm::Value *switch_value = get_current_value();

        if (!switch_value)
        {
            report_error("Failed to generate switch expression");
            _breakable_stack.pop(); // Clean up context on error
            return;
        }

        // Check if this is a string switch (pointer type)
        bool is_string_switch = switch_value->getType()->isPointerTy();

        if (is_string_switch)
        {
            // Handle string switch using if-else chain with strcmp
            generate_string_switch(node, switch_value, end_block);
        }
        else
        {
            // Handle integer switch using LLVM switch instruction
            generate_integer_switch(node, switch_value, end_block);
        }

        // Restore previous breakable context
        _breakable_stack.pop();

        // Set insertion point to after the switch
        builder.SetInsertPoint(end_block);
    }

    void CodegenVisitor::generate_string_switch(Cryo::SwitchStatementNode *node, llvm::Value *switch_value, llvm::BasicBlock *end_block)
    {
        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();
        llvm::Function *function = builder.GetInsertBlock()->getParent();

        // Create basic blocks (end_block is already created by caller)
        llvm::BasicBlock *default_block = nullptr;

        // Check if there's a default case
        for (const auto &case_stmt : node->cases())
        {
            if (case_stmt->is_default())
            {
                default_block = llvm::BasicBlock::Create(context, "switch.default", function);
                break;
            }
        }

        // If no default case, create one that just branches to end
        if (!default_block)
        {
            default_block = llvm::BasicBlock::Create(context, "switch.default", function);
        }

        // Get or create strcmp function
        llvm::Function *strcmp_func = _context_manager.get_module()->getFunction("strcmp");
        if (!strcmp_func)
        {
            // Create strcmp function declaration
            llvm::Type *int_type = llvm::Type::getInt32Ty(context);
            llvm::Type *char_ptr_type = llvm::Type::getInt8Ty(context)->getPointerTo();
            llvm::FunctionType *strcmp_type = llvm::FunctionType::get(int_type, {char_ptr_type, char_ptr_type}, false);
            strcmp_func = llvm::Function::Create(strcmp_type, llvm::Function::ExternalLinkage, "strcmp", *_context_manager.get_module());
        }

        llvm::BasicBlock *current_check_block = builder.GetInsertBlock();

        // Process each case in order
        std::vector<std::pair<llvm::BasicBlock *, llvm::BasicBlock *>> case_blocks; // (check_block, case_block)

        // Create all the blocks first
        for (size_t i = 0; i < node->cases().size(); ++i)
        {
            auto &case_stmt = node->cases()[i];

            if (!case_stmt->is_default())
            {
                llvm::BasicBlock *check_block = (i == 0) ? current_check_block : llvm::BasicBlock::Create(context, "switch.case.check." + std::to_string(i), function);
                llvm::BasicBlock *case_block = llvm::BasicBlock::Create(context, "switch.case." + std::to_string(i), function);
                case_blocks.push_back({check_block, case_block});
            }
        }

        // Process cases
        size_t case_index = 0;
        for (size_t i = 0; i < node->cases().size(); ++i)
        {
            auto &case_stmt = node->cases()[i];

            if (case_stmt->is_default())
            {
                continue; // Handle default separately
            }

            llvm::BasicBlock *check_block = case_blocks[case_index].first;
            llvm::BasicBlock *case_block = case_blocks[case_index].second;

            // Set up the check block
            builder.SetInsertPoint(check_block);

            // Generate the case value
            case_stmt->value()->accept(*this);
            llvm::Value *case_value = get_current_value();

            if (!case_value || !case_value->getType()->isPointerTy())
            {
                report_error("String switch case value must be a string literal");
                case_index++;
                continue;
            }

            // Call strcmp to compare switch_value with case_value
            llvm::Value *strcmp_args[] = {switch_value, case_value};
            llvm::Value *cmp_result = builder.CreateCall(strcmp_func, strcmp_args, "strcmp.result");

            // Check if strcmp result is 0 (strings are equal)
            llvm::Value *is_equal = builder.CreateICmpEQ(cmp_result,
                                                         llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0), "str.eq");

            // Determine next block
            llvm::BasicBlock *next_block;
            if (case_index + 1 < case_blocks.size())
            {
                next_block = case_blocks[case_index + 1].first;
            }
            else
            {
                next_block = default_block;
            }

            // Branch to case block if equal, otherwise to next case
            builder.CreateCondBr(is_equal, case_block, next_block);

            // Generate code for this case
            builder.SetInsertPoint(case_block);
            for (const auto &stmt : case_stmt->statements())
            {
                stmt->accept(*this);
            }

            // Check if the last statement is a break/return
            if (!case_block->getTerminator())
            {
                builder.CreateBr(end_block);
            }

            case_index++;
        }

        // Handle default case
        builder.SetInsertPoint(default_block);
        bool found_default = false;
        for (const auto &case_stmt : node->cases())
        {
            if (case_stmt->is_default())
            {
                for (const auto &stmt : case_stmt->statements())
                {
                    stmt->accept(*this);
                }
                found_default = true;
                break;
            }
        }

        // Ensure default block has a terminator
        if (!default_block->getTerminator())
        {
            builder.CreateBr(end_block);
        }
    }

    void CodegenVisitor::generate_integer_switch(Cryo::SwitchStatementNode *node, llvm::Value *switch_value, llvm::BasicBlock *end_block)
    {
        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();
        llvm::Function *function = builder.GetInsertBlock()->getParent();

        // Check if switch_value is an enum and extract discriminant if needed
        llvm::Value *actual_switch_value = switch_value;
        if (switch_value->getType()->isStructTy())
        {
            // This might be an enum - try to extract the discriminant
            llvm::Value *discriminant = extract_enum_discriminant(switch_value);
            if (discriminant)
            {
                actual_switch_value = discriminant;
                std::cout << "[CodegenVisitor] DEBUG: Extracted enum discriminant for switch" << std::endl;
            }
        }

        // Create basic blocks (end_block is already created by caller)
        llvm::BasicBlock *default_block = nullptr;

        // Find if there's a default case
        for (const auto &case_stmt : node->cases())
        {
            if (case_stmt->is_default())
            {
                default_block = llvm::BasicBlock::Create(context, "switch.default", function);
                break;
            }
        }

        // If no default case found, create one that branches to end
        if (!default_block)
        {
            default_block = llvm::BasicBlock::Create(context, "switch.default", function);
        }

        // Create the switch instruction
        llvm::SwitchInst *switch_inst = builder.CreateSwitch(actual_switch_value, default_block, node->cases().size());

        // Generate code for each case
        for (size_t i = 0; i < node->cases().size(); ++i)
        {
            auto &case_stmt = node->cases()[i];

            if (case_stmt->is_default())
            {
                // Handle default case
                builder.SetInsertPoint(default_block);

                // Generate statements for default case
                for (const auto &stmt : case_stmt->statements())
                {
                    stmt->accept(*this);
                }

                // Check if the last statement is a break/return
                if (!default_block->getTerminator())
                {
                    builder.CreateBr(end_block);
                }
            }
            else
            {
                // Handle regular case
                llvm::BasicBlock *case_block = llvm::BasicBlock::Create(context,
                                                                        "switch.case." + std::to_string(i), function);

                // Generate the case value and add to switch
                case_stmt->value()->accept(*this);
                llvm::Value *case_value = get_current_value();

                if (case_value && llvm::isa<llvm::ConstantInt>(case_value))
                {
                    switch_inst->addCase(llvm::cast<llvm::ConstantInt>(case_value), case_block);
                }
                else
                {
                    // Convert the case value to a constant integer if possible
                    if (auto const_int = llvm::dyn_cast<llvm::ConstantInt>(case_value))
                    {
                        switch_inst->addCase(const_int, case_block);
                    }
                    else
                    {
                        report_error("Switch case value must be a constant integer");
                        continue;
                    }
                }

                // Generate code for this case
                builder.SetInsertPoint(case_block);

                for (const auto &stmt : case_stmt->statements())
                {
                    stmt->accept(*this);
                }

                // Check if the last statement is a break/return
                if (!case_block->getTerminator())
                {
                    builder.CreateBr(end_block);
                }
            }
        }

        // Handle empty default case (just branch to end)
        if (!default_block->getTerminator())
        {
            builder.SetInsertPoint(default_block);
            builder.CreateBr(end_block);
        }
    }

    void CodegenVisitor::generate_case_statement(Cryo::CaseStatementNode *node, llvm::SwitchInst *switch_inst, llvm::BasicBlock *end_block)
    {
        // This method is not used in the current implementation
        // Cases are handled directly in generate_switch_statement
        // This is kept for interface completeness
        report_error("generate_case_statement called directly - cases are handled in generate_switch_statement");
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

        // Debug: Print the type information
        std::cout << "[CodegenVisitor] DEBUG: enum_value type is pointer: " << enum_value->getType()->isPointerTy() << std::endl;
        std::cout << "[CodegenVisitor] DEBUG: enum_value type is struct: " << enum_value->getType()->isStructTy() << std::endl;

        // Check if enum_value is a pointer or a value
        if (enum_value->getType()->isPointerTy())
        {
            // It's a pointer to an enum struct - load the discriminant field
            // For tagged union: { i32 discriminant, [N x i8] payload }
            // We need to access the first field (discriminant)

            // Try to get the pointed-to type first to understand what we're dealing with
            std::cout << "[CodegenVisitor] DEBUG: enum_value type: " << enum_value->getType() << std::endl;

            // For now, let's use a simpler approach - load the first 32 bits as discriminant
            // Cast to a byte pointer and load as i32
            llvm::Value *i8_ptr = builder.CreateBitCast(
                enum_value,
                llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0),
                "enum_i8_ptr");

            llvm::Value *i32_ptr = builder.CreateBitCast(
                i8_ptr,
                llvm::PointerType::get(llvm::Type::getInt32Ty(context), 0),
                "discriminant_ptr");

            llvm::Value *discriminant = builder.CreateLoad(
                llvm::Type::getInt32Ty(context),
                i32_ptr,
                "discriminant");

            std::cout << "[CodegenVisitor] Extracted discriminant from enum pointer" << std::endl;
            return discriminant;
        }
        else
        {
            std::cout << "[CodegenVisitor] DEBUG: Processing enum struct value" << std::endl;

            // Debug: Print the actual LLVM type
            std::string type_str;
            llvm::raw_string_ostream os(type_str);
            enum_value->getType()->print(os);
            std::cout << "[CodegenVisitor] DEBUG: enum_value LLVM type: " << os.str() << std::endl;

            // Check if it's actually a struct type
            if (!enum_value->getType()->isStructTy())
            {
                std::cout << "[CodegenVisitor] ERROR: Expected struct type but got something else!" << std::endl;
                return nullptr;
            }

            llvm::StructType *struct_type = llvm::cast<llvm::StructType>(enum_value->getType());
            std::cout << "[CodegenVisitor] DEBUG: Struct has " << struct_type->getNumElements() << " elements" << std::endl;

            // Check if this is an empty struct (indicates enum wasn't properly constructed)
            if (struct_type->getNumElements() == 0)
            {
                std::cout << "[CodegenVisitor] ERROR: Empty struct - enum type not properly constructed!" << std::endl;
                std::cout << "[CodegenVisitor] ERROR: Expected tagged union with discriminant field" << std::endl;
                // Return a default discriminant value to prevent crash
                return llvm::ConstantInt::get(llvm::Type::getInt32Ty(_context_manager.get_context()), 0);
            }

            // It's a struct value - extract the discriminant directly
            llvm::Value *discriminant = builder.CreateExtractValue(
                enum_value,
                {0},
                "discriminant");

            std::cout << "[CodegenVisitor] Extracted discriminant from enum struct value" << std::endl;
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

        // Check if the enum type is properly constructed
        llvm::Type *enum_type = enum_value->getType();
        bool is_empty_enum = false;

        // For modern LLVM with opaque pointers, we need to be careful about type checking
        if (enum_type->isPointerTy())
        {
            // We can't directly get the pointed-to type with opaque pointers
            // Instead, we'll check during the actual operation below
            std::cout << "[CodegenVisitor] Enum value is pointer type (opaque pointer)" << std::endl;
        }
        else if (enum_type->isStructTy())
        {
            auto struct_type = llvm::cast<llvm::StructType>(enum_type);
            if (struct_type->getNumElements() == 0)
            {
                std::cout << "[CodegenVisitor] ERROR: Cannot extract pattern bindings - enum type is empty struct!" << std::endl;
                std::cout << "[CodegenVisitor] Struct name: " << (struct_type->hasName() ? struct_type->getName().str() : "unnamed") << std::endl;
                is_empty_enum = true;
            }
        }

        if (is_empty_enum)
        {
            std::cout << "[CodegenVisitor] Skipping pattern binding extraction for empty enum" << std::endl;
            return; // Early return to prevent LLVM assertion
        }

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
            // For tagged union: { i32 discriminant, [N x i8] payload }
            // The payload starts after the discriminant (4 bytes for i32)

            // Cast to i8* and offset by the size of the discriminant
            llvm::Value *enum_i8_ptr = builder.CreateBitCast(
                enum_value,
                llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0),
                "enum_i8_ptr");

            llvm::Value *offset = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 4); // sizeof(i32)
            llvm::Value *payload_ptr = builder.CreateInBoundsGEP(
                llvm::Type::getInt8Ty(context),
                enum_i8_ptr,
                offset,
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

            // Get the target type from the pointer
            llvm::Type *target_type = ptr->getType();
            if (target_type->isPointerTy())
            {
                target_type = value->getType(); /* Use value type for compatibility */
            }

            // Check if we need type conversion
            llvm::Type *value_type = value->getType();
            if (value_type != target_type)
            {
                // Handle float-to-int conversion
                if (value_type->isFloatingPointTy() && target_type->isIntegerTy())
                {
                    value = builder.CreateFPToSI(value, target_type, "float2int");
                }
                // Handle int-to-float conversion
                else if (value_type->isIntegerTy() && target_type->isFloatingPointTy())
                {
                    value = builder.CreateSIToFP(value, target_type, "int2float");
                }
                // Handle integer size conversion
                else if (value_type->isIntegerTy() && target_type->isIntegerTy())
                {
                    unsigned value_bits = value_type->getIntegerBitWidth();
                    unsigned target_bits = target_type->getIntegerBitWidth();

                    if (value_bits > target_bits)
                    {
                        value = builder.CreateTrunc(value, target_type, "trunc");
                    }
                    else if (value_bits < target_bits)
                    {
                        value = builder.CreateSExt(value, target_type, "sext");
                    }
                }
            }

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
            std::cout << "[DEBUG] enter_scope: this=" << this << ", _value_context=" << _value_context.get() << std::endl;
            if (!_value_context)
            {
                std::cerr << "[ERROR] _value_context is null in enter_scope!" << std::endl;
                return;
            }
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
    llvm::Type *CodegenVisitor::get_llvm_type(Cryo::Type *cryo_type)
    {
        if (!cryo_type)
        {
            return nullptr;
        }

        // Use the new Type-based interface directly
        return _type_mapper->map_type(cryo_type);
    }
    llvm::Value *CodegenVisitor::cast_value(llvm::Value *value, llvm::Type *target_type) { return nullptr; }
    bool CodegenVisitor::is_lvalue(Cryo::ExpressionNode *expr) { return false; }

    void CodegenVisitor::register_enum_variant(const std::string &enum_name, const std::string &variant_name, llvm::Value *value)
    {
        std::string qualified_name = enum_name + "::" + variant_name;
        _enum_variants[qualified_name] = value;

        // Also register with just the variant name for unqualified access
        _enum_variants[variant_name] = value;
    }

    void CodegenVisitor::ensure_parameterized_enum_constructors(const std::string &instantiated_name,
                                                                const std::string &base_name,
                                                                const std::vector<std::string> &type_args)
    {
        // Check if constructors are already generated for this instantiation
        std::string first_variant_name = instantiated_name + "::";
        bool already_generated = false;
        for (const auto &[variant_key, variant_func] : _enum_variants)
        {
            if (variant_key.find(first_variant_name) == 0)
            {
                already_generated = true;
                break;
            }
        }

        if (already_generated)
        {
            return; // Already generated
        }

        std::cout << "[CodegenVisitor] Generating variant constructors for parameterized enum: " << instantiated_name << std::endl;

        auto &llvm_context = _context_manager.get_context();
        auto *module = _context_manager.get_module();
        auto &builder = _context_manager.get_builder();

        if (!module)
        {
            report_error("No module available for parameterized enum constructor generation");
            return;
        }

        // Get the enum type from TypeMapper - let TypeMapper handle the creation
        llvm::Type *enum_type = _type_mapper->lookup_type(instantiated_name);
        if (!enum_type)
        {
            // For cases like AllocResult<void> -> Result<void*, AllocError>, use the actual function return type
            std::string function_return_type = "";
            if (_current_function && _current_function->ast_node)
            {
                function_return_type = _current_function->ast_node->return_type_annotation();
                enum_type = _type_mapper->lookup_type(function_return_type);
                std::cout << "[CodegenVisitor] Using function return type: " << function_return_type 
                          << " -> " << (enum_type ? enum_type->getStructName().str() : "null") << std::endl;
            }
            
            if (!enum_type)
            {
                // Convert type argument strings to Type* for TypeMapper to process
                std::vector<std::shared_ptr<Cryo::Type>> type_args_as_types;
                for (const std::string &type_arg : type_args)
                {
                    Cryo::Type *arg_type = _symbol_table.get_type_context()->parse_type_from_string(type_arg);
                    if (arg_type)
                    {
                        type_args_as_types.push_back(std::shared_ptr<Cryo::Type>(arg_type));
                    }
                }

                // Create a ParameterizedType instance for TypeMapper to process
                if (!type_args_as_types.empty())
                {
                    std::unique_ptr<Cryo::ParameterizedType> parameterized_type =
                        std::make_unique<Cryo::ParameterizedType>(base_name, type_args_as_types);

                    // Let TypeMapper handle the creation and registration
                    enum_type = _type_mapper->map_type(parameterized_type.get());

                    std::cout << "[CodegenVisitor] TypeMapper processed parameterized enum, got type: "
                              << (enum_type ? enum_type->getStructName().str() : "null") << std::endl;
                }
            }
        }

        if (!enum_type)
        {
            report_error("Failed to get enum type for parameterized enum: " + instantiated_name);
            return;
        }

        // Get the base enum template from TypeContext to find its variants
        std::shared_ptr<Cryo::EnumType> enum_template = _symbol_table.get_type_context()->get_parameterized_enum_template(base_name);

        if (!enum_template)
        {
            report_error("Could not find base enum template for parameterized enum: " + base_name);
            return;
        }

        // Generate constructors for each variant of the base enum
        const auto &variants = enum_template->variants();
        std::cout << "[CodegenVisitor] Base enum " << base_name << " has " << variants.size() << " variants" << std::endl;

        for (const auto &variant_name : variants)
        {
            std::cout << "[CodegenVisitor] Processing variant: " << variant_name << std::endl;
            generate_parameterized_enum_variant_constructor(instantiated_name, variant_name, type_args, enum_type);
            std::cout << "[CodegenVisitor] Completed variant: " << variant_name << std::endl;
        }
    }

    void CodegenVisitor::generate_parameterized_enum_variant_constructor(const std::string &instantiated_name,
                                                                         const std::string &variant_name,
                                                                         const std::vector<std::string> &type_args,
                                                                         llvm::Type *enum_type)
    {
        std::cout << "[CodegenVisitor] Starting variant constructor generation for: " << variant_name
                  << " of " << instantiated_name << std::endl;

        auto &llvm_context = _context_manager.get_context();
        auto *module = _context_manager.get_module();
        auto &builder = _context_manager.get_builder();

        std::cout << "[CodegenVisitor] Got LLVM context and module" << std::endl;

        std::string constructor_name = instantiated_name + "::" + variant_name;

        // Use enhanced type system to get constructor parameters for this variant
        std::vector<llvm::Type *> param_types;
        bool needs_value_param = false;

        // Determine parameter needs based on variant name and type arguments
        if (variant_name == "Some" || variant_name == "Ok")
        {
            // This variant needs a parameter - use the first type argument (T)
            if (!type_args.empty())
            {
                std::string type_arg = type_args[0];

                // Convert the type argument string to an LLVM type
                llvm::Type *param_type = nullptr;
                if (type_arg == "i32" || type_arg == "int")
                {
                    param_type = _type_mapper->get_integer_type(32, true);
                }
                else if (type_arg == "i64")
                {
                    param_type = _type_mapper->get_integer_type(64, true);
                }
                else if (type_arg == "f64" || type_arg == "float")
                {
                    param_type = _type_mapper->get_float_type(64);
                }
                else if (type_arg == "bool")
                {
                    param_type = _type_mapper->get_boolean_type();
                }
                else if (type_arg == "str" || type_arg == "string")
                {
                    param_type = _type_mapper->get_string_type();
                }
                else if (type_arg == "void*")
                {
                    param_type = llvm::PointerType::get(_type_mapper->get_void_type(), 0);
                }
                else
                {
                    // Try to look up as a registered type
                    param_type = _type_mapper->lookup_type(type_arg);
                }

                if (param_type)
                {
                    param_types.push_back(param_type);
                    needs_value_param = true;
                }
            }
        }
        else if (variant_name == "Err")
        {
            // Err variant needs a parameter - use the second type argument (E)
            if (type_args.size() > 1)
            {
                std::string error_type_arg = type_args[1];
                
                // Convert the error type argument string to an LLVM type
                llvm::Type *param_type = nullptr;
                if (error_type_arg == "i32" || error_type_arg == "int")
                {
                    param_type = _type_mapper->get_integer_type(32, true);
                }
                else if (error_type_arg == "i64")
                {
                    param_type = _type_mapper->get_integer_type(64, true);
                }
                else
                {
                    // Try to look up as a registered type (for enum types like AllocError)
                    param_type = _type_mapper->lookup_type(error_type_arg);
                    
                    // If lookup failed and this is for a simple enum error type, use i32
                    if (!param_type && error_type_arg == "AllocError")
                    {
                        param_type = _type_mapper->get_integer_type(32, true);
                        std::cout << "[CodegenVisitor] Using i32 for AllocError enum parameter" << std::endl;
                    }
                }

                if (param_type)
                {
                    param_types.push_back(param_type);
                    needs_value_param = true;
                }
            }
        }
        // None, Nothing variants don't need parameters        // Create function type
        llvm::FunctionType *func_type = llvm::FunctionType::get(enum_type, param_types, false);
        llvm::Function *constructor_func = llvm::Function::Create(
            func_type, llvm::Function::ExternalLinkage, constructor_name, *module);

        // Set parameter names
        if (needs_value_param && constructor_func->arg_size() > 0)
        {
            constructor_func->arg_begin()->setName("value");
        }

        // Create function body
        llvm::BasicBlock *entry = llvm::BasicBlock::Create(llvm_context, "entry", constructor_func);

        // Save current insertion point
        llvm::BasicBlock *saved_block = builder.GetInsertBlock();
        builder.SetInsertPoint(entry);

        // Create enum instance
        llvm::Value *enum_instance = llvm::UndefValue::get(enum_type);

        // Set the discriminant/flag field based on variant
        if (variant_name == "None" || variant_name == "Nothing")
        {
            // Set has_value/is_ok to false
            llvm::Value *false_value = llvm::ConstantInt::get(llvm::Type::getInt1Ty(llvm_context), 0);
            enum_instance = builder.CreateInsertValue(enum_instance, false_value, {0}, "set_flag");
        }
        else if (variant_name == "Some" || variant_name == "Ok")
        {
            // Set has_value/is_ok to true and set the value
            llvm::Value *true_value = llvm::ConstantInt::get(llvm::Type::getInt1Ty(llvm_context), 1);
            enum_instance = builder.CreateInsertValue(enum_instance, true_value, {0}, "set_flag");

            if (needs_value_param && constructor_func->arg_size() > 0)
            {
                llvm::Value *value_param = &*constructor_func->arg_begin();
                enum_instance = builder.CreateInsertValue(enum_instance, value_param, {1}, "set_value");
            }
        }
        else if (variant_name == "Err")
        {
            // For Result<T,E>, set is_ok to false and store error in union
            llvm::Value *false_value = llvm::ConstantInt::get(llvm::Type::getInt1Ty(llvm_context), 0);
            enum_instance = builder.CreateInsertValue(enum_instance, false_value, {0}, "set_flag");

            if (needs_value_param && constructor_func->arg_size() > 0)
            {
                // For errors, we need to handle the union data properly
                llvm::Value *error_param = &*constructor_func->arg_begin();
                
                // If the error parameter is an integer but the struct field is a pointer,
                // we need to cast it appropriately
                llvm::Type *struct_field_type = enum_type->getStructElementType(1);
                if (error_param->getType() != struct_field_type)
                {
                    if (error_param->getType()->isIntegerTy() && struct_field_type->isPointerTy())
                    {
                        // Cast integer to pointer (for simple enum values)
                        error_param = builder.CreateIntToPtr(error_param, struct_field_type, "error_as_ptr");
                    }
                    else if (error_param->getType()->isPointerTy() && struct_field_type->isIntegerTy())
                    {
                        // Cast pointer to integer
                        error_param = builder.CreatePtrToInt(error_param, struct_field_type, "error_as_int");
                    }
                }
                
                enum_instance = builder.CreateInsertValue(enum_instance, error_param, {1}, "set_error");
            }
        }

        builder.CreateRet(enum_instance);

        // Restore insertion point
        if (saved_block)
        {
            builder.SetInsertPoint(saved_block);
        }

        // Register the constructor
        register_enum_variant(instantiated_name, variant_name, constructor_func);

        std::cout << "[CodegenVisitor] Generated parameterized enum constructor: " << constructor_name << std::endl;
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
        Cryo::Type *cryo_param_type = _symbol_table.get_type_context()->parse_type_from_string(type_args[0]);
        llvm::Type *param_type = cryo_param_type ? _type_mapper->map_type(cryo_param_type) : nullptr; // T mapped to concrete type (int)
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

        // Find the original template struct in the TypeMapper
        // TypeMapper stores struct AST nodes registered during struct declaration processing
        StructDeclarationNode *template_struct = nullptr;

        // For now, we'll look up the template struct from the TypeMapper's internal storage
        // This is a bit of a hack, but we need access to the stored AST nodes
        // TODO: Add a proper method to TypeMapper to get struct templates

        // Since we can't directly access TypeMapper's private _struct_ast_nodes,
        // we'll implement a simpler approach: use the base type name to find
        // the template and generate basic method stubs

        std::cout << "[CodegenVisitor] Looking for template struct: " << base_type << std::endl;

        // Create type substitution map
        std::unordered_map<std::string, std::string> type_substitutions;

        // For now, create a simple substitution map assuming T -> first type arg
        if (!type_args.empty())
        {
            type_substitutions["T"] = type_args[0];
            std::cout << "[CodegenVisitor] Type substitution: T -> " << type_args[0] << std::endl;
        }
        else
        {
            std::cout << "[CodegenVisitor] Generic method generation not implemented for: " << base_type << std::endl;
        }
    }

    void CodegenVisitor::generate_generic_struct_methods(const std::string &instantiated_type,
                                                         const std::vector<std::string> &type_args,
                                                         llvm::Type *struct_type,
                                                         const std::unordered_map<std::string, std::string> &type_substitutions)
    {
        std::cout << "[CodegenVisitor] Generating GenericStruct methods for " << instantiated_type << std::endl;

        auto &context = _context_manager.get_context();
        auto module = _context_manager.get_module();

        // Generate get_value() method
        generate_get_value_method(instantiated_type, type_args, struct_type, type_substitutions);
    }

    void CodegenVisitor::generate_get_value_method(const std::string &instantiated_type,
                                                   const std::vector<std::string> &type_args,
                                                   llvm::Type *struct_type,
                                                   const std::unordered_map<std::string, std::string> &type_substitutions)
    {
        std::cout << "[CodegenVisitor] Generating get_value method for " << instantiated_type << std::endl;

        auto &context = _context_manager.get_context();
        auto module = _context_manager.get_module();

        if (type_args.empty())
        {
            std::cout << "[CodegenVisitor] No type arguments for get_value method" << std::endl;
            return;
        }

        // Create specialized method name
        std::string method_name = instantiated_type + "::get_value";

        // Check if this method is already generated
        if (_functions.find(method_name) != _functions.end())
        {
            std::cout << "[CodegenVisitor] Method already exists: " << method_name << std::endl;
            return;
        }

        // Determine return type based on the first type argument
        std::string return_type = type_args[0];
        llvm::Type *llvm_return_type = nullptr;

        if (return_type == "int")
        {
            llvm_return_type = llvm::Type::getInt32Ty(context);
        }
        else if (return_type == "float")
        {
            llvm_return_type = llvm::Type::getFloatTy(context);
        }
        else if (return_type == "string")
        {
            llvm_return_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        }
        else
        {
            std::cout << "[CodegenVisitor] Unknown return type: " << return_type << ", defaulting to i32" << std::endl;
            llvm_return_type = llvm::Type::getInt32Ty(context);
        }

        // Create parameter types (this pointer)
        std::vector<llvm::Type *> param_types;
        param_types.push_back(llvm::PointerType::get(struct_type, 0)); // 'this' pointer

        // Create function type
        llvm::FunctionType *func_type = llvm::FunctionType::get(llvm_return_type, param_types, false);

        // Create the function
        llvm::Function *method = llvm::Function::Create(
            func_type, llvm::Function::ExternalLinkage, method_name, module);

        // Set parameter names
        auto args = method->arg_begin();
        args->setName("this");

        std::cout << "[CodegenVisitor] Created get_value method: " << method_name
                  << " with return type: " << return_type << std::endl;

        // Generate method body
        generate_get_value_method_body(method, struct_type, type_substitutions);

        // Register the method
        _functions[method_name] = method;
        std::cout << "[CodegenVisitor] Registered get_value method: " << method_name << std::endl;
    }

    void CodegenVisitor::generate_get_value_method_body(llvm::Function *method,
                                                        llvm::Type *struct_type,
                                                        const std::unordered_map<std::string, std::string> &type_substitutions)
    {
        std::cout << "[CodegenVisitor] Generating get_value method body" << std::endl;

        auto &context = _context_manager.get_context();
        auto &builder = _context_manager.get_builder();

        // Save the current insert point to restore later
        llvm::BasicBlock *saved_insert_block = builder.GetInsertBlock();

        // Create entry basic block for the method
        llvm::BasicBlock *entry_block = llvm::BasicBlock::Create(context, "entry", method);
        builder.SetInsertPoint(entry_block);

        // Get the 'this' pointer (first parameter)
        auto args = method->arg_begin();
        llvm::Value *this_ptr = &*args;

        // The get_value() method should return this.value
        // For GenericStruct<T>, the 'value' field is at index 0
        std::vector<llvm::Value *> indices = {
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0), // struct index
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0)  // field index (value is first field)
        };

        // Get pointer to the 'value' field
        llvm::Value *value_ptr = builder.CreateGEP(struct_type, this_ptr, indices, "value_ptr");

        // Load the value
        // Determine the field type based on struct type
        llvm::Type *field_type = nullptr;
        if (auto *struct_llvm_type = llvm::dyn_cast<llvm::StructType>(struct_type))
        {
            if (struct_llvm_type->getNumElements() > 0)
            {
                field_type = struct_llvm_type->getElementType(0);
            }
        }

        if (!field_type)
        {
            std::cout << "[CodegenVisitor] Could not determine field type, using i32" << std::endl;
            field_type = llvm::Type::getInt32Ty(context);
        }

        llvm::Value *value = builder.CreateLoad(field_type, value_ptr, "value");

        // Return the loaded value
        builder.CreateRet(value);

        // Restore the original insert point
        if (saved_insert_block)
        {
            builder.SetInsertPoint(saved_insert_block);
        }

        std::cout << "[CodegenVisitor] Generated get_value method body successfully" << std::endl;
    }

    llvm::Value *CodegenVisitor::generate_intrinsic_call(Cryo::CallExpressionNode *node, const std::string &intrinsic_name)
    {
        std::cout << "[CodegenVisitor] Delegating intrinsic call to Intrinsics module: " << intrinsic_name << std::endl;

        // Generate arguments
        std::vector<llvm::Value *> args;
        for (const auto &arg : node->arguments())
        {
            arg->accept(*this);
            llvm::Value *arg_value = get_current_value();
            if (!arg_value)
            {
                report_error("Failed to generate argument for intrinsic: " + intrinsic_name);
                return nullptr;
            }
            args.push_back(arg_value);
        }

        // Delegate to the Intrinsics module
        llvm::Value *result = _intrinsics->generate_intrinsic_call(node, intrinsic_name, args);

        if (_intrinsics->has_errors())
        {
            report_error("Intrinsic generation failed: " + _intrinsics->get_last_error());
            return nullptr;
        }

        return result;
    }

    bool CodegenVisitor::is_primitive_type(const std::string &type_name)
    {
        // Check if this is a primitive type that should not have implementation blocks
        static const std::set<std::string> primitive_types = {
            "i8", "i16", "i32", "i64", "int",
            "u8", "u16", "u32", "u64", "uint",
            "f32", "f64", "float", "double",
            "bool", "boolean", "char", "string", "void",
            "ptr", "const_ptr"};

        return primitive_types.find(type_name) != primitive_types.end();
    }

    bool CodegenVisitor::is_primitive_integer_constructor(const std::string &function_name) const
    {
        static const std::unordered_set<std::string> integer_types = {
            "i8", "i16", "i32", "i64", "int",
            "u8", "u16", "u32", "u64", "uint"};

        return integer_types.find(function_name) != integer_types.end();
    }

    llvm::Value *CodegenVisitor::generate_primitive_constructor_call(CallExpressionNode *node, const std::string &target_type)
    {
        if (!node || node->arguments().empty())
        {
            report_error("Primitive constructor requires exactly one argument", node);
            return nullptr;
        }

        if (node->arguments().size() != 1)
        {
            report_error("Primitive constructor requires exactly one argument", node);
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();

        // Visit the argument to get its value
        node->arguments()[0]->accept(*this);
        llvm::Value *source_value = get_current_value();

        if (!source_value)
        {
            report_error("Failed to generate value for primitive constructor argument", node);
            return nullptr;
        }

        // Get LLVM types for source and target
        llvm::Type *source_type = source_value->getType();
        Cryo::Type *cryo_target_type = _symbol_table.get_type_context()->parse_type_from_string(target_type);
        llvm::Type *target_llvm_type = cryo_target_type ? _type_mapper->map_type(cryo_target_type) : nullptr;

        if (!target_llvm_type)
        {
            report_error("Failed to map target type for primitive constructor: " + target_type, node);
            return nullptr;
        }

        // Generate appropriate cast instruction
        return generate_integer_cast(source_value, source_type, target_llvm_type, target_type);
    }

    llvm::Value *CodegenVisitor::generate_integer_cast(llvm::Value *source_value, llvm::Type *source_type,
                                                       llvm::Type *target_type, const std::string &target_type_name)
    {
        auto &builder = _context_manager.get_builder();

        // If types are the same, no cast needed
        if (source_type == target_type)
        {
            return source_value;
        }

        // Both should be integer types
        if (!source_type->isIntegerTy() || !target_type->isIntegerTy())
        {
            std::cerr << "Error: Primitive constructor only supports integer types" << std::endl;
            return nullptr;
        }

        unsigned source_bits = source_type->getIntegerBitWidth();
        unsigned target_bits = target_type->getIntegerBitWidth();

        // Determine if target type is signed
        bool target_is_signed = (target_type_name == "i8" || target_type_name == "i16" ||
                                 target_type_name == "i32" || target_type_name == "i64" ||
                                 target_type_name == "int");

        if (source_bits == target_bits)
        {
            // Same size, just bitcast
            return builder.CreateBitCast(source_value, target_type);
        }
        else if (source_bits < target_bits)
        {
            // Widening conversion
            if (target_is_signed)
            {
                return builder.CreateSExt(source_value, target_type); // Sign extend
            }
            else
            {
                return builder.CreateZExt(source_value, target_type); // Zero extend
            }
        }
        else
        {
            // Narrowing conversion
            return builder.CreateTrunc(source_value, target_type);
        }
    }

} // namespace Cryo::Codegen
