#include "LSPAnalyzer.hpp"
#include "Utils/Logger.hpp"
#include <regex>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <cstdlib>

namespace CryoLSP
{
    // Comprehensive AST visitor class for precise position-based queries
    class ASTPositionVisitor
    {
    public:
        Cryo::ASTNode *find_node_at_position(Cryo::ASTNode *root, const Cryo::SourceLocation &target)
        {
            if (!root)
                return nullptr;

            target_position = target;
            best_node = nullptr;
            best_specificity = 0;

            visit_node(root);
            return best_node;
        }

    private:
        Cryo::SourceLocation target_position;
        Cryo::ASTNode *best_node = nullptr;
        int best_specificity = 0;

        bool position_could_contain(const Cryo::SourceLocation &node_pos, const Cryo::SourceLocation &target) const
        {
            // For now, use simple containment logic
            // Could be enhanced with end position tracking if available
            return node_pos.line() <= target.line() &&
                   (node_pos.line() < target.line() || node_pos.column() <= target.column());
        }

        void visit_node(Cryo::ASTNode *node)
        {
            if (!node)
                return;

            const auto &node_loc = node->location();

            // Debug: Log all nodes we visit
            std::string debug_info = "Visiting node at " +
                                     std::to_string(node_loc.line()) + ":" + std::to_string(node_loc.column()) +
                                     " (target: " + std::to_string(target_position.line()) + ":" + std::to_string(target_position.column()) + ")";

            if (node->kind() == Cryo::NodeKind::Identifier)
            {
                auto *id = static_cast<Cryo::IdentifierNode *>(node);
                debug_info += " - identifier '" + id->name() + "'";
            }
            else if (node->kind() == Cryo::NodeKind::Literal)
            {
                auto *lit = static_cast<Cryo::LiteralNode *>(node);
                debug_info += " - literal '" + lit->value() + "'";
            }
            else
            {
                debug_info += " - " + std::to_string(static_cast<int>(node->kind()));
            }

            Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, debug_info);

            if (!position_could_contain(node_loc, target_position))
            {
                return;
            }

            // Calculate specificity for this node
            int specificity = calculate_specificity(node);
            if (specificity > best_specificity)
            {
                best_node = node;
                best_specificity = specificity;

                // Debug: Log what node we found
                std::string node_name = "unknown";
                if (node->kind() == Cryo::NodeKind::Identifier)
                {
                    auto *id = static_cast<Cryo::IdentifierNode *>(node);
                    node_name = "identifier '" + id->name() + "'";
                }
                else if (node->kind() == Cryo::NodeKind::Literal)
                {
                    auto *lit = static_cast<Cryo::LiteralNode *>(node);
                    node_name = "literal '" + lit->value() + "'";
                }

                Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                               "New best match: " + node_name + " at " +
                                                   std::to_string(node_loc.line()) + ":" + std::to_string(node_loc.column()) +
                                                   " (specificity=" + std::to_string(specificity) + ")");
            }

            // Visit children based on node type
            visit_children(node);
        }

        int calculate_specificity(Cryo::ASTNode *node) const
        {
            switch (node->kind())
            {
            case Cryo::NodeKind::Literal:
            case Cryo::NodeKind::Identifier:
                return 100; // Most specific

            case Cryo::NodeKind::BinaryExpression:
            case Cryo::NodeKind::UnaryExpression:
            case Cryo::NodeKind::CallExpression:
                return 80; // Expression level

            case Cryo::NodeKind::VariableDeclaration:
            case Cryo::NodeKind::FunctionDeclaration:
                return 60; // Declaration level

            case Cryo::NodeKind::ExpressionStatement:
            case Cryo::NodeKind::ReturnStatement:
            case Cryo::NodeKind::IfStatement:
                return 40; // Statement level

            case Cryo::NodeKind::BlockStatement:
                return 20; // Block level

            case Cryo::NodeKind::Program:
                return 1; // Least specific

            default:
                return 30; // Default for unknown types
            }
        }

        void visit_children(Cryo::ASTNode *node)
        {
            if (!node)
                return;

            switch (node->kind())
            {
            case Cryo::NodeKind::Program:
            {
                auto *prog = static_cast<Cryo::ProgramNode *>(node);
                for (const auto &stmt : prog->statements())
                {
                    visit_node(stmt.get());
                }
                break;
            }

            case Cryo::NodeKind::BlockStatement:
            {
                auto *block = static_cast<Cryo::BlockStatementNode *>(node);
                for (const auto &stmt : block->statements())
                {
                    visit_node(stmt.get());
                }
                break;
            }

            case Cryo::NodeKind::FunctionDeclaration:
            {
                auto *func = static_cast<Cryo::FunctionDeclarationNode *>(node);
                if (func->body())
                {
                    visit_node(func->body());
                }
                break;
            }

            case Cryo::NodeKind::VariableDeclaration:
            {
                auto *var = static_cast<Cryo::VariableDeclarationNode *>(node);
                if (var->initializer())
                {
                    visit_node(var->initializer());
                }
                break;
            }

            case Cryo::NodeKind::BinaryExpression:
            {
                auto *binary = static_cast<Cryo::BinaryExpressionNode *>(node);
                visit_node(binary->left());
                visit_node(binary->right());
                break;
            }

            case Cryo::NodeKind::UnaryExpression:
            {
                auto *unary = static_cast<Cryo::UnaryExpressionNode *>(node);
                visit_node(unary->operand());
                break;
            }

            case Cryo::NodeKind::CallExpression:
            {
                auto *call = static_cast<Cryo::CallExpressionNode *>(node);
                visit_node(call->callee());
                for (const auto &arg : call->arguments())
                {
                    visit_node(arg.get());
                }
                break;
            }

            case Cryo::NodeKind::ExpressionStatement:
            {
                auto *expr_stmt = static_cast<Cryo::ExpressionStatementNode *>(node);
                if (expr_stmt->expression())
                {
                    visit_node(expr_stmt->expression());
                }
                break;
            }

            case Cryo::NodeKind::ReturnStatement:
            {
                auto *ret = static_cast<Cryo::ReturnStatementNode *>(node);
                if (ret->expression())
                {
                    visit_node(ret->expression());
                }
                break;
            }

            case Cryo::NodeKind::IfStatement:
            {
                auto *if_stmt = static_cast<Cryo::IfStatementNode *>(node);
                if (if_stmt->condition())
                {
                    visit_node(if_stmt->condition());
                }
                if (if_stmt->then_statement())
                {
                    visit_node(if_stmt->then_statement());
                }
                if (if_stmt->else_statement())
                {
                    visit_node(if_stmt->else_statement());
                }
                break;
            }

            // Leaf nodes - no children to visit
            case Cryo::NodeKind::Literal:
            case Cryo::NodeKind::Identifier:
            default:
                break;
            }
        }
    };

    LSPAnalyzer::LSPAnalyzer()
    {
        try
        {
            // Initialize compiler with error handling
            compiler_ = std::make_unique<Cryo::CompilerInstance>();

            // Configure compiler for LSP use - enable stdlib linking and configure stdlib location
            compiler_->set_stdlib_linking(true);

            // Check for CRYO_SRC environment variable first
            const char *cryo_src = std::getenv("CRYO_SRC");
            bool stdlib_configured = false;

            if (cryo_src)
            {
                std::string stdlib_path = std::string(cryo_src) + "/stdlib";
                Cryo::Logger::instance().info(Cryo::LogComponent::LSP,
                                              "Using CRYO_SRC environment variable: " + std::string(cryo_src));
                Cryo::Logger::instance().info(Cryo::LogComponent::LSP,
                                              "Setting stdlib path to: " + stdlib_path);

                // Set the stdlib root directory manually
                compiler_->module_loader()->set_stdlib_root(stdlib_path);
                stdlib_configured = true;
                Cryo::Logger::instance().info(Cryo::LogComponent::LSP,
                                              "Successfully configured stdlib location using CRYO_SRC");
            }

            // Fall back to auto-detection if CRYO_SRC not available or failed
            if (!stdlib_configured)
            {
                if (!compiler_->module_loader()->auto_detect_stdlib_root())
                {
                    Cryo::Logger::instance().warn(Cryo::LogComponent::LSP,
                                                  "Could not auto-detect stdlib location - some imports may fail");
                }
                else
                {
                    Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                                   "Successfully configured stdlib location via auto-detection");
                }
            }

            Cryo::Logger::instance().info(Cryo::LogComponent::LSP, "LSPAnalyzer initialized with compiler integration enabled");
        }
        catch (const std::exception &e)
        {
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP,
                                           "Failed to initialize compiler: " + std::string(e.what()) + " - running in safe mode");
            compiler_ = nullptr;
        }
        catch (...)
        {
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP,
                                           "Unknown error initializing compiler - running in safe mode");
            compiler_ = nullptr;
        }
    }

    LSPAnalyzer::~LSPAnalyzer() = default;

    void LSPAnalyzer::open_document(const std::string &uri, const std::string &content)
    {
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Opening document: " + uri);
        document_cache_[uri] = content;

        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Processing document content (" + std::to_string(content.length()) + " chars)");

        // Initialize with empty diagnostics
        diagnostics_cache_[uri] = std::vector<DiagnosticInfo>();

        // Safely attempt document parsing
        try
        {
            if (compiler_)
            {
                auto ast = parse_document_safe(uri, content);
                if (ast)
                {
                    ast_cache_[uri] = std::move(ast);
                    Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "AST parsed successfully for: " + uri);
                }
                else
                {
                    Cryo::Logger::instance().info(Cryo::LogComponent::LSP, "AST parsing failed gracefully for: " + uri);
                }
            }
            else
            {
                Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Skipping compilation - compiler not available");
            }
        }
        catch (...)
        {
            // Ensure any unexpected exception doesn't crash the server
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP, "Unexpected error during document processing: " + uri);
            add_diagnostic_error(uri, "Internal error processing document");
        }

        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Document processing completed safely: " + uri);
    }

    void LSPAnalyzer::update_document(const std::string &uri, const std::string &content)
    {
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Updating document: " + uri);
        open_document(uri, content); // Reuse open logic for updates
    }

    void LSPAnalyzer::close_document(const std::string &uri)
    {
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Closing document: " + uri);
        document_cache_.erase(uri);
        ast_cache_.erase(uri);
        diagnostics_cache_.erase(uri);
    }

    std::optional<SymbolInfo> LSPAnalyzer::get_hover_info(const std::string &uri, const Position &position)
    {
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                       "Getting hover info for " + uri + " at " + position_to_string(position));

        // Check if we have AST for this document
        auto ast_it = ast_cache_.find(uri);
        if (ast_it == ast_cache_.end())
        {
            Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "No AST found in cache for URI: " + uri);
            return std::nullopt;
        }

        // Find the AST node at the specified position
        Cryo::ASTNode *node = find_node_at_position(ast_it->second.get(), position);
        if (!node)
        {
            Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "No AST node found at position");
            return std::nullopt;
        }

        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                       "Found AST node at position");

        // Extract symbol information from the AST node
        SymbolInfo symbol = extract_symbol_info(node, uri);

        if (symbol.name.empty())
        {
            Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "No symbol information extracted from node");
            return std::nullopt;
        }

        return symbol;
    }

    std::vector<CompletionItem> LSPAnalyzer::get_completions(const std::string &uri, const Position &position)
    {
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                       "Getting completions for " + uri + " at " + position_to_string(position));

        std::vector<CompletionItem> completions;

        // Add scope-based completions (variables, functions in current scope)
        auto scope_completions = get_scope_completions(uri, position);
        completions.insert(completions.end(), scope_completions.begin(), scope_completions.end());

        // Add builtin completions (keywords, primitives, standard functions)
        auto builtin_completions = get_builtin_completions();
        completions.insert(completions.end(), builtin_completions.begin(), builtin_completions.end());

        // Sort by label alphabetically
        std::sort(completions.begin(), completions.end(),
                  [](const CompletionItem &a, const CompletionItem &b)
                  {
                      return a.label < b.label;
                  });

        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                       "Returning " + std::to_string(completions.size()) + " completion items");

        return completions;
    }

    std::optional<Location> LSPAnalyzer::get_definition(const std::string &uri, const Position &position)
    {
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                       "Getting definition for " + uri + " at " + position_to_string(position));

        auto ast_it = ast_cache_.find(uri);
        if (ast_it == ast_cache_.end())
        {
            return std::nullopt;
        }

        Cryo::ASTNode *node = find_node_at_position(ast_it->second.get(), position);
        if (!node)
        {
            return std::nullopt;
        }

        // For now, return the current position - we'll implement proper symbol resolution next
        Location def_location;
        def_location.uri = uri;
        def_location.range.start = position;
        def_location.range.end = position;

        return def_location;
    }

    std::vector<Location> LSPAnalyzer::get_references(const std::string &uri, const Position &position)
    {
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                       "Getting references for " + uri + " at " + position_to_string(position));

        // Placeholder implementation
        std::vector<Location> references;
        return references;
    }

    std::vector<DocumentSymbol> LSPAnalyzer::get_document_symbols(const std::string &uri)
    {
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Getting document symbols for " + uri);

        std::vector<DocumentSymbol> symbols;

        auto ast_it = ast_cache_.find(uri);
        if (ast_it == ast_cache_.end())
        {
            return symbols;
        }

        // Extract symbols from AST - we'll implement AST traversal next
        return symbols;
    }

    std::vector<DiagnosticInfo> LSPAnalyzer::get_diagnostics(const std::string &uri)
    {
        auto it = diagnostics_cache_.find(uri);
        if (it != diagnostics_cache_.end())
        {
            return it->second;
        }
        return {};
    }

    std::string LSPAnalyzer::position_to_string(const Position &pos) const
    {
        return std::to_string(pos.line) + ":" + std::to_string(pos.character);
    }

    Position LSPAnalyzer::source_location_to_position(const Cryo::SourceLocation &loc) const
    {
        Position pos;
        pos.line = loc.line() - 1;        // Convert from 1-based to 0-based
        pos.character = loc.column() - 1; // Convert from 1-based to 0-based
        return pos;
    }

    Range LSPAnalyzer::source_range_to_range(const Cryo::SourceLocation &start, const Cryo::SourceLocation &end) const
    {
        Range range;
        range.start = source_location_to_position(start);
        range.end = source_location_to_position(end);
        return range;
    }

    std::shared_ptr<Cryo::ASTNode> LSPAnalyzer::parse_document_safe(const std::string &uri, const std::string &content)
    {
        if (!compiler_)
        {
            Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "No compiler available for parsing: " + uri);
            return nullptr;
        }

        try
        {
            std::string file_path = uri_to_file_path(uri);
            Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Parsing file path: " + file_path);

            // Create a temporary file for parsing
            std::string temp_file = file_path + ".tmp";
            {
                std::ofstream temp_stream(temp_file);
                if (!temp_stream.is_open())
                {
                    Cryo::Logger::instance().error(Cryo::LogComponent::LSP, "Failed to create temp file: " + temp_file);
                    add_diagnostic_error(uri, "Could not create temporary file for parsing");
                    return nullptr;
                }
                temp_stream << content;
                Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                               "Wrote " + std::to_string(content.length()) + " chars to temp file: " + temp_file);
            } // Close file before parsing

            std::shared_ptr<Cryo::ASTNode> ast = nullptr;

            try
            {
                Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Attempting real compilation of: " + temp_file);

                // Use actual compiler to parse the file
                compiler_->compile_file(temp_file);

                // Check if compilation succeeded by examining the AST context
                if (compiler_->ast_context())
                {
                    auto ast_context = compiler_->ast_context();
                    Cryo::Logger::instance().info(Cryo::LogComponent::LSP,
                                                  "Compilation completed. AST context nodes: " + std::to_string(ast_context->node_count()));

                    // Get the program AST root from the compiler
                    auto program_node = compiler_->ast_root();
                    if (program_node)
                    {
                        size_t stmt_count = program_node->statements().size();
                        Cryo::Logger::instance().info(Cryo::LogComponent::LSP,
                                                      "Found AST root with " + std::to_string(stmt_count) + " statements");

                        if (stmt_count > 0)
                        {
                            // Store the actual AST root for symbol resolution using a custom deleter that does nothing
                            ast = std::shared_ptr<Cryo::ASTNode>(program_node, [](Cryo::ASTNode *) {});
                            Cryo::Logger::instance().info(Cryo::LogComponent::LSP,
                                                          "AST cached successfully with " + std::to_string(stmt_count) + " statements: " + uri);
                        }
                        else
                        {
                            Cryo::Logger::instance().warn(Cryo::LogComponent::LSP,
                                                          "AST root found but contains 0 statements - parsing may have failed");
                            add_diagnostic_error(uri, "No statements parsed from document");
                        }
                    }
                    else
                    {
                        Cryo::Logger::instance().warn(Cryo::LogComponent::LSP,
                                                      "Compilation completed but ast_root() returned null");
                        add_diagnostic_error(uri, "No AST root generated");
                    }
                }
                else
                {
                    Cryo::Logger::instance().warn(Cryo::LogComponent::LSP, "No AST context available after compilation");
                    add_diagnostic_error(uri, "Compilation failed - no AST context");
                }
            }
            catch (const std::exception &e)
            {
                std::string error_msg = std::string(e.what());
                Cryo::Logger::instance().info(Cryo::LogComponent::LSP, "Compilation exception: " + error_msg);

                // Parse error message to create appropriate diagnostic
                if (error_msg.find("parsing") != std::string::npos || error_msg.find("syntax") != std::string::npos)
                {
                    add_diagnostic_error(uri, "Syntax error: " + error_msg);
                }
                else if (error_msg.find("lexing") != std::string::npos || error_msg.find("token") != std::string::npos)
                {
                    add_diagnostic_error(uri, "Lexical error: " + error_msg);
                }
                else if (error_msg.find("type") != std::string::npos)
                {
                    add_diagnostic_error(uri, "Type error: " + error_msg);
                }
                else
                {
                    add_diagnostic_error(uri, "Compilation error: " + error_msg);
                }
            }

            // Clean up temp file
            std::error_code ec;
            std::filesystem::remove(temp_file, ec);
            if (ec)
            {
                Cryo::Logger::instance().warn(Cryo::LogComponent::LSP, "Failed to remove temp file: " + ec.message());
            }

            return ast;
        }
        catch (const std::exception &e)
        {
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP,
                                           "Exception in parse_document_safe: " + std::string(e.what()));
            add_diagnostic_error(uri, "Internal parsing error");
            return nullptr;
        }
        catch (...)
        {
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP, "Unknown exception in parse_document_safe: " + uri);
            add_diagnostic_error(uri, "Unknown internal error");
            return nullptr;
        }
    }

    Cryo::ASTNode *LSPAnalyzer::find_node_at_position(Cryo::ASTNode *root, const Position &position)
    {
        if (!root)
            return nullptr;

        // Convert LSP position (0-based) to Cryo SourceLocation (1-based)
        Cryo::SourceLocation target_loc(position.line + 1, position.character + 1);

        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                       "Finding node at position " + std::to_string(position.line) + ":" + std::to_string(position.character) +
                                           " (target_loc: " + std::to_string(target_loc.line()) + ":" + std::to_string(target_loc.column()) + ")");

        // Use comprehensive AST visitor for precise position-based queries
        ASTPositionVisitor visitor;
        return visitor.find_node_at_position(root, target_loc);
    }

    SymbolInfo LSPAnalyzer::extract_symbol_info(Cryo::ASTNode *node, const std::string &uri)
    {
        SymbolInfo info;

        if (!node)
            return info;

        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                       "Extracting symbol info from AST node");

        // Extract specific information based on node type
        switch (node->kind())
        {
        case Cryo::NodeKind::Identifier:
        {
            auto *identifier = static_cast<Cryo::IdentifierNode *>(node);
            info.name = identifier->name();
            info.kind = "identifier";
            info.type_name = "variable";
            info.documentation = "**" + info.name + "** - Identifier";

            // Try to get type information from compiler
            if (compiler_)
            {
                try
                {
                    auto type_checker = compiler_->type_checker();
                    if (type_checker)
                    {
                        // Look up the symbol in the type checker's symbol table
                        auto symbol = type_checker->lookup_symbol(identifier->name());
                        if (symbol)
                        {
                            info.type_name = get_type_string(symbol->type);
                            info.documentation = "**" + info.name + "** : `" + info.type_name + "`";
                        }
                    }
                }
                catch (const std::exception &e)
                {
                    Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                                   "Error looking up symbol type: " + std::string(e.what()));
                }
            }
            break;
        }

        case Cryo::NodeKind::FunctionDeclaration:
        {
            auto *func = static_cast<Cryo::FunctionDeclarationNode *>(node);
            info.name = func->name();
            info.kind = "function";
            info.type_name = "function";

            // Build function signature
            std::string params = "(";
            for (size_t i = 0; i < func->parameters().size(); ++i)
            {
                if (i > 0)
                    params += ", ";
                auto param = func->parameters()[i].get();
                params += param->name() + ": " + (param->get_resolved_type() ? get_type_string(param->get_resolved_type()) : "unknown");
            }
            params += ")";

            std::string return_type = func->get_resolved_return_type() ? get_type_string(func->get_resolved_return_type()) : "void";
            info.signature = "function " + info.name + params + " -> " + return_type;
            info.documentation = "**Function** `" + info.signature + "`";
            break;
        }

        case Cryo::NodeKind::VariableDeclaration:
        {
            auto *var = static_cast<Cryo::VariableDeclarationNode *>(node);
            info.name = var->name();
            info.kind = "variable";
            info.type_name = var->get_resolved_type() ? get_type_string(var->get_resolved_type()) : "inferred";
            info.signature = (var->is_mutable() ? "mut " : "") + info.name + ": " + info.type_name;
            info.documentation = "**Variable** `" + info.signature + "`";
            break;
        }

        case Cryo::NodeKind::StructDeclaration:
        {
            auto *struct_node = static_cast<Cryo::StructDeclarationNode *>(node);
            info.name = struct_node->name();
            info.kind = "struct";
            info.type_name = "struct";
            info.signature = "struct " + info.name;
            info.documentation = "**Struct** `" + info.signature + "`";
            break;
        }

        case Cryo::NodeKind::ClassDeclaration:
        {
            auto *class_node = static_cast<Cryo::ClassDeclarationNode *>(node);
            info.name = class_node->name();
            info.kind = "class";
            info.type_name = "class";
            info.signature = "class " + info.name;
            info.documentation = "**Class** `" + info.signature + "`";
            break;
        }

        case Cryo::NodeKind::Literal:
        {
            auto *literal = static_cast<Cryo::LiteralNode *>(node);
            info.name = literal->value();
            info.kind = "literal";
            info.type_name = get_literal_type_string(literal->literal_kind());
            info.signature = info.name + ": " + info.type_name;
            info.documentation = "**Literal** `" + info.signature + "`";
            break;
        }

        default:
        {
            // Generic fallback
            info.name = "symbol";
            info.kind = "symbol";
            info.type_name = "unknown";
            info.signature = "";
            info.documentation = "**Symbol** - " + info.kind + " at this position";
            break;
        }
        }

        info.scope = "local";

        // Set definition location
        info.definition_location.uri = uri;
        info.definition_location.range.start = source_location_to_position(node->location());
        info.definition_location.range.end = source_location_to_position(node->location());

        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                       "Extracted symbol: name='" + info.name + "', kind=" + info.kind + ", type=" + info.type_name);

        return info;
    }

    std::string LSPAnalyzer::get_type_string(Cryo::Type *type) const
    {
        if (!type)
            return "unknown";
        return type->to_string();
    }

    std::string LSPAnalyzer::get_literal_type_string(Cryo::TokenKind literal_kind) const
    {
        switch (literal_kind)
        {
        case Cryo::TokenKind::TK_NUMERIC_CONSTANT:
            return "number";
        case Cryo::TokenKind::TK_STRING_LITERAL:
            return "string";
        case Cryo::TokenKind::TK_CHAR_CONSTANT:
            return "char";
        case Cryo::TokenKind::TK_BOOLEAN_LITERAL:
            return "boolean";
        default:
            return "literal";
        }
    }

    std::vector<CompletionItem> LSPAnalyzer::get_scope_completions(const std::string &uri, const Position &position)
    {
        std::vector<CompletionItem> completions;

        // Get symbol table from compiler and extract symbols
        // This is a placeholder - we'll implement proper scope analysis

        return completions;
    }

    std::vector<CompletionItem> LSPAnalyzer::get_builtin_completions() const
    {
        std::vector<CompletionItem> completions;

        // Add Cryo language keywords
        std::vector<std::string> keywords = {
            "function", "public", "private", "return", "if", "else", "while", "for",
            "struct", "class", "namespace", "import", "export", "const", "let", "var",
            "true", "false", "null", "void", "int", "float", "string", "bool", "array"};

        for (const auto &keyword : keywords)
        {
            CompletionItem item;
            item.label = keyword;
            item.kind = 14; // LSP CompletionItemKind::Keyword
            item.detail = "Cryo keyword";
            item.documentation = "Built-in language keyword";
            item.insertText = keyword;
            completions.push_back(item);
        }

        // Add built-in functions
        std::vector<std::pair<std::string, std::string>> builtins = {
            {"println", "println(message: string) -> void"},
            {"print", "print(message: string) -> void"},
            {"len", "len(array: T[]) -> int"},
            {"sizeof", "sizeof(type: T) -> int"}};

        for (const auto &builtin : builtins)
        {
            CompletionItem item;
            item.label = builtin.first;
            item.kind = 3; // LSP CompletionItemKind::Function
            item.detail = builtin.second;
            item.documentation = "Built-in function";
            item.insertText = builtin.first + "()";
            completions.push_back(item);
        }

        return completions;
    }

    std::vector<DiagnosticInfo> LSPAnalyzer::extract_diagnostics_from_compiler(const std::string &uri)
    {
        std::vector<DiagnosticInfo> diagnostics;

        // Extract diagnostics from the compiler's diagnostic system
        // This is a placeholder - we'll integrate with your GDM system

        return diagnostics;
    }

    std::string LSPAnalyzer::uri_to_file_path(const std::string &uri) const
    {
        // Convert file:///c%3A/path/to/file.cryo to c:\path\to\file.cryo
        std::string path = uri;

        if (path.starts_with("file:///"))
        {
            path = path.substr(8); // Remove "file:///"
        }

        // URL decode - simple implementation
        size_t pos = 0;
        while ((pos = path.find('%', pos)) != std::string::npos)
        {
            if (pos + 2 < path.length())
            {
                std::string hex = path.substr(pos + 1, 2);
                int value = std::stoi(hex, nullptr, 16);
                path.replace(pos, 3, 1, static_cast<char>(value));
            }
            pos++;
        }

// Convert forward slashes to backslashes on Windows
#ifdef _WIN32
        std::replace(path.begin(), path.end(), '/', '\\');
#endif

        return path;
    }

    std::string LSPAnalyzer::file_path_to_uri(const std::string &file_path) const
    {
        std::string uri = "file:///";

        std::string path = file_path;
#ifdef _WIN32
        // Convert backslashes to forward slashes
        std::replace(path.begin(), path.end(), '\\', '/');
#endif

        // URL encode special characters
        for (char c : path)
        {
            if (c == ':' || c == ' ')
            {
                uri += "%" + std::to_string(static_cast<int>(c));
            }
            else
            {
                uri += c;
            }
        }

        return uri;
    }

    void LSPAnalyzer::add_diagnostic_error(const std::string &uri, const std::string &message)
    {
        DiagnosticInfo diagnostic;
        diagnostic.message = message;
        diagnostic.severity = 1; // Error severity
        diagnostic.range.start.line = 0;
        diagnostic.range.start.character = 0;
        diagnostic.range.end.line = 0;
        diagnostic.range.end.character = 1;
        diagnostic.source = "CryoLSP";

        auto &diagnostics = diagnostics_cache_[uri];
        diagnostics.push_back(diagnostic);

        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                       "Added diagnostic for " + uri + ": " + message);
    }

    void LSPAnalyzer::add_diagnostic_warning(const std::string &uri, const std::string &message)
    {
        DiagnosticInfo diagnostic;
        diagnostic.message = message;
        diagnostic.severity = 2; // Warning severity
        diagnostic.range.start.line = 0;
        diagnostic.range.start.character = 0;
        diagnostic.range.end.line = 0;
        diagnostic.range.end.character = 1;
        diagnostic.source = "CryoLSP";

        auto &diagnostics = diagnostics_cache_[uri];
        diagnostics.push_back(diagnostic);

        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                       "Added warning for " + uri + ": " + message);
    }

    void LSPAnalyzer::clear_diagnostics(const std::string &uri)
    {
        diagnostics_cache_[uri].clear();
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Cleared diagnostics for " + uri);
    }

    std::string LSPAnalyzer::extract_word_at_position(const std::string &content, const Position &position)
    {
        // Split content into lines
        std::vector<std::string> lines;
        std::stringstream ss(content);
        std::string line;
        while (std::getline(ss, line))
        {
            lines.push_back(line);
        }

        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                       "Word extraction: position " + std::to_string(position.line) + ":" + std::to_string(position.character) +
                                           ", total lines: " + std::to_string(lines.size()));

        if (position.line >= lines.size())
        {
            Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Position line out of bounds");
            return "";
        }

        const std::string &target_line = lines[position.line];
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                       "Target line (" + std::to_string(target_line.length()) + " chars): '" + target_line + "'");

        if (position.character >= target_line.length())
        {
            Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Position character out of bounds");
            return "";
        }

        // Find word boundaries
        int start = position.character;
        int end = position.character;

        // Move start backward to find word start
        while (start > 0 && (std::isalnum(target_line[start - 1]) || target_line[start - 1] == '_'))
        {
            start--;
        }

        // Move end forward to find word end
        while (end < target_line.length() && (std::isalnum(target_line[end]) || target_line[end] == '_'))
        {
            end++;
        }

        if (end > start)
        {
            std::string word = target_line.substr(start, end - start);
            Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Extracted word: '" + word + "'");
            return word;
        }

        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                       "No word found at position (char: '" + std::string(1, target_line[position.character]) + "')");
        return "";
    }

    std::optional<SymbolInfo> LSPAnalyzer::resolve_symbol_with_compiler(const std::string &symbol_name,
                                                                        const Position &position,
                                                                        Cryo::ASTNode *ast_root)
    {
        if (!compiler_ || !ast_root)
        {
            return std::nullopt;
        }

        try
        {
            // Get the type checker from compiler
            auto type_checker = compiler_->type_checker();
            if (!type_checker)
            {
                Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "No type checker available");
                return std::nullopt;
            }

            // Look up symbol using type checker's lookup methods
            auto symbol = type_checker->lookup_symbol(symbol_name);
            if (!symbol)
            {
                // Try lookup in any namespace
                symbol = type_checker->lookup_symbol_in_any_namespace(symbol_name);
            }

            if (symbol)
            {
                SymbolInfo info;
                info.name = symbol_name;

                // Extract type information from TypedSymbol
                if (symbol->type)
                {
                    info.type_name = symbol->type->name();
                }
                else
                {
                    info.type_name = "unknown";
                }

                // Determine symbol kind based on type
                if (info.type_name.find("function") != std::string::npos ||
                    info.type_name.find("Function") != std::string::npos)
                {
                    info.kind = "function";
                    info.documentation = "**Function: " + symbol_name + "**\n\n";
                    info.documentation += "Type: `" + info.type_name + "`";
                }
                else if (info.type_name == "i32" || info.type_name == "i64" ||
                         info.type_name == "u32" || info.type_name == "u64" ||
                         info.type_name == "bool" || info.type_name == "string")
                {
                    info.kind = "variable";
                    info.documentation = "**Variable: " + symbol_name + "**\n\n";
                    info.documentation += "Type: `" + info.type_name + "`";
                }
                else
                {
                    info.kind = "symbol";
                    info.documentation = "**Symbol: " + symbol_name + "**\n\n";
                    info.documentation += "Type: `" + info.type_name + "`";
                }

                info.scope = "document"; // For now, all symbols are document scope

                Cryo::Logger::instance().info(Cryo::LogComponent::LSP,
                                              "Resolved symbol: " + symbol_name + " as " + info.kind);

                return info;
            }

            Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                           "Symbol not found in symbol table: " + symbol_name);
            return std::nullopt;
        }
        catch (const std::exception &e)
        {
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP,
                                           "Exception in symbol resolution: " + std::string(e.what()));
            return std::nullopt;
        }
    }

    std::string LSPAnalyzer::get_keyword_documentation(const std::string &keyword)
    {
        if (keyword == "function")
            return "Declares a function";
        if (keyword == "pub")
            return "Makes the item publicly accessible";
        if (keyword == "mut")
            return "Makes a variable mutable";
        if (keyword == "const")
            return "Declares a constant value";
        if (keyword == "namespace")
            return "Declares a namespace";
        if (keyword == "import")
            return "Imports symbols from another module";
        if (keyword == "if")
            return "Conditional execution";
        if (keyword == "else")
            return "Alternative execution branch";
        if (keyword == "for")
            return "Loop construct";
        if (keyword == "while")
            return "While loop construct";
        if (keyword == "return")
            return "Returns a value from function";
        return "CryoLang keyword";
    }

    std::string LSPAnalyzer::get_type_documentation(const std::string &type_name)
    {
        if (type_name == "i32")
            return "32-bit signed integer";
        if (type_name == "i64")
            return "64-bit signed integer";
        if (type_name == "u32")
            return "32-bit unsigned integer";
        if (type_name == "u64")
            return "64-bit unsigned integer";
        if (type_name == "bool")
            return "Boolean type (true/false)";
        if (type_name == "string")
            return "String type for text";
        if (type_name == "void")
            return "No return value";
        if (type_name == "int")
            return "Integer type";
        return "Built-in type";
    }

} // namespace CryoLSP