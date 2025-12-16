#pragma once

#include <fstream>
#include <string>
#include <mutex>
#include <chrono>

namespace CryoLSP {

class LSPLogger {
private:
    std::ofstream log_file;
    std::mutex log_mutex;
    bool is_initialized = false;
    
    std::string get_timestamp();
    void write_entry(const std::string& level, const std::string& message);

public:
    LSPLogger() = default;
    ~LSPLogger();
    
    bool initialize(const std::string& log_file_path);
    void shutdown();
    
    void info(const std::string& message);
    void debug(const std::string& message);
    void error(const std::string& message);
    void warn(const std::string& message);
};

}