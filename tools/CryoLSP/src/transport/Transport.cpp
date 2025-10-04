#include "Transport.hpp"
#include "Logger.hpp"
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

namespace Cryo {
namespace LSP {

// Simple, working stdin/stdout transport
StdioTransport::StdioTransport() : active_(false), initialized_(false) {}

StdioTransport::~StdioTransport() {
    shutdown();
}

bool StdioTransport::initialize() {
    if (initialized_) {
        return true;
    }
    
    // Simple initialization
    std::ios_base::sync_with_stdio(false);
    
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    
    active_ = true;
    initialized_ = true;
    
    return true;
}

std::optional<std::string> StdioTransport::read_message() {
    if (!is_active()) {
        Logger::instance().debug("Transport", "Transport not active, returning null");
        return std::nullopt;
    }
    
    Logger::instance().debug("Transport", "Starting to read message from stdin...");
    
    // Check if stdin has data available
    if (std::cin.eof()) {
        Logger::instance().debug("Transport", "stdin is at EOF");
        return std::nullopt;
    }
    
    if (std::cin.fail()) {
        Logger::instance().debug("Transport", "stdin is in fail state");
        return std::nullopt;
    }
    
    // Read Content-Length header
    std::string line;
    size_t content_length = 0;
    
    Logger::instance().debug("Transport", "Reading headers...");
    
    // Read headers until empty line
    while (std::getline(std::cin, line)) {
        Logger::instance().debug("Transport", "Read header line: " + line);
        
        // Remove \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        if (line.empty()) {
            Logger::instance().debug("Transport", "Found empty line, end of headers");
            break; // End of headers
        }
        
        // Look for Content-Length
        if (line.find("Content-Length:") == 0) {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string len_str = line.substr(colon + 1);
                // Trim whitespace
                size_t start = len_str.find_first_not_of(" \t");
                if (start != std::string::npos) {
                    len_str = len_str.substr(start);
                }
                content_length = std::stoull(len_str);
                Logger::instance().debug("Transport", "Content-Length: " + std::to_string(content_length));
            }
        }
    }
    
    if (content_length == 0) {
        Logger::instance().debug("Transport", "No content length found or stdin closed");
        return std::nullopt;
    }
    
    Logger::instance().debug("Transport", "Reading content of " + std::to_string(content_length) + " bytes...");
    
    // Read content
    std::string content(content_length, '\0');
    std::cin.read(&content[0], content_length);
    
    if (std::cin.gcount() != static_cast<std::streamsize>(content_length)) {
        Logger::instance().debug("Transport", "Failed to read expected content length. Read " + std::to_string(std::cin.gcount()) + " bytes");
        return std::nullopt;
    }
    
    Logger::instance().debug("Transport", "Successfully read message: " + content.substr(0, std::min(100, static_cast<int>(content.length()))));
    
    return content;
}

bool StdioTransport::write_message(const std::string& message) {
    if (!is_active()) {
        return false;
    }
    
    // Debug: Log the exact message being sent
    Logger::instance().debug("Transport", "Writing message of length: " + std::to_string(message.length()));
    Logger::instance().debug("Transport", "Message content: " + message);
    
    // Write LSP message with proper headers
    std::string header = "Content-Length: " + std::to_string(message.length()) + "\r\n\r\n";
    Logger::instance().debug("Transport", "Writing header: " + header);
    
    std::cout << header;
    std::cout << message;
    
    // Ensure everything is flushed immediately
    std::cout.flush();
    
    // On Windows, also sync the C stream
#ifdef _WIN32
    fflush(stdout);
#endif
    
    Logger::instance().debug("Transport", "Message written and flushed successfully");
    
    return true;
}

bool StdioTransport::is_active() const {
    return active_ && initialized_;
}

void StdioTransport::shutdown() {
    active_ = false;
}

} // namespace LSP
} // namespace Cryo