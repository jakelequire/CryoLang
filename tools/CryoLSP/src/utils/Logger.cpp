#include "Logger.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>

namespace Cryo::LSP {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

Logger::~Logger() {
    close();
}

void Logger::set_log_level(Level level) {
    std::lock_guard<std::mutex> lock(mutex_);
    min_level_ = level;
}

void Logger::set_console_output(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    console_output_ = enabled;
}

void Logger::set_file_output(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (file_stream_) {
        file_stream_->close();
        file_stream_.reset();
    }
    
    if (!filepath.empty()) {
        file_stream_ = std::make_unique<std::ofstream>(filepath, std::ios::app);
        file_output_ = file_stream_->is_open();
        
        if (file_output_) {
            *file_stream_ << "\n=== CryoLSP Session Started: " << format_timestamp() << " ===\n";
        }
    } else {
        file_output_ = false;
    }
}

void Logger::enable_debug_mode(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    debug_enabled_ = enabled;
    if (enabled) {
        min_level_ = Level::DEBUG;
    }
}

void Logger::debug(const std::string& component, const std::string& message) {
    if (debug_enabled_) {
        log_internal(Level::DEBUG, component, message);
    }
}

void Logger::info(const std::string& component, const std::string& message) {
    log_internal(Level::INFO, component, message);
}

void Logger::warn(const std::string& component, const std::string& message) {
    log_internal(Level::WARN, component, message);
}

void Logger::error(const std::string& component, const std::string& message) {
    log_internal(Level::ERR, component, message);
}

void Logger::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (console_output_) {
        // In LSP servers, don't flush stdout as it's reserved for LSP protocol
        std::cerr.flush();
    }
    
    if (file_output_ && file_stream_) {
        file_stream_->flush();
    }
}

void Logger::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (file_stream_) {
        *file_stream_ << "=== CryoLSP Session Ended: " << format_timestamp() << " ===\n\n";
        file_stream_->close();
        file_stream_.reset();
    }
    
    file_output_ = false;
}

void Logger::log_internal(Level level, const std::string& component, const std::string& message) {
    if (level < min_level_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string timestamp = format_timestamp();
    std::string level_str = level_to_string(level);
    
    // Format: [timestamp] [LEVEL] [Component] message
    std::string log_line = "[" + timestamp + "] [" + level_str + "] [" + component + "] " + message;
    
    if (console_output_) {
        // In LSP servers, stdout is reserved for LSP protocol only
        // All logging must go to stderr to avoid corrupting the protocol
        std::cerr << log_line << std::endl;
    }
    
    if (file_output_ && file_stream_) {
        *file_stream_ << log_line << std::endl;
    }
}

std::string Logger::format_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    
    return oss.str();
}

std::string Logger::level_to_string(Level level) const {
    switch (level) {
        case Level::DEBUG: return "DEBUG";
        case Level::INFO:  return "INFO ";
        case Level::WARN:  return "WARN ";
        case Level::ERR: return "ERROR";
        default:           return "UNKNOWN";
    }
}

std::string Logger::format_message(const std::string& format) const {
    return format;
}

} // namespace Cryo::LSP