#pragma once

#include <string>
#include <string_view>
#include <memory>
#include <mutex>
#include <fstream>
#include <chrono>
#include <sstream>
#include <unordered_map>
#include <functional>

namespace Cryo
{
    // ================================================================
    // Log Level Enumeration
    // ================================================================

    enum class LogLevel
    {
        TRACE = 0, // Most verbose - internal state tracking
        DEBUG = 1, // Debug information for development
        INFO = 2,  // General informational messages
        WARN = 3,  // Warning messages (potential issues)
        ERROR = 4, // Error messages (recoverable errors)
        FATAL = 5, // Fatal errors (unrecoverable)
        NONE = 6   // Disable all logging
    };

    // Convert log level to string
    inline const char *log_level_to_string(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::TRACE:
            return "TRACE";
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARN:
            return "WARN";
        case LogLevel::ERROR:
            return "ERROR";
        case LogLevel::FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
        }
    }

    // Convert log level to color code (ANSI)
    inline const char *log_level_to_color(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::TRACE:
            return "\033[0;37m"; // White
        case LogLevel::DEBUG:
            return "\033[0;36m"; // Cyan
        case LogLevel::INFO:
            return "\033[0;32m"; // Green
        case LogLevel::WARN:
            return "\033[0;33m"; // Yellow
        case LogLevel::ERROR:
            return "\033[0;31m"; // Red
        case LogLevel::FATAL:
            return "\033[1;31m"; // Bold Red
        default:
            return "\033[0m"; // Reset
        }
    }

    // ================================================================
    // Logger Component
    // ================================================================

    /**
     * @brief Component identifier for filtering logs by subsystem
     *
     * Allows granular control over which parts of the compiler produce logs
     */
    enum class LogComponent
    {
        GENERAL,     // General compiler messages
        LEXER,       // Lexical analysis
        PARSER,      // Syntax parsing
        AST,         // AST construction and manipulation
        TYPECHECKER, // Type checking and inference
        CODEGEN,     // Code generation (LLVM IR)
        LINKER,      // Linking and executable generation
        OPTIMIZER,   // Optimization passes
        DIAGNOSTIC,  // Error and warning messages
        RUNTIME,     // Runtime system
        STDLIB,      // Standard library
        CLI,         // Command-line interface
        LSP,         // Language server protocol
        ALL          // All components (for enable/disable)
    };

    inline const char *log_component_to_string(LogComponent component)
    {
        switch (component)
        {
        case LogComponent::GENERAL:
            return "GENERAL";
        case LogComponent::LEXER:
            return "LEXER";
        case LogComponent::PARSER:
            return "PARSER";
        case LogComponent::AST:
            return "AST";
        case LogComponent::TYPECHECKER:
            return "TYPECHECKER";
        case LogComponent::CODEGEN:
            return "CODEGEN";
        case LogComponent::LINKER:
            return "LINKER";
        case LogComponent::OPTIMIZER:
            return "OPTIMIZER";
        case LogComponent::DIAGNOSTIC:
            return "DIAGNOSTIC";
        case LogComponent::RUNTIME:
            return "RUNTIME";
        case LogComponent::STDLIB:
            return "STDLIB";
        case LogComponent::CLI:
            return "CLI";
        case LogComponent::LSP:
            return "LSP";
        case LogComponent::ALL:
            return "ALL";
        default:
            return "UNKNOWN";
        }
    }

    // ================================================================
    // Logger Configuration
    // ================================================================

    struct LoggerConfig
    {
        LogLevel console_level = LogLevel::INFO; // Minimum level for console output
        LogLevel file_level = LogLevel::DEBUG;   // Minimum level for file output
        bool enable_colors = true;               // Enable ANSI colors in console
        bool enable_timestamps = true;           // Include timestamps in logs
        bool enable_component_tags = true;       // Show component tags
        bool enable_thread_id = false;           // Show thread ID in logs
        bool enable_source_location = false;     // Show file:line in logs
        std::string log_file_path = "";          // Path to log file (empty = no file logging)
        bool append_to_file = true;              // Append to existing log file
        size_t max_file_size_mb = 100;           // Maximum log file size before rotation
        bool auto_flush = false;                 // Flush after every log message

        // Component filters (if empty, all components are logged)
        std::unordered_map<LogComponent, bool> component_filters;
    };

    // ================================================================
    // Logger Class
    // ================================================================

    /**
     * @brief Thread-safe logger for the CryoLang compiler
     *
     * Features:
     * - Multiple log levels (TRACE, DEBUG, INFO, WARN, ERROR, FATAL)
     * - Component-based filtering
     * - Console and file output
     * - Colorized console output
     * - Thread-safe operation
     * - Minimal overhead when disabled
     * - Automatic log file rotation
     *
     * Usage:
     *   Logger& logger = Logger::instance();
     *   logger.info(LogComponent::PARSER, "Parsing file: {}", filename);
     *   logger.error(LogComponent::CODEGEN, "Failed to generate code");
     */
    class Logger
    {
    private:
        LoggerConfig _config;
        std::ofstream _log_file;
        std::mutex _mutex;
        size_t _current_file_size = 0;
        bool _initialized = false;

        // Private constructor for singleton
        Logger() = default;

        // Rotate log file if it exceeds max size
        void rotate_log_file();

        // Write formatted message to outputs
        void write_log(LogLevel level, LogComponent component, const std::string &message,
                       const char *file = nullptr, int line = 0);

        // Check if logging is enabled for this level and component
        bool should_log(LogLevel level, LogComponent component) const;

        // Format timestamp
        std::string format_timestamp() const;

    public:
        // Singleton access
        static Logger &instance();

        // Delete copy/move constructors
        Logger(const Logger &) = delete;
        Logger &operator=(const Logger &) = delete;
        Logger(Logger &&) = delete;
        Logger &operator=(Logger &&) = delete;

        // Destructor
        ~Logger();

        // ================================================================
        // Configuration
        // ================================================================

        /**
         * @brief Initialize logger with configuration
         */
        void initialize(const LoggerConfig &config);

        /**
         * @brief Set console log level
         */
        void set_console_level(LogLevel level) { _config.console_level = level; }

        /**
         * @brief Set file log level
         */
        void set_file_level(LogLevel level) { _config.file_level = level; }

        /**
         * @brief Enable/disable colors in console output
         */
        void set_colors_enabled(bool enabled) { _config.enable_colors = enabled; }

        /**
         * @brief Enable/disable timestamps
         */
        void set_timestamps_enabled(bool enabled) { _config.enable_timestamps = enabled; }

        /**
         * @brief Enable/disable component tags
         */
        void set_component_tags_enabled(bool enabled) { _config.enable_component_tags = enabled; }

        /**
         * @brief Set log file path
         */
        void set_log_file(const std::string &path);

        /**
         * @brief Enable logging for specific component
         */
        void enable_component(LogComponent component);

        /**
         * @brief Disable logging for specific component
         */
        void disable_component(LogComponent component);

        /**
         * @brief Check if component is enabled
         */
        bool is_component_enabled(LogComponent component) const;

        /**
         * @brief Get current configuration
         */
        const LoggerConfig &config() const { return _config; }

        // ================================================================
        // Logging Methods
        // ================================================================

        /**
         * @brief Log trace message
         */
        template <typename... Args>
        void trace(LogComponent component, const std::string &message, Args &&...args)
        {
            if (should_log(LogLevel::TRACE, component))
            {
                write_log(LogLevel::TRACE, component, format_message(message, std::forward<Args>(args)...));
            }
        }

        /**
         * @brief Log debug message
         */
        template <typename... Args>
        void debug(LogComponent component, const std::string &message, Args &&...args)
        {
            if (should_log(LogLevel::DEBUG, component))
            {
                write_log(LogLevel::DEBUG, component, format_message(message, std::forward<Args>(args)...));
            }
        }

        /**
         * @brief Log info message
         */
        template <typename... Args>
        void info(LogComponent component, const std::string &message, Args &&...args)
        {
            if (should_log(LogLevel::INFO, component))
            {
                write_log(LogLevel::INFO, component, format_message(message, std::forward<Args>(args)...));
            }
        }

        /**
         * @brief Log warning message
         */
        template <typename... Args>
        void warn(LogComponent component, const std::string &message, Args &&...args)
        {
            if (should_log(LogLevel::WARN, component))
            {
                write_log(LogLevel::WARN, component, format_message(message, std::forward<Args>(args)...));
            }
        }

        /**
         * @brief Log error message
         */
        template <typename... Args>
        void error(LogComponent component, const std::string &message, Args &&...args)
        {
            if (should_log(LogLevel::ERROR, component))
            {
                write_log(LogLevel::ERROR, component, format_message(message, std::forward<Args>(args)...));
            }
        }

        /**
         * @brief Log fatal error message
         */
        template <typename... Args>
        void fatal(LogComponent component, const std::string &message, Args &&...args)
        {
            if (should_log(LogLevel::FATAL, component))
            {
                write_log(LogLevel::FATAL, component, format_message(message, std::forward<Args>(args)...));
            }
        }

        // ================================================================
        // Advanced Logging with Source Location
        // ================================================================

        /**
         * @brief Log message with source file and line information
         */
        template <typename... Args>
        void log_with_location(LogLevel level, LogComponent component,
                               const char *file, int line,
                               const std::string &message, Args &&...args)
        {
            if (should_log(level, component))
            {
                write_log(level, component, format_message(message, std::forward<Args>(args)...), file, line);
            }
        }

        // ================================================================
        // Utility Methods
        // ================================================================

        /**
         * @brief Flush all pending log messages
         */
        void flush();

        /**
         * @brief Close log file and cleanup
         */
        void shutdown();

    private:
        /**
         * @brief Format message with arguments (simple implementation)
         */
        template <typename... Args>
        std::string format_message(const std::string &format, Args &&...args)
        {
            // Simple implementation - no formatting for now
            // Can be enhanced with std::format (C++20) or fmt library
            if constexpr (sizeof...(args) == 0)
            {
                return format;
            }
            else
            {
                return format_message_impl(format, std::forward<Args>(args)...);
            }
        }

        // Implementation of variadic formatting
        template <typename T, typename... Rest>
        std::string format_message_impl(const std::string &format, T &&first, Rest &&...rest)
        {
            std::ostringstream oss;
            oss << format << " ";
            append_to_stream(oss, std::forward<T>(first));
            if constexpr (sizeof...(rest) > 0)
            {
                ((append_to_stream(oss << " ", std::forward<Rest>(rest))), ...);
            }
            return oss.str();
        }

        template <typename T>
        void append_to_stream(std::ostream &os, T &&value)
        {
            os << std::forward<T>(value);
        }
    };

    // ================================================================
    // Convenience Macros
    // ================================================================

#define LOG_TRACE(component, ...) ::Cryo::Logger::instance().trace(component, __VA_ARGS__)
#define LOG_DEBUG(component, ...) ::Cryo::Logger::instance().debug(component, __VA_ARGS__)
#define LOG_INFO(component, ...) ::Cryo::Logger::instance().info(component, __VA_ARGS__)
#define LOG_WARN(component, ...) ::Cryo::Logger::instance().warn(component, __VA_ARGS__)
#define LOG_ERROR(component, ...) ::Cryo::Logger::instance().error(component, __VA_ARGS__)
#define LOG_FATAL(component, ...) ::Cryo::Logger::instance().fatal(component, __VA_ARGS__)

// Macros with source location (file:line)
#define LOG_TRACE_LOC(component, ...) \
    ::Cryo::Logger::instance().log_with_location(::Cryo::LogLevel::TRACE, component, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG_LOC(component, ...) \
    ::Cryo::Logger::instance().log_with_location(::Cryo::LogLevel::DEBUG, component, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO_LOC(component, ...) \
    ::Cryo::Logger::instance().log_with_location(::Cryo::LogLevel::INFO, component, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN_LOC(component, ...) \
    ::Cryo::Logger::instance().log_with_location(::Cryo::LogLevel::WARN, component, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR_LOC(component, ...) \
    ::Cryo::Logger::instance().log_with_location(::Cryo::LogLevel::ERROR, component, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL_LOC(component, ...) \
    ::Cryo::Logger::instance().log_with_location(::Cryo::LogLevel::FATAL, component, __FILE__, __LINE__, __VA_ARGS__)

} // namespace Cryo
