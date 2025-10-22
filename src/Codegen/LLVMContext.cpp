#include "Codegen/LLVMContext.hpp"
#include "Utils/Logger.hpp"
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

    LLVMContextManager::LLVMContextManager(const std::string &module_name)
        : _module_name(module_name), _active_module(nullptr), _main_module(nullptr), _active_module_name(""), _main_module_name(""), _target_triple(""), _cpu_target("generic"), _target_features(""), _initialized(false), _target_machine_created(false), _has_errors(false)
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
        if (_initialized)
        {
            return true;
        }

        clear_errors();

        try
        {
            // Initialize LLVM targets
            if (!initialize_native_target())
            {
                set_error("Failed to initialize native target");
                return false;
            }

            // Detect native target triple if not set
            if (_target_triple.empty())
            {
                _target_triple = detect_native_target_triple();
            }

            // Create initial module (if module name was provided)
            if (!_module_name.empty())
            {
                create_module(_module_name);
            }

            // Create target machine
            if (!create_target_machine())
            {
                set_error("Failed to create target machine");
                return false;
            }

            _initialized = true;
            return true;
        }
        catch (const std::exception &e)
        {
            set_error("Exception during initialization: " + std::string(e.what()));
            return false;
        }
    }

    bool LLVMContextManager::set_target_triple(const std::string &triple)
    {
        _target_triple = triple;

        // Update all existing modules with new target triple
        for (auto &[name, module] : _modules)
        {
            if (module)
            {
                module->setTargetTriple(triple);
            }
        }

        // Recreate target machine with new triple
        if (_initialized)
        {
            return create_target_machine();
        }

        return true;
    }

    void LLVMContextManager::set_cpu_target(const std::string &cpu)
    {
        _cpu_target = cpu;

        // Recreate target machine if already created
        if (_target_machine_created)
        {
            create_target_machine();
        }
    }

    void LLVMContextManager::set_target_features(const std::string &features)
    {
        _target_features = features;

        // Recreate target machine if already created
        if (_target_machine_created)
        {
            create_target_machine();
        }
    }

    bool LLVMContextManager::create_target_machine()
    {
        std::string error;
        const llvm::Target *target = llvm::TargetRegistry::lookupTarget(_target_triple, error);

        if (!target)
        {
            set_error("Failed to lookup target '" + _target_triple + "': " + error);
            return false;
        }

        llvm::TargetOptions options;
        auto reloc_model = llvm::Reloc::PIC_;

        _target_machine = std::unique_ptr<llvm::TargetMachine>(
            target->createTargetMachine(_target_triple, _cpu_target, _target_features,
                                        options, reloc_model));

        if (!_target_machine)
        {
            set_error("Failed to create target machine for '" + _target_triple + "'");
            return false;
        }

        // Configure all modules with target machine data layout
        for (auto &[name, module] : _modules)
        {
            if (module)
            {
                module->setDataLayout(_target_machine->createDataLayout());
            }
        }

        _target_machine_created = true;
        return true;
    }

    //===================================================================
    // Component Access
    //===================================================================

    llvm::DIBuilder *LLVMContextManager::get_debug_builder()
    {
        if (!_debug_builder && _active_module)
        {
            _debug_builder = std::make_unique<llvm::DIBuilder>(*_active_module);
        }
        return _debug_builder.get();
    }

    //===================================================================
    // Module Management
    //===================================================================

    llvm::Module *LLVMContextManager::create_module(const std::string &name)
    {
        // Check if module already exists
        if (_modules.find(name) != _modules.end())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Module '{}' already exists, returning existing module", name);
            return _modules[name].get();
        }

        // Create new module
        auto module = std::make_unique<llvm::Module>(name, *_context);
        module->setTargetTriple(_target_triple);

        if (_target_machine)
        {
            module->setDataLayout(_target_machine->createDataLayout());
        }

        // Store the module and get pointer before moving
        llvm::Module *module_ptr = module.get();
        _modules[name] = std::move(module);

        // Set as active module
        _active_module = module_ptr;
        _active_module_name = name;

        // Set as main module if this is the first module or has same name as constructor parameter
        if (_main_module == nullptr || name == _module_name)
        {
            _main_module = module_ptr;
            _main_module_name = name;
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Set module '{}' as main module", name);
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Created and activated module: {}", name);
        return module_ptr;
    }

    llvm::Module *LLVMContextManager::get_module(const std::string &name) const
    {
        auto it = _modules.find(name);
        return (it != _modules.end()) ? it->second.get() : nullptr;
    }

    bool LLVMContextManager::set_active_module(const std::string &name)
    {
        auto it = _modules.find(name);
        if (it != _modules.end())
        {
            _active_module = it->second.get();
            _active_module_name = name;
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Switched active module to: {}", name);
            return true;
        }
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Cannot switch to module '{}' - does not exist", name);
        return false;
    }

    bool LLVMContextManager::set_main_module(const std::string &name)
    {
        auto it = _modules.find(name);
        if (it != _modules.end())
        {
            _main_module = it->second.get();
            _main_module_name = name;
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Set main module to: {}", name);
            return true;
        }
        return false;
    }

    bool LLVMContextManager::has_module(const std::string &name) const
    {
        return _modules.find(name) != _modules.end();
    }

    std::vector<std::string> LLVMContextManager::get_module_names() const
    {
        std::vector<std::string> names;
        names.reserve(_modules.size());
        for (const auto &pair : _modules)
        {
            names.push_back(pair.first);
        }
        return names;
    }

    bool LLVMContextManager::remove_module(const std::string &name)
    {
        auto it = _modules.find(name);
        if (it != _modules.end())
        {
            // Update active/main pointers if they point to the module being removed
            if (_active_module == it->second.get())
            {
                _active_module = nullptr;
                _active_module_name.clear();
            }
            if (_main_module == it->second.get())
            {
                _main_module = nullptr;
                _main_module_name.clear();
            }

            _modules.erase(it);
            return true;
        }
        return false;
    }

    void LLVMContextManager::set_module_metadata(const std::string &source_file,
                                                 const std::string &compile_flags,
                                                 const std::string &module_name)
    {
        llvm::Module *target_module = module_name.empty() ? _active_module : get_module(module_name);
        if (!target_module)
        {
            return;
        }

        // Set source file metadata
        target_module->setSourceFileName(source_file);

        // Add compile flags as module flag
        if (!compile_flags.empty())
        {
            target_module->addModuleFlag(llvm::Module::Warning, "compile_flags",
                                         llvm::MDString::get(*_context, compile_flags));
        }
    }

    bool LLVMContextManager::verify_module(const std::string &module_name) const
    {
        llvm::Module *target_module = module_name.empty() ? _active_module : get_module(module_name);
        if (!target_module)
        {
            return false;
        }

        // Disable LLVM error output to prevent type dump spam that can trigger segfaults
        return !llvm::verifyModule(*target_module, nullptr);
    }

    void LLVMContextManager::print_module(std::ostream &os, const std::string &module_name) const
    {
        llvm::Module *target_module = module_name.empty() ? _active_module : get_module(module_name);
        if (!target_module)
        {
            os << "; No module available for printing" << std::endl;
            return;
        }

        // Create a raw_ostream wrapper for std::ostream
        std::string str;
        llvm::raw_string_ostream string_stream(str);
        target_module->print(string_stream, nullptr);
        os << string_stream.str();
    }

    //===================================================================
    // Target Information
    //===================================================================

    bool LLVMContextManager::is_64bit() const
    {
        if (!_target_machine)
        {
            return sizeof(void *) == 8; // Fallback to host
        }

        return _target_machine->getPointerSize(0) == 8;
    }

    size_t LLVMContextManager::get_pointer_size() const
    {
        if (!_target_machine)
        {
            return sizeof(void *); // Fallback to host
        }

        return _target_machine->getPointerSize(0);
    }

    size_t LLVMContextManager::get_type_alignment(llvm::Type *type) const
    {
        if (!_target_machine || !type)
        {
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

    void LLVMContextManager::set_error(const std::string &message)
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
        if (initialized)
        {
            return true;
        }

        try
        {
            llvm::InitializeAllTargetInfos();
            llvm::InitializeAllTargets();
            llvm::InitializeAllTargetMCs();
            llvm::InitializeAllAsmParsers();
            llvm::InitializeAllAsmPrinters();

            initialized = true;
            return true;
        }
        catch (const std::exception &e)
        {
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
        for (const auto &target : llvm::TargetRegistry::targets())
        {
            targets.push_back(std::string(target.getName()));
        }

        return targets;
    }

    bool is_target_supported(const std::string &triple)
    {
        std::string error;
        const llvm::Target *target = llvm::TargetRegistry::lookupTarget(triple, error);
        return target != nullptr;
    }

} // namespace Cryo::Codegen