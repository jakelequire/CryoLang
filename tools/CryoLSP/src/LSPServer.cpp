#include "LSPServer.hpp"
#include "Utils/file.hpp"
#include <iostream>
#include <sstream>
#include <regex>
#include <unordered_map>
#include <algorithm>
#include <cctype>

namespace Cryo::LSP
{
    LSPServer::LSPServer()
        : _initialized(false), _shutdown_requested(false)
    {
        // Initialize compiler instance
        _compiler = Cryo::create_compiler_instance();
        _compiler->set_debug_mode(false);
        
        // Initialize message handler
        _message_handler = std::make_unique<MessageHandler>();
        
        std::cerr << "[LSP] CryoLSP server created" << std::endl;
    }

    void LSPServer::run()
    {
        std::cerr << "[LSP] Server listening for messages..." << std::endl;
        
        while (!_shutdown_requested && !_message_handler->is_shutdown_requested())
        {
            try 
            {
                auto message = _message_handler->receive_message();
                if (message.has_value())
                {
                    process_message(message.value());
                }
                else
                {
                    // No more messages or error occurred
                    break;
                }
            }
            catch (const std::exception& e)
            {
                std::cerr << "[LSP] Error processing message: " << e.what() << std::endl;
            }
        }
        
        std::cerr << "[LSP] Server shutting down" << std::endl;
    }

    void LSPServer::process_message(const LSPMessage& message)
    {
        std::cerr << "[LSP] Processing method: " << message.method << std::endl;
        
        if (message.method == "initialize")
        {
            handle_initialize(message.id.value_or(""));
        }
        else if (message.method == "shutdown")
        {
            handle_shutdown(message.id.value_or(""));
        }
        else if (message.method == "exit")
        {
            handle_exit();
        }
        else if (message.method == "textDocument/didOpen")
        {
            handle_text_document_did_open(message.params_json.value_or(""));
        }
        else if (message.method == "textDocument/didChange")
        {
            handle_text_document_did_change(message.params_json.value_or(""));
        }
        else if (message.method == "textDocument/hover")
        {
            handle_text_document_hover(message.id.value_or(""), message.params_json.value_or(""));
        }
        else
        {
            std::cerr << "[LSP] Unhandled method: " << message.method << std::endl;
        }
    }

    void LSPServer::handle_initialize(const std::string& request_id)
    {
        std::string response = "{"
            "\"capabilities\": {"
                "\"textDocumentSync\": 1,"
                "\"hoverProvider\": true,"
                "\"completionProvider\": {"
                    "\"triggerCharacters\": [\".\", \":\", \"@\"]"
                "}"
            "}"
        "}";
        
        _message_handler->send_response(request_id, response);
        _initialized = true;
        std::cerr << "[LSP] Initialized with hover support" << std::endl;
    }

    void LSPServer::handle_shutdown(const std::string& request_id)
    {
        _message_handler->send_response(request_id, "null");
        _shutdown_requested = true;
    }

    void LSPServer::handle_exit()
    {
        std::exit(0);
    }

    void LSPServer::handle_text_document_did_open(const std::string& params_json)
    {
        // Extract URI and content from JSON using simple regex
        std::regex uri_regex("\"uri\":\\s*\"([^\"]+)\"");
        std::regex text_regex("\"text\":\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
        std::regex version_regex("\"version\":\\s*(\\d+)");
        
        std::smatch match;
        std::string uri, content;
        int version = 1;
        
        if (std::regex_search(params_json, match, uri_regex))
        {
            uri = match[1].str();
        }
        
        if (std::regex_search(params_json, match, text_regex))
        {
            content = match[1].str();
            // Basic JSON string unescaping
            content = std::regex_replace(content, std::regex("\\\\n"), "\n");
            content = std::regex_replace(content, std::regex("\\\\r"), "\r");
            content = std::regex_replace(content, std::regex("\\\\t"), "\t");
            content = std::regex_replace(content, std::regex("\\\\\""), "\"");
            content = std::regex_replace(content, std::regex("\\\\\\\\"), "\\");
        }
        
        if (std::regex_search(params_json, match, version_regex))
        {
            version = std::stoi(match[1].str());
        }
        
        if (!uri.empty())
        {
            update_document(uri, content, version);
            std::cerr << "[LSP] Document opened: " << uri << std::endl;
        }
    }

    void LSPServer::handle_text_document_did_change(const std::string& params_json)
    {
        // For full document sync (textDocumentSync = 1)
        std::regex uri_regex("\"uri\":\\s*\"([^\"]+)\"");
        std::regex text_regex("\"text\":\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
        std::regex version_regex("\"version\":\\s*(\\d+)");
        
        std::smatch match;
        std::string uri, content;
        int version = 1;
        
        if (std::regex_search(params_json, match, uri_regex))
        {
            uri = match[1].str();
        }
        
        if (std::regex_search(params_json, match, text_regex))
        {
            content = match[1].str();
            // Basic JSON string unescaping
            content = std::regex_replace(content, std::regex("\\\\n"), "\n");
            content = std::regex_replace(content, std::regex("\\\\r"), "\r");
            content = std::regex_replace(content, std::regex("\\\\t"), "\t");
            content = std::regex_replace(content, std::regex("\\\\\""), "\"");
            content = std::regex_replace(content, std::regex("\\\\\\\\"), "\\");
        }
        
        if (std::regex_search(params_json, match, version_regex))
        {
            version = std::stoi(match[1].str());
        }
        
        if (!uri.empty())
        {
            update_document(uri, content, version);
        }
    }

    void LSPServer::handle_text_document_hover(const std::string& request_id, const std::string& params_json)
    {
        std::cerr << "[LSP] Handling hover request with params: " << params_json << std::endl;
        
        // Extract URI and position from JSON
        std::regex uri_regex("\"uri\":\\s*\"([^\"]+)\"");
        std::regex line_regex("\"line\":\\s*(\\d+)");
        std::regex char_regex("\"character\":\\s*(\\d+)");
        
        std::smatch match;
        std::string uri;
        uint32_t line = 0, character = 0;
        
        if (std::regex_search(params_json, match, uri_regex))
        {
            uri = match[1].str();
            std::cerr << "[LSP] Extracted URI: " << uri << std::endl;
        }
        
        if (std::regex_search(params_json, match, line_regex))
        {
            line = static_cast<uint32_t>(std::stoul(match[1].str()));
            std::cerr << "[LSP] Extracted line: " << line << std::endl;
        }
        
        if (std::regex_search(params_json, match, char_regex))
        {
            character = static_cast<uint32_t>(std::stoul(match[1].str()));
            std::cerr << "[LSP] Extracted character: " << character << std::endl;
        }
        
        Position position(line, character);
        auto hover_info = get_hover_info(uri, position);
        
        std::string response;
        if (hover_info.has_value())
        {
            std::cerr << "[LSP] Hover info found, sending response" << std::endl;
            response = "{"
                "\"contents\": {"
                    "\"kind\": \"" + hover_info->contents.kind + "\","
                    "\"value\": \"" + hover_info->contents.value + "\""
                "}"
            "}";
        }
        else
        {
            std::cerr << "[LSP] No hover info found, sending null" << std::endl;
            response = "null";
        }
        
        _message_handler->send_response(request_id, response);
    }

    void LSPServer::update_document(const std::string& uri, const std::string& content, int version)
    {
        _document_contents[uri] = content;
        _document_versions[uri] = version;
        
        // Parse the document with our compiler to get fresh AST and symbol table
        if (!content.empty())
        {
            try
            {
                _compiler->parse_source(content);
                std::cerr << "[LSP] Document parsed successfully: " << uri << std::endl;
            }
            catch (const std::exception& e)
            {
                std::cerr << "[LSP] Error parsing document " << uri << ": " << e.what() << std::endl;
            }
        }
    }

    std::optional<std::string> LSPServer::get_document_content(const std::string& uri)
    {
        auto it = _document_contents.find(uri);
        if (it != _document_contents.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    std::optional<Hover> LSPServer::get_hover_info(const std::string& uri, const Position& position)
    {
        std::cerr << "[LSP] get_hover_info called for URI: " << uri << " at position " << position.line << ":" << position.character << std::endl;
        
        auto content = get_document_content(uri);
        if (!content.has_value())
        {
            std::cerr << "[LSP] No document content found for URI: " << uri << std::endl;
            return std::nullopt;
        }
        
        std::cerr << "[LSP] Document content length: " << content.value().length() << std::endl;
        
        // Get the symbol at the cursor position
        std::string symbol_name = get_symbol_at_position(content.value(), position);
        if (symbol_name.empty())
        {
            std::cerr << "[LSP] No symbol found at position" << std::endl;
            return std::nullopt;
        }
        
        std::cerr << "[LSP] Found symbol: '" << symbol_name << "'" << std::endl;
        
        // Find information about the symbol
        auto symbol_info = find_symbol_info(symbol_name, uri);
        if (!symbol_info.has_value())
        {
            std::cerr << "[LSP] No symbol info found for: " << symbol_name << std::endl;
            return std::nullopt;
        }
        
        std::cerr << "[LSP] Found symbol info, creating hover response" << std::endl;
        
        Hover hover;
        hover.contents.kind = "markdown";
        hover.contents.value = symbol_info.value();
        
        return hover;
    }

    std::string LSPServer::get_symbol_at_position(const std::string& content, const Position& position)
    {
        // Split content into lines
        std::vector<std::string> lines;
        std::stringstream ss(content);
        std::string line;
        
        while (std::getline(ss, line))
        {
            lines.push_back(line);
        }
        
        if (position.line >= lines.size())
        {
            return "";
        }
        
        const std::string& target_line = lines[position.line];
        if (position.character >= target_line.length())
        {
            return "";
        }
        
        // Find word boundaries around the position
        size_t start = position.character;
        size_t end = position.character;
        
        // Move start backward to find beginning of identifier
        while (start > 0 && (std::isalnum(target_line[start - 1]) || target_line[start - 1] == '_'))
        {
            start--;
        }
        
        // Move end forward to find end of identifier
        while (end < target_line.length() && (std::isalnum(target_line[end]) || target_line[end] == '_'))
        {
            end++;
        }
        
        if (start == end)
        {
            return "";
        }
        
        return target_line.substr(start, end - start);
    }

    std::optional<std::string> LSPServer::find_symbol_info(const std::string& symbol_name, const std::string& uri)
    {
        std::cerr << "[LSP] find_symbol_info called for symbol: " << symbol_name << std::endl;
        
        if (!_compiler || !_compiler->symbol_table())
        {
            std::cerr << "[LSP] Compiler or symbol table is null" << std::endl;
            return std::nullopt;
        }
        
        auto* symbol_table = _compiler->symbol_table();
        std::cerr << "[LSP] Symbol table exists, looking up symbol..." << std::endl;
        
        // Try to find the symbol in the symbol table
        auto symbol = symbol_table->lookup_symbol(symbol_name);
        if (symbol)
        {
            std::cerr << "[LSP] Found symbol in symbol table!" << std::endl;
            std::stringstream info;
            
            // Header with symbol name and kind
            switch (symbol->kind)
            {
                case Cryo::SymbolKind::Function:
                    info << "🔧 **function** `" << symbol->name << "`\\n\\n";
                    break;
                case Cryo::SymbolKind::Variable:
                    info << "📦 **variable** `" << symbol->name << "`\\n\\n";
                    break;
                case Cryo::SymbolKind::Type:
                    info << "🏷️ **type** `" << symbol->name << "`\\n\\n";
                    break;
                default:
                    info << "**" << symbol->name << "**\\n\\n";
                    break;
            }
            
            // Type information with enhanced formatting
            if (symbol->data_type)
            {
                std::string type_str = symbol->data_type->to_string();
                info << "```cryo\\n" << type_str << "\\n```\\n\\n";
                
                // Add additional type context
                if (symbol->kind == Cryo::SymbolKind::Function)
                {
                    info << "**Function Signature**\\n\\n";
                    if (type_str.find("->") != std::string::npos)
                    {
                        size_t arrow_pos = type_str.find("->");
                        std::string params = type_str.substr(0, arrow_pos);
                        std::string return_type = type_str.substr(arrow_pos + 2);
                        
                        // Clean up formatting
                        params.erase(0, params.find_first_not_of(" \t"));
                        params.erase(params.find_last_not_of(" \t") + 1);
                        return_type.erase(0, return_type.find_first_not_of(" \t"));
                        return_type.erase(return_type.find_last_not_of(" \t") + 1);
                        
                        info << "- **Parameters:** `" << params << "`\\n";
                        info << "- **Returns:** `" << return_type << "`\\n\\n";
                    }
                }
                else if (symbol->kind == Cryo::SymbolKind::Variable)
                {
                    // Check if it's an array, pointer, or reference
                    if (type_str.find("[]") != std::string::npos)
                    {
                        info << "*Array type*\\n\\n";
                    }
                    else if (type_str.find("*") != std::string::npos)
                    {
                        info << "*Pointer type*\\n\\n";
                    }
                    else if (type_str.find("&") != std::string::npos)
                    {
                        info << "*Reference type*\\n\\n";
                    }
                }
            }
            else
            {
                info << "**Type:** `unknown`\\n\\n";
            }
            
            // Scope information
            if (!symbol->scope.empty() && symbol->scope != "Global")
            {
                info << "**Scope:** `" << symbol->scope << "`\\n\\n";
            }
            else if (symbol->scope == "Global")
            {
                info << "**Scope:** *Global*\\n\\n";
            }
            
            // Location information
            info << "**Declared at:** Line " << symbol->declaration_location.line() 
                 << ", Column " << symbol->declaration_location.column() << "\\n\\n";
            
            // Try to extract documentation comments from the source
            auto doc_comment = extract_documentation_comment(uri, symbol->declaration_location);
            if (!doc_comment.empty())
            {
                info << "---\\n\\n";
                info << doc_comment << "\\n\\n";
            }
            
            // Add usage hints based on symbol type
            if (symbol->kind == Cryo::SymbolKind::Function)
            {
                info << "*💡 Tip: Functions can be called with arguments matching their parameter types.*";
            }
            else if (symbol->kind == Cryo::SymbolKind::Variable)
            {
                info << "*💡 Tip: Variables can be used in expressions and assignments.*";
            }
            else if (symbol->kind == Cryo::SymbolKind::Type)
            {
                info << "*💡 Tip: Types can be used for variable declarations and function parameters.*";
            }
            
            return info.str();
        }
        else 
        {
            std::cerr << "[LSP] Symbol not found in symbol table" << std::endl;
        }
        
        // If not found, try to provide context-aware fallback information
        std::cerr << "[LSP] Trying fallback information..." << std::endl;
        auto fallback_info = get_context_aware_fallback(symbol_name, uri);
        if (fallback_info.has_value())
        {
            std::cerr << "[LSP] Fallback info found" << std::endl;
            return fallback_info.value();
        }
        
        std::cerr << "[LSP] No fallback info, returning basic message" << std::endl;
        // Last resort - basic message
        return "❓ **" + symbol_name + "**\\n\\n*Symbol information not available*\\n\\n*This symbol may be defined in an external module or may contain a typo.*";
    }

    Position LSPServer::convert_location_to_position(const SourceLocation& loc)
    {
        // LSP positions are 0-based, SourceLocation might be 1-based
        return Position(loc.line() - 1, loc.column() - 1);
    }

    SourceLocation LSPServer::convert_position_to_location(const Position& pos)
    {
        // Convert 0-based LSP position to 1-based SourceLocation
        return SourceLocation(pos.line + 1, pos.character + 1);
    }

    std::string LSPServer::extract_documentation_comment(const std::string& uri, const SourceLocation& location)
    {
        auto content = get_document_content(uri);
        if (!content.has_value())
        {
            return "";
        }
        
        // Split content into lines
        std::vector<std::string> lines;
        std::stringstream ss(content.value());
        std::string line;
        
        while (std::getline(ss, line))
        {
            lines.push_back(line);
        }
        
        if (location.line() - 1 >= lines.size() || location.line() == 0)
        {
            return "";
        }
        
        std::stringstream doc_comment;
        size_t target_line = location.line() - 1; // Convert to 0-based
        
        // Look for comments above the declaration line
        std::vector<std::string> comment_lines;
        for (int i = static_cast<int>(target_line) - 1; i >= 0; i--)
        {
            std::string trimmed = lines[i];
            // Remove leading whitespace
            trimmed.erase(0, trimmed.find_first_not_of(" \t"));
            
            if (trimmed.empty())
            {
                // Empty line - check if we already found comments
                if (!comment_lines.empty())
                {
                    break;
                }
                continue;
            }
            
            // Check for various comment patterns
            if ((trimmed.length() >= 2 && trimmed.substr(0, 2) == "//") || 
                (trimmed.length() >= 3 && trimmed.substr(0, 3) == "///"))
            {
                // Single line comment
                size_t start_pos = (trimmed.length() >= 3 && trimmed.substr(0, 3) == "///") ? 3 : 2;
                std::string comment_text = (trimmed.length() > start_pos) ? trimmed.substr(start_pos) : "";
                comment_text.erase(0, comment_text.find_first_not_of(" \t"));
                comment_lines.insert(comment_lines.begin(), comment_text);
            }
            else if ((trimmed.length() >= 2 && trimmed.substr(0, 2) == "/*") || 
                     (trimmed.length() >= 3 && trimmed.substr(0, 3) == "/**"))
            {
                // Multi-line comment start - extract the content
                size_t start_pos = (trimmed.length() >= 3 && trimmed.substr(0, 3) == "/**") ? 3 : 2;
                std::string comment_text = (trimmed.length() > start_pos) ? trimmed.substr(start_pos) : "";
                
                // Check if it's a single line /* ... */ comment
                if (trimmed.length() >= 2 && trimmed.substr(trimmed.length() - 2) == "*/")
                {
                    if (comment_text.length() >= 2)
                    {
                        comment_text = comment_text.substr(0, comment_text.length() - 2);
                    }
                    comment_text.erase(0, comment_text.find_first_not_of(" \t"));
                    if (!comment_text.empty())
                    {
                        size_t last_non_space = comment_text.find_last_not_of(" \t");
                        if (last_non_space != std::string::npos)
                        {
                            comment_text.erase(last_non_space + 1);
                        }
                    }
                    if (!comment_text.empty())
                    {
                        comment_lines.insert(comment_lines.begin(), comment_text);
                    }
                    break;
                }
                else
                {
                    // Multi-line comment - collect until closing */
                    comment_text.erase(0, comment_text.find_first_not_of(" \t"));
                    if (!comment_text.empty())
                    {
                        comment_lines.insert(comment_lines.begin(), comment_text);
                    }
                    
                    // Look for continuation and closing
                    for (int j = i - 1; j >= 0; j--)
                    {
                        std::string multi_line = lines[j];
                        multi_line.erase(0, multi_line.find_first_not_of(" \t"));
                        
                        if (multi_line.find("*/") != std::string::npos)
                        {
                            // Found closing
                            std::string final_part = multi_line.substr(0, multi_line.find("*/"));
                            final_part.erase(0, final_part.find_first_not_of(" \t*"));
                            if (!final_part.empty())
                            {
                                comment_lines.insert(comment_lines.begin(), final_part);
                            }
                            i = j; // Update outer loop
                            break;
                        }
                        else
                        {
                            // Continuation line
                            multi_line.erase(0, multi_line.find_first_not_of(" \t*"));
                            if (!multi_line.empty())
                            {
                                comment_lines.insert(comment_lines.begin(), multi_line);
                            }
                        }
                    }
                    break;
                }
            }
            else
            {
                // Not a comment - stop looking
                break;
            }
        }
        
        // Format the extracted comments
        if (!comment_lines.empty())
        {
            doc_comment << "**Documentation**\\n\\n";
            for (const auto& comment_line : comment_lines)
            {
                if (!comment_line.empty())
                {
                    doc_comment << comment_line << "\\n";
                }
            }
        }
        
        return doc_comment.str();
    }

    std::optional<std::string> LSPServer::get_context_aware_fallback(const std::string& symbol_name, const std::string& uri)
    {
        // Check if it's a built-in type
        auto builtin_info = get_builtin_type_info(symbol_name);
        if (!builtin_info.empty())
        {
            return builtin_info;
        }
        
        // Check for common programming patterns
        if ((symbol_name.length() >= 5 && symbol_name.substr(0, 5) == "test_") || 
            (symbol_name.length() >= 4 && symbol_name.substr(0, 4) == "Test"))
        {
            return "🧪 **" + symbol_name + "**\\n\\n*Test function or variable*\\n\\nThis appears to be part of a test suite or testing functionality.";
        }
        
        if ((symbol_name.length() >= 4 && symbol_name.substr(0, 4) == "get_") || 
            (symbol_name.length() >= 3 && symbol_name.substr(0, 3) == "Get"))
        {
            return "📥 **" + symbol_name + "**\\n\\n*Getter function*\\n\\nThis appears to be a getter function that retrieves a value.";
        }
        
        if ((symbol_name.length() >= 4 && symbol_name.substr(0, 4) == "set_") || 
            (symbol_name.length() >= 3 && symbol_name.substr(0, 3) == "Set"))
        {
            return "📤 **" + symbol_name + "**\\n\\n*Setter function*\\n\\nThis appears to be a setter function that modifies a value.";
        }
        
        if ((symbol_name.length() >= 3 && symbol_name.substr(0, 3) == "is_") || 
            (symbol_name.length() >= 4 && symbol_name.substr(0, 4) == "has_") || 
            (symbol_name.length() >= 4 && symbol_name.substr(0, 4) == "can_"))
        {
            return "❓ **" + symbol_name + "**\\n\\n*Boolean predicate*\\n\\nThis appears to be a function that returns a boolean value.";
        }
        
        // Check if it looks like a constant (all uppercase)
        if (std::all_of(symbol_name.begin(), symbol_name.end(), 
                       [](char c) { return std::isupper(c) || c == '_' || std::isdigit(c); }) && 
            !symbol_name.empty())
        {
            return "📋 **" + symbol_name + "**\\n\\n*Constant*\\n\\nThis appears to be a constant value (all uppercase naming convention).";
        }
        
        return std::nullopt;
    }

    std::string LSPServer::get_builtin_type_info(const std::string& type_name)
    {
        static const std::unordered_map<std::string, std::string> builtin_types = {
            // Basic types
            {"int", "🔢 **int**\\n\\n```cryo\\nint\\n```\\n\\n*Signed 32-bit integer*\\n\\nRange: -2,147,483,648 to 2,147,483,647\\n\\n💡 *Use for whole numbers and counting.*"},
            {"i8", "🔢 **i8**\\n\\n```cryo\\ni8\\n```\\n\\n*Signed 8-bit integer*\\n\\nRange: -128 to 127\\n\\n💡 *Use for small integer values to save memory.*"},
            {"i16", "🔢 **i16**\\n\\n```cryo\\ni16\\n```\\n\\n*Signed 16-bit integer*\\n\\nRange: -32,768 to 32,767\\n\\n💡 *Use for medium-sized integer values.*"},
            {"i32", "🔢 **i32**\\n\\n```cryo\\ni32\\n```\\n\\n*Signed 32-bit integer*\\n\\nRange: -2,147,483,648 to 2,147,483,647\\n\\n💡 *Standard integer size for most applications.*"},
            {"i64", "🔢 **i64**\\n\\n```cryo\\ni64\\n```\\n\\n*Signed 64-bit integer*\\n\\nRange: -9,223,372,036,854,775,808 to 9,223,372,036,854,775,807\\n\\n💡 *Use for very large integer values.*"},
            
            {"uint", "🔢 **uint**\\n\\n```cryo\\nuint\\n```\\n\\n*Unsigned 32-bit integer*\\n\\nRange: 0 to 4,294,967,295\\n\\n💡 *Use for non-negative whole numbers.*"},
            {"uint8", "🔢 **uint8**\\n\\n```cryo\\nuint8\\n```\\n\\n*Unsigned 8-bit integer*\\n\\nRange: 0 to 255\\n\\n💡 *Common for byte operations and small counts.*"},
            {"uint16", "🔢 **uint16**\\n\\n```cryo\\nuint16\\n```\\n\\n*Unsigned 16-bit integer*\\n\\nRange: 0 to 65,535\\n\\n💡 *Use for medium-sized positive values.*"},
            {"uint32", "🔢 **uint32**\\n\\n```cryo\\nuint32\\n```\\n\\n*Unsigned 32-bit integer*\\n\\nRange: 0 to 4,294,967,295\\n\\n💡 *Standard size for positive integers.*"},
            {"uint64", "🔢 **uint64**\\n\\n```cryo\\nuint64\\n```\\n\\n*Unsigned 64-bit integer*\\n\\nRange: 0 to 18,446,744,073,709,551,615\\n\\n💡 *Use for very large positive values.*"},
            
            {"float", "🔢 **float**\\n\\n```cryo\\nfloat\\n```\\n\\n*32-bit floating-point number*\\n\\nPrecision: ~7 decimal digits\\n\\n💡 *Use for decimal numbers with moderate precision.*"},
            {"f32", "🔢 **f32**\\n\\n```cryo\\nf32\\n```\\n\\n*32-bit floating-point number*\\n\\nPrecision: ~7 decimal digits\\n\\n💡 *Single precision floating-point.*"},
            {"f64", "🔢 **f64**\\n\\n```cryo\\nf64\\n```\\n\\n*64-bit floating-point number*\\n\\nPrecision: ~15 decimal digits\\n\\n💡 *Double precision floating-point for high accuracy.*"},
            {"double", "🔢 **double**\\n\\n```cryo\\ndouble\\n```\\n\\n*64-bit floating-point number*\\n\\nPrecision: ~15 decimal digits\\n\\n💡 *High precision decimal numbers.*"},
            
            {"boolean", "✅ **boolean**\\n\\n```cryo\\nboolean\\n```\\n\\n*Boolean value*\\n\\nValues: `true` or `false`\\n\\n💡 *Use for logical operations and conditions.*"},
            {"bool", "✅ **bool**\\n\\n```cryo\\nbool\\n```\\n\\n*Boolean value*\\n\\nValues: `true` or `false`\\n\\n💡 *Use for logical operations and conditions.*"},
            
            {"char", "📝 **char**\\n\\n```cryo\\nchar\\n```\\n\\n*Single character*\\n\\nUTF-8 encoded character\\n\\n💡 *Use for individual characters and text processing.*"},
            {"string", "📝 **string**\\n\\n```cryo\\nstring\\n```\\n\\n*Text string*\\n\\nUTF-8 encoded string of characters\\n\\n💡 *Use for text, names, and string operations.*"},
            
            {"void", "🚫 **void**\\n\\n```cryo\\nvoid\\n```\\n\\n*No value type*\\n\\nRepresents the absence of a value\\n\\n💡 *Used for functions that don't return a value.*"},
            
            // Common literals
            {"true", "✅ **true**\\n\\n```cryo\\ntrue\\n```\\n\\n*Boolean literal*\\n\\nRepresents the logical true value\\n\\n💡 *Use in boolean expressions and conditions.*"},
            {"false", "❌ **false**\\n\\n```cryo\\nfalse\\n```\\n\\n*Boolean literal*\\n\\nRepresents the logical false value\\n\\n💡 *Use in boolean expressions and conditions.*"},
            
            {"null", "🚫 **null**\\n\\n```cryo\\nnull\\n```\\n\\n*Null value*\\n\\nRepresents the absence of a valid object reference\\n\\n💡 *Use to indicate 'no value' for pointer types.*"}
        };
        
        auto it = builtin_types.find(type_name);
        if (it != builtin_types.end())
        {
            return it->second;
        }
        
        return "";
    }
}
