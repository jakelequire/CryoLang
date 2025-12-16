#include "Utils/Logger.hpp"
#include "Utils/OS.hpp"
#include <iostream>
#include <iomanip>
#include <filesystem>
#include <ctime>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Cryo
{
    // ================================================================
    // Singleton Instance
    // ================================================================

    Logger &Logger::instance()
    {
        static Logger instance;
        return instance;
    }

    // ================================================================
    // Destructor
    // ================================================================

    Logger::~Logger()
    {
        shutdown();
    }

    // ================================================================
    // Initialization
    // ================================================================

    void Logger::initialize(const LoggerConfig &config)
    {
        std::lock_guard<std::mutex> lock(_mutex);

        _config = config;
        _initialized = true;

        // Open log file if path is specified
        if (!_config.log_file_path.empty())
        {
            set_log_file(_config.log_file_path);
        }

        // Enable colors on Windows console
#ifdef _WIN32
        if (_config.enable_colors)
        {
            HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
            DWORD dwMode = 0;
            GetConsoleMode(hOut, &dwMode);
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
#endif
    }

    // ================================================================
    // Default Configuration Factory
    // ================================================================

    LoggerConfig Logger::create_default_config()
    {
        LoggerConfig config;
        
        // Disable logging by default (will be configured by CLI flags)
        config.console_level = LogLevel::NONE;
        config.file_level = LogLevel::NONE;
        config.log_file_path = "";
        
        // Enable nice formatting features
        config.enable_colors = true;
        config.enable_timestamps = true;
        config.enable_component_tags = true;
        
        // Reasonable defaults for other options
        config.enable_thread_id = false;
        config.enable_source_location = false;
        config.append_to_file = true;
        config.max_file_size_mb = 100;
        config.auto_flush = false;
        
        return config;
    }

    // ================================================================
    // Configuration Methods
    // ================================================================

    void Logger::set_log_file(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(_mutex);

        // Close existing file if open
        if (_log_file.is_open())
        {
            _log_file.close();
        }

        _config.log_file_path = path;
        _current_file_size = 0;

        // Create directory if it doesn't exist using standard filesystem
        std::filesystem::path file_path(path);
        if (file_path.has_parent_path())
        {
            try {
                std::filesystem::create_directories(file_path.parent_path());
            } catch (const std::exception& e) {
                std::cerr << "Warning: Could not create log directory: " << e.what() << std::endl;
                // Continue anyway - let file opening fail if needed
            }
        }

        // Open log file
        auto mode = _config.append_to_file ? (std::ios::out | std::ios::app) : std::ios::out;
        _log_file.open(path, mode);

        if (!_log_file.is_open())
        {
            std::cerr << "Warning: Failed to open log file: " << path << std::endl;
            return;
        }

        // Get current file size if appending
        if (_config.append_to_file && std::filesystem::exists(path))
        {
            _current_file_size = std::filesystem::file_size(path);
        }
    }

    void Logger::enable_component(LogComponent component)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _config.component_filters[component] = true;
    }

    void Logger::disable_component(LogComponent component)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _config.component_filters[component] = false;
    }

    bool Logger::is_component_enabled(LogComponent component) const
    {
        // If no filters are set, all components are enabled
        if (_config.component_filters.empty())
        {
            return true;
        }

        // Check if ALL is disabled
        auto all_it = _config.component_filters.find(LogComponent::ALL);
        if (all_it != _config.component_filters.end() && !all_it->second)
        {
            return false;
        }

        // Check specific component
        auto it = _config.component_filters.find(component);
        if (it != _config.component_filters.end())
        {
            return it->second;
        }

        // Default to enabled if not explicitly set
        return true;
    }

    // ================================================================
    // Core Logging Logic
    // ================================================================

    bool Logger::should_log(LogLevel level, LogComponent component) const
    {
        // Check if component is enabled
        if (!is_component_enabled(component))
        {
            return false;
        }

        // Check if level meets console or file threshold
        return level >= _config.console_level ||
               (level >= _config.file_level && !_config.log_file_path.empty());
    }

    std::string Logger::format_timestamp() const
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) %
                  1000;

        std::tm tm_time;
#ifdef _WIN32
        localtime_s(&tm_time, &time);
#else
        localtime_r(&time, &tm_time);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm_time, "%Y-%m-%d %H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }

    void Logger::write_log(LogLevel level, LogComponent component,
                           const std::string &message,
                           const char *file, int line)
    {
        std::lock_guard<std::mutex> lock(_mutex);

        std::ostringstream log_stream;

        // Build log message
        if (_config.enable_timestamps)
        {
            log_stream << "[" << format_timestamp() << "] ";
        }

        // Add log level
        log_stream << "[" << log_level_to_string(level) << "] ";

        // Add component tag
        if (_config.enable_component_tags)
        {
            log_stream << "[" << log_component_to_string(component) << "] ";
        }

        // Add thread ID if enabled
        if (_config.enable_thread_id)
        {
            log_stream << "[Thread:" << std::this_thread::get_id() << "] ";
        }

        // Add source location if provided and enabled
        if (_config.enable_source_location && file != nullptr && line > 0)
        {
            // Extract just filename from path
            std::string filename(file);
            size_t last_slash = filename.find_last_of("/\\");
            if (last_slash != std::string::npos)
            {
                filename = filename.substr(last_slash + 1);
            }
            log_stream << "[" << filename << ":" << line << "] ";
        }

        // Add the actual message
        log_stream << message;

        std::string final_message = log_stream.str();

        // Write to console if level is sufficient
        if (level >= _config.console_level)
        {
            if (_config.enable_colors)
            {
                std::cout << log_level_to_color(level) << final_message
                          << "\033[0m" << std::endl;
            }
            else
            {
                std::cout << final_message << std::endl;
            }

            if (_config.auto_flush)
            {
                std::cout.flush();
            }
        }

        // Write to file if enabled and level is sufficient
        if (_log_file.is_open() && level >= _config.file_level)
        {
            _log_file << final_message << std::endl;
            _current_file_size += final_message.size() + 1;

            if (_config.auto_flush)
            {
                _log_file.flush();
            }

            // Check if rotation is needed
            if (_current_file_size >= (_config.max_file_size_mb * 1024 * 1024))
            {
                rotate_log_file();
            }
        }
    }

    void Logger::rotate_log_file()
    {
        if (!_log_file.is_open() || _config.log_file_path.empty())
        {
            return;
        }

        _log_file.close();

        // Create rotated filename with timestamp
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm_time;
#ifdef _WIN32
        localtime_s(&tm_time, &time);
#else
        localtime_r(&time, &tm_time);
#endif

        std::ostringstream oss;
        oss << _config.log_file_path << "."
            << std::put_time(&tm_time, "%Y%m%d_%H%M%S");

        // Rename current log file
        try
        {
            std::filesystem::rename(_config.log_file_path, oss.str());
        }
        catch (const std::filesystem::filesystem_error &e)
        {
            std::cerr << "Warning: Failed to rotate log file: " << e.what() << std::endl;
        }

        // Open new log file
        _log_file.open(_config.log_file_path, std::ios::out);
        _current_file_size = 0;
    }

    // ================================================================
    // Utility Methods
    // ================================================================

    void Logger::flush()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::cout.flush();
        if (_log_file.is_open())
        {
            _log_file.flush();
        }
    }

    void Logger::shutdown()
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if (_log_file.is_open())
        {
            _log_file.flush();
            _log_file.close();
        }

        _initialized = false;
    }

} // namespace Cryo
