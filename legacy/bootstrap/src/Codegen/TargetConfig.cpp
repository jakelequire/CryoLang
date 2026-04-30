#include "Codegen/TargetConfig.hpp"
#include <llvm/TargetParser/Host.h>
#include <llvm/MC/TargetRegistry.h>
#include <algorithm>
#include <sstream>

namespace Cryo::Codegen
{
    //===================================================================
    // Construction
    //===================================================================

    TargetConfig::TargetConfig()
        : _target_triple(llvm::sys::getDefaultTargetTriple()), _cpu("generic"), _features(""), _code_model(CodeModel::Small), _relocation_model(RelocationModel::PIC), _optimization_level(OptimizationLevel::Default), _debug_info(false), _frame_pointer(true), _stack_protection(1), _static_linking(false), _triple_parsed(false)
    {
        initialize_defaults();
    }

    TargetConfig::TargetConfig(const std::string &target_triple)
        : _target_triple(target_triple), _cpu("generic"), _features(""), _code_model(CodeModel::Small), _relocation_model(RelocationModel::PIC), _optimization_level(OptimizationLevel::Default), _debug_info(false), _frame_pointer(true), _stack_protection(1), _static_linking(false), _triple_parsed(false)
    {
        initialize_defaults();
    }

    //===================================================================
    // Target Configuration
    //===================================================================

    void TargetConfig::set_target_triple(const std::string &triple)
    {
        _target_triple = triple;
        _triple_parsed = false; // Force re-parsing
    }

    void TargetConfig::set_cpu(const std::string &cpu)
    {
        _cpu = cpu;
    }

    void TargetConfig::set_features(const std::string &features)
    {
        _features = features;
    }

    void TargetConfig::add_feature(const std::string &feature)
    {
        if (_features.empty())
        {
            _features = feature;
        }
        else
        {
            _features += "," + feature;
        }
    }

    //===================================================================
    // Platform Detection
    //===================================================================

    bool TargetConfig::is_windows() const
    {
        parse_target_triple();
        return _cached_os == "windows" || _target_triple.find("windows") != std::string::npos;
    }

    bool TargetConfig::is_linux() const
    {
        parse_target_triple();
        return _cached_os == "linux" || _target_triple.find("linux") != std::string::npos;
    }

    bool TargetConfig::is_macos() const
    {
        parse_target_triple();
        return _cached_os == "darwin" || _cached_os == "macos" ||
               _target_triple.find("darwin") != std::string::npos ||
               _target_triple.find("macos") != std::string::npos;
    }

    bool TargetConfig::is_64bit() const
    {
        parse_target_triple();
        return _cached_arch == "x86_64" || _cached_arch == "aarch64" ||
               _cached_arch == "ppc64" || _cached_arch == "s390x" ||
               _cached_arch == "wasm64";
    }

    bool TargetConfig::is_wasm() const
    {
        parse_target_triple();
        return _cached_arch == "wasm32" || _cached_arch == "wasm64" ||
               _target_triple.find("wasm") != std::string::npos;
    }

    bool TargetConfig::is_msvc() const
    {
        parse_target_triple();
        return _cached_env == "msvc" || _target_triple.find("msvc") != std::string::npos;
    }

    std::string TargetConfig::get_architecture() const
    {
        parse_target_triple();
        return _cached_arch;
    }

    std::string TargetConfig::get_os() const
    {
        parse_target_triple();
        return _cached_os;
    }

    std::string TargetConfig::get_environment() const
    {
        parse_target_triple();
        return _cached_env;
    }

    //===================================================================
    // ABI and Calling Conventions
    //===================================================================

    std::string TargetConfig::get_default_calling_convention() const
    {
        if (is_windows())
        {
            return is_64bit() ? "fastcall" : "stdcall";
        }
        return "cdecl";
    }

    int TargetConfig::get_pointer_size_bits() const
    {
        return is_64bit() ? 64 : 32;
    }

    int TargetConfig::get_pointer_alignment() const
    {
        return is_64bit() ? 8 : 4;
    }

    bool TargetConfig::supports_tls() const
    {
        // Most modern targets support TLS
        return !is_windows() || is_msvc();
    }

    //===================================================================
    // Factory Methods
    //===================================================================

    std::unique_ptr<TargetConfig> TargetConfig::create_native()
    {
        return std::make_unique<TargetConfig>();
    }

    std::unique_ptr<TargetConfig> TargetConfig::create_windows_x64()
    {
        auto config = std::make_unique<TargetConfig>("x86_64-pc-windows-msvc");
        config->set_cpu("x86-64");
        return config;
    }

    std::unique_ptr<TargetConfig> TargetConfig::create_linux_x64()
    {
        auto config = std::make_unique<TargetConfig>("x86_64-pc-linux-gnu");
        config->set_cpu("x86-64");
        return config;
    }

    std::unique_ptr<TargetConfig> TargetConfig::create_macos_x64()
    {
        auto config = std::make_unique<TargetConfig>("x86_64-apple-macosx");
        config->set_cpu("x86-64");
        return config;
    }

    std::unique_ptr<TargetConfig> TargetConfig::create_macos_arm64()
    {
        auto config = std::make_unique<TargetConfig>("aarch64-apple-macosx");
        config->set_cpu("apple-m1");
        return config;
    }

    std::unique_ptr<TargetConfig> TargetConfig::create_wasm32()
    {
        auto config = std::make_unique<TargetConfig>("wasm32-unknown-emscripten");
        config->set_cpu("generic");
        config->set_code_model(CodeModel::Small);
        config->set_relocation_model(RelocationModel::PIC);
        config->set_static_linking(true); // WASM uses static linking
        return config;
    }

    std::unique_ptr<TargetConfig> TargetConfig::create_wasm64()
    {
        auto config = std::make_unique<TargetConfig>("wasm64-unknown-emscripten");
        config->set_cpu("generic");
        config->set_code_model(CodeModel::Small);
        config->set_relocation_model(RelocationModel::PIC);
        config->set_static_linking(true); // WASM uses static linking
        return config;
    }

    //===================================================================
    // Validation
    //===================================================================

    bool TargetConfig::validate() const
    {
        if (_target_triple.empty())
        {
            return false;
        }

        if (_cpu.empty())
        {
            return false;
        }

        // Check if target triple is supported by LLVM
        std::string error;
        const llvm::Target *target = llvm::TargetRegistry::lookupTarget(_target_triple, error);
        return target != nullptr;
    }

    std::vector<std::string> TargetConfig::get_validation_errors() const
    {
        std::vector<std::string> errors;

        if (_target_triple.empty())
        {
            errors.push_back("Target triple is empty");
        }

        if (_cpu.empty())
        {
            errors.push_back("CPU target is empty");
        }

        // Check LLVM target support
        std::string error;
        const llvm::Target *target = llvm::TargetRegistry::lookupTarget(_target_triple, error);
        if (!target)
        {
            errors.push_back("Target triple not supported by LLVM: " + error);
        }

        return errors;
    }

    //===================================================================
    // Private Methods
    //===================================================================

    void TargetConfig::initialize_defaults()
    {
        // Set platform-specific defaults
        if (is_windows())
        {
            _runtime_paths.push_back("C:\\Program Files\\CryoLang\\lib");
            _system_lib_paths.push_back("C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\VC\\Tools\\MSVC\\14.29.30133\\lib\\x64");
        }
        else if (is_linux())
        {
            _runtime_paths.push_back("/usr/local/lib/cryo");
            _runtime_paths.push_back("/usr/lib/cryo");
            _system_lib_paths.push_back("/usr/lib");
            _system_lib_paths.push_back("/usr/local/lib");
        }
        else if (is_macos())
        {
            _runtime_paths.push_back("/usr/local/lib/cryo");
            _system_lib_paths.push_back("/usr/lib");
            _system_lib_paths.push_back("/usr/local/lib");
        }

        // Runtime paths are no longer needed - using stdlib instead
    }

    void TargetConfig::parse_target_triple() const
    {
        if (_triple_parsed)
        {
            return;
        }

        // Parse target triple format: arch-vendor-os-environment
        std::vector<std::string> components;
        std::stringstream ss(_target_triple);
        std::string component;

        while (std::getline(ss, component, '-'))
        {
            components.push_back(component);
        }

        if (components.size() >= 1)
        {
            _cached_arch = components[0];
        }
        if (components.size() >= 3)
        {
            _cached_os = components[2];
        }
        if (components.size() >= 4)
        {
            _cached_env = components[3];
        }

        _triple_parsed = true;
    }

    //=======================================================================
    // Utility Functions
    //=======================================================================

    std::string detect_native_target_triple()
    {
        return llvm::sys::getDefaultTargetTriple();
    }

    std::string get_default_cpu_features(const std::string &cpu)
    {
        // Return default features for common CPUs
        if (cpu == "x86-64")
        {
            return "+sse2,+cx16";
        }
        else if (cpu == "generic")
        {
            return "";
        }
        else if (cpu.find("apple") != std::string::npos)
        {
            return "+neon,+fp-armv8,+crypto";
        }

        return "";
    }

    bool is_target_triple_supported(const std::string &triple)
    {
        std::string error;
        const llvm::Target *target = llvm::TargetRegistry::lookupTarget(triple, error);
        return target != nullptr;
    }

} // namespace Cryo::Codegen