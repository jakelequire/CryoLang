#include "../../include/analyzer/CryoAnalyzer.hpp"
#include "../../include/Logger.hpp"

#include <fstream>
#include <sstream>
#include <cstdlib>    // for std::getenv
#include <functional> // for std::hash
#include <chrono>     // for rate limiting
#include <regex>      // for pattern matching

using namespace CryoLSP;
using namespace Cryo::LSP;

namespace CryoLSP
{

    CryoAnalyzer::CryoAnalyzer()
    {
        Logger::instance().debug("CryoAnalyzer", "Initialized CryoAnalyzer with direct compiler integration");
    }

    CryoAnalyzer::~CryoAnalyzer()
    {
        Logger::instance().debug("CryoAnalyzer", "Destroying CryoAnalyzer, cleaning up {} analyzed files", analyzed_files_.size());

        // Explicitly cleanup all compiler instances
        for (auto &[file_path, analysis] : analyzed_files_)
        {
            if (analysis.compiler)
            {
                Logger::instance().debug("CryoAnalyzer", "Cleaning up compiler instance for: {}", file_path);
                analysis.compiler.reset();
            }
        }

        analyzed_files_.clear();
        file_contents_.clear();

        Logger::instance().debug("CryoAnalyzer", "CryoAnalyzer cleanup complete");
    }

    bool CryoAnalyzer::parseFile(const std::string &file_path, const std::string &content)
    {
        Logger::instance().debug("CryoAnalyzer", "Parsing file: {}", file_path);

        try
        {
            // Store content for fallback analysis
            file_contents_[file_path] = content;

            // Check if we already have analysis for this file with the same content
            if (analyzed_files_.count(file_path))
            {
                FileAnalysis &existing = analyzed_files_[file_path];
                if (existing.content == content && existing.parsed_successfully)
                {
                    Logger::instance().debug("CryoAnalyzer", "File content unchanged, reusing existing analysis: {}", file_path);
                    return true;
                }
                // Content changed, cleanup old compiler instance before creating new one
                if (existing.compiler)
                {
                    Logger::instance().debug("CryoAnalyzer", "Cleaning up previous compiler instance for: {}", file_path);
                    existing.compiler.reset();
                }
            }

            // Create a new FileAnalysis entry
            FileAnalysis &analysis = analyzed_files_[file_path];
            analysis.content = content;
            analysis.parsed_successfully = false;

            // Cleanup old files if we're exceeding the cache limit
            cleanupOldAnalysisIfNeeded();

            // Create a new compiler instance for this file
            analysis.compiler = std::make_unique<Cryo::CompilerInstance>();

            // Create a temporary file in the system temp directory
            std::string temp_dir;
            const char *temp_env = std::getenv("TMPDIR"); // Linux/macOS
            if (!temp_env)
            {
                temp_env = std::getenv("TEMP"); // Windows
            }
            if (!temp_env)
            {
                temp_env = std::getenv("TMP"); // Windows fallback
            }
            if (temp_env)
            {
                temp_dir = std::string(temp_env);
            }
            else
            {
                temp_dir = "/tmp"; // Linux/macOS fallback, or "." for current dir
            }

            // Create a unique temporary filename using proper path separator
            std::string temp_file = temp_dir + "/cryo_lsp_" + std::to_string(std::hash<std::string>{}(file_path)) + ".cryo";

            std::ofstream temp_out(temp_file);
            if (!temp_out)
            {
                Logger::instance().error("CryoAnalyzer", "Failed to create temporary file for: {}", file_path);
                return false;
            }
            temp_out << content;
            temp_out.close();

            bool compilation_success = false;
            try
            {
                // Parse the file using the frontend-only compiler mode (no codegen)
                compilation_success = analysis.compiler->compile_frontend_only(temp_file);

                // Check if we have an AST (even if semantic analysis had issues)
                analysis.ast = analysis.compiler->ast_root();
                if (analysis.ast)
                {
                    analysis.parsed_successfully = true;
                    Logger::instance().debug("CryoAnalyzer", "Successfully parsed file with AST: {}", file_path);

                    // Debug: Log symbol table contents
                    if (analysis.compiler && analysis.compiler->symbol_table())
                    {
                        try
                        {
                            // Create a string stream to capture symbol table output
                            std::ostringstream symbol_dump;
                            analysis.compiler->dump_symbol_table(symbol_dump);
                            std::string symbol_contents = symbol_dump.str();
                            if (!symbol_contents.empty())
                            {
                                Logger::instance().debug("CryoAnalyzer", "Symbol table contents for {}: {}", file_path, symbol_contents);
                            }
                            else
                            {
                                Logger::instance().debug("CryoAnalyzer", "Symbol table is empty for file: {}", file_path);
                            }
                        }
                        catch (const std::exception &e)
                        {
                            Logger::instance().debug("CryoAnalyzer", "Error dumping symbol table: {}", e.what());
                        }
                    }
                }
                else if (compilation_success)
                {
                    analysis.parsed_successfully = true;
                    Logger::instance().debug("CryoAnalyzer", "Frontend compilation succeeded for: {}", file_path);
                }
                else
                {
                    Logger::instance().debug("CryoAnalyzer", "Frontend compilation failed but continuing for hover support: {}", file_path);
                    // Still mark as parsed so we can provide fallback hover info
                    analysis.parsed_successfully = true;
                }
            }
            catch (const std::exception &e)
            {
                Logger::instance().error("CryoAnalyzer", "Exception during compilation: {}", e.what());
                // Mark as parsed to allow fallback hover
                analysis.parsed_successfully = true;
            }

            // Clean up temporary file
            std::remove(temp_file.c_str());

            return analysis.parsed_successfully;
        }
        catch (const std::exception &e)
        {
            Logger::instance().error("CryoAnalyzer", "Error parsing file {}: {}", file_path, e.what());
            return false;
        }
    }

    std::optional<HoverInfo> CryoAnalyzer::getHoverInfo(const std::string &file_path, const Position &position)
    {
        // Rate limiting: prevent excessive hover requests
        auto now = std::chrono::steady_clock::now();
        if (now - last_hover_time_ < MIN_HOVER_INTERVAL)
        {
            Logger::instance().debug("CryoAnalyzer", "Hover request rate limited for {}:{},{}", file_path, position.line, position.character);
            return std::nullopt;
        }
        last_hover_time_ = now;

        Logger::instance().debug("CryoAnalyzer", "Getting hover info for {}:{},{}", file_path, position.line, position.character);

        // First try to get info from compiler analysis
        if (analyzed_files_.count(file_path) && analyzed_files_[file_path].parsed_successfully)
        {
            HoverInfo compiler_hover = analyzeWithCompiler(file_path, position);
            if (!compiler_hover.name.empty())
            {
                return compiler_hover;
            }
        }

        // Fallback to simple pattern analysis
        if (file_contents_.count(file_path))
        {
            return analyzeSimplePattern(file_contents_[file_path], position);
        }

        return std::nullopt;
    }

    HoverInfo CryoAnalyzer::analyzeWithCompiler(const std::string &file_path, const Position &position)
    {
        HoverInfo info;

        const FileAnalysis &analysis = analyzed_files_[file_path];
        if (!analysis.parsed_successfully || !analysis.compiler)
        {
            return info;
        }

        // Get word at position for symbol lookup
        std::string word = getWordAtPosition(analysis.content, position);
        if (word.empty())
        {
            return info;
        }

        info.name = word;

        // Try to find symbol in symbol table with better error handling
        try
        {
            if (analysis.compiler && analysis.compiler->symbol_table())
            {
                // Debug: Log that we're looking up a symbol
                Logger::instance().debug("CryoAnalyzer", "Symbol table exists, looking up symbol: '{}'", word);

                auto symbol = findSymbolInSymbolTable(*analysis.compiler->symbol_table(), word);
                if (symbol && symbol.value())
                {
                    Logger::instance().debug("CryoAnalyzer", "Found symbol '{}' in symbol table", word);

                    // Extract information from the symbol with null checks
                    if (symbol.value()->data_type)
                    {
                        try
                        {
                            info.type = symbol.value()->data_type->to_string();
                            Logger::instance().debug("CryoAnalyzer", "Symbol '{}' has type: {}", word, info.type);
                        }
                        catch (const std::exception &e)
                        {
                            Logger::instance().debug("CryoAnalyzer", "Error getting type string: {}", e.what());
                            info.type = "unknown";
                        }
                    }
                    else
                    {
                        Logger::instance().debug("CryoAnalyzer", "Symbol '{}' has null data_type", word);
                        info.type = "unknown";
                    }

                    // Get symbol kind
                    switch (symbol.value()->kind)
                    {
                    case Cryo::SymbolKind::Variable:
                        info.kind = "variable";
                        break;
                    case Cryo::SymbolKind::Function:
                        info.kind = "function";
                        break;
                    case Cryo::SymbolKind::Type:
                        info.kind = "type";
                        break;
                    case Cryo::SymbolKind::Intrinsic:
                        info.kind = "intrinsic";
                        break;
                    default:
                        info.kind = "symbol";
                        break;
                    }

                    info.signature = word + ": " + info.type;

                    Logger::instance().debug("CryoAnalyzer", "Found symbol in symbol table: {} -> {}", word, info.type);
                    return info;
                }
                else
                {
                    Logger::instance().debug("CryoAnalyzer", "Symbol '{}' not found in symbol table", word);
                }
            }
            else
            {
                Logger::instance().debug("CryoAnalyzer", "No symbol table available for lookup");
            }
        }
        catch (const std::exception &e)
        {
            Logger::instance().debug("CryoAnalyzer", "Exception during symbol table lookup: {}", e.what());
        }

        // If not found in symbol table, fall back to simple analysis
        return analyzeSimplePattern(analysis.content, position);
    }

    std::optional<Cryo::Symbol *> CryoAnalyzer::findSymbolInSymbolTable(const Cryo::SymbolTable &symbol_table, const std::string &symbol_name)
    {
        try
        {
            // Try to find the symbol in the symbol table
            auto symbol = symbol_table.lookup_symbol(symbol_name);
            if (symbol)
            {
                return symbol;
            }
        }
        catch (const std::exception &e)
        {
            Logger::instance().debug("CryoAnalyzer", "Exception in symbol table lookup for '{}': {}", symbol_name, e.what());
        }

        return std::nullopt;
    }

    std::optional<FunctionSignature> CryoAnalyzer::extractFunctionFromAST(const Cryo::ProgramNode *ast, const Position &position)
    {
        // TODO: Implement AST traversal to find function at position
        // This would walk the AST to find function declaration nodes at the given position
        return std::nullopt;
    }

    HoverInfo CryoAnalyzer::analyzeSimplePattern(const std::string &content, const Position &position)
    {
        HoverInfo info;

        // Get word at position
        std::string word = getWordAtPosition(content, position);
        if (word.empty())
        {
            return info;
        }

        // Simple analysis
        info = analyzeSimpleSymbol(word, content, position);

        return info;
    }

    std::optional<std::string> CryoAnalyzer::getSymbolAtPosition(const std::string &file_path, const Position &position)
    {
        if (file_contents_.count(file_path))
        {
            return getWordAtPosition(file_contents_[file_path], position);
        }
        return std::nullopt;
    }

    void CryoAnalyzer::updateFileContent(const std::string &file_path, const std::string &content)
    {
        parseFile(file_path, content);
    }

    std::optional<FunctionSignature> CryoAnalyzer::getFunctionSignature(const std::string &file_path, const Position &position)
    {
        if (analyzed_files_.count(file_path) && analyzed_files_[file_path].parsed_successfully)
        {
            return extractFunctionFromAST(analyzed_files_[file_path].ast, position);
        }
        return std::nullopt;
    }

    std::vector<HoverInfo> CryoAnalyzer::getStructMembers(const std::string &file_path, const std::string &struct_name)
    {
        std::vector<HoverInfo> members;

        // TODO: Implement struct member extraction from symbol table
        // This would look up the struct type in the type system and extract member information

        return members;
    }

    std::optional<Position> CryoAnalyzer::getDefinitionLocation(const std::string &file_path, const Position &position)
    {
        // TODO: Implement definition location lookup using symbol table location information
        return std::nullopt;
    }

    // Simple analysis implementation (keeping existing logic)
    std::string CryoAnalyzer::getWordAtPosition(const std::string &content, const Position &position)
    {
        std::istringstream stream(content);
        std::string line;
        int current_line = 0;

        while (std::getline(stream, line) && current_line < position.line)
        {
            current_line++;
        }

        if (current_line != position.line || position.character >= static_cast<int>(line.length()))
        {
            return "";
        }

        // Find word boundaries
        int start = position.character;
        int end = position.character;

        // Move start back to beginning of word
        while (start > 0 && (std::isalnum(line[start - 1]) || line[start - 1] == '_'))
        {
            start--;
        }

        // Move end forward to end of word
        while (end < static_cast<int>(line.length()) && (std::isalnum(line[end]) || line[end] == '_'))
        {
            end++;
        }

        return line.substr(start, end - start);
    }

    std::string CryoAnalyzer::getLineAtPosition(const std::string &content, const Position &position)
    {
        std::istringstream stream(content);
        std::string line;
        int current_line = 0;

        while (std::getline(stream, line) && current_line < position.line)
        {
            current_line++;
        }

        if (current_line == position.line)
        {
            return line;
        }

        return "";
    }

    HoverInfo CryoAnalyzer::analyzeSimpleSymbol(const std::string &word, const std::string &content, const Position &position)
    {
        HoverInfo info;
        info.name = word;

        // Try to extract type information from Cryo syntax patterns
        std::string line = getLineAtPosition(content, position);
        
        // Pattern 1: Variable declaration "const/let/var name: type = value"
        std::regex var_pattern(R"(\b(const|let|var)\s+)" + word + R"(\s*:\s*([^=\s]+))");
        std::smatch var_match;
        if (std::regex_search(line, var_match, var_pattern)) {
            std::string type = var_match[2].str();
            info.type = type;
            info.kind = "variable";
            info.signature = var_match[1].str() + " " + word + ": " + type;
            Logger::instance().debug("CryoAnalyzer", "Found variable declaration: {} -> {}", word, type);
            return info;
        }
        
        // Pattern 2: Function declaration "function name(params) -> return_type"
        std::regex func_pattern(R"(\bfunction\s+)" + word + R"(\s*\([^)]*\)\s*->\s*([^;{]+))");
        std::smatch func_match;
        if (std::regex_search(line, func_match, func_pattern)) {
            std::string return_type = func_match[1].str();
            // Trim whitespace
            return_type.erase(return_type.find_last_not_of(" \t\n\r\f\v") + 1);
            info.type = "function";
            info.kind = "function";
            info.signature = "function " + word + "() -> " + return_type;
            Logger::instance().debug("CryoAnalyzer", "Found function declaration: {} -> {}", word, return_type);
            return info;
        }
        
        // Pattern 3: Function without return type "function name(params)"
        std::regex func_simple_pattern(R"(\bfunction\s+)" + word + R"(\s*\([^)]*\))");
        if (std::regex_search(line, func_simple_pattern)) {
            info.type = "function";
            info.kind = "function";
            info.signature = "function " + word + "()";
            Logger::instance().debug("CryoAnalyzer", "Found simple function declaration: {}", word);
            return info;
        }

        // Fast fallback - basic hardcoded cases
        if (word == "main")
        {
            info.type = "function";
            info.kind = "function";
            info.signature = "function main()";
        }
        else if (word == "printf" || word == "print")
        {
            info.type = "function";
            info.kind = "intrinsic";
            info.signature = "function " + word + "(...)";
        }
        else
        {
            // Default to unknown but fast response
            info.type = "unknown";
            info.kind = "identifier";
            info.signature = word + ": unknown";
        }

        return info;
    }

    bool CryoAnalyzer::isVariableDeclaration(const std::string &word, const std::string &content, const Position &position)
    {
        std::istringstream stream(content);
        std::string line;
        int current_line = 0;

        while (std::getline(stream, line) && current_line < position.line)
        {
            current_line++;
        }

        if (current_line != position.line)
        {
            return false;
        }

        // Simple heuristic: check if line contains ":" after the word (Cryo syntax)
        size_t word_pos = line.find(word);
        if (word_pos != std::string::npos)
        {
            size_t colon_pos = line.find(':', word_pos);
            return colon_pos != std::string::npos;
        }

        return false;
    }

    std::string CryoAnalyzer::getVariableType(const std::string &word, const std::string &content, const Position &position)
    {
        std::istringstream stream(content);
        std::string line;
        int current_line = 0;

        while (std::getline(stream, line) && current_line < position.line)
        {
            current_line++;
        }

        if (current_line != position.line)
        {
            return "unknown";
        }

        // Extract type after colon
        size_t word_pos = line.find(word);
        if (word_pos != std::string::npos)
        {
            size_t colon_pos = line.find(':', word_pos);
            if (colon_pos != std::string::npos)
            {
                size_t type_start = colon_pos + 1;
                while (type_start < line.length() && std::isspace(line[type_start]))
                {
                    type_start++;
                }

                size_t type_end = type_start;
                while (type_end < line.length() && (std::isalnum(line[type_end]) || line[type_end] == '_'))
                {
                    type_end++;
                }

                if (type_end > type_start)
                {
                    return line.substr(type_start, type_end - type_start);
                }
            }
        }

        return "unknown";
    }

    void CryoAnalyzer::cleanupOldAnalysisIfNeeded()
    {
        if (analyzed_files_.size() <= MAX_CACHED_FILES)
        {
            return;
        }

        Logger::instance().info("CryoAnalyzer", "Cache limit exceeded ({} files), cleaning up oldest entries", analyzed_files_.size());

        // Simple cleanup: remove half of the entries
        // In a production system, you'd use LRU or access-time based cleanup
        size_t to_remove = analyzed_files_.size() / 2;
        auto it = analyzed_files_.begin();

        while (to_remove > 0 && it != analyzed_files_.end())
        {
            Logger::instance().debug("CryoAnalyzer", "Removing cached analysis for: {}", it->first);
            if (it->second.compiler)
            {
                it->second.compiler.reset();
            }
            file_contents_.erase(it->first);
            it = analyzed_files_.erase(it);
            to_remove--;
        }

        Logger::instance().info("CryoAnalyzer", "Cache cleanup complete, {} files remaining", analyzed_files_.size());
    }

} // namespace CryoLSP