#include "LSPServer.hpp"
#include <filesystem>

namespace CryoLSP
{

    LSPResponse LSPServer::handle_initialize(const LSPRequest &request, socket_t socket)
    {
        log_info("Handling initialize request");

        LSPResponse response;
        response.id = request.id;

        ServerCapabilities capabilities;
        response.result = JsonValue::object({{"capabilities", capabilities.to_json()},
                                             {"serverInfo", JsonValue::object({{"name", JsonValue("CryoLang LSP Server")},
                                                                               {"version", JsonValue("1.0.0")}})}});

        return response;
    }

    LSPResponse LSPServer::handle_initialized(const LSPRequest &request, socket_t socket)
    {
        log_info("Client initialized");
        LSPResponse response;
        response.id = request.id;
        response.result = json::object();
        return response;
    }

    LSPResponse LSPServer::handle_shutdown(const LSPRequest &request, socket_t socket)
    {
        log_info("Shutdown requested");
        _shutdown_requested.store(true);

        LSPResponse response;
        response.id = request.id;
        response.result = json::object();
        return response;
    }

    LSPResponse LSPServer::handle_exit(const LSPRequest &request, socket_t socket)
    {
        log_info("Exit requested");
        _running.store(false);

        LSPResponse response;
        response.id = request.id;
        response.result = json::object();
        return response;
    }

    LSPResponse LSPServer::handle_text_document_did_open(const LSPRequest &request, socket_t socket)
    {
        try
        {
            TextDocumentItem document = TextDocumentItem::from_json(request.params["textDocument"]);

            log_info("Document opened: " + document.uri);

            _document_manager->open_document(document);

            // Compile and publish diagnostics
            if (_config.enable_diagnostics)
            {
                compile_document(document.uri, document.text);
                publish_diagnostics(document.uri, socket);
            }
        }
        catch (const std::exception &e)
        {
            log_error("Error in textDocument/didOpen: " + std::string(e.what()));
        }

        LSPResponse response;
        response.id = request.id;
        response.result = json::object();
        return response;
    }

    LSPResponse LSPServer::handle_text_document_did_change(const LSPRequest &request, socket_t socket)
    {
        try
        {
            VersionedTextDocumentIdentifier doc_id = VersionedTextDocumentIdentifier::from_json(request.params["textDocument"]);

            std::vector<TextDocumentContentChangeEvent> changes;
            const auto &content_changes = request.params.as_object().at("contentChanges");
            if (content_changes.is_array())
            {
                const auto &arr = content_changes.as_array();
                for (size_t i = 0; i < arr.size(); ++i)
                {
                    changes.push_back(TextDocumentContentChangeEvent::from_json(arr[i]));
                }
            }

            log_debug("Document changed: " + doc_id.uri + " version: " + std::to_string(doc_id.version));

            _document_manager->update_document(doc_id.uri, doc_id.version, changes);

            // Compile and publish diagnostics
            if (_config.enable_diagnostics)
            {
                std::string content = _document_manager->get_document_content(doc_id.uri);
                compile_document(doc_id.uri, content);
                publish_diagnostics(doc_id.uri, socket);
            }
        }
        catch (const std::exception &e)
        {
            log_error("Error in textDocument/didChange: " + std::string(e.what()));
        }

        LSPResponse response;
        response.id = request.id;
        response.result = json::object();
        return response;
    }

    LSPResponse LSPServer::handle_text_document_did_save(const LSPRequest &request, socket_t socket)
    {
        try
        {
            TextDocumentIdentifier doc_id = TextDocumentIdentifier::from_json(request.params["textDocument"]);

            log_info("Document saved: " + doc_id.uri);

            _document_manager->save_document(doc_id.uri);

            // Re-compile on save for accuracy
            if (_config.enable_diagnostics)
            {
                std::string content = _document_manager->get_document_content(doc_id.uri);
                compile_document(doc_id.uri, content);
                publish_diagnostics(doc_id.uri, socket);
            }
        }
        catch (const std::exception &e)
        {
            log_error("Error in textDocument/didSave: " + std::string(e.what()));
        }

        LSPResponse response;
        response.id = request.id;
        response.result = json::object();
        return response;
    }

    LSPResponse LSPServer::handle_text_document_did_close(const LSPRequest &request, socket_t socket)
    {
        try
        {
            TextDocumentIdentifier doc_id = TextDocumentIdentifier::from_json(request.params["textDocument"]);

            log_info("Document closed: " + doc_id.uri);

            _document_manager->close_document(doc_id.uri);
            _diagnostics_provider->clear_diagnostics(doc_id.uri);
        }
        catch (const std::exception &e)
        {
            log_error("Error in textDocument/didClose: " + std::string(e.what()));
        }

        LSPResponse response;
        response.id = request.id;
        response.result = json::object();
        return response;
    }

    LSPResponse LSPServer::handle_text_document_hover(const LSPRequest &request, socket_t socket)
    {
        LSPResponse response;
        response.id = request.id;

        if (!_config.enable_hover)
        {
            response.result = json(nullptr);
            return response;
        }

        try
        {
            TextDocumentPositionParams params = TextDocumentPositionParams::from_json(request.params);

            log_debug("Hover requested at " + params.text_document.uri + " line: " +
                      std::to_string(params.position.line) + " char: " + std::to_string(params.position.character));

            auto hover_info = _hover_provider->get_hover(params.text_document.uri, params.position);

            if (hover_info)
            {
                response.result = hover_info->to_json();
            }
            else
            {
                response.result = json(nullptr);
            }
        }
        catch (const std::exception &e)
        {
            log_error("Error in textDocument/hover: " + std::string(e.what()));
            response.result = json(nullptr);
        }

        return response;
    }

    LSPResponse LSPServer::handle_text_document_completion(const LSPRequest &request, socket_t socket)
    {
        LSPResponse response;
        response.id = request.id;

        if (!_config.enable_completion)
        {
            response.result = CompletionList{false, {}}.to_json();
            return response;
        }

        try
        {
            TextDocumentPositionParams params = TextDocumentPositionParams::from_json(request.params);

            log_debug("Completion requested at " + params.text_document.uri);

            CompletionList completions = _completion_provider->get_completions(params.text_document.uri, params.position);
            response.result = completions.to_json();
        }
        catch (const std::exception &e)
        {
            log_error("Error in textDocument/completion: " + std::string(e.what()));
            response.result = CompletionList{false, {}}.to_json();
        }

        return response;
    }

    LSPResponse LSPServer::handle_text_document_definition(const LSPRequest &request, socket_t socket)
    {
        LSPResponse response;
        response.id = request.id;

        if (!_config.enable_goto_definition)
        {
            response.result = json::array();
            return response;
        }

        try
        {
            TextDocumentPositionParams params = TextDocumentPositionParams::from_json(request.params);

            log_debug("Definition requested at " + params.text_document.uri);

            std::vector<Location> definitions = _symbol_provider->find_definition(params.text_document.uri, params.position);

            auto definitions_json = JsonValue::array();
            for (const auto &def : definitions)
            {
                definitions_json.as_array().push_back(def.to_json());
            }

            response.result = definitions_json;
        }
        catch (const std::exception &e)
        {
            log_error("Error in textDocument/definition: " + std::string(e.what()));
            response.result = JsonValue::array();
        }

        return response;
    }

    LSPResponse LSPServer::handle_text_document_references(const LSPRequest &request, socket_t socket)
    {
        LSPResponse response;
        response.id = request.id;

        try
        {
            TextDocumentPositionParams params = TextDocumentPositionParams::from_json(request.params);
            bool include_declaration = request.params.as_object().at("context").as_object().at("includeDeclaration").as_boolean();

            log_debug("References requested at " + params.text_document.uri);

            std::vector<Location> references = _symbol_provider->find_references(params.text_document.uri, params.position, include_declaration);

            auto references_json = JsonValue::array();
            for (const auto &ref : references)
            {
                references_json.as_array().push_back(ref.to_json());
            }

            response.result = references_json;
        }
        catch (const std::exception &e)
        {
            log_error("Error in textDocument/references: " + std::string(e.what()));
            response.result = JsonValue::array();
        }

        return response;
    }

    LSPResponse LSPServer::handle_text_document_document_symbol(const LSPRequest &request, socket_t socket)
    {
        LSPResponse response;
        response.id = request.id;

        try
        {
            TextDocumentIdentifier doc_id = TextDocumentIdentifier::from_json(request.params["textDocument"]);

            log_debug("Document symbols requested for " + doc_id.uri);

            std::vector<SymbolInfo> symbols = _symbol_provider->get_document_symbols(doc_id.uri);

            auto symbols_json = JsonValue::array();
            for (const auto &symbol : symbols)
            {
                symbols_json.as_array().push_back(symbol.to_document_symbol().to_json());
            }

            response.result = symbols_json;
        }
        catch (const std::exception &e)
        {
            log_error("Error in textDocument/documentSymbol: " + std::string(e.what()));
            response.result = JsonValue::array();
        }

        return response;
    }

    LSPResponse LSPServer::handle_workspace_symbol(const LSPRequest &request, socket_t socket)
    {
        LSPResponse response;
        response.id = request.id;

        try
        {
            std::string query = request.params.as_object().at("query").as_string();

            log_debug("Workspace symbols requested with query: " + query);

            std::vector<SymbolInfo> symbols = _symbol_provider->get_workspace_symbols(query);

            auto symbols_json = JsonValue::array();
            for (const auto &symbol : symbols)
            {
                symbols_json.as_array().push_back(JsonValue::object({{"name", JsonValue(symbol.name)},
                                                                     {"kind", JsonValue(static_cast<int>(symbol.kind))},
                                                                     {"location", symbol.location.to_json()}}));
            }

            response.result = symbols_json;
        }
        catch (const std::exception &e)
        {
            log_error("Error in workspace/symbol: " + std::string(e.what()));
            response.result = JsonValue::array();
        }

        return response;
    }

    bool LSPServer::compile_document(const std::string &uri, const std::string &content)
    {
        std::lock_guard<std::mutex> lock(_compiler_mutex);

        try
        {
            std::string file_path = uri_to_path(uri);

            // Use the compiler's LSP compilation mode
            bool success = _diagnostics_provider->compile_and_analyze(uri, content);

            if (!success)
            {
                log_debug("Compilation failed for " + uri);
            }

            return success;
        }
        catch (const std::exception &e)
        {
            log_error("Error compiling document: " + std::string(e.what()));
            return false;
        }
    }

    void LSPServer::publish_diagnostics(const std::string &uri, socket_t socket)
    {
        try
        {
            std::vector<Diagnostic> diagnostics = _diagnostics_provider->get_diagnostics(uri);

            auto diagnostics_json = JsonValue::array();
            for (const auto &diag : diagnostics)
            {
                diagnostics_json.as_array().push_back(diag.to_json());
            }

            LSPNotification notification;
            notification.method = "textDocument/publishDiagnostics";
            notification.params = JsonValue::object({{"uri", JsonValue(uri)},
                                                     {"diagnostics", diagnostics_json}});

            std::string message = notification.to_json().dump();
            send_message(socket, message);

            log_debug("Published " + std::to_string(diagnostics.size()) + " diagnostics for " + uri);
        }
        catch (const std::exception &e)
        {
            log_error("Error publishing diagnostics: " + std::string(e.what()));
        }
    }

} // namespace CryoLSP
