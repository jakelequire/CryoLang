#pragma once

#include "Compiler/CompilerInstance.hpp"
#include "LSPProtocol.hpp"
#include "MessageHandler.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace Cryo::LSP
{
    class LSPServer
    {
    public:
        LSPServer();
        ~LSPServer() = default;

        void run();

    private:
        std::unique_ptr<Cryo::CompilerInstance> _compiler;
        std::unique_ptr<MessageHandler> _message_handler;
        bool _initialized;
        bool _shutdown_requested;
        
        // Document tracking
        std::unordered_map<std::string, std::string> _document_contents;
        std::unordered_map<std::string, int> _document_versions;

        // Message processing
        void process_message(const LSPMessage& message);
        void handle_initialize(const std::string& request_id);
        void handle_shutdown(const std::string& request_id);
        void handle_exit();
        void handle_text_document_did_open(const std::string& params_json);
        void handle_text_document_did_change(const std::string& params_json);
        void handle_text_document_hover(const std::string& request_id, const std::string& params_json);
        
        // Document management
        void update_document(const std::string& uri, const std::string& content, int version);
        std::optional<std::string> get_document_content(const std::string& uri);
        
        // Symbol resolution
        std::optional<Hover> get_hover_info(const std::string& uri, const Position& position);
        std::string get_symbol_at_position(const std::string& content, const Position& position);
        std::optional<std::string> find_symbol_info(const std::string& symbol_name, const std::string& uri);
        
        // Documentation and context extraction
        std::string extract_documentation_comment(const std::string& uri, const SourceLocation& location);
        std::optional<std::string> get_context_aware_fallback(const std::string& symbol_name, const std::string& uri);
        std::string get_builtin_type_info(const std::string& type_name);
        
        // Utility
        Position convert_location_to_position(const SourceLocation& loc);
        SourceLocation convert_position_to_location(const Position& pos);
    };
}
