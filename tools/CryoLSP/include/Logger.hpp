#pragma once
#include <fstream>
#include <string>
#include <memory>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace Cryo::LSP {

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }
    
    void log(const std::string& level, const std::string& component, const std::string& message) {
        if (!log_file_.is_open()) {
            return;
        }
        
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        ss << "." << std::setfill('0') << std::setw(3) << ms.count();
        
        log_file_ << "[" << ss.str() << "] [" << level << "] [" << component << "] " 
                  << message << std::endl;
        log_file_.flush();
    }
    
    void info(const std::string& component, const std::string& message) {
        log(component, "INFO", message);
    }
    
    void debug(const std::string& component, const std::string& message) {
        log("DEBUG", component, message);
    }
    
    void error(const std::string& component, const std::string& message) {
        log("ERROR", component, message);
    }
    
private:
    Logger() {
        // Create logs directory if it doesn't exist
        // Use absolute path to ensure we write to the right location
        std::string log_path = "C:\\Programming\\apps\\CryoLang\\logs\\cryo-lsp.log";
        log_file_.open(log_path, std::ios::app);
        if (log_file_.is_open()) {
            log("INFO", "Logger", "=== CryoLSP Server Started ===");
        } else {
            // Fallback to relative path
            log_file_.open("../../logs/cryo-lsp.log", std::ios::app);
            if (log_file_.is_open()) {
                log("INFO", "Logger", "=== CryoLSP Server Started (fallback path) ===");
            }
        }
    }
    
    ~Logger() {
        if (log_file_.is_open()) {
            log("INFO", "Logger", "=== CryoLSP Server Stopped ===");
            log_file_.close();
        }
    }
    
    std::ofstream log_file_;
};

// Convenience macros
#define LOG_DEBUG(component, message) Logger::getInstance().debug(component, message)
#define LOG_INFO(component, message) Logger::getInstance().info(component, message)  
#define LOG_ERROR(component, message) Logger::getInstance().error(component, message)

}
