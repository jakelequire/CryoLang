#include "Transport.hpp"
#include "Logger.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#include <sys/types.h>
#endif

namespace Cryo
{
    namespace LSP
    {

        // Simple, working stdin/stdout transport
        StdioTransport::StdioTransport() : active_(false), initialized_(false) {}

        StdioTransport::~StdioTransport()
        {
            shutdown();
        }

        bool StdioTransport::initialize()
        {
            if (initialized_)
            {
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

        std::optional<std::string> StdioTransport::read_message()
        {
            if (!is_active())
            {
                Logger::instance().debug("Transport", "Transport not active, returning null");
                return std::nullopt;
            }

            Logger::instance().debug("Transport", "Starting to read message from stdin...");

            // Check if stdin has data available
            if (std::cin.eof())
            {
                Logger::instance().debug("Transport", "stdin is at EOF - marking transport as inactive");
                active_ = false; // Mark transport as inactive when EOF is reached
                return std::nullopt;
            }

            if (std::cin.fail())
            {
                Logger::instance().debug("Transport", "stdin is in fail state - marking transport as inactive");
                active_ = false; // Mark transport as inactive on failure
                return std::nullopt;
            }

            // Read Content-Length header
            std::string line;
            size_t content_length = 0;

            Logger::instance().debug("Transport", "Reading headers...");

            // Read headers until empty line
            while (std::getline(std::cin, line))
            {
                Logger::instance().debug("Transport", "Read header line: " + line);

                // Remove \r if present
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }

                if (line.empty())
                {
                    Logger::instance().debug("Transport", "Found empty line, end of headers");
                    break; // End of headers
                }

                // Look for Content-Length
                if (line.find("Content-Length:") == 0)
                {
                    size_t colon = line.find(':');
                    if (colon != std::string::npos)
                    {
                        std::string len_str = line.substr(colon + 1);
                        // Trim whitespace
                        size_t start = len_str.find_first_not_of(" \t");
                        if (start != std::string::npos)
                        {
                            len_str = len_str.substr(start);
                        }
                        content_length = std::stoull(len_str);
                        Logger::instance().debug("Transport", "Content-Length: " + std::to_string(content_length));
                    }
                }
            }

            if (content_length == 0)
            {
                Logger::instance().debug("Transport", "No content length found or stdin closed");
                return std::nullopt;
            }

            Logger::instance().debug("Transport", "Reading content of " + std::to_string(content_length) + " bytes...");

            // Read content
            std::string content(content_length, '\0');
            std::cin.read(&content[0], content_length);

            if (std::cin.gcount() != static_cast<std::streamsize>(content_length))
            {
                Logger::instance().debug("Transport", "Failed to read expected content length. Read " + std::to_string(std::cin.gcount()) + " bytes");
                return std::nullopt;
            }

            Logger::instance().debug("Transport", "Successfully read message: " + content.substr(0, std::min(100, static_cast<int>(content.length()))));

            return content;
        }

        bool StdioTransport::write_message(const std::string &message)
        {
            if (!is_active())
            {
                Logger::instance().debug("Transport", "Transport not active, cannot write message");
                return false;
            }

            // Validate message content before sending
            if (message.empty())
            {
                Logger::instance().error("Transport", "Attempted to send empty message");
                return false;
            }

            // Basic JSON validation
            if (message.front() != '{' || message.back() != '}')
            {
                Logger::instance().error("Transport", "Message does not appear to be valid JSON: " + message.substr(0, 100));
                return false;
            }

            // Debug: Log the exact message being sent
            Logger::instance().debug("Transport", "Writing message of length: " + std::to_string(message.length()));
            Logger::instance().debug("Transport", "Message content: " + message);

            // Calculate content length in bytes (UTF-8)
            size_t content_length = message.length();

            // Build complete message as single string to avoid any timing issues
            std::string response = "Content-Length: " + std::to_string(content_length) + "\r\n\r\n" + message;

            // Use write() system call directly to stdout file descriptor
            const char *data = response.c_str();
            size_t total_len = response.length();
            size_t written = 0;

            while (written < total_len)
            {
                ssize_t result = write(STDOUT_FILENO, data + written, total_len - written);
                if (result < 0)
                {
                    Logger::instance().error("Transport", "Write failed");
                    return false;
                }
                written += result;
            }

            // Force immediate sync
            fsync(STDOUT_FILENO);

            Logger::instance().debug("Transport", "Writing header: Content-Length: " + std::to_string(content_length));

            Logger::instance().debug("Transport", "Message written and flushed successfully");

            return true;
        }

        bool StdioTransport::is_active() const
        {
            return active_ && initialized_;
        }

        void StdioTransport::shutdown()
        {
            active_ = false;
        }

    } // namespace LSP
} // namespace Cryo