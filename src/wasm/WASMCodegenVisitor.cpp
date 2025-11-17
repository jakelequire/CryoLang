#include "wasm/WASMCodegenVisitor.hpp"
#include "AST/ASTNode.hpp"
#include "Utils/Logger.hpp"

// For WASM builds, we generate JavaScript instead of LLVM IR
#ifdef WASM_BUILD
// No LLVM dependencies for WASM builds
#else
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/InlineAsm.h>
#endif

namespace Cryo::WASM
{
    //===================================================================
    // Construction
    //===================================================================

    WASMCodegenVisitor::WASMCodegenVisitor(
        Cryo::Codegen::LLVMContextManager& context_manager,
        Cryo::SymbolTable& symbol_table,
        Cryo::DiagnosticManager* gdm)
        : Cryo::Codegen::CodegenVisitor(context_manager, symbol_table, gdm)
    {
        initialize_wasm_types();
        setup_wasm_runtime_functions();
    }

    //===================================================================
    // WASM-specific Configuration
    //===================================================================

    void WASMCodegenVisitor::set_exported_functions(const std::vector<std::string>& functions)
    {
        _exported_functions = functions;
    }

    void WASMCodegenVisitor::register_js_function(const std::string& name, const std::string& signature)
    {
        _js_function_signatures[name] = signature;
    }

    //===================================================================
    // AST Visitor Overrides
    //===================================================================

    void WASMCodegenVisitor::visit(Cryo::FunctionDeclarationNode& node)
    {
        // Call parent implementation first
        Cryo::Codegen::CodegenVisitor::visit(node);

        // Check if this function should be exported to JavaScript
        const std::string& func_name = node.name();
        if (std::find(_exported_functions.begin(), _exported_functions.end(), func_name) != _exported_functions.end())
        {
            llvm::Function* func = _context_manager.get_module()->getFunction(func_name);
            if (func)
            {
                export_function_to_js(func, func_name);
            }
        }
    }

    void WASMCodegenVisitor::visit(Cryo::CallExpressionNode& node)
    {
        const std::string& callee_name = node.callee_name();
        
        // Check if this is a call to a JavaScript function
        if (_js_function_signatures.find(callee_name) != _js_function_signatures.end())
        {
            // Generate JavaScript interop call
            std::vector<llvm::Value*> args;
            for (size_t i = 0; i < node.arguments().size(); ++i)
            {
                node.arguments()[i]->accept(*this);
                args.push_back(_current_value);
            }

            llvm::Type* return_type = convert_to_wasm_type(node.get_resolved_type());
            _current_value = generate_js_call(callee_name, args, return_type);
        }
        else
        {
            // Use parent implementation for regular calls
            Cryo::Codegen::CodegenVisitor::visit(node);
        }
    }

    void WASMCodegenVisitor::visit(Cryo::ProgramNode& node)
    {
        // Generate WASM module initialization first
        generate_wasm_module_init();
        
        // Generate JavaScript interop functions
        if (_js_interop_enabled)
        {
            generate_js_interop_functions();
        }

        // Generate WASM memory management functions
        generate_wasm_memory_functions();

        // Call parent implementation
        Cryo::Codegen::CodegenVisitor::visit(node);
    }

    //===================================================================
    // WASM-specific Code Generation
    //===================================================================

    void WASMCodegenVisitor::generate_wasm_module_init()
    {
        llvm::Module* module = _context_manager.get_module();
        llvm::IRBuilder<>& builder = _context_manager.get_builder();
        llvm::LLVMContext& context = _context_manager.get_llvm_context();

        // Create module initialization function
        llvm::FunctionType* init_type = llvm::FunctionType::get(
            llvm::Type::getVoidTy(context), false);
        
        llvm::Function* init_func = llvm::Function::Create(
            init_type, llvm::Function::ExternalLinkage, 
            "__wasm_call_ctors", module);

        llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", init_func);
        builder.SetInsertPoint(entry);

        // Add any global constructor calls here
        builder.CreateRetVoid();
    }

    void WASMCodegenVisitor::generate_js_interop_functions()
    {
        llvm::Module* module = _context_manager.get_module();
        llvm::LLVMContext& context = _context_manager.get_llvm_context();

        // Generate function declarations for JavaScript functions
        for (const auto& [name, signature] : _js_function_signatures)
        {
            // Parse signature to create function type
            // For now, create simple signatures
            std::vector<llvm::Type*> param_types;
            llvm::Type* return_type = llvm::Type::getVoidTy(context);

            // Simple signature parsing (extend as needed)
            if (signature == "vi") // void function(int)
            {
                param_types.push_back(llvm::Type::getInt32Ty(context));
                return_type = llvm::Type::getVoidTy(context);
            }
            else if (signature == "d") // double function()
            {
                return_type = llvm::Type::getDoubleTy(context);
            }

            llvm::FunctionType* func_type = llvm::FunctionType::get(return_type, param_types, false);
            llvm::Function::Create(func_type, llvm::Function::ExternalLinkage, name, module);
        }
    }

    void WASMCodegenVisitor::generate_wasm_memory_functions()
    {
        // These will be implemented by the WASM runtime
        get_or_create_wasm_alloc_function();
        get_or_create_wasm_free_function();
    }

    void WASMCodegenVisitor::export_function_to_js(llvm::Function* func, const std::string& js_name)
    {
        if (!func) return;

        // Add export attribute for Emscripten
        func->addFnAttr("emscripten-export-name", js_name);
        func->setLinkage(llvm::Function::ExternalLinkage);
        func->setDSOLocal(false);
    }

    //===================================================================
    // Helper Methods
    //===================================================================

    llvm::FunctionType* WASMCodegenVisitor::create_wasm_function_type(
        llvm::Type* return_type, const std::vector<llvm::Type*>& param_types)
    {
        return llvm::FunctionType::get(return_type, param_types, false);
    }

    llvm::Value* WASMCodegenVisitor::generate_js_call(
        const std::string& js_function_name,
        const std::vector<llvm::Value*>& args,
        llvm::Type* return_type)
    {
        llvm::Module* module = _context_manager.get_module();
        llvm::IRBuilder<>& builder = _context_manager.get_builder();

        // Get or create the JavaScript function declaration
        llvm::Function* js_func = module->getFunction(js_function_name);
        if (!js_func)
        {
            std::vector<llvm::Type*> param_types;
            for (llvm::Value* arg : args)
            {
                param_types.push_back(arg->getType());
            }
            
            llvm::FunctionType* func_type = create_wasm_function_type(return_type, param_types);
            js_func = llvm::Function::Create(func_type, llvm::Function::ExternalLinkage, 
                                           js_function_name, module);
        }

        return builder.CreateCall(js_func, args);
    }

    llvm::Type* WASMCodegenVisitor::convert_to_wasm_type(Cryo::Type* cryo_type)
    {
        if (!cryo_type) {
            return llvm::Type::getVoidTy(_context_manager.get_llvm_context());
        }

        // Use existing type mapper with WASM-specific adjustments
        llvm::Type* llvm_type = _type_mapper->get_llvm_type(cryo_type);
        
        // WASM-specific type conversions
        llvm::LLVMContext& context = _context_manager.get_llvm_context();
        
        if (llvm_type->isPointerTy()) {
            // In WASM, pointers are 32-bit integers (for WASM32)
            return _wasm_ptr_type;
        }

        return llvm_type;
    }

    llvm::Value* WASMCodegenVisitor::generate_wasm_memory_access(
        llvm::Value* offset, llvm::Type* element_type, bool is_store, llvm::Value* store_value)
    {
        llvm::IRBuilder<>& builder = _context_manager.get_builder();
        llvm::LLVMContext& context = _context_manager.get_llvm_context();

        // Convert offset to pointer
        llvm::Value* ptr = builder.CreateIntToPtr(offset, 
            llvm::PointerType::get(element_type, 0));

        if (is_store && store_value) {
            return builder.CreateStore(store_value, ptr);
        } else {
            return builder.CreateLoad(element_type, ptr);
        }
    }

    void WASMCodegenVisitor::initialize_wasm_types()
    {
        llvm::LLVMContext& context = _context_manager.get_llvm_context();
        
        // WASM32 uses 32-bit pointers, WASM64 uses 64-bit
        _wasm_ptr_type = llvm::Type::getInt32Ty(context); // Assume WASM32 for now
        _wasm_size_type = llvm::Type::getInt32Ty(context);
    }

    void WASMCodegenVisitor::setup_wasm_runtime_functions()
    {
        // Register standard JavaScript interop functions
        register_js_function("cryo_js_console_log", "vi");
        register_js_function("cryo_js_alert", "vi");
        register_js_function("cryo_js_get_timestamp", "d");
    }

    llvm::Function* WASMCodegenVisitor::get_or_create_wasm_alloc_function()
    {
        llvm::Module* module = _context_manager.get_module();
        llvm::Function* alloc_func = module->getFunction("cryo_wasm_alloc");
        
        if (!alloc_func) {
            llvm::LLVMContext& context = _context_manager.get_llvm_context();
            llvm::FunctionType* alloc_type = llvm::FunctionType::get(
                _wasm_ptr_type, {_wasm_size_type}, false);
            
            alloc_func = llvm::Function::Create(alloc_type, 
                llvm::Function::ExternalLinkage, "cryo_wasm_alloc", module);
        }

        return alloc_func;
    }

    llvm::Function* WASMCodegenVisitor::get_or_create_wasm_free_function()
    {
        llvm::Module* module = _context_manager.get_module();
        llvm::Function* free_func = module->getFunction("cryo_wasm_free");
        
        if (!free_func) {
            llvm::LLVMContext& context = _context_manager.get_llvm_context();
            llvm::FunctionType* free_type = llvm::FunctionType::get(
                llvm::Type::getVoidTy(context), {_wasm_ptr_type}, false);
            
            free_func = llvm::Function::Create(free_type, 
                llvm::Function::ExternalLinkage, "cryo_wasm_free", module);
        }

        return free_func;
    }

} // namespace Cryo::WASM