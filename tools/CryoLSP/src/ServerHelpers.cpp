#include "Server.hpp"
#include "JsonUtils.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "GDM/GDM.hpp"
#include "AST/SymbolTable.hpp"
#include "Utils/Logger.hpp"

#include <filesystem>
#include <fstream>

namespace CryoLSP
{

    // Helper methods that integrate with the compiler's LSP APIs

    std::vector<Diagnostic> Server::get_diagnostics_for_document(const std::string &uri)
    {
        std::vector<Diagnostic> diagnostics;

        if (!compiler)
            return diagnostics;

        try
        {
            std::string file_path = uri_to_path(uri);

            // Check if we have cached content
            auto cache_it = document_cache.find(uri);
            if (cache_it != document_cache.end())
            {
                // Write cached content to a temporary file for compilation
                std::string temp_path = file_path + ".tmp";
                std::ofstream temp_file(temp_path);
                temp_file << cache_it->second;
                temp_file.close();

                // Compile with LSP mode
                compiler->compile_for_lsp(temp_path);

                // Get diagnostics from GDM
                auto &gdm = *compiler->diagnostic_manager();
                if (gdm.has_lsp_diagnostics())
                {
                    auto lsp_diagnostics = gdm.get_lsp_diagnostics();

                    for (const auto &lsp_diag : lsp_diagnostics)
                    {
                        Diagnostic diag;
                        diag.range.start.line = lsp_diag.line;
                        diag.range.start.character = lsp_diag.column;
                        diag.range.end.line = lsp_diag.end_line;
                        diag.range.end.character = lsp_diag.end_column;
                        diag.message = lsp_diag.message;
                        // Convert severity string to LSP severity int (1=Error, 2=Warning, 3=Info, 4=Hint)
                        if (lsp_diag.severity == "error")
                            diag.severity = 1;
                        else if (lsp_diag.severity == "warning")
                            diag.severity = 2;
                        else if (lsp_diag.severity == "info")
                            diag.severity = 3;
                        else
                            diag.severity = 4; // hint
                        diag.code = lsp_diag.code;
                        diag.source = "cryo";

                        diagnostics.push_back(diag);
                    }

                    gdm.clear_lsp_diagnostics();
                }

                // Clean up temporary file
                std::filesystem::remove(temp_path);
            }
        }
        catch (const std::exception &e)
        {
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP, "Error getting diagnostics: {}", e.what());
        }

        return diagnostics;
    }

    std::vector<CompletionItem> Server::get_completions(const std::string &uri, const Position &position)
    {
        std::vector<CompletionItem> completions;

        if (!compiler)
            return completions;

        try
        {
            std::string file_path = uri_to_path(uri);

            // Compile the document to get symbol information
            auto cache_it = document_cache.find(uri);
            if (cache_it != document_cache.end())
            {
                std::string temp_path = file_path + ".tmp";
                std::ofstream temp_file(temp_path);
                temp_file << cache_it->second;
                temp_file.close();

                compiler->compile_for_lsp(temp_path);

                // Get completions from symbol table
                auto &symbol_table = *compiler->symbol_table();
                // For now, get completions from global scope - ideally we'd determine scope from position
                auto lsp_completions = symbol_table.get_completions_for_scope("global");

                for (const auto &lsp_comp : lsp_completions)
                {
                    CompletionItem item;
                    item.label = lsp_comp.name;
                    // Convert kind string to LSP CompletionItemKind int
                    if (lsp_comp.kind == "function")
                        item.kind = 3;
                    else if (lsp_comp.kind == "variable")
                        item.kind = 6;
                    else if (lsp_comp.kind == "class" || lsp_comp.kind == "struct")
                        item.kind = 7;
                    else if (lsp_comp.kind == "enum")
                        item.kind = 13;
                    else
                        item.kind = 1; // Text
                    item.detail = lsp_comp.type_name;
                    item.documentation = lsp_comp.documentation;
                    item.insertText = lsp_comp.name;

                    completions.push_back(item);
                }

                std::filesystem::remove(temp_path);
            }
        }
        catch (const std::exception &e)
        {
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP, "Error getting completions: {}", e.what());
        }

        return completions;
    }

    Hover Server::get_hover_info(const std::string &uri, const Position &position)
    {
        Hover hover;

        if (!compiler)
            return hover;

        try
        {
            std::string file_path = uri_to_path(uri);

            auto cache_it = document_cache.find(uri);
            if (cache_it != document_cache.end())
            {
                std::string temp_path = file_path + ".tmp";
                std::ofstream temp_file(temp_path);
                temp_file << cache_it->second;
                temp_file.close();

                compiler->compile_for_lsp(temp_path);

                // Get symbol information at position
                auto &symbol_table = *compiler->symbol_table();
                auto symbol_opt = symbol_table.find_symbol_at_position(temp_path, position.line, position.character);

                if (symbol_opt.has_value())
                {
                    const auto &symbol = symbol_opt.value();
                    hover.contents = symbol.name + ": " + symbol.type_name;
                    if (!symbol.documentation.empty())
                    {
                        hover.contents += "\n\n" + symbol.documentation;
                    }

                    hover.range.start = position;
                    hover.range.end.line = position.line;
                    hover.range.end.character = position.character + static_cast<int>(symbol.name.length());
                }

                std::filesystem::remove(temp_path);
            }
        }
        catch (const std::exception &e)
        {
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP, "Error getting hover info: {}", e.what());
        }

        return hover;
    }

    std::vector<Location> Server::get_definition(const std::string &uri, const Position &position)
    {
        std::vector<Location> definitions;

        if (!compiler)
            return definitions;

        try
        {
            std::string file_path = uri_to_path(uri);

            auto cache_it = document_cache.find(uri);
            if (cache_it != document_cache.end())
            {
                std::string temp_path = file_path + ".tmp";
                std::ofstream temp_file(temp_path);
                temp_file << cache_it->second;
                temp_file.close();

                compiler->compile_for_lsp(temp_path);

                // Get symbol definition
                auto &symbol_table = *compiler->symbol_table();
                auto symbol_opt = symbol_table.find_symbol_at_position(temp_path, position.line, position.character);

                if (symbol_opt.has_value())
                {
                    const auto &symbol = symbol_opt.value();
                    if (!symbol.definition_file.empty())
                    {
                        Location location;
                        location.uri = path_to_uri(symbol.definition_file);
                        location.range.start.line = symbol.definition_line;
                        location.range.start.character = symbol.definition_column;
                        location.range.end.line = symbol.definition_line;
                        location.range.end.character = symbol.definition_column + static_cast<int>(symbol.name.length());

                        definitions.push_back(location);
                    }
                }

                std::filesystem::remove(temp_path);
            }
        }
        catch (const std::exception &e)
        {
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP, "Error getting definition: {}", e.what());
        }

        return definitions;
    }

    std::vector<SymbolInformation> Server::get_workspace_symbols(const std::string &query)
    {
        std::vector<SymbolInformation> symbols;

        if (!compiler)
            return symbols;

        try
        {
            // For workspace symbols, we need to compile all known files
            // For now, let's get symbols from the current symbol table
            auto &symbol_table = *compiler->symbol_table();
            auto lsp_symbols = symbol_table.get_all_symbols_for_lsp();

            for (const auto &lsp_symbol : lsp_symbols)
            {
                // Filter by query if provided
                if (!query.empty() && lsp_symbol.name.find(query) == std::string::npos)
                {
                    continue;
                }

                SymbolInformation symbol;
                symbol.name = lsp_symbol.name;
                // Convert kind string to LSP SymbolKind int
                if (lsp_symbol.kind == "function")
                    symbol.kind = 12;
                else if (lsp_symbol.kind == "variable")
                    symbol.kind = 13;
                else if (lsp_symbol.kind == "class" || lsp_symbol.kind == "struct")
                    symbol.kind = 5;
                else if (lsp_symbol.kind == "enum")
                    symbol.kind = 10;
                else
                    symbol.kind = 1; // File
                symbol.location.uri = path_to_uri(lsp_symbol.definition_file);
                symbol.location.range.start.line = lsp_symbol.definition_line;
                symbol.location.range.start.character = lsp_symbol.definition_column;
                symbol.location.range.end.line = lsp_symbol.definition_line;
                symbol.location.range.end.character = lsp_symbol.definition_column + static_cast<int>(lsp_symbol.name.length());
                symbol.containerName = lsp_symbol.scope;

                symbols.push_back(symbol);
            }
        }
        catch (const std::exception &e)
        {
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP, "Error getting workspace symbols: {}", e.what());
        }

        return symbols;
    }

    std::vector<Location> Server::get_references(const std::string &uri, const Position &position)
    {
        std::vector<Location> references;

        if (!compiler)
            return references;

        try
        {
            std::string file_path = uri_to_path(uri);

            auto cache_it = document_cache.find(uri);
            if (cache_it != document_cache.end())
            {
                std::string temp_path = file_path + ".tmp";
                std::ofstream temp_file(temp_path);
                temp_file << cache_it->second;
                temp_file.close();

                compiler->compile_for_lsp(temp_path);

                // Get symbol at position
                auto &symbol_table = *compiler->symbol_table();
                auto symbol_opt = symbol_table.find_symbol_at_position(temp_path, position.line, position.character);

                if (symbol_opt.has_value())
                {
                    const auto &symbol = symbol_opt.value();
                    // Find all references to this symbol
                    auto symbol_references = symbol_table.find_symbols_by_name(symbol.name);

                    for (const auto &ref : symbol_references)
                    {
                        Location location;
                        location.uri = path_to_uri(ref.definition_file);
                        location.range.start.line = ref.definition_line;
                        location.range.start.character = ref.definition_column;
                        location.range.end.line = ref.definition_line;
                        location.range.end.character = ref.definition_column + static_cast<int>(ref.name.length());

                        references.push_back(location);
                    }
                }

                std::filesystem::remove(temp_path);
            }
        }
        catch (const std::exception &e)
        {
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP, "Error getting references: {}", e.what());
        }

        return references;
    }

    // Utility methods
    std::string Server::uri_to_path(const std::string &uri)
    {
        // Convert file:// URI to local path
        if (uri.substr(0, 7) == "file://")
        {
            std::string path = uri.substr(7);

            // On Windows, handle drive letters
            if (path.length() > 2 && path[0] == '/' && path[2] == ':')
            {
                path = path.substr(1); // Remove leading slash
            }

            // Convert forward slashes to backslashes on Windows
            std::replace(path.begin(), path.end(), '/', '\\');

            return path;
        }

        return uri;
    }

    std::string Server::path_to_uri(const std::string &path)
    {
        // Convert local path to file:// URI
        std::string uri = path;

        // Convert backslashes to forward slashes
        std::replace(uri.begin(), uri.end(), '\\', '/');

        // Add file:// prefix
        if (uri.substr(0, 7) != "file://")
        {
            if (uri[1] == ':')
            {
                // Windows drive letter
                uri = "file:///" + uri;
            }
            else
            {
                uri = "file://" + uri;
            }
        }

        return uri;
    }

    Position Server::offset_to_position(const std::string &text, int offset)
    {
        Position position = {0, 0};

        for (int i = 0; i < offset && i < static_cast<int>(text.length()); ++i)
        {
            if (text[i] == '\n')
            {
                position.line++;
                position.character = 0;
            }
            else
            {
                position.character++;
            }
        }

        return position;
    }

    int Server::position_to_offset(const std::string &text, const Position &position)
    {
        int offset = 0;
        int current_line = 0;
        int current_character = 0;

        for (size_t i = 0; i < text.length(); ++i)
        {
            if (current_line == position.line && current_character == position.character)
            {
                return offset;
            }

            if (text[i] == '\n')
            {
                current_line++;
                current_character = 0;
            }
            else
            {
                current_character++;
            }

            offset++;
        }

        return offset;
    }

} // namespace CryoLSP