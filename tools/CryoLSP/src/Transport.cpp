#include "LSP/Transport.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <chrono>
#include <iomanip>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

namespace CryoLSP
{

    // Static member definitions
    std::ofstream Transport::_log_file;
    std::mutex Transport::_log_mutex;

    // ================================================================
    // Logging
    // ================================================================

    static std::string timestamp()
    {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) %
                  1000;

        std::tm tm_buf{};
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t_now);
#else
        localtime_r(&time_t_now, &tm_buf);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm_buf, "%H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }

    void Transport::initLogFile(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(_log_mutex);

        // Truncate on open (auto-cleans previous session)
        _log_file.open(path, std::ios::out | std::ios::trunc);
        if (_log_file.is_open())
        {
            _log_file << "=== CryoLSP Log Started ===" << std::endl;
            _log_file.flush();
        }
        else
        {
            std::cerr << "[CryoLSP] Warning: Could not open log file: " << path << std::endl;
        }
    }

    void Transport::log(const std::string &message)
    {
        std::lock_guard<std::mutex> lock(_log_mutex);

        std::string ts = timestamp();
        std::string formatted = "[" + ts + "] " + message;

        // Always write to stderr (VS Code output channel picks this up)
        std::cerr << "[CryoLSP] " << message << std::endl;

        // Write to log file if open
        if (_log_file.is_open())
        {
            _log_file << formatted << std::endl;
            _log_file.flush();
        }
    }

    // ================================================================
    // JSON-RPC Transport
    // ================================================================

    std::optional<cjson::JsonValue> Transport::readMessage()
    {
        try
        {
            // Read headers until empty line
            size_t content_length = 0;
            bool found_length = false;

            while (true)
            {
                std::string line = readLine();
                if (line.empty())
                {
                    // EOF
                    return std::nullopt;
                }

                // Strip \r if present
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();

                // Empty line signals end of headers
                if (line.empty())
                    break;

                // Parse Content-Length header
                const std::string prefix = "Content-Length: ";
                if (line.substr(0, prefix.size()) == prefix)
                {
                    content_length = std::stoull(line.substr(prefix.size()));
                    found_length = true;
                }
                // Ignore other headers (e.g., Content-Type)
            }

            if (!found_length || content_length == 0)
            {
                log("Warning: No Content-Length header found");
                return std::nullopt;
            }

            // Read the JSON body
            std::string body = readBytes(content_length);
            if (body.size() != content_length)
            {
                log("Warning: Read " + std::to_string(body.size()) + " bytes but expected " + std::to_string(content_length));
                return std::nullopt;
            }

            // Log incoming messages
            log("<-- recv (" + std::to_string(content_length) + " bytes)");

            return cjson::JsonValue::parse(body);
        }
        catch (const std::exception &e)
        {
            log(std::string("Error reading message: ") + e.what());
            return std::nullopt;
        }
    }

    void Transport::sendResponse(const cjson::JsonValue &id, const cjson::JsonValue &result)
    {
        cjson::JsonObject msg;
        msg["jsonrpc"] = cjson::JsonValue("2.0");
        msg["id"] = id;
        msg["result"] = result;

        log("--> response (id=" + id.dump() + ")");
        writeMessage(cjson::JsonValue(std::move(msg)));
    }

    void Transport::sendError(const cjson::JsonValue &id, int code, const std::string &message)
    {
        cjson::JsonObject error;
        error["code"] = cjson::JsonValue(code);
        error["message"] = cjson::JsonValue(message);

        cjson::JsonObject msg;
        msg["jsonrpc"] = cjson::JsonValue("2.0");
        msg["id"] = id;
        msg["error"] = cjson::JsonValue(std::move(error));

        log("--> error (id=" + id.dump() + ", code=" + std::to_string(code) + "): " + message);
        writeMessage(cjson::JsonValue(std::move(msg)));
    }

    void Transport::sendNotification(const std::string &method, const cjson::JsonValue &params)
    {
        cjson::JsonObject msg;
        msg["jsonrpc"] = cjson::JsonValue("2.0");
        msg["method"] = cjson::JsonValue(method);
        msg["params"] = params;

        log("--> notify: " + method);
        writeMessage(cjson::JsonValue(std::move(msg)));
    }

    void Transport::writeMessage(const cjson::JsonValue &msg)
    {
        std::string json = msg.dump();
        std::string header = "Content-Length: " + std::to_string(json.size()) + "\r\n\r\n";

        // Write header + body atomically
        std::string full = header + json;
        std::cout.write(full.c_str(), full.size());
        std::cout.flush();
    }

    std::string Transport::readBytes(size_t count)
    {
        std::string result;
        result.resize(count);

        size_t read_so_far = 0;
        while (read_so_far < count)
        {
            int ch = std::cin.get();
            if (ch == EOF)
                break;
            result[read_so_far++] = static_cast<char>(ch);
        }
        result.resize(read_so_far);

        return result;
    }

    std::string Transport::readLine()
    {
        std::string line;
        while (true)
        {
            int ch = std::cin.get();
            if (ch == EOF)
                return line;
            if (ch == '\n')
                return line;
            line += static_cast<char>(ch);
        }
    }

} // namespace CryoLSP
