#pragma once

#include <string>
#include <vector>
#include <optional>
#include <filesystem>

namespace Cryo::Utils
{
    /**
     * @brief Cross-platform operating system utility class
     * 
     * This class provides a centralized interface for all platform-specific
     * operations including path handling, executable extensions, environment
     * variables, and directory discovery. It follows a singleton pattern
     * and should be initialized once at application startup.
     * 
     * Design principles:
     * - Single point of truth for OS-specific logic
     * - Immutable after initialization for thread safety
     * - Clean, type-safe APIs that hide platform complexity
     * - Automatic path normalization and validation
     */
    class OS
    {
    public:
        enum class Platform
        {
            Windows,
            Linux,
            MacOS,
            Unknown
        };

        enum class Architecture
        {
            x86_64,
            x86,
            ARM64,
            ARM,
            Unknown
        };

        //===================================================================
        // Singleton Management
        //===================================================================

        /**
         * @brief Get the singleton OS instance
         * @return Reference to the OS instance
         */
        static OS& instance();

        /**
         * @brief Initialize the OS utility with executable path and working directory
         * @param executable_path Path to the current executable (from argv[0])
         * @param working_directory Optional working directory override
         * @return true if initialization succeeded, false otherwise
         */
        static bool initialize(const std::string& executable_path, 
                             const std::string& working_directory = "");

        /**
         * @brief Check if the OS utility has been initialized
         * @return true if initialized, false otherwise
         */
        static bool is_initialized();

        //===================================================================
        // Platform Information
        //===================================================================

        /**
         * @brief Get the current platform
         * @return Platform enum value
         */
        Platform get_platform() const;

        /**
         * @brief Get the current architecture
         * @return Architecture enum value
         */
        Architecture get_architecture() const;

        /**
         * @brief Get platform name as string
         * @return Platform name (e.g., "Windows", "Linux", "MacOS")
         */
        std::string get_platform_name() const;

        /**
         * @brief Get architecture name as string
         * @return Architecture name (e.g., "x86_64", "ARM64")
         */
        std::string get_architecture_name() const;

        /**
         * @brief Check if running on Windows
         * @return true if on Windows, false otherwise
         */
        bool is_windows() const;

        /**
         * @brief Check if running on Unix-like system (Linux/MacOS)
         * @return true if on Unix-like system, false otherwise
         */
        bool is_unix() const;

        //===================================================================
        // Path Operations
        //===================================================================

        /**
         * @brief Get the platform-specific path separator
         * @return Path separator character ('\\' on Windows, '/' on Unix)
         */
        char get_path_separator() const;

        /**
         * @brief Join multiple path components using platform separator
         * @param components Vector of path components to join
         * @return Joined path string
         */
        std::string join_path(const std::vector<std::string>& components) const;

        /**
         * @brief Join two path components using platform separator
         * @param base Base path
         * @param component Component to append
         * @return Joined path string
         */
        std::string join_path(const std::string& base, const std::string& component) const;

        /**
         * @brief Normalize path separators for current platform
         * @param path Path to normalize
         * @return Normalized path string
         */
        std::string normalize_path(const std::string& path) const;

        /**
         * @brief Convert path to absolute form
         * @param path Relative or absolute path
         * @return Absolute path string
         */
        std::string absolute_path(const std::string& path) const;

        /**
         * @brief Check if path exists and is accessible
         * @param path Path to check
         * @return true if path exists, false otherwise
         */
        bool path_exists(const std::string& path) const;

        /**
         * @brief Check if path is a directory
         * @param path Path to check
         * @return true if path is a directory, false otherwise
         */
        bool is_directory(const std::string& path) const;

        /**
         * @brief Create directory and all parent directories
         * @param path Directory path to create
         * @return true if creation succeeded, false otherwise
         */
        bool create_directories(const std::string& path) const;

        //===================================================================
        // Executable Operations
        //===================================================================

        /**
         * @brief Get platform-specific executable extension
         * @return Executable extension (".exe" on Windows, "" on Unix)
         */
        std::string get_executable_extension() const;

        /**
         * @brief Get platform-specific library extension
         * @param is_shared Whether to get shared library extension
         * @return Library extension (".dll"/".lib" on Windows, ".so"/".a" on Unix)
         */
        std::string get_library_extension(bool is_shared = true) const;

        /**
         * @brief Add appropriate executable extension to filename if missing
         * @param base_name Base executable name without extension
         * @return Executable name with platform-appropriate extension
         */
        std::string make_executable_name(const std::string& base_name) const;

        /**
         * @brief Create platform-appropriate executable command
         * @param executable_path Path to executable
         * @return Command string for running executable
         */
        std::string make_executable_command(const std::string& executable_path) const;

        //===================================================================
        // Environment Variables
        //===================================================================

        /**
         * @brief Get environment variable value
         * @param name Environment variable name
         * @return Optional string value (empty if variable doesn't exist)
         */
        std::optional<std::string> get_env(const std::string& name) const;

        /**
         * @brief Get environment variable with default value
         * @param name Environment variable name
         * @param default_value Default value if variable doesn't exist
         * @return Environment variable value or default
         */
        std::string get_env_or_default(const std::string& name, const std::string& default_value) const;

        /**
         * @brief Check if environment variable exists
         * @param name Environment variable name
         * @return true if variable exists, false otherwise
         */
        bool has_env(const std::string& name) const;

        //===================================================================
        // Directory Discovery
        //===================================================================

        /**
         * @brief Get the CryoLang project root directory
         * @return Project root path (where makefile/cryoconfig exists)
         */
        const std::string& get_project_root() const;

        /**
         * @brief Get the logs directory for the project
         * @return Logs directory path
         */
        std::string get_logs_directory() const;

        /**
         * @brief Get the bin directory containing executables
         * @return Bin directory path
         */
        std::string get_bin_directory() const;

        /**
         * @brief Get the stdlib directory
         * @return Standard library directory path
         */
        std::string get_stdlib_directory() const;

        /**
         * @brief Get the current working directory
         * @return Current working directory path
         */
        const std::string& get_working_directory() const;

        /**
         * @brief Get the executable directory (where this process is located)
         * @return Executable directory path
         */
        const std::string& get_executable_directory() const;

        //===================================================================
        // Utility Methods
        //===================================================================

        /**
         * @brief Get comprehensive system information for debugging
         * @return Formatted string with platform, paths, and environment info
         */
        std::string get_system_info() const;

        /**
         * @brief Validate that all critical directories exist and are accessible
         * @return true if validation passed, false otherwise
         */
        bool validate_environment() const;

    private:
        // Private constructor for singleton
        OS() = default;

        // Disable copy and assignment
        OS(const OS&) = delete;
        OS& operator=(const OS&) = delete;

        //===================================================================
        // Initialization Helpers
        //===================================================================

        bool initialize_impl(const std::string& executable_path, 
                            const std::string& working_directory);
        
        bool detect_platform_info();
        bool discover_project_structure();
        std::string find_project_root_from_executable(const std::string& executable_path) const;
        std::string find_project_root_from_working_dir(const std::string& working_directory) const;

        //===================================================================
        // Member Variables
        //===================================================================

        // Initialization state
        bool _initialized = false;

        // Platform information
        Platform _platform = Platform::Unknown;
        Architecture _architecture = Architecture::Unknown;

        // Path information
        std::string _project_root;
        std::string _working_directory;
        std::string _executable_directory;
        std::string _executable_path;

        // Cached directories
        std::string _logs_directory;
        std::string _bin_directory;
        std::string _stdlib_directory;

        // Platform-specific constants
        char _path_separator;
        std::string _executable_extension;
        std::string _shared_lib_extension;
        std::string _static_lib_extension;
    };

} // namespace Cryo::Utils