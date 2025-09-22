#include "MessageHandler.hpp"
#include "Logger.hpp"
#include <sstream>
#include <regex>
#include <algorithm>
#include <cstdio>
#include <thread>
#include <chrono>

namespace Cryo::LSP
{
    MessageHandler::MessageHandler(std::istream& input, std::ostream& output)
        : _input(input), _output(output)
    {
    }

    std::optional<LSPMessage> MessageHandler::receive_message()
    {
        LOG_DEBUG("MessageHandler", "Waiting for incoming message...");
        
        if (_shutdown_requested) {
            LOG_DEBUG("MessageHandler", "Shutdown requested, not receiving messages");
            return std::nullopt;
        }

        auto raw_message = read_message();
        if (!raw_message) {
            LOG_ERROR("MessageHandler", "Failed to read message or no message available");
            return std::nullopt;
        }

        LOG_DEBUG("MessageHandler", "Raw message received: " + *raw_message);
        
        auto parsed = parse_message(*raw_message);
        if (parsed.has_value()) {
            LOG_INFO("MessageHandler", "Successfully parsed message: " + parsed->method);
        } else {
            LOG_ERROR("MessageHandler", "Failed to parse message");
        }
        
        return parsed;
    }

    std::optional<std::string> MessageHandler::read_message()
    {
        LOG_DEBUG("MessageHandler", "Starting to read message...");
        
        std::string line;
        size_t content_length = 0;

        // Read headers until we find Content-Length and empty line
        while (true) {
            // Check if input is available without blocking
            if (!_input.rdbuf()->in_avail()) {
                // No input available, check if stream is closed
                if (_input.peek() == EOF) {
                    LOG_DEBUG("MessageHandler", "No input available and EOF reached");
                    return std::nullopt;
                }
                // Sleep briefly and try again to avoid busy-waiting
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            
            // Input is available, try to read a line
            if (!std::getline(_input, line)) {
                LOG_DEBUG("MessageHandler", "Failed to read line or EOF reached");
                return std::nullopt;
            }
            LOG_DEBUG("MessageHandler", "Read header line: '" + line + "'");
            
            // Check if input stream is still good
            if (!_input.good() && !_input.eof()) {
                LOG_ERROR("MessageHandler", "Input stream error while reading headers");
                return std::nullopt;
            }
            
            // Add extra debugging for empty reads
            if (_input.eof()) {
                LOG_DEBUG("MessageHandler", "Reached EOF while reading headers");
                return std::nullopt;
            }
            
            // Check if input is ready to avoid indefinite blocking
            if (!_input.rdbuf()->in_avail() && _input.peek() == std::istream::traits_type::eof()) {
                LOG_DEBUG("MessageHandler", "No more input available");
                return std::nullopt;
            }
            
            // Remove \r if present (Windows line endings)
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (line.empty()) {
                LOG_DEBUG("MessageHandler", "Found empty line, headers complete");
                // Empty line indicates end of headers
                break;
            }

            // Parse Content-Length header
            if (line.find("Content-Length:") == 0) {
                std::string length_str = line.substr(15); // Skip "Content-Length:"
                // Remove leading/trailing whitespace
                length_str.erase(0, length_str.find_first_not_of(" \t"));
                length_str.erase(length_str.find_last_not_of(" \t\r\n") + 1);
                
                try {
                    content_length = std::stoull(length_str);
                    LOG_DEBUG("MessageHandler", "Parsed Content-Length: " + std::to_string(content_length));
                } catch (const std::exception&) {
                    LOG_ERROR("MessageHandler", "Invalid Content-Length: " + length_str);
                    return std::nullopt; // Invalid Content-Length
                }
            }
        }

        if (content_length == 0) {
            LOG_ERROR("MessageHandler", "No valid Content-Length found");
            return std::nullopt; // No valid Content-Length found
        }

        // Check if input stream is still good before reading
        if (!_input.good()) {
            LOG_ERROR("MessageHandler", "Input stream is not in good state before reading content");
            return std::nullopt;
        }

        // Read the JSON content
        LOG_DEBUG("MessageHandler", "Reading " + std::to_string(content_length) + " bytes of JSON content...");
        std::string content;
        content.resize(content_length);
        _input.read(&content[0], content_length);

        if (_input.gcount() != static_cast<std::streamsize>(content_length)) {
            return std::nullopt; // Failed to read expected amount
        }

        return content;
    }

    std::optional<LSPMessage> MessageHandler::parse_message(const std::string& json_content)
    {
        LSPMessage message;

        // Determine message type based on presence of "id" field
        bool has_id = has_json_key(json_content, "id");
        bool has_method = has_json_key(json_content, "method");
        bool has_result = has_json_key(json_content, "result");
        bool has_error = has_json_key(json_content, "error");

        if (has_method && has_id) {
            message.type = MessageType::Request;
        } else if (has_method && !has_id) {
            message.type = MessageType::Notification;
        } else if (has_id && (has_result || has_error)) {
            message.type = MessageType::Response;
        } else {
            return std::nullopt; // Invalid message format
        }

        // Extract common fields
        if (has_id) {
            // Try to extract ID as string first, then as number
            auto string_id = extract_json_string(json_content, "id");
            if (string_id) {
                message.id = *string_id;
                std::cerr << "[DEBUG] Extracted string ID: " << *string_id << std::endl;
            } else {
                // Try as number
                auto int_id = extract_json_int(json_content, "id");
                if (int_id) {
                    message.id = std::to_string(*int_id);
                    std::cerr << "[DEBUG] Extracted int ID: " << *int_id << std::endl;
                } else {
                    std::cerr << "[DEBUG] Failed to extract ID from: " << json_content << std::endl;
                }
            }
        }

        if (has_method) {
            auto method = extract_json_string(json_content, "method");
            if (!method) {
                return std::nullopt;
            }
            message.method = *method;
        }

        // Extract params (for requests and notifications)
        if (has_json_key(json_content, "params")) {
            auto params = extract_json_object(json_content, "params");
            if (params) {
                message.params_json = *params;
                std::cerr << "[DEBUG] Extracted params: " << *params << std::endl;
            } else {
                std::cerr << "[DEBUG] Failed to extract params object" << std::endl;
            }
        } else {
            std::cerr << "[DEBUG] No params key found in: " << json_content << std::endl;
        }

        // Extract result (for responses)
        if (has_result) {
            message.result_json = json_content; // Full message for now
        }

        // Extract error (for error responses)
        if (has_error) {
            auto error_msg = extract_json_string(json_content, "message");
            message.error_message = error_msg.value_or("Unknown error");
        }

        return message;
    }

    void MessageHandler::send_response(const std::string& request_id, const std::string& result_json)
    {
        std::string response = create_response(request_id, result_json);
        
        LOG_INFO("MessageHandler", "Sending response to request ID: " + request_id);
        LOG_DEBUG("MessageHandler", "Response length: " + std::to_string(response.length()));
        LOG_DEBUG("MessageHandler", "Full response: " + response);
        
        _output << "Content-Length: " << response.length() << "\r\n";
        _output << "\r\n";
        _output << response;
        
        // Aggressive flushing for Windows LSP compatibility
        _output.flush();
        std::cout.flush();
        fflush(stdout);
        
        LOG_DEBUG("MessageHandler", "Response sent and flushed (multiple methods)");
    }

    void MessageHandler::send_error(const std::string& request_id, int error_code, const std::string& error_message)
    {
        std::string response = create_error_response(request_id, error_code, error_message);
        
        _output << "Content-Length: " << response.length() << "\r\n";
        _output << "\r\n";
        _output << response;
        _output.flush();
    }

    void MessageHandler::send_notification(const std::string& method, const std::string& params_json)
    {
        std::string notification = create_notification(method, params_json);
        
        _output << "Content-Length: " << notification.length() << "\r\n";
        _output << "\r\n";
        _output << notification;
        _output.flush();
    }

    std::string MessageHandler::create_response(const std::string& id, const std::string& result_json)
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"jsonrpc\":\"2.0\",";
        
        // Check if ID is numeric
        bool is_numeric = !id.empty() && std::all_of(id.begin(), id.end(), ::isdigit);
        if (is_numeric) {
            oss << "\"id\":" << id << ",";
        } else {
            oss << "\"id\":\"" << id << "\",";
        }
        
        oss << "\"result\":" << result_json;
        oss << "}";
        return oss.str();
    }

    std::string MessageHandler::create_error_response(const std::string& id, int error_code, const std::string& error_message)
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"jsonrpc\":\"2.0\",";
        oss << "\"id\":\"" << id << "\",";
        oss << "\"error\":{";
        oss << "\"code\":" << error_code << ",";
        oss << "\"message\":\"" << error_message << "\"";
        oss << "}";
        oss << "}";
        return oss.str();
    }

    std::string MessageHandler::create_notification(const std::string& method, const std::string& params_json)
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"jsonrpc\":\"2.0\",";
        oss << "\"method\":\"" << method << "\",";
        oss << "\"params\":" << params_json;
        oss << "}";
        return oss.str();
    }

    std::optional<std::string> MessageHandler::extract_json_object(const std::string& json, const std::string& key)
    {
        // Look for "key":{ ... } pattern
        std::string pattern = "\"" + key + "\"\\s*:\\s*\\{";
        std::regex start_regex(pattern);
        std::smatch match;
        
        if (!std::regex_search(json, match, start_regex)) {
            return std::nullopt;
        }
        
        size_t start_pos = match.position() + match.length() - 1; // Position of the opening '{'
        size_t brace_count = 1;
        size_t pos = start_pos + 1;
        
        // Find the matching closing brace
        while (pos < json.length() && brace_count > 0) {
            if (json[pos] == '{') {
                brace_count++;
            } else if (json[pos] == '}') {
                brace_count--;
            } else if (json[pos] == '"') {
                // Skip string literals to avoid counting braces inside strings
                pos++;
                while (pos < json.length() && json[pos] != '"') {
                    if (json[pos] == '\\') {
                        pos++; // Skip escaped character
                    }
                    pos++;
                }
            }
            pos++;
        }
        
        if (brace_count == 0) {
            return json.substr(start_pos, pos - start_pos);
        }
        
        return std::nullopt;
    }

    // Simple JSON extraction helpers (basic implementation)
    std::optional<std::string> MessageHandler::extract_json_string(const std::string& json, const std::string& key)
    {
        // Look for "key":"value" pattern
        std::string pattern = "\"" + key + "\"\\s*:\\s*\"([^\"]*?)\"";
        std::regex regex(pattern);
        std::smatch match;
        
        if (std::regex_search(json, match, regex)) {
            return match[1].str();
        }
        return std::nullopt;
    }

    std::optional<int> MessageHandler::extract_json_int(const std::string& json, const std::string& key)
    {
        // Look for "key":number pattern
        std::string pattern = "\"" + key + "\"\\s*:\\s*(-?\\d+)";
        std::regex regex(pattern);
        std::smatch match;
        
        if (std::regex_search(json, match, regex)) {
            try {
                return std::stoi(match[1].str());
            } catch (const std::exception&) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    bool MessageHandler::has_json_key(const std::string& json, const std::string& key)
    {
        // Simple check for "key": pattern
        std::string pattern = "\"" + key + "\"\\s*:";
        std::regex regex(pattern);
        return std::regex_search(json, regex);
    }

    // Convenience methods
    void MessageHandler::send_diagnostics(const std::string& uri, const std::vector<Diagnostic>& diagnostics)
    {
        std::ostringstream params;
        params << "{";
        params << "\"uri\":\"" << uri << "\",";
        params << "\"diagnostics\":[";
        
        for (size_t i = 0; i < diagnostics.size(); ++i) {
            const auto& diag = diagnostics[i];
            if (i > 0) params << ",";
            
            params << "{";
            params << "\"range\":{";
            params << "\"start\":{\"line\":" << diag.range.start.line << ",\"character\":" << diag.range.start.character << "},";
            params << "\"end\":{\"line\":" << diag.range.end.line << ",\"character\":" << diag.range.end.character << "}";
            params << "},";
            if (diag.severity) {
                params << "\"severity\":" << static_cast<int>(*diag.severity) << ",";
            }
            if (diag.source) {
                params << "\"source\":\"" << *diag.source << "\",";
            }
            params << "\"message\":\"" << diag.message << "\"";
            params << "}";
        }
        
        params << "]";
        params << "}";
        
        send_notification("textDocument/publishDiagnostics", params.str());
    }

    void MessageHandler::send_log_message(const std::string& message, int type)
    {
        std::ostringstream params;
        params << "{";
        params << "\"type\":" << type << ",";
        params << "\"message\":\"" << message << "\"";
        params << "}";
        
        send_notification("window/logMessage", params.str());
    }

    void MessageHandler::send_show_message(const std::string& message, int type)
    {
        std::ostringstream params;
        params << "{";
        params << "\"type\":" << type << ",";
        params << "\"message\":\"" << message << "\"";
        params << "}";
        
        send_notification("window/showMessage", params.str());
    }

} // namespace Cryo::LSP
