#include "Codegen/LLVMContext.hpp"
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <iostream>

namespace Cryo::Codegen
{
    //===================================================================
    // Construction & Initialization
    //===================================================================

    LLVMContextManager::LLVMContextManager(const std::string& module_name)
        : _module_name(module_name)
        , _target_triple("")
        , _cpu_target("generic")
        , _target_features("")
        , _initialized(false)
        , _target_machine_created(false)
        , _has_errors(false)
    {
        // Initialize LLVM components
        _context = std::make_unique<llvm::LLVMContext>();
        _builder = std::make_unique<llvm::IRBuilder<>>(*_context);
    }

    LLVMContextManager::~LLVMContextManager()
    {
        // LLVM components are managed by unique_ptr, so they'll be cleaned up automatically
    }

    //===================================================================
    // Initialization & Configuration
    //===================================================================

    bool LLVMContextManager::initialize()
    {
        if (_initialized) {
            return true;
        }

        clear_errors();

        try {
            // Initialize LLVM targets
            if (!initialize_native_target()) {
                set_error("Failed to initialize native target");
                return false;
            }

            // Detect native target triple if not set
            if (_target_triple.empty()) {
                _target_triple = detect_native_target_triple();
            }

            // Create module
            _module = std::make_unique<llvm::Module>(_module_name, *_context);
            _module->setTargetTriple(_target_triple);

            // Create target machine
            if (!create_target_machine()) {
                set_error("Failed to create target machine");
                return false;
            }

            _initialized = true;
            return true;
        }
        catch (const std::exception& e) {
            set_error("Exception during initialization: " + std::string(e.what()));
            return false;
        }
    }

    bool LLVMContextManager::set_target_triple(const std::string& triple)
    {
        _target_triple = triple;
        
        if (_module) {
            _module->setTargetTriple(triple);
        }

        // Recreate target machine with new triple
        if (_initialized) {
            return create_target_machine();
        }

        return true;
    }

    void LLVMContextManager::set_cpu_target(const std::string& cpu)
    {
        _cpu_target = cpu;
        
        // Recreate target machine if already created
        if (_target_machine_created) {
            create_target_machine();
        }
    }

    void LLVMContextManager::set_target_features(const std::string& features)
    {
        _target_features = features;
        
        // Recreate target machine if already created
        if (_target_machine_created) {
            create_target_machine();
        }
    }

    bool LLVMContextManager::create_target_machine()
    {
        std::string error;
        const llvm::Target* target = llvm::TargetRegistry::lookupTarget(_target_triple, error);
        
        if (!target) {
            set_error("Failed to lookup target '" + _target_triple + "': " + error);
            return false;
        }

        llvm::TargetOptions options;
        auto reloc_model = llvm::Reloc::PIC_;
        
        _target_machine = std::unique_ptr<llvm::TargetMachine>(
            target->createTargetMachine(_target_triple, _cpu_target, _target_features, 
                                       options, reloc_model)
        );

        if (!_target_machine) {
            set_error("Failed to create target machine for '" + _target_triple + "'");
            return false;
        }

        // Configure module with target machine data layout
        if (_module) {
            _module->setDataLayout(_target_machine->createDataLayout());
        }

        _target_machine_created = true;
        return true;
    }

    //===================================================================
    // Component Access
    //===================================================================

    llvm::DIBuilder* LLVMContextManager::get_debug_builder()
    {
        if (!_debug_builder && _module) {
            _debug_builder = std::make_unique<llvm::DIBuilder>(*_module);
        }
        return _debug_builder.get();
    }

    //===================================================================
    // Module Management
    //===================================================================

    llvm::Module* LLVMContextManager::create_module(const std::string& name)
    {
        _module = std::make_unique<llvm::Module>(name, *_context);
        _module->setTargetTriple(_target_triple);
        
        if (_target_machine) {
            _module->setDataLayout(_target_machine->createDataLayout());
        }

        return _module.get();
    }

    void LLVMContextManager::set_module_metadata(const std::string& source_file, 
                                                 const std::string& compile_flags)
    {
        if (!_module) {
            return;
        }

        // Set source file metadata
        _module->setSourceFileName(source_file);

        // Add compile flags as module flag
        if (!compile_flags.empty()) {
            _module->addModuleFlag(llvm::Module::Warning, "compile_flags", 
                                  llvm::MDString::get(*_context, compile_flags));
        }
    }

    bool LLVMContextManager::verify_module() const
    {
        if (!_module) {
            return false;
        }

        return !llvm::verifyModule(*_module, &llvm::errs());
    }

    void LLVMContextManager::print_module(std::ostream& os) const
    {
        if (_module) {
            llvm::raw_ostream* llvm_os = nullptr;
            // Create a raw_ostream wrapper for std::ostream
            std::string str;
            llvm::raw_string_ostream string_stream(str);
            _module->print(string_stream, nullptr);
            os << string_stream.str();
        }
    }

    //===================================================================
    // Target Information
    //===================================================================

    bool LLVMContextManager::is_64bit() const
    {
        if (!_target_machine) {
            return sizeof(void*) == 8; // Fallback to host
        }

        return _target_machine->getPointerSize(0) == 8;
    }

    size_t LLVMContextManager::get_pointer_size() const
    {
        if (!_target_machine) {
            return sizeof(void*); // Fallback to host
        }

        return _target_machine->getPointerSize(0);
    }

    size_t LLVMContextManager::get_type_alignment(llvm::Type* type) const
    {
        if (!_target_machine || !type) {
            return 1;
        }

        return _target_machine->createDataLayout().getABITypeAlign(type).value();
    }

    //===================================================================
    // Error Handling
    //===================================================================

    void LLVMContextManager::clear_errors()
    {
        _has_errors = false;
        _last_error.clear();
    }

    //===================================================================
    // Private Methods
    //===================================================================

    bool LLVMContextManager::initialize_native_target()
    {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
        
        // For cross-compilation support, also initialize all targets
        initialize_all_targets();
        
        return true;
    }

    void LLVMContextManager::initialize_all_targets()
    {
        llvm::InitializeAllTargetInfos();
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();
        llvm::InitializeAllAsmParsers();
        llvm::InitializeAllAsmPrinters();
    }

    std::string LLVMContextManager::detect_native_target_triple()
    {
        return llvm::sys::getDefaultTargetTriple();
    }

    void LLVMContextManager::set_error(const std::string& message)
    {
        _has_errors = true;
        _last_error = message;
    }

    //=======================================================================
    // Utility Functions
    //=======================================================================

    bool initialize_llvm_for_cryo()
    {
        static bool initialized = false;
        if (initialized) {
            return true;
        }

        try {
            llvm::InitializeAllTargetInfos();
            llvm::InitializeAllTargets();
            llvm::InitializeAllTargetMCs();
            llvm::InitializeAllAsmParsers();
            llvm::InitializeAllAsmPrinters();
            
            initialized = true;
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "Failed to initialize LLVM: " << e.what() << std::endl;
            return false;
        }
    }

    void shutdown_llvm_for_cryo()
    {
        // LLVM doesn't require explicit shutdown for most components in modern versions
        // llvm_shutdown() has been removed in newer LLVM versions
    }

    std::vector<std::string> get_supported_targets()
    {
        std::vector<std::string> targets;
        
        // Get all available targets
        for (const auto& target : llvm::TargetRegistry::targets()) {
            targets.push_back(std::string(target.getName()));
        }
        
        return targets;
    }

    bool is_target_supported(const std::string& triple)
    {
        std::string error;
        const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, error);
        return target != nullptr;
    }

} // namespace Cryo::Codegen