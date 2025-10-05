#include "../../include/analyzer/CryoAnalyzer.hpp"
#include "../../include/Logger.hpp"

#include <fstream>
#include <sstream>
#include <cstdlib>    // for std::getenv
#include <functional> // for std::hash
#include <chrono>     // for rate limiting
#include <regex>      // for pattern matching
#include <unordered_map> // for documentation maps
#include <filesystem> // for std::filesystem::exists
#ifdef _WIN32
#include <direct.h>   // for _getcwd on Windows
#else
#include <unistd.h>   // for getcwd on Unix
#endif

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
            
            // Check file size to prevent processing extremely large files that might cause issues
            const size_t MAX_FILE_SIZE = 50 * 1024; // 50KB limit to prevent crashes on large files
            if (content.size() > MAX_FILE_SIZE)
            {
                Logger::instance().warn("CryoAnalyzer", "File {} is too large ({} bytes), using basic analysis only", 
                    file_path, content.size());
                    
                // Still create a basic analysis entry for hover fallback
                FileAnalysis &analysis = analyzed_files_[file_path];
                analysis.content = content;
                analysis.parsed_successfully = false; // Mark as not fully parsed
                analysis.diagnostics.clear();
                analysis.last_analyzed = std::chrono::steady_clock::now();
                
                // Add a diagnostic about the large file
                LSPDiagnostic size_diag;
                size_diag.start = {0, 0};
                size_diag.end = {0, 0};
                size_diag.message = "File too large for full analysis (" + std::to_string(content.size()) + " bytes). Basic analysis only.";
                size_diag.severity = "info";
                size_diag.source = "cryo-lsp";
                size_diag.code = "file_too_large";
                analysis.diagnostics.push_back(size_diag);
                
                // Still return true so basic hover works
                return true;
            }

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
            analysis.diagnostics.clear();
            analysis.last_analyzed = std::chrono::steady_clock::now();

            // Cleanup old files if we're exceeding the cache limit
            cleanupOldAnalysisIfNeeded();

            // Create a new compiler instance for this file with proper configuration
            analysis.compiler = Cryo::create_compiler_instance();
            
            // Configure the compiler for LSP use - enable stdlib linking
            analysis.compiler->set_debug_mode(true);
            analysis.compiler->set_stdlib_linking(true); // Enable stdlib for imports like <io/stdio>
            
            Logger::instance().debug("CryoAnalyzer", "Created compiler instance with stdlib support for: {}", file_path);

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
            std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
            
            try
            {
                Logger::instance().debug("CryoAnalyzer", "Starting compilation of: {}", file_path);
                
                // Add timeout protection - if compilation takes more than 10 seconds, abort
                const auto COMPILATION_TIMEOUT = std::chrono::seconds(10);
                
                // Parse the file using the frontend-only compiler mode (no codegen)
                compilation_success = analysis.compiler->compile_frontend_only(temp_file);
                
                auto elapsed = std::chrono::steady_clock::now() - start_time;
                if (elapsed > COMPILATION_TIMEOUT)
                {
                    Logger::instance().warn("CryoAnalyzer", "Compilation timeout for: {} ({}ms)", file_path, 
                        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
                    compilation_success = false;
                }
                else
                {
                    Logger::instance().debug("CryoAnalyzer", "Compilation completed in {}ms for: {}", 
                        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), file_path);
                }

                // Extract diagnostics even if compilation failed
                if (analysis.compiler->diagnostic_manager())
                {
                    analysis.diagnostics = extractDiagnosticsFromCompiler(*analysis.compiler);
                    Logger::instance().debug("CryoAnalyzer", "Extracted {} diagnostics from compiler", analysis.diagnostics.size());
                }

                // Check if we have an AST (even if semantic analysis had issues)
                analysis.ast = analysis.compiler->ast_root();
                if (analysis.ast)
                {
                    analysis.parsed_successfully = true;
                    Logger::instance().debug("CryoAnalyzer", "Successfully parsed file with AST: {}", file_path);

                    // Debug: Log symbol table contents with enhanced info (with safety checks)
                    if (analysis.compiler && analysis.compiler->symbol_table())
                    {
                        try
                        {
                            auto* symbol_table = analysis.compiler->symbol_table();
                            if (symbol_table)
                            {
                                // Safely check symbol table size first
                                Logger::instance().debug("CryoAnalyzer", "Symbol table available for: {}", file_path);
                                
                                // Try to access namespaces with additional safety
                                try
                                {
                                    const auto &namespaces = symbol_table->get_namespaces();
                                    Logger::instance().debug("CryoAnalyzer", "Found {} namespaces in symbol table", namespaces.size());
                                    
                                    for (const auto &[ns_name, symbols] : namespaces)
                                    {
                                        if (!ns_name.empty() && symbols.size() < 1000) // Sanity check
                                        {
                                            Logger::instance().debug("CryoAnalyzer", "Namespace '{}' contains {} symbols", ns_name, symbols.size());
                                        }
                                    }
                                }
                                catch (const std::exception &ns_e)
                                {
                                    Logger::instance().warn("CryoAnalyzer", "Error accessing namespaces: {}", ns_e.what());
                                }
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
                    // Still mark as parsed so we can provide fallback hover info and show diagnostics
                    analysis.parsed_successfully = true;
                }
            }
            catch (const std::bad_alloc &e)
            {
                Logger::instance().error("CryoAnalyzer", "Memory allocation error during compilation of {}: {}", file_path, e.what());
                analysis.parsed_successfully = false;
            }
            catch (const std::runtime_error &e)
            {
                Logger::instance().error("CryoAnalyzer", "Runtime error during compilation of {}: {}", file_path, e.what());
                analysis.parsed_successfully = true; // Allow fallback hover
            }
            catch (const std::exception &e)
            {
                Logger::instance().error("CryoAnalyzer", "Exception during compilation of {}: {}", file_path, e.what());
                analysis.parsed_successfully = true; // Allow fallback hover

                // Add the exception as a diagnostic
                if (analysis.diagnostics.empty())
                {
                    LSPDiagnostic error_diag;
                    error_diag.start = {0, 0};
                    error_diag.end = {0, 0};
                    error_diag.message = "Compilation error: " + std::string(e.what());
                    error_diag.severity = "error";
                    error_diag.source = "cryo-lsp";
                    error_diag.code = "compilation_error";
                    analysis.diagnostics.push_back(error_diag);
                }
            }
            catch (...)
            {
                Logger::instance().error("CryoAnalyzer", "Unknown exception during compilation of: {}", file_path);
                analysis.parsed_successfully = false;
            }

            // Clean up temporary file
            std::remove(temp_file.c_str());

            return analysis.parsed_successfully;
        }
        catch (const std::exception &e)
        {
            Logger::instance().error("CryoAnalyzer", "Error parsing file {}: {}", file_path, e.what());
            
            // Make sure we have a FileAnalysis entry even if parsing completely failed
            if (!analyzed_files_.count(file_path))
            {
                FileAnalysis &analysis = analyzed_files_[file_path];
                analysis.content = content;
                analysis.parsed_successfully = true; // Allow fallback analysis
                analysis.last_analyzed = std::chrono::steady_clock::now();
                
                LSPDiagnostic error_diag;
                error_diag.start = {0, 0};
                error_diag.end = {0, 0};
                error_diag.message = "Failed to parse file: " + std::string(e.what());
                error_diag.severity = "error";
                error_diag.source = "cryo-lsp";
                error_diag.code = "parse_error";
                analysis.diagnostics.push_back(error_diag);
            }
            
            return true; // Return true to allow fallback analysis
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

        // Check if this is a qualified symbol (contains ::)
        std::string line = getLineAtPosition(analysis.content, position);
        std::string qualified_symbol = extractQualifiedSymbolAtPosition(line, position);
        
        Logger::instance().debug("CryoAnalyzer", "Analyzing symbol '{}', qualified form: '{}', line: '{}'", word, qualified_symbol, line);

        // Try to find symbol in symbol table with better error handling
        try
        {
            if (analysis.compiler && analysis.compiler->symbol_table())
            {
                Cryo::Symbol *symbol = nullptr;

                // Debug: Log what namespaces are available
                const auto &namespaces = analysis.compiler->symbol_table()->get_namespaces();
                Logger::instance().debug("CryoAnalyzer", "Available namespaces: {}", namespaces.size());
                for (const auto &[ns_name, symbols] : namespaces)
                {
                    Logger::instance().debug("CryoAnalyzer", "  Namespace '{}' has {} symbols", ns_name, symbols.size());
                    for (const auto &[sym_name, sym] : symbols)
                    {
                        Logger::instance().debug("CryoAnalyzer", "    - '{}' ({})", sym_name, sym.scope);
                    }
                }

                // First try qualified lookup if we have a qualified symbol
                if (!qualified_symbol.empty() && qualified_symbol != word)
                {
                    symbol = findQualifiedSymbol(*analysis.compiler->symbol_table(), qualified_symbol).value_or(nullptr);
                    Logger::instance().debug("CryoAnalyzer", "Qualified lookup for '{}': {}", qualified_symbol, symbol ? "found" : "not found");
                }

                // If qualified lookup failed or we don't have a qualified symbol, try regular lookup
                if (!symbol)
                {
                    symbol = findSymbolInSymbolTable(*analysis.compiler->symbol_table(), word).value_or(nullptr);
                    Logger::instance().debug("CryoAnalyzer", "Regular lookup for '{}': {}", word, symbol ? "found" : "not found");
                }

                // If still not found, try namespace-aware lookup with common imports
                if (!symbol)
                {
                    std::vector<std::string> common_imports = {"std", "io", "core", "runtime"};
                    symbol = findSymbolWithImports(*analysis.compiler->symbol_table(), word, common_imports).value_or(nullptr);
                    Logger::instance().debug("CryoAnalyzer", "Import-aware lookup for '{}': {}", word, symbol ? "found" : "not found");
                }

                if (symbol)
                {
                    Logger::instance().debug("CryoAnalyzer", "Found symbol '{}' in symbol table", word);
                    return buildEnhancedHoverInfo(*symbol, !qualified_symbol.empty() ? qualified_symbol : word, file_path);
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

    std::string CryoAnalyzer::getPrimitiveTypeDocumentation(const std::string &type_name)
    {
        static const std::unordered_map<std::string, std::string> primitive_docs = {
            // Integer types
            {"int", "64-bit signed integer (default integer type). Range: -9,223,372,036,854,775,808 to 9,223,372,036,854,775,807"},
            {"i8", "8-bit signed integer. Range: -128 to 127"},
            {"i16", "16-bit signed integer. Range: -32,768 to 32,767"},
            {"i32", "32-bit signed integer. Range: -2,147,483,648 to 2,147,483,647"},
            {"i64", "64-bit signed integer. Range: -9,223,372,036,854,775,808 to 9,223,372,036,854,775,807"},
            {"u8", "8-bit unsigned integer. Range: 0 to 255"},
            {"u16", "16-bit unsigned integer. Range: 0 to 65,535"},
            {"u32", "32-bit unsigned integer. Range: 0 to 4,294,967,295"},
            {"u64", "64-bit unsigned integer. Range: 0 to 18,446,744,073,709,551,615"},
            
            // Floating point types
            {"float", "32-bit floating point number (IEEE 754 single precision)"},
            {"f32", "32-bit floating point number (IEEE 754 single precision)"},
            {"double", "64-bit floating point number (IEEE 754 double precision)"},
            {"f64", "64-bit floating point number (IEEE 754 double precision)"},
            
            // Other primitive types
            {"string", "UTF-8 encoded string type. Managed string with automatic memory handling"},
            {"char", "Single character type (32-bit Unicode code point)"},
            {"boolean", "Boolean type with values true or false"},
            {"bool", "Boolean type with values true or false"},
            {"void", "Unit type representing no value (used in function return types)"},
            
            // Special types
            {"ptr", "Generic pointer type. Used as ptr<T> for typed pointers"},
            {"const_ptr", "Immutable pointer type. Used as const_ptr<T> for read-only pointers"},
            
            // Generic/Template types
            {"Array", "Dynamic array type. Used as Array<T> for arrays of type T"},
            {"Option", "Optional value type. Used as Option<T> for values that may or may not exist"},
            {"Result", "Result type for error handling. Used as Result<T, E> for operations that can succeed or fail"}
        };
        
        auto it = primitive_docs.find(type_name);
        return (it != primitive_docs.end()) ? it->second : "";
    }

    std::string CryoAnalyzer::getBuiltinFunctionDocumentation(const std::string &function_name)
    {
        static const std::unordered_map<std::string, std::string> builtin_docs = {
            {"main", "Entry point function for CryoLang programs. Must return int (exit code)"},
            {"sizeof", "Returns the size in bytes of a type or expression"},
            {"new", "Memory allocation operator. Creates new instances of types on the heap"},
            {"delete", "Memory deallocation operator. Frees memory allocated with new"},
        };
        
        auto it = builtin_docs.find(function_name);
        return (it != builtin_docs.end()) ? it->second : "";
    }

    HoverInfo CryoAnalyzer::analyzeSimpleSymbol(const std::string &word, const std::string &content, const Position &position)
    {
        HoverInfo info;
        info.name = word;

        // Check if this is a primitive type first
        auto primitive_docs = getPrimitiveTypeDocumentation(word);
        if (!primitive_docs.empty()) {
            info.type = "primitive type";
            info.kind = "type";
            info.signature = word;
            info.documentation = primitive_docs;
            Logger::instance().debug("CryoAnalyzer", "Found primitive type: {} -> {}", word, primitive_docs.substr(0, 50) + "...");
            return info;
        }

        // Try to extract type information from Cryo syntax patterns
        std::string line = getLineAtPosition(content, position);
        
        // Check if this is a function call first (word followed by parentheses)
        std::regex func_call_pattern(word + R"(\s*\()");
        if (std::regex_search(line, func_call_pattern)) {
            // Look for the function definition in the content
            std::regex func_def_pattern(R"(\bfunction\s+)" + word + R"(\s*\(([^)]*)\)\s*(?:->\s*([^;{]+))?)");
            std::smatch func_def_match;
            if (std::regex_search(content, func_def_match, func_def_pattern)) {
                std::string params = func_def_match[1].str();
                std::string return_type = func_def_match.size() > 2 ? func_def_match[2].str() : "";
                
                // Clean up parameters and return type
                if (!params.empty()) {
                    // Remove extra whitespace but preserve the parameter structure
                    std::regex ws_pattern(R"(\s+)");
                    params = std::regex_replace(params, ws_pattern, " ");
                    // Trim leading/trailing spaces
                    size_t start = params.find_first_not_of(" \t\n\r\f\v");
                    size_t end = params.find_last_not_of(" \t\n\r\f\v");
                    if (start != std::string::npos && end != std::string::npos) {
                        params = params.substr(start, end - start + 1);
                    }
                }
                
                if (!return_type.empty()) {
                    return_type.erase(return_type.find_last_not_of(" \t\n\r\f\v") + 1);
                }
                
                info.type = "function";
                info.kind = "function";
                if (!return_type.empty()) {
                    info.signature = "function " + word + "(" + params + ") -> " + return_type;
                } else {
                    info.signature = "function " + word + "(" + params + ")";
                }
                
                Logger::instance().debug("CryoAnalyzer", "Found function call referencing definition: {}", word);
                return info;
            }
        }
        
        // Pattern 1: Variable declaration "const/mut name: type = value"
        std::regex var_pattern(R"(\b(const|mut)\s+)" + word + R"(\s*:\s*([^=\s;]+))");
        std::smatch var_match;
        if (std::regex_search(line, var_match, var_pattern)) {
            std::string qualifier = var_match[1].str();
            std::string type = var_match[2].str();
            
            // Clean up the type (remove trailing characters that might be captured)
            size_t clean_end = type.find_first_of(" \t\n\r\f\v,;)}");
            if (clean_end != std::string::npos) {
                type = type.substr(0, clean_end);
            }
            
            info.type = type;
            info.kind = "variable";
            info.signature = qualifier + " " + word + ": " + type;
            
            // Add documentation for the type
            std::string type_docs;
            
            // Check for array types (e.g., int[], string[])
            if (type.find("[]") != std::string::npos) {
                std::string base_type = type.substr(0, type.find("[]"));
                type_docs = "Array of " + base_type + " elements";
                auto base_docs = getPrimitiveTypeDocumentation(base_type);
                if (!base_docs.empty()) {
                    type_docs += ". " + base_docs;
                }
            }
            // Check for generic types (e.g., Array<int>, Option<string>)
            else if (type.find('<') != std::string::npos && type.find('>') != std::string::npos) {
                size_t start = type.find('<');
                std::string base_type = type.substr(0, start);
                std::string inner_type = type.substr(start + 1, type.find('>') - start - 1);
                
                auto base_docs = getPrimitiveTypeDocumentation(base_type);
                if (!base_docs.empty()) {
                    type_docs = base_docs + " containing " + inner_type;
                } else {
                    type_docs = "Generic type " + base_type + " with parameter " + inner_type;
                }
            }
            // Check for primitive types
            else {
                type_docs = getPrimitiveTypeDocumentation(type);
            }
            
            if (!type_docs.empty()) {
                info.documentation = type_docs;
            }
            
            Logger::instance().debug("CryoAnalyzer", "Found variable declaration: {} {} -> {}", qualifier, word, type);
            return info;
        }
        
        // Pattern 2: Function declaration "function name(params) -> return_type"
        std::regex func_pattern(R"(\bfunction\s+)" + word + R"(\s*\(([^)]*)\)\s*->\s*([^;{]+))");
        std::smatch func_match;
        if (std::regex_search(line, func_match, func_pattern)) {
            std::string params = func_match[1].str();
            std::string return_type = func_match[2].str();
            // Trim whitespace
            return_type.erase(return_type.find_last_not_of(" \t\n\r\f\v") + 1);
            params.erase(params.find_last_not_of(" \t\n\r\f\v") + 1);
            
            info.type = "function";
            info.kind = "function";
            info.signature = "function " + word + "(" + params + ") -> " + return_type;
            // No additional documentation - just the signature
            Logger::instance().debug("CryoAnalyzer", "Found function declaration: {} -> {}", word, return_type);
            return info;
        }
        
        // Pattern 3: Function without return type "function name(params)"
        std::regex func_simple_pattern(R"(\bfunction\s+)" + word + R"(\s*\(([^)]*)\))");
        std::smatch func_simple_match;
        if (std::regex_search(line, func_simple_match, func_simple_pattern)) {
            std::string params = func_simple_match[1].str();
            params.erase(params.find_last_not_of(" \t\n\r\f\v") + 1);
            
            info.type = "function";
            info.kind = "function";
            info.signature = "function " + word + "(" + params + ")";
            // No additional documentation - just the signature
            Logger::instance().debug("CryoAnalyzer", "Found simple function declaration: {}", word);
            return info;
        }

        // Check for built-in/standard library functions
        auto builtin_docs = getBuiltinFunctionDocumentation(word);
        if (!builtin_docs.empty()) {
            info.type = "function";
            info.kind = "builtin";
            info.signature = "function " + word + "(...)";
            // Only add documentation for truly important builtin functions
            if (word == "main" || word == "sizeof") {
                info.documentation = builtin_docs;
            }
            return info;
        }

        // Default to unknown but provide context
        info.type = "unknown";
        info.kind = "identifier";
        info.signature = word + ": unknown";
        info.documentation = "Symbol not recognized. Check if it's properly declared or imported.";

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

    // ========================================
    // Enhanced Diagnostic Support
    // ========================================

    std::vector<LSPDiagnostic> CryoAnalyzer::getDiagnostics(const std::string &file_path)
    {
        if (analyzed_files_.count(file_path))
        {
            return analyzed_files_[file_path].diagnostics;
        }
        return {};
    }

    void CryoAnalyzer::clearDiagnostics(const std::string &file_path)
    {
        if (analyzed_files_.count(file_path))
        {
            analyzed_files_[file_path].diagnostics.clear();
        }
    }

    std::vector<LSPDiagnostic> CryoAnalyzer::extractDiagnosticsFromCompiler(const Cryo::CompilerInstance &compiler)
    {
        std::vector<LSPDiagnostic> lsp_diagnostics;

        try
        {
            if (compiler.diagnostic_manager())
            {
                const auto &diagnostics = compiler.diagnostic_manager()->diagnostics();
                for (const auto &diagnostic : diagnostics)
                {
                    lsp_diagnostics.push_back(convertCryoDiagnosticToLSP(diagnostic));
                }
            }
        }
        catch (const std::exception &e)
        {
            Logger::instance().error("CryoAnalyzer", "Error extracting diagnostics: {}", e.what());
        }

        return lsp_diagnostics;
    }

    LSPDiagnostic CryoAnalyzer::convertCryoDiagnosticToLSP(const Cryo::Diagnostic &diagnostic)
    {
        LSPDiagnostic lsp_diag;

        // Convert positions
        lsp_diag.start = convertSourceLocationToPosition(diagnostic.range().start);
        lsp_diag.end = convertSourceLocationToPosition(diagnostic.range().end);

        // Convert message
        lsp_diag.message = diagnostic.message();

        // Convert severity
        switch (diagnostic.severity())
        {
        case Cryo::DiagnosticSeverity::Error:
        case Cryo::DiagnosticSeverity::Fatal:
            lsp_diag.severity = "error";
            break;
        case Cryo::DiagnosticSeverity::Warning:
            lsp_diag.severity = "warning";
            break;
        case Cryo::DiagnosticSeverity::Note:
            lsp_diag.severity = "info";
            break;
        default:
            lsp_diag.severity = "info";
            break;
        }

        lsp_diag.source = "cryo-lsp";

        // Convert diagnostic ID to code
        switch (diagnostic.id())
        {
        case Cryo::DiagnosticID::UndefinedVariable:
            lsp_diag.code = "undefined_variable";
            break;
        case Cryo::DiagnosticID::UndefinedFunction:
            lsp_diag.code = "undefined_function";
            break;
        case Cryo::DiagnosticID::TypeMismatch:
            lsp_diag.code = "type_mismatch";
            break;
        case Cryo::DiagnosticID::RedefinedSymbol:
            lsp_diag.code = "redefined_symbol";
            break;
        case Cryo::DiagnosticID::ExpectedToken:
            lsp_diag.code = "expected_token";
            break;
        case Cryo::DiagnosticID::UnexpectedToken:
            lsp_diag.code = "unexpected_token";
            break;
        default:
            lsp_diag.code = "compilation_error";
            break;
        }

        return lsp_diag;
    }

    Position CryoAnalyzer::convertSourceLocationToPosition(const Cryo::SourceLocation &loc)
    {
        Position pos;
        pos.line = loc.line() > 0 ? loc.line() - 1 : 0; // LSP is 0-based, Cryo is 1-based
        pos.character = loc.column() > 0 ? loc.column() - 1 : 0;
        return pos;
    }

    // ========================================
    // Enhanced Symbol Resolution
    // ========================================

    std::optional<Cryo::Symbol *> CryoAnalyzer::findQualifiedSymbol(const Cryo::SymbolTable &symbol_table, const std::string &qualified_name)
    {
        try
        {
            // Parse qualified name (e.g., "std::IO::println" -> namespace="std::IO", symbol="println")
            size_t last_scope = qualified_name.rfind("::");
            if (last_scope == std::string::npos)
            {
                // Not a qualified name, fallback to regular lookup
                return findSymbolInSymbolTable(symbol_table, qualified_name);
            }

            std::string namespace_path = qualified_name.substr(0, last_scope);
            std::string symbol_name = qualified_name.substr(last_scope + 2);

            Logger::instance().debug("CryoAnalyzer", "Looking for symbol '{}' in namespace '{}'", symbol_name, namespace_path);

            // Try direct namespaced lookup
            auto symbol = symbol_table.lookup_namespaced_symbol(namespace_path, symbol_name);
            if (symbol)
            {
                return symbol;
            }

            // Try breaking down nested namespaces (e.g., "std::IO" -> try "std" then "IO")
            std::vector<std::string> namespace_parts;
            std::stringstream ss(namespace_path);
            std::string part;
            while (std::getline(ss, part, ':'))
            {
                if (!part.empty() && part != ":")
                {
                    namespace_parts.push_back(part);
                }
            }

            // Try each namespace part
            for (const auto &ns_part : namespace_parts)
            {
                symbol = symbol_table.lookup_namespaced_symbol(ns_part, symbol_name);
                if (symbol)
                {
                    Logger::instance().debug("CryoAnalyzer", "Found '{}' in namespace '{}'", symbol_name, ns_part);
                    return symbol;
                }
            }
        }
        catch (const std::exception &e)
        {
            Logger::instance().debug("CryoAnalyzer", "Exception in qualified symbol lookup: {}", e.what());
        }

        return std::nullopt;
    }

    std::optional<Cryo::Symbol *> CryoAnalyzer::findSymbolWithImports(const Cryo::SymbolTable &symbol_table, const std::string &symbol_name, const std::vector<std::string> &imported_namespaces)
    {
        try
        {
            // Try each imported namespace
            for (const auto &ns : imported_namespaces)
            {
                auto symbol = symbol_table.lookup_namespaced_symbol(ns, symbol_name);
                if (symbol)
                {
                    Logger::instance().debug("CryoAnalyzer", "Found '{}' in imported namespace '{}'", symbol_name, ns);
                    return symbol;
                }
            }

            // Also try the import-aware lookup if available
            auto symbol = symbol_table.lookup_symbol_with_import_resolution(symbol_name, imported_namespaces);
            if (symbol)
            {
                return symbol;
            }
        }
        catch (const std::exception &e)
        {
            Logger::instance().debug("CryoAnalyzer", "Exception in import-aware lookup: {}", e.what());
        }

        return std::nullopt;
    }

    HoverInfo CryoAnalyzer::buildEnhancedHoverInfo(const Cryo::Symbol &symbol, const std::string &word, const std::string &file_path)
    {
        HoverInfo info;

        info.name = word;
        info.scope = symbol.scope;

        // Build qualified name
        if (!symbol.scope.empty() && symbol.scope != "Global")
        {
            info.qualified_name = symbol.scope + "::" + symbol.name;
        }
        else
        {
            info.qualified_name = symbol.name;
        }

        // Extract type information with better error handling
        if (symbol.data_type)
        {
            try
            {
                info.type = symbol.data_type->to_string();
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
        switch (symbol.kind)
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

        // Build enhanced signature
        info.signature = buildQualifiedSignature(symbol, file_path);

        // Add documentation
        info.documentation = getSymbolDocumentation(symbol, word);

        // Convert source location if available
        if (symbol.declaration_location.line() > 0)
        {
            info.definition_location = convertSourceLocationToPosition(symbol.declaration_location);
        }

        Logger::instance().debug("CryoAnalyzer", "Built enhanced hover info for '{}': {}", word, info.signature);
        return info;
    }

    std::string CryoAnalyzer::buildQualifiedSignature(const Cryo::Symbol &symbol, const std::string &file_path)
    {
        std::string signature;

        switch (symbol.kind)
        {
        case Cryo::SymbolKind::Function:
            {
                // Build qualified function name
                std::string qualified_name = symbol.name;
                if (!symbol.scope.empty() && symbol.scope != "Global")
                {
                    qualified_name = symbol.scope + "::" + symbol.name;
                }
                
                if (symbol.data_type && symbol.data_type->to_string().find("(") != std::string::npos)
                {
                    // Function with signature: "function std::IO::println(string) -> i64"
                    signature = "function " + qualified_name + symbol.data_type->to_string();
                }
                else
                {
                    // Function without detailed signature
                    signature = "function " + qualified_name + "(...)";
                }
            }
            break;
        case Cryo::SymbolKind::Variable:
            {
                // Check if variable is mutable by looking in the AST
                bool is_mutable = findVariableMutability(file_path, symbol.name);
                std::string mutability_prefix = is_mutable ? "mut " : "const ";
                signature = mutability_prefix + symbol.name + ": " + (symbol.data_type ? symbol.data_type->to_string() : "unknown");
            }
            break;
        case Cryo::SymbolKind::Type:
            {
                std::string qualified_name = symbol.name;
                if (!symbol.scope.empty() && symbol.scope != "Global")
                {
                    qualified_name = symbol.scope + "::" + symbol.name;
                }
                signature = "type " + qualified_name;
                if (symbol.data_type)
                {
                    signature += " = " + symbol.data_type->to_string();
                }
            }
            break;
        case Cryo::SymbolKind::Intrinsic:
            {
                std::string qualified_name = symbol.name;
                if (!symbol.scope.empty() && symbol.scope != "Global")
                {
                    qualified_name = symbol.scope + "::" + symbol.name;
                }
                signature = "intrinsic " + qualified_name;
                if (symbol.data_type)
                {
                    signature += symbol.data_type->to_string();
                }
            }
            break;
        default:
            {
                std::string qualified_name = symbol.name;
                if (!symbol.scope.empty() && symbol.scope != "Global")
                {
                    qualified_name = symbol.scope + "::" + symbol.name;
                }
                signature = qualified_name + ": " + (symbol.data_type ? symbol.data_type->to_string() : "unknown");
            }
            break;
        }

        return signature;
    }

    bool CryoAnalyzer::findVariableMutability(const std::string &file_path, const std::string &variable_name)
    {
        auto it = analyzed_files_.find(file_path);
        if (it == analyzed_files_.end() || !it->second.ast)
        {
            return false; // Default to const if we can't find the AST
        }

        // Traverse the AST to find the variable declaration
        return findVariableInAST(it->second.ast, variable_name);
    }

    bool CryoAnalyzer::findVariableInAST(Cryo::ASTNode *node, const std::string &variable_name)
    {
        if (!node) return false;

        // Check if this is a variable declaration node
        if (auto var_decl = dynamic_cast<Cryo::VariableDeclarationNode*>(node))
        {
            if (var_decl->name() == variable_name)
            {
                return var_decl->is_mutable();
            }
        }

        // Check if this is a function declaration - look in parameters
        if (auto func_decl = dynamic_cast<Cryo::FunctionDeclarationNode*>(node))
        {
            for (const auto& param : func_decl->parameters())
            {
                if (param->name() == variable_name)
                {
                    return param->is_mutable();
                }
            }
            // Also check the function body
            if (func_decl->body())
            {
                if (findVariableInAST(func_decl->body(), variable_name))
                {
                    return true;
                }
            }
        }

        // Check if this is a block statement - recurse into statements
        if (auto block = dynamic_cast<Cryo::BlockStatementNode*>(node))
        {
            for (const auto& stmt : block->statements())
            {
                if (findVariableInAST(stmt.get(), variable_name))
                {
                    return true;
                }
            }
        }

        // Check if this is a declaration statement
        if (auto decl_stmt = dynamic_cast<Cryo::DeclarationStatementNode*>(node))
        {
            if (findVariableInAST(decl_stmt->declaration(), variable_name))
            {
                return true;
            }
        }

        // Check if this is a program node - recurse into statements
        if (auto program = dynamic_cast<Cryo::ProgramNode*>(node))
        {
            for (const auto& stmt : program->statements())
            {
                if (findVariableInAST(stmt.get(), variable_name))
                {
                    return true;
                }
            }
        }

        return false; // Default to const if not found
    }

    std::string CryoAnalyzer::getSymbolDocumentation(const Cryo::Symbol &symbol, const std::string &symbol_name)
    {
        // Enhanced symbol documentation
        static const std::unordered_map<std::string, std::string> enhanced_docs = {
            // IO namespace
            {"std::IO::println", "Prints a line to standard output with a newline. Commonly used for console output."},
            {"std::IO::print", "Prints text to standard output without a newline."},
            {"std::IO::read", "Reads input from standard input stream."},
            {"std::IO::readLine", "Reads a complete line from standard input."},
            
            // Core namespace
            {"std::core::length", "Returns the length of a collection or string."},
            {"std::core::size", "Returns the size in elements of a collection."},
            
            // Memory management
            {"std::memory::alloc", "Allocates memory on the heap. Use with caution."},
            {"std::memory::free", "Frees previously allocated memory."},
            
            // Standard functions
            {"println", "Prints a line to standard output. Part of the IO module."},
            {"print", "Prints text to standard output without newline."},
            {"main", "Entry point function for CryoLang programs. Must return int (exit code)."},
        };

        // Check for specific documentation
        std::string qualified_key = symbol.scope.empty() || symbol.scope == "Global" 
            ? symbol_name 
            : symbol.scope + "::" + symbol_name;
            
        auto it = enhanced_docs.find(qualified_key);
        if (it != enhanced_docs.end())
        {
            return it->second;
        }

        // Check for just the symbol name
        it = enhanced_docs.find(symbol_name);
        if (it != enhanced_docs.end())
        {
            return it->second;
        }

        // Fallback to primitive type documentation
        if (symbol.kind == Cryo::SymbolKind::Type)
        {
            return getPrimitiveTypeDocumentation(symbol_name);
        }

        // Default documentation based on kind and scope
        std::string doc;
        // Simple documentation without verbose namespace information

        switch (symbol.kind)
        {
        case Cryo::SymbolKind::Function:
            // No additional documentation for functions - signature is sufficient
            break;
        case Cryo::SymbolKind::Variable:
            // No additional documentation for variables - signature is sufficient
            break;
        case Cryo::SymbolKind::Type:
            doc += doc.empty() ? "Type definition" : " - Type definition";
            break;
        case Cryo::SymbolKind::Intrinsic:
            doc += doc.empty() ? "Built-in intrinsic function" : " - Built-in intrinsic function";
            break;
        }

        return doc;
    }

    // ========================================
    // Enhanced Scope Support
    // ========================================

    std::optional<HoverInfo> CryoAnalyzer::getQualifiedSymbolInfo(const std::string &file_path, const std::string &qualified_name)
    {
        if (!analyzed_files_.count(file_path) || !analyzed_files_[file_path].compiler)
        {
            return std::nullopt;
        }

        const auto &analysis = analyzed_files_[file_path];
        if (!analysis.compiler->symbol_table())
        {
            return std::nullopt;
        }

        auto symbol = findQualifiedSymbol(*analysis.compiler->symbol_table(), qualified_name);
        if (symbol && symbol.value())
        {
            return buildEnhancedHoverInfo(*symbol.value(), qualified_name, file_path);
        }

        return std::nullopt;
    }

    std::vector<std::string> CryoAnalyzer::getAvailableNamespaces(const std::string &file_path)
    {
        std::vector<std::string> namespaces;

        if (analyzed_files_.count(file_path) && analyzed_files_[file_path].compiler && analyzed_files_[file_path].compiler->symbol_table())
        {
            const auto &ns_map = analyzed_files_[file_path].compiler->symbol_table()->get_namespaces();
            for (const auto &[ns_name, symbols] : ns_map)
            {
                namespaces.push_back(ns_name);
            }
        }

        return namespaces;
    }

    std::vector<HoverInfo> CryoAnalyzer::getAllSymbolsInScope(const std::string &file_path, const std::string &scope)
    {
        std::vector<HoverInfo> symbols;

        if (!analyzed_files_.count(file_path) || !analyzed_files_[file_path].compiler || !analyzed_files_[file_path].compiler->symbol_table())
        {
            return symbols;
        }

        const auto &symbol_table = *analyzed_files_[file_path].compiler->symbol_table();

        try
        {
            if (scope.empty())
            {
                // Get all symbols in global scope
                const auto &global_symbols = symbol_table.get_symbols();
                for (const auto &[name, symbol] : global_symbols)
                {
                    symbols.push_back(buildEnhancedHoverInfo(symbol, name, file_path));
                }
            }
            else
            {
                // Get symbols in specific namespace
                const auto &namespaces = symbol_table.get_namespaces();
                auto ns_it = namespaces.find(scope);
                if (ns_it != namespaces.end())
                {
                    for (const auto &[name, symbol] : ns_it->second)
                    {
                        symbols.push_back(buildEnhancedHoverInfo(symbol, name, file_path));
                    }
                }
            }
        }
        catch (const std::exception &e)
        {
            Logger::instance().error("CryoAnalyzer", "Error getting symbols in scope '{}': {}", scope, e.what());
        }

        return symbols;
    }

    // ========================================
    // Helper Methods
    // ========================================

    std::string CryoAnalyzer::extractQualifiedSymbolAtPosition(const std::string &line, const Position &position)
    {
        // Extract a qualified symbol like "std::IO::println" from the line at the given position
        if (position.character >= static_cast<int>(line.length()))
        {
            return "";
        }

        // Find the boundaries of the qualified symbol
        int start = position.character;
        int end = position.character;

        // Move start back to find the beginning of the qualified symbol
        while (start > 0)
        {
            char c = line[start - 1];
            if (std::isalnum(c) || c == '_' || c == ':')
            {
                start--;
            }
            else
            {
                break;
            }
        }

        // Move end forward to find the end of the qualified symbol
        while (end < static_cast<int>(line.length()))
        {
            char c = line[end];
            if (std::isalnum(c) || c == '_' || c == ':')
            {
                end++;
            }
            else
            {
                break;
            }
        }

        if (end > start)
        {
            std::string qualified = line.substr(start, end - start);
            
            // Clean up any leading/trailing colons
            while (!qualified.empty() && qualified.front() == ':')
            {
                qualified.erase(0, 1);
            }
            while (!qualified.empty() && qualified.back() == ':')
            {
                qualified.pop_back();
            }
            
            return qualified;
        }

        return "";
    }

} // namespace CryoLSP