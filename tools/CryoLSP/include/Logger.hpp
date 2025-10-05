#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <memory>
#include <sstream>

namespace Cryo::LSP {

class Logger {
public:
    enum class Level {
        DEBUG = 0,
        INFO = 1,
        WARN = 2,
        ERR = 3  // Note: ERR not ERROR to match existing implementation
    };

    static Logger& instance();
    
    ~Logger();

    void set_log_level(Level level);
    void set_file_output(const std::string& filename);
    void set_console_output(bool enabled);
    void enable_debug_mode(bool enabled);

    // Simple string logging
    void debug(const std::string& component, const std::string& message);
    void info(const std::string& component, const std::string& message);
    void warn(const std::string& component, const std::string& message);
    void error(const std::string& component, const std::string& message);

    // Format-style logging with placeholders  
    template<typename... Args>
    void debug(const std::string& component, const std::string& format, Args... args) {
        if (debug_enabled_) {
            log_internal(Level::DEBUG, component, format_message_template(format, args...));
        }
    }

    template<typename... Args>
    void info(const std::string& component, const std::string& format, Args... args) {
        log_internal(Level::INFO, component, format_message_template(format, args...));
    }

    template<typename... Args>
    void warn(const std::string& component, const std::string& format, Args... args) {
        log_internal(Level::WARN, component, format_message_template(format, args...));
    }

    template<typename... Args>
    void error(const std::string& component, const std::string& format, Args... args) {
        log_internal(Level::ERR, component, format_message_template(format, args...));
    }

    void flush();
    void close();

private:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void log_internal(Level level, const std::string& component, const std::string& message);
    std::string format_timestamp() const;
    std::string level_to_string(Level level) const;
    std::string format_message(const std::string& format) const;

    // Simple format function that replaces {} with arguments
    template<typename T>
    std::string format_message_template(const std::string& format, T&& value) {
        std::string result = format;
        size_t pos = result.find("{}");
        if (pos != std::string::npos) {
            std::ostringstream oss;
            oss << value;
            result.replace(pos, 2, oss.str());
        }
        return result;
    }

    template<typename T, typename... Args>
    std::string format_message_template(const std::string& format, T&& first, Args&&... rest) {
        std::string result = format;
        size_t pos = result.find("{}");
        if (pos != std::string::npos) {
            std::ostringstream oss;
            oss << first;
            result.replace(pos, 2, oss.str());
            return format_message_template(result, std::forward<Args>(rest)...);
        }
        return result;
    }

    std::mutex mutex_;
    Level min_level_ = Level::DEBUG;
    std::unique_ptr<std::ofstream> file_stream_;
    bool console_output_ = true;
    bool debug_enabled_ = false;
    bool file_output_ = false;
};

// Convenience macros for backward compatibility
#define LOG_DEBUG(component, ...) Logger::instance().debug(component, __VA_ARGS__)
#define LOG_INFO(component, ...) Logger::instance().info(component, __VA_ARGS__)
#define LOG_WARN(component, ...) Logger::instance().warn(component, __VA_ARGS__)
#define LOG_ERROR(component, ...) Logger::instance().error(component, __VA_ARGS__)

} // namespace Cryo::LSP