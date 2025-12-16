#include "LSPLogger.hpp"
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace CryoLSP {

LSPLogger::~LSPLogger() {
    shutdown();
}

bool LSPLogger::initialize(const std::string& log_file_path) {
    std::lock_guard<std::mutex> lock(log_mutex);
    
    if (is_initialized) {
        return true;
    }
    
    try {
        // Create directory if needed
        std::filesystem::path file_path(log_file_path);
        if (file_path.has_parent_path()) {
            std::filesystem::create_directories(file_path.parent_path());
        }
        
        // Open log file in append mode
        log_file.open(log_file_path, std::ios::out | std::ios::app);
        
        if (!log_file.is_open()) {
            return false;
        }
        
        is_initialized = true;
        
        // Write initialization message
        write_entry("INFO", "LSP Logger initialized");
        
        return true;
    } catch (...) {
        return false;
    }
}

void LSPLogger::shutdown() {
    std::lock_guard<std::mutex> lock(log_mutex);
    
    if (is_initialized && log_file.is_open()) {
        write_entry("INFO", "LSP Logger shutting down");
        log_file.close();
        is_initialized = false;
    }
}

std::string LSPLogger::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void LSPLogger::write_entry(const std::string& level, const std::string& message) {
    if (!is_initialized || !log_file.is_open()) {
        return;
    }
    
    try {
        log_file << "[" << get_timestamp() << "] [" << level << "] " << message << std::endl;
        log_file.flush();
    } catch (...) {
        // Ignore write errors to prevent cascading failures
    }
}

void LSPLogger::info(const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex);
    write_entry("INFO", message);
}

void LSPLogger::debug(const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex);
    write_entry("DEBUG", message);
}

void LSPLogger::error(const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex);
    write_entry("ERROR", message);
}

void LSPLogger::warn(const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex);
    write_entry("WARN", message);
}

}