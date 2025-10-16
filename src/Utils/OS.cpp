#include "Utils/OS.hpp"
#include "Utils/Logger.hpp"
#include <iostream>
#include <algorithm>
#include <cstdlib>

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #define getcwd _getcwd
#else
    #include <unistd.h>
    #include <sys/utsname.h>
#endif

namespace Cryo::Utils
{
    //===================================================================
    // Singleton Management
    //===================================================================

    OS& OS::instance()
    {
        static OS instance;
        return instance;
    }

    bool OS::initialize(const std::string& executable_path, const std::string& working_directory)
    {
        return instance().initialize_impl(executable_path, working_directory);
    }

    bool OS::is_initialized()
    {
        return instance()._initialized;
    }

    //===================================================================
    // Platform Information
    //===================================================================

    OS::Platform OS::get_platform() const
    {
        return _platform;
    }

    OS::Architecture OS::get_architecture() const
    {
        return _architecture;
    }

    std::string OS::get_platform_name() const
    {
        switch (_platform)
        {
            case Platform::Windows: return "Windows";
            case Platform::Linux:   return "Linux";
            case Platform::MacOS:   return "MacOS";
            default:                return "Unknown";
        }
    }

    std::string OS::get_architecture_name() const
    {
        switch (_architecture)
        {
            case Architecture::x86_64: return "x86_64";
            case Architecture::x86:    return "x86";
            case Architecture::ARM64:  return "ARM64";
            case Architecture::ARM:    return "ARM";
            default:                   return "Unknown";
        }
    }

    bool OS::is_windows() const
    {
        return _platform == Platform::Windows;
    }

    bool OS::is_unix() const
    {
        return _platform == Platform::Linux || _platform == Platform::MacOS;
    }

    //===================================================================
    // Path Operations
    //===================================================================

    char OS::get_path_separator() const
    {
        return _path_separator;
    }

    std::string OS::join_path(const std::vector<std::string>& components) const
    {
        if (components.empty())
            return "";

        std::string result = components[0];
        for (size_t i = 1; i < components.size(); ++i)
        {
            if (!result.empty() && result.back() != _path_separator)
                result += _path_separator;
            result += components[i];
        }
        return normalize_path(result);
    }

    std::string OS::join_path(const std::string& base, const std::string& component) const
    {
        if (base.empty())
            return component;
        if (component.empty())
            return base;

        std::string result = base;
        if (result.back() != _path_separator)
            result += _path_separator;
        result += component;
        return normalize_path(result);
    }

    std::string OS::normalize_path(const std::string& path) const
    {
        std::string normalized = path;
        
        // Replace all separators with platform-specific separator
        char wrong_sep = (_path_separator == '/') ? '\\' : '/';
        std::replace(normalized.begin(), normalized.end(), wrong_sep, _path_separator);

        // Use std::filesystem for full normalization
        try
        {
            return std::filesystem::path(normalized).lexically_normal().string();
        }
        catch (const std::exception&)
        {
            // Fallback to simple replacement if filesystem fails
            return normalized;
        }
    }

    std::string OS::absolute_path(const std::string& path) const
    {
        try
        {
            return std::filesystem::absolute(path).string();
        }
        catch (const std::exception&)
        {
            return path; // Return original path if conversion fails
        }
    }

    bool OS::path_exists(const std::string& path) const
    {
        try
        {
            return std::filesystem::exists(path);
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    bool OS::is_directory(const std::string& path) const
    {
        try
        {
            return std::filesystem::is_directory(path);
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    bool OS::create_directories(const std::string& path) const
    {
        try
        {
            return std::filesystem::create_directories(path);
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    //===================================================================
    // Executable Operations
    //===================================================================

    std::string OS::get_executable_extension() const
    {
        return _executable_extension;
    }

    std::string OS::get_library_extension(bool is_shared) const
    {
        return is_shared ? _shared_lib_extension : _static_lib_extension;
    }

    std::string OS::make_executable_name(const std::string& base_name) const
    {
        if (base_name.empty())
            return base_name;

        // Check if extension is already present
        if (_executable_extension.empty())
            return base_name;

        if (base_name.length() >= _executable_extension.length())
        {
            std::string end = base_name.substr(base_name.length() - _executable_extension.length());
            if (end == _executable_extension)
                return base_name; // Extension already present
        }

        return base_name + _executable_extension;
    }

    std::string OS::make_executable_command(const std::string& executable_path) const
    {
        std::string normalized = normalize_path(executable_path);
        
        if (is_windows())
        {
            // On Windows, we can run executables directly by path
            return normalized;
        }
        else
        {
            // On Unix, prefix with ./ if it's a relative path without directory separators
            if (normalized.find(_path_separator) == std::string::npos)
            {
                return "./" + normalized;
            }
            return normalized;
        }
    }

    //===================================================================
    // Environment Variables
    //===================================================================

    std::optional<std::string> OS::get_env(const std::string& name) const
    {
        const char* value = std::getenv(name.c_str());
        if (value != nullptr)
        {
            return std::string(value);
        }
        return std::nullopt;
    }

    std::string OS::get_env_or_default(const std::string& name, const std::string& default_value) const
    {
        auto value = get_env(name);
        return value ? *value : default_value;
    }

    bool OS::has_env(const std::string& name) const
    {
        return get_env(name).has_value();
    }

    //===================================================================
    // Directory Discovery
    //===================================================================

    const std::string& OS::get_project_root() const
    {
        return _project_root;
    }

    std::string OS::get_logs_directory() const
    {
        return _logs_directory;
    }

    std::string OS::get_bin_directory() const
    {
        return _bin_directory;
    }

    std::string OS::get_stdlib_directory() const
    {
        return _stdlib_directory;
    }

    const std::string& OS::get_working_directory() const
    {
        return _working_directory;
    }

    const std::string& OS::get_executable_directory() const
    {
        return _executable_directory;
    }

    //===================================================================
    // Utility Methods
    //===================================================================

    std::string OS::get_system_info() const
    {
        std::string info;
        info += "System Information:\n";
        info += "  Platform: " + get_platform_name() + "\n";
        info += "  Architecture: " + get_architecture_name() + "\n";
        info += "  Path Separator: '" + std::string(1, _path_separator) + "'\n";
        info += "  Executable Extension: '" + _executable_extension + "'\n";
        info += "\nDirectories:\n";
        info += "  Project Root: " + _project_root + "\n";
        info += "  Working Directory: " + _working_directory + "\n";
        info += "  Executable Directory: " + _executable_directory + "\n";
        info += "  Logs Directory: " + _logs_directory + "\n";
        info += "  Bin Directory: " + _bin_directory + "\n";
        info += "  Stdlib Directory: " + _stdlib_directory + "\n";
        info += "\nEnvironment:\n";
        info += "  HOME: " + get_env_or_default("HOME", "not set") + "\n";
        info += "  PATH: " + get_env_or_default("PATH", "not set").substr(0, 100) + "...\n";
        return info;
    }

    bool OS::validate_environment() const
    {
        bool valid = true;

        // Check critical directories exist
        if (!path_exists(_project_root))
        {
            std::cerr << "Error: Project root directory not found: " << _project_root << std::endl;
            valid = false;
        }

        if (!path_exists(_bin_directory))
        {
            std::cerr << "Warning: Bin directory not found: " << _bin_directory << std::endl;
        }

        if (!path_exists(_stdlib_directory))
        {
            std::cerr << "Warning: Stdlib directory not found: " << _stdlib_directory << std::endl;
        }

        // Try to create logs directory if it doesn't exist
        if (!path_exists(_logs_directory))
        {
            if (!create_directories(_logs_directory))
            {
                std::cerr << "Warning: Could not create logs directory: " << _logs_directory << std::endl;
            }
        }

        return valid;
    }

    //===================================================================
    // Private Implementation
    //===================================================================

    bool OS::initialize_impl(const std::string& executable_path, const std::string& working_directory)
    {
        if (_initialized)
        {
            std::cerr << "Warning: OS utility already initialized" << std::endl;
            return true;
        }

        // Store executable path
        _executable_path = absolute_path(executable_path);
        _executable_directory = std::filesystem::path(_executable_path).parent_path().string();

        // Set working directory
        if (working_directory.empty())
        {
            try
            {
                _working_directory = std::filesystem::current_path().string();
            }
            catch (const std::exception&)
            {
                _working_directory = ".";
            }
        }
        else
        {
            _working_directory = absolute_path(working_directory);
        }

        // Detect platform information
        if (!detect_platform_info())
        {
            std::cerr << "Error: Failed to detect platform information" << std::endl;
            return false;
        }

        // Discover project structure
        if (!discover_project_structure())
        {
            std::cerr << "Error: Failed to discover project structure" << std::endl;
            return false;
        }

        _initialized = true;

        // Validate environment (optional, continues even if validation fails)
        validate_environment();

        return true;
    }

    bool OS::detect_platform_info()
    {
        // Detect platform
#ifdef _WIN32
        _platform = Platform::Windows;
        _path_separator = '\\';
        _executable_extension = ".exe";
        _shared_lib_extension = ".dll";
        _static_lib_extension = ".lib";
#elif defined(__APPLE__)
        _platform = Platform::MacOS;
        _path_separator = '/';
        _executable_extension = "";
        _shared_lib_extension = ".dylib";
        _static_lib_extension = ".a";
#elif defined(__linux__)
        _platform = Platform::Linux;
        _path_separator = '/';
        _executable_extension = "";
        _shared_lib_extension = ".so";
        _static_lib_extension = ".a";
#else
        _platform = Platform::Unknown;
        _path_separator = '/'; // Default to Unix-style
        _executable_extension = "";
        _shared_lib_extension = ".so";
        _static_lib_extension = ".a";
#endif

        // Detect architecture
#if defined(_M_X64) || defined(__x86_64__) || defined(__x86_64) || defined(__amd64__) || defined(__amd64)
        _architecture = Architecture::x86_64;
#elif defined(_M_IX86) || defined(__i386__) || defined(__i386) || defined(i386)
        _architecture = Architecture::x86;
#elif defined(_M_ARM64) || defined(__aarch64__)
        _architecture = Architecture::ARM64;
#elif defined(_M_ARM) || defined(__arm__)
        _architecture = Architecture::ARM;
#else
        _architecture = Architecture::Unknown;
#endif

        return _platform != Platform::Unknown;
    }

    bool OS::discover_project_structure()
    {
        // Try to find project root from executable path first
        _project_root = find_project_root_from_executable(_executable_path);
        
        // If that fails, try from working directory
        if (_project_root.empty())
        {
            _project_root = find_project_root_from_working_dir(_working_directory);
        }

        // If still not found, use working directory as fallback
        if (_project_root.empty())
        {
            _project_root = _working_directory;
        }

        // Set up directory paths based on project root
        _logs_directory = join_path(_project_root, "logs");
        _bin_directory = join_path(_project_root, "bin");
        _stdlib_directory = join_path(_project_root, "stdlib");

        return !_project_root.empty();
    }

    std::string OS::find_project_root_from_executable(const std::string& executable_path) const
    {
        std::filesystem::path current = std::filesystem::path(executable_path).parent_path();
        
        // Look for project markers going up the directory tree
        for (int depth = 0; depth < 10; ++depth) // Limit search depth
        {
            // Check for project markers
            std::vector<std::string> markers = {"makefile", "Makefile", "cryoconfig", "README.md"};
            
            for (const auto& marker : markers)
            {
                std::filesystem::path marker_path = current / marker;
                if (std::filesystem::exists(marker_path))
                {
                    return current.string();
                }
            }

            // Move up one directory
            std::filesystem::path parent = current.parent_path();
            if (parent == current) // Reached root
                break;
            current = parent;
        }

        return ""; // Not found
    }

    std::string OS::find_project_root_from_working_dir(const std::string& working_directory) const
    {
        std::filesystem::path current = working_directory;
        
        // Look for project markers going up the directory tree
        for (int depth = 0; depth < 10; ++depth) // Limit search depth
        {
            // Check for project markers
            std::vector<std::string> markers = {"makefile", "Makefile", "cryoconfig", "README.md"};
            
            for (const auto& marker : markers)
            {
                std::filesystem::path marker_path = current / marker;
                if (std::filesystem::exists(marker_path))
                {
                    return current.string();
                }
            }

            // Move up one directory
            std::filesystem::path parent = current.parent_path();
            if (parent == current) // Reached root
                break;
            current = parent;
        }

        return ""; // Not found
    }

} // namespace Cryo::Utils