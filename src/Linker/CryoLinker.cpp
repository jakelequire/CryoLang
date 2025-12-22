#include "Linker/CryoLinker.hpp"
#include "Utils/Logger.hpp"
#include "Utils/OS.hpp"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>
#include <iostream>
#include <filesystem>
#include <cstdlib>

namespace Cryo::Linker
{
    //===================================================================
    // Construction and Configuration
    //===================================================================

    CryoLinker::CryoLinker(Cryo::SymbolTable &symbol_table)
        : _symbol_table(symbol_table), _link_mode(LinkMode::Dynamic), _entry_point("main"), _debug_symbols(false), _optimization_level(2), _runtime_linking_enabled(true), _runtime_library_name("cryoruntime"), _has_errors(false), _initialized(false), _modules_linked(0), _objects_linked(0), _libraries_linked(0)
    {
// Set default target triple based on current platform
#if defined(_WIN32) || defined(_WIN64)
        _target_triple = "x86_64-w64-windows-gnu"; // Use MinGW instead of MSVC
#elif defined(__APPLE__)
        _target_triple = "x86_64-apple-macosx";
#else
        _target_triple = "x86_64-pc-linux-gnu";
#endif
    }

    //===================================================================
    // Primary Linking Interface
    //===================================================================

    bool CryoLinker::link_modules(
        const std::vector<llvm::Module *> &modules,
        const std::string &output_path,
        LinkTarget target)
    {
        clear_errors();

        if (!_initialized && !initialize_runtime())
        {
            report_error("Failed to initialize runtime functions");
            return false;
        }

        if (modules.empty())
        {
            report_error("No modules provided for linking");
            return false;
        }

        // For now, we support single module linking
        // TODO: Implement multi-module linking
        if (modules.size() > 1)
        {
            report_error("Multi-module linking not yet implemented");
            return false;
        }

        llvm::Module *module = modules[0];

        // Generate appropriate output based on target type
        switch (target)
        {
        case LinkTarget::Executable:
            return generate_executable(module, output_path);
        case LinkTarget::SharedLibrary:
            return generate_shared_library(module, output_path);
        case LinkTarget::StaticLibrary:
            return generate_static_library(module, output_path);
        case LinkTarget::ObjectFile:
            return generate_object_file(module, output_path);
        }

        return false;
    }

    void CryoLinker::add_object_file(const std::string &object_path)
    {
        _object_files.push_back(object_path);
    }

    void CryoLinker::add_library(const std::string &library_name, bool is_static)
    {
        _libraries.emplace_back(library_name, is_static);
    }

    void CryoLinker::add_library_path(const std::string &path)
    {
        _library_paths.push_back(path);
    }

    //===================================================================
    // Runtime Library Integration
    //===================================================================

    bool CryoLinker::initialize_runtime()
    {
        if (_initialized)
        {
            return true;
        }

        clear_errors();

        if (!initialize_runtime_functions())
        {
            report_error("Failed to initialize runtime function registry");
            return false;
        }

        _initialized = true;
        return true;
    }

    void CryoLinker::add_runtime_path(const std::string &path)
    {
        _runtime_paths.push_back(path);
    }

    void CryoLinker::set_runtime_library_name(const std::string &name)
    {
        _runtime_library_name = name;
    }

    void CryoLinker::enable_runtime_linking(bool enable)
    {
        _runtime_linking_enabled = enable;
    }

    llvm::Function *CryoLinker::declare_runtime_function(llvm::Module *module, const std::string &cryo_name)
    {
        auto it = _runtime_functions.find(cryo_name);
        if (it == _runtime_functions.end())
        {
            return nullptr;
        }

        const RuntimeFunction &runtime_func = it->second;

        // Check if function is already declared in this module
        if (llvm::Function *existing = module->getFunction(runtime_func.runtime_name))
        {
            return existing;
        }

        // Create function declaration
        llvm::Function *func = llvm::Function::Create(
            runtime_func.type,
            llvm::Function::ExternalLinkage,
            runtime_func.runtime_name,
            module);

        return func;
    }

    const CryoLinker::RuntimeFunction *CryoLinker::get_runtime_function(const std::string &cryo_name)
    {
        auto it = _runtime_functions.find(cryo_name);
        return (it != _runtime_functions.end()) ? &it->second : nullptr;
    }

    bool CryoLinker::is_runtime_function(const std::string &name)
    {
        return _runtime_functions.find(name) != _runtime_functions.end();
    }

    //===================================================================
    // Configuration
    //===================================================================

    void CryoLinker::set_link_mode(LinkMode mode)
    {
        _link_mode = mode;
    }

    void CryoLinker::set_target_triple(const std::string &triple)
    {
        _target_triple = triple;
    }

    void CryoLinker::set_debug_symbols(bool enable)
    {
        _debug_symbols = enable;
    }

    void CryoLinker::set_optimization_level(int level)
    {
        _optimization_level = std::max(0, std::min(3, level));
    }

    void CryoLinker::add_linker_flag(const std::string &flag)
    {
        _linker_flags.push_back(flag);
    }

    void CryoLinker::set_entry_point(const std::string &entry_point)
    {
        _entry_point = entry_point;
    }

    //===================================================================
    // Output Format Support
    //===================================================================

    bool CryoLinker::generate_executable(llvm::Module *module, const std::string &output_path)
    {
        LOG_DEBUG(Cryo::LogComponent::LINKER, "CryoLinker::generate_executable called with output_path: {}", output_path);

        // Step 1: Generate object file from LLVM IR
        std::string temp_obj = output_path + ".o";
        LOG_DEBUG(Cryo::LogComponent::LINKER, "Generating object file: {}", temp_obj);

        if (!generate_object_file(module, temp_obj))
        {
            LOG_ERROR(Cryo::LogComponent::LINKER, "generate_object_file failed!");
            return false;
        }

        LOG_DEBUG(Cryo::LogComponent::LINKER, "Object file generated successfully");

        // Step 2: Use system linker to create executable with runtime
        std::vector<std::string> linker_args;

        // Add the generated object file
        linker_args.push_back(temp_obj);

        // Add all object files that were added via add_object_file (including libcryo.a)
        for (const auto &obj_file : _object_files)
        {
            linker_args.push_back(obj_file);
            LOG_DEBUG(Cryo::LogComponent::LINKER, "Added object file: {}", obj_file);
        }

        // Add library paths
        for (const auto &lib_path : _library_paths)
        {
            linker_args.push_back("-L" + lib_path);
            LOG_DEBUG(Cryo::LogComponent::LINKER, "Added library path: -L{}", lib_path);
        }

        // Add libraries
        for (const auto &[lib_name, is_static] : _libraries)
        {
            if (is_static)
            {
                linker_args.push_back("-Wl,-Bstatic");
            }
            linker_args.push_back("-l" + lib_name);
            if (is_static)
            {
                linker_args.push_back("-Wl,-Bdynamic");
            }
            LOG_DEBUG(Cryo::LogComponent::LINKER, "Added library: -l{}{}", lib_name, (is_static ? " (static)" : " (dynamic)"));
        }

        // Add standard libraries
        linker_args.push_back("-lm");       // math library
        linker_args.push_back("-lpthread"); // pthread library

        // #if defined(_WIN32) || defined(_WIN64)
        //         // For Windows console applications, specify the console subsystem
        //         linker_args.push_back("-Wl,--subsystem,console");
        // #endif

        // Add output specification
        linker_args.push_back("-o");
        linker_args.push_back(output_path);

        LOG_DEBUG(Cryo::LogComponent::LINKER, "About to call execute_linker_command with {} args", linker_args.size());
        for (size_t i = 0; i < linker_args.size(); ++i)
        {
            LOG_TRACE(Cryo::LogComponent::LINKER, "linker_args[{}] = '{}'", i, linker_args[i]);
        }

        // Execute system linker
        bool success = execute_linker_command(linker_args);

        LOG_DEBUG(Cryo::LogComponent::LINKER, "execute_linker_command returned: {}", (success ? "true" : "false"));

        // Clean up temp object file
        std::filesystem::remove(temp_obj);

        return success;
    }

    bool CryoLinker::generate_shared_library(llvm::Module *module, const std::string &output_path)
    {
        // TODO: Implement shared library generation
        report_error("Shared library generation not yet implemented");
        return false;
    }

    bool CryoLinker::generate_static_library(llvm::Module *module, const std::string &output_path)
    {
        // TODO: Implement static library generation
        report_error("Static library generation not yet implemented");
        return false;
    }

    bool CryoLinker::generate_object_file(llvm::Module *module, const std::string &output_path)
    {
        llvm::InitializeAllTargetInfos();
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();
        llvm::InitializeAllAsmParsers();
        llvm::InitializeAllAsmPrinters();

        std::string error;
        auto target_triple = _target_triple; // Use our configured target triple
        module->setTargetTriple(target_triple);

        auto target = llvm::TargetRegistry::lookupTarget(target_triple, error);
        if (!target)
        {
            report_error("Failed to lookup target: " + error);
            return false;
        }

        auto cpu = "generic";
        auto features = "";
        llvm::TargetOptions opt;
        auto rm = std::make_optional<llvm::Reloc::Model>(llvm::Reloc::PIC_);
        auto target_machine = std::unique_ptr<llvm::TargetMachine>(
            target->createTargetMachine(target_triple, cpu, features, opt, rm));

        module->setDataLayout(target_machine->createDataLayout());

        std::error_code ec;
        llvm::raw_fd_ostream dest(output_path, ec, llvm::sys::fs::OF_None);
        if (ec)
        {
            report_error("Could not open file: " + ec.message());
            return false;
        }

        llvm::legacy::PassManager pass;
        auto file_type = llvm::CodeGenFileType::ObjectFile;

        if (target_machine->addPassesToEmitFile(pass, dest, nullptr, file_type))
        {
            report_error("TargetMachine can't emit a file of this type");
            return false;
        }

        // Debug: Check if global constructors are still present before pass run
        auto *global_ctors_before = module->getNamedGlobal("llvm.global_ctors");
        if (global_ctors_before)
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Before PassManager run: @llvm.global_ctors found");

            // Mark the global constructor array as used to prevent optimization removal
            global_ctors_before->setLinkage(llvm::GlobalValue::ExternalLinkage);

            // Also try to mark any global constructor functions as used
            for (auto &func : module->getFunctionList())
            {
                if (func.getName().contains("cryo_global_constructors"))
                {
                    func.setLinkage(llvm::GlobalValue::ExternalLinkage);
                    LOG_DEBUG(Cryo::LogComponent::GENERAL, "Marked function {} as external linkage", func.getName().str());
                }
            }
        }
        else
        {
            LOG_ERROR(Cryo::LogComponent::GENERAL, "Before PassManager run: @llvm.global_ctors NOT found!");
        }

        pass.run(*module);

        // Debug: Check if global constructors are still present after pass run
        auto *global_ctors_after = module->getNamedGlobal("llvm.global_ctors");
        if (global_ctors_after)
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "After PassManager run: @llvm.global_ctors found");
        }
        else
        {
            LOG_ERROR(Cryo::LogComponent::GENERAL, "After PassManager run: @llvm.global_ctors REMOVED by PassManager!");
        }
        dest.flush();

        return true;
    }

    //===================================================================
    // Cross-Platform Support
    //===================================================================

    std::string CryoLinker::get_system_linker()
    {
#if defined(_WIN32) || defined(_WIN64)
        return "link.exe";
#else
        return "ld";
#endif
    }

    std::string CryoLinker::get_library_extension(bool is_shared) const
    {
        return Cryo::Utils::OS::instance().get_library_extension(is_shared);
    }

    std::string CryoLinker::get_executable_extension()
    {
        return Cryo::Utils::OS::instance().get_executable_extension();
    }

    //===================================================================
    // Diagnostics and Debugging
    //===================================================================

    void CryoLinker::print_link_info(std::ostream &os) const
    {
        os << "=== CryoLinker Configuration ===\n";
        os << "Target Triple: " << _target_triple << "\n";
        os << "Link Mode: " << static_cast<int>(_link_mode) << "\n";
        os << "Entry Point: " << _entry_point << "\n";
        os << "Debug Symbols: " << (_debug_symbols ? "enabled" : "disabled") << "\n";
        os << "Optimization Level: " << _optimization_level << "\n";
        os << "Runtime Linking: " << (_runtime_linking_enabled ? "enabled" : "disabled") << "\n";
        os << "Runtime Library: " << _runtime_library_name << "\n";

        os << "\nObject Files (" << _object_files.size() << "):\n";
        for (const auto &obj : _object_files)
        {
            os << "  " << obj << "\n";
        }

        os << "\nLibraries (" << _libraries.size() << "):\n";
        for (const auto &[name, is_static] : _libraries)
        {
            os << "  " << name << " (" << (is_static ? "static" : "dynamic") << ")\n";
        }

        os << "\nLibrary Paths (" << _library_paths.size() << "):\n";
        for (const auto &path : _library_paths)
        {
            os << "  " << path << "\n";
        }
    }

    void CryoLinker::print_stats(std::ostream &os) const
    {
        os << "=== CryoLinker Statistics ===\n";
        os << "Modules Linked: " << _modules_linked << "\n";
        os << "Objects Linked: " << _objects_linked << "\n";
        os << "Libraries Linked: " << _libraries_linked << "\n";
        os << "Runtime Functions Registered: " << _runtime_functions.size() << "\n";
    }

    std::vector<std::string> CryoLinker::get_linker_args() const
    {
        std::vector<std::string> args;

        // Add linker flags
        args.insert(args.end(), _linker_flags.begin(), _linker_flags.end());

        // Add library paths
        for (const auto &path : _library_paths)
        {
            args.push_back("-L" + path);
        }

        // Add libraries
        for (const auto &[name, is_static] : _libraries)
        {
            if (is_static)
            {
                args.push_back("-Wl,-Bstatic");
            }
            args.push_back("-l" + name);
            if (is_static)
            {
                args.push_back("-Wl,-Bdynamic");
            }
        }

        // Add runtime library if enabled
        if (_runtime_linking_enabled)
        {
            std::string runtime_path = find_runtime_library();
            if (!runtime_path.empty())
            {
                args.push_back(runtime_path);
            }
        }

        return args;
    }

    //===================================================================
    // Private Methods
    //===================================================================

    bool CryoLinker::initialize_runtime_functions()
    {
        // TODO: Initialize runtime function registry with actual function signatures
        // For now, this is a placeholder
        return true;
    }

    void CryoLinker::register_runtime_function(
        const std::string &cryo_name,
        const std::string &runtime_name,
        llvm::Type *return_type,
        const std::vector<llvm::Type *> &param_types,
        bool is_variadic,
        const std::string &description)
    {
        RuntimeFunction func;
        func.cryo_name = cryo_name;
        func.runtime_name = runtime_name;
        func.is_variadic = is_variadic;
        func.description = description;

        // Create function type
        // Note: This requires LLVM context, which should be provided by the code generator
        // For now, we store nullptrs and will populate them when needed
        func.type = nullptr;
        func.declaration = nullptr;

        _runtime_functions[cryo_name] = func;
    }

    bool CryoLinker::execute_linker_command(const std::vector<std::string> &args)
    {
        // Build the linker command
        std::vector<std::string> full_command;
#if defined(_WIN32) || defined(_WIN64)
        full_command.push_back("C:/msys64/mingw64/bin/clang++"); // Use MinGW clang++ on Windows
                                                                 // Skip PIE flags on Windows as they can cause runtime issues with MinGW
#else
        full_command.push_back("clang++"); // Use system clang++ on other platforms
        // Add PIE-related flags now that libcryo.a is compiled with -fPIC
        full_command.push_back("-fPIE");
        // Add debug symbols for better debugging
        full_command.push_back("-g");
#endif

        // Add all the linker arguments
        for (const auto &arg : args)
        {
            full_command.push_back(arg);
        }

        // Add any additional linker flags
        for (const auto &flag : _linker_flags)
        {
            full_command.push_back(flag);
        }

        // Build command string for execution
        std::string cmd = full_command[0];
        for (size_t i = 1; i < full_command.size(); ++i)
        {
            cmd += " \"" + full_command[i] + "\"";
        }

        // Log the command for debugging
        LOG_DEBUG(Cryo::LogComponent::LINKER, "Executing linker command: {}", cmd);

        // On Windows, redirect stderr to capture error output
#if defined(_WIN32) || defined(_WIN64)
        std::string cmd_with_redirect = cmd + " 2>&1";
        FILE *pipe = _popen(cmd_with_redirect.c_str(), "r");
        if (!pipe)
        {
            report_error("Failed to execute linker command: " + cmd);
            return false;
        }

        std::string output;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
        {
            output += buffer;
        }

        int result = _pclose(pipe);
#else
        // For Unix-like systems, use popen to capture output
        std::string cmd_with_redirect = cmd + " 2>&1";
        FILE *pipe = popen(cmd_with_redirect.c_str(), "r");
        if (!pipe)
        {
            report_error("Failed to execute linker command: " + cmd);
            return false;
        }

        std::string output;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
        {
            output += buffer;
        }

        int result = pclose(pipe);
#endif

        if (result != 0)
        {
            std::string error_msg = "System linker failed with exit code " + std::to_string(result);
            error_msg += "\nLinker command: " + cmd;
            if (!output.empty())
            {
                error_msg += "\nLinker output:\n" + output;
            }
            report_error(error_msg);
            return false;
        }

        // If there was output but success, log it as info
        if (!output.empty())
        {
            LOG_INFO(Cryo::LogComponent::LINKER, "Linker output: {}", output);
        }

        return true;
    }

    std::vector<std::string> CryoLinker::build_linker_args(
        const std::string &output_path,
        LinkTarget target,
        const std::vector<std::string> &input_files)
    {
        std::vector<std::string> args;

        // Add output specification
        args.push_back("-o");
        args.push_back(output_path);

        // Add input files
        args.insert(args.end(), input_files.begin(), input_files.end());

        // Add target-specific flags
        switch (target)
        {
        case LinkTarget::SharedLibrary:
            args.push_back("-shared");
            break;
        case LinkTarget::StaticLibrary:
            // Static libraries use 'ar' not 'ld'
            break;
        default:
            break;
        }

        // Add debug symbols if enabled
        if (_debug_symbols)
        {
            args.push_back("-g");
        }

        return args;
    }

    std::string CryoLinker::find_runtime_library() const
    {
        std::string lib_name = "lib" + _runtime_library_name;
        std::string extension = get_library_extension(_link_mode == LinkMode::Dynamic);
        std::string full_name = lib_name + extension;

        // Check runtime paths
        auto &os = Cryo::Utils::OS::instance();
        for (const auto &path : _runtime_paths)
        {
            std::string lib_path = os.join_path(path, full_name);
            if (os.path_exists(lib_path))
            {
                return lib_path;
            }
        }

        // Check default paths
        auto default_paths = get_default_runtime_paths();
        for (const auto &path : default_paths)
        {
            std::string lib_path = os.join_path(path, full_name);
            if (os.path_exists(lib_path))
            {
                return lib_path;
            }
        }

        return "";
    }

    void CryoLinker::report_error(const std::string &message)
    {
        _has_errors = true;
        _last_error = message;
    }

    void CryoLinker::clear_errors()
    {
        _has_errors = false;
        _last_error.clear();
    }

    // Placeholder implementations for LLVM type getters
    // These would need proper LLVM context in a real implementation
    llvm::Type *CryoLinker::get_i8_type() { return nullptr; }
    llvm::Type *CryoLinker::get_i32_type() { return nullptr; }
    llvm::Type *CryoLinker::get_i64_type() { return nullptr; }
    llvm::Type *CryoLinker::get_f64_type() { return nullptr; }
    llvm::Type *CryoLinker::get_void_type() { return nullptr; }
    llvm::PointerType *CryoLinker::get_i8_ptr_type() { return nullptr; }

    //=======================================================================
    // Factory Functions
    //=======================================================================

    std::unique_ptr<CryoLinker> create_default_linker(Cryo::SymbolTable &symbol_table)
    {
        return std::make_unique<CryoLinker>(symbol_table);
    }

    std::unique_ptr<CryoLinker> create_target_linker(
        const std::string &target_triple,
        Cryo::SymbolTable &symbol_table)
    {
        auto linker = std::make_unique<CryoLinker>(symbol_table);
        linker->set_target_triple(target_triple);
        return linker;
    }

    //=======================================================================
    // Utility Functions
    //=======================================================================

    std::vector<std::string> get_default_runtime_paths()
    {
        std::vector<std::string> paths;

        // Add common runtime library paths
        paths.push_back("./bin");
        paths.push_back("/usr/local/lib");
        paths.push_back("/usr/lib");

#if defined(_WIN32) || defined(_WIN64)
        paths.push_back("C:\\Programming\\apps\\CryoLang\\bin");
#endif

        return paths;
    }

    bool runtime_library_exists(const std::string &path, const std::string &library_name)
    {
        auto &os = Cryo::Utils::OS::instance();

        // Check standard Unix library formats (static and shared)
        std::string static_lib = os.join_path(path, "lib" + library_name + os.get_library_extension(false));
        if (os.path_exists(static_lib))
        {
            return true;
        }

        std::string shared_lib = os.join_path(path, "lib" + library_name + os.get_library_extension(true));
        if (os.path_exists(shared_lib))
        {
            return true;
        }

        // Check Windows-specific formats (without "lib" prefix)
        if (os.is_windows())
        {
            std::string win_static = os.join_path(path, library_name + ".lib");
            if (os.path_exists(win_static))
            {
                return true;
            }

            std::string win_shared = os.join_path(path, library_name + ".dll");
            if (os.path_exists(win_shared))
            {
                return true;
            }
        }

        return false;
    }

} // namespace Cryo::Linker