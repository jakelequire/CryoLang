#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <memory>
#include <sstream>

namespace Cryo::LSP {

/**
 * @brief Thread-safe logger with configurable output destinations
 * 
 * Features:
 * - Multiple log levels with filtering
 * - File and console output
 * - Thread-safe operations
 * - Automatic timestamp generation
 * - Component-based logging for better organization
 */
class Logger {
public:
    enum class Level {
        DEBUG = 0,
        INFO = 1,
        WARN = 2,
        ERROR = 3
    };

    // Get global logger instance
    static Logger& instance();
    
    // Configuration
    void set_log_level(Level level);
    void set_console_output(bool enabled);
    void set_file_output(const std::string& filepath);
    void enable_debug_mode(bool enabled);
    
    // Core logging methods
    void debug(const std::string& component, const std::string& message);
    void info(const std::string& component, const std::string& message);
    void warn(const std::string& component, const std::string& message);
    void error(const std::string& component, const std::string& message);
    
    // Template methods for formatted logging
    template<typename... Args>
    void debug(const std::string& component, const std::string& format, Args&&... args);
    
    template<typename... Args>
    void info(const std::string& component, const std::string& format, Args&&... args);
    
    template<typename... Args>
    void warn(const std::string& component, const std::string& format, Args&&... args);
    
    template<typename... Args>
    void error(const std::string& component, const std::string& format, Args&&... args);

    // Utility methods
    void flush();
    void close();

private:
    Logger() = default;
    ~Logger();
    
    void log_internal(Level level, const std::string& component, const std::string& message);
    std::string format_timestamp() const;
    std::string level_to_string(Level level) const;
    std::string format_message(const std::string& format) const;
    
    template<typename T, typename... Args>
    std::string format_message(const std::string& format, T&& value, Args&&... args) const;

    Level min_level_ = Level::INFO;
    bool console_output_ = true;
    bool file_output_ = false;
    bool debug_enabled_ = false;
    
    std::unique_ptr<std::ofstream> file_stream_;
    std::mutex mutex_;
};

// Template implementations
template<typename... Args>
void Logger::debug(const std::string& component, const std::string& format, Args&&... args) {
    if (debug_enabled_) {
        log_internal(Level::DEBUG, component, format_message(format, std::forward<Args>(args)...));
    }
}

template<typename... Args>
void Logger::info(const std::string& component, const std::string& format, Args&&... args) {
    log_internal(Level::INFO, component, format_message(format, std::forward<Args>(args)...));
}

template<typename... Args>
void Logger::warn(const std::string& component, const std::string& format, Args&&... args) {
    log_internal(Level::WARN, component, format_message(format, std::forward<Args>(args)...));
}

template<typename... Args>
void Logger::error(const std::string& component, const std::string& format, Args&&... args) {
    log_internal(Level::ERROR, component, format_message(format, std::forward<Args>(args)...));
}

template<typename T, typename... Args>
std::string Logger::format_message(const std::string& format, T&& value, Args&&... args) const {
    // Simple string formatting - replace {} with values in order
    std::string result = format;
    size_t pos = result.find("{}");
    if (pos != std::string::npos) {
        std::ostringstream oss;
        oss << value;
        result.replace(pos, 2, oss.str());
        
        if constexpr (sizeof...(args) > 0) {
            return format_message(result, std::forward<Args>(args)...);
        }
    }
    return result;
}

// Convenience macros for common logging
#define LOG_DEBUG(component, ...) Logger::instance().debug(component, __VA_ARGS__)
#define LOG_INFO(component, ...)  Logger::instance().info(component, __VA_ARGS__)
#define LOG_WARN(component, ...)  Logger::instance().warn(component, __VA_ARGS__)
#define LOG_ERROR(component, ...) Logger::instance().error(component, __VA_ARGS__)

} // namespace Cryo::LSP