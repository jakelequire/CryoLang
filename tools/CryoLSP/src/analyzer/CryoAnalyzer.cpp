#include "../../include/analyzer/CryoAnalyzer.hpp"
#include "../../include/Logger.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>  // for std::replace
#include <cstdlib>    // for std::getenv
#include <functional> // for std::hash
#include <chrono>     // for rate limiting
#include <thread>     // for std::this_thread::sleep_for
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
                    // File content unchanged, reusing analysis
                    return true;
                }
                // Content changed, cleanup old compiler instance before creating new one
                if (existing.compiler)
                {
                    // Cleaning up previous compiler instance
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
            analysis.compiler->set_stdlib_linking(true); // Enable stdlib for imports like <io/stdio>
            
            Logger::instance().debug("CryoAnalyzer", "Created compiler instance with stdlib support for: {}", file_path);

            // Enhanced crash protection: Try compilation with comprehensive error handling
            bool should_skip_compilation = false;
            std::string skip_reason = "";
            
            // Check file size first (files over 50KB are more likely to cause issues)
            if (content.size() > 50000) {
                should_skip_compilation = true;
                skip_reason = "File too large (" + std::to_string(content.size()) + " bytes)";
            }
            
            if (should_skip_compilation) {
                Logger::instance().warn("CryoAnalyzer", "Skipping compilation for: {} - {}", file_path, skip_reason);
                analysis.parsed_successfully = false;
                
                // Add a diagnostic explaining why we skipped it
                LSPDiagnostic skip_diag;
                skip_diag.start = {0, 0};
                skip_diag.end = {0, 0};
                skip_diag.message = "File skipped: " + skip_reason + " (basic hover still available)";
                skip_diag.severity = "info";
                skip_diag.source = "cryo-lsp";
                skip_diag.code = "skip_compilation";
                analysis.diagnostics.push_back(skip_diag);
                
                return true; // Return true to allow basic hover to work
            }

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
                Logger::instance().debug("CryoAnalyzer", "Starting compilation with enhanced protection for: {}", file_path);
                
                // Add timeout protection - if compilation takes more than 5 seconds, abort
                const auto COMPILATION_TIMEOUT = std::chrono::seconds(5);
                
                // Attempt compilation in a controlled manner
                compilation_success = analysis.compiler->compile_frontend_only(temp_file);
                
                auto elapsed = std::chrono::steady_clock::now() - start_time;
                if (elapsed > COMPILATION_TIMEOUT)
                {
                    Logger::instance().warn("CryoAnalyzer", "Compilation timeout for: {} ({}ms)", file_path, 
                        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
                    compilation_success = false;
                }
                else if (compilation_success)
                {
                    // Compilation completed successfully
                }
                else
                {
                    Logger::instance().debug("CryoAnalyzer", "Compilation failed but process completed safely in {}ms for: {}", 
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
                                    // Found namespaces
                                    
                                    for (const auto &[ns_name, symbols] : namespaces)
                                    {
                                        if (!ns_name.empty() && symbols.size() < 1000) // Sanity check
                                        {
                                            // Namespace symbols available
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
                
                // Add specific diagnostic for memory issues
                LSPDiagnostic memory_diag;
                memory_diag.start = {0, 0};
                memory_diag.end = {0, 0};
                memory_diag.message = "Compilation failed: Out of memory";
                memory_diag.severity = "error";
                memory_diag.source = "cryo-lsp";
                memory_diag.code = "memory_error";
                analysis.diagnostics.push_back(memory_diag);
            }
            catch (const std::runtime_error &e)
            {
                Logger::instance().error("CryoAnalyzer", "Runtime error during compilation of {}: {}", file_path, e.what());
                analysis.parsed_successfully = true; // Allow fallback hover
                
                // Add specific diagnostic for runtime issues
                LSPDiagnostic runtime_diag;
                runtime_diag.start = {0, 0};
                runtime_diag.end = {0, 0};
                runtime_diag.message = "Compilation warning: " + std::string(e.what()) + " (basic hover available)";
                runtime_diag.severity = "warning";
                runtime_diag.source = "cryo-lsp";
                runtime_diag.code = "runtime_error";
                analysis.diagnostics.push_back(runtime_diag);
            }
            catch (const std::exception &e)
            {
                Logger::instance().error("CryoAnalyzer", "Exception during compilation of {}: {}", file_path, e.what());
                analysis.parsed_successfully = true; // Allow fallback hover

                // Add more informative diagnostic based on error type
                LSPDiagnostic error_diag;
                error_diag.start = {0, 0};
                error_diag.end = {0, 0};
                std::string error_msg = std::string(e.what());
                if (error_msg.find("symbol") != std::string::npos || error_msg.find("undefined") != std::string::npos) {
                    error_diag.message = "Symbol resolution issue: " + error_msg;
                    error_diag.severity = "warning";
                } else if (error_msg.find("syntax") != std::string::npos || error_msg.find("parse") != std::string::npos) {
                    error_diag.message = "Syntax error: " + error_msg;
                    error_diag.severity = "error";
                } else {
                    error_diag.message = "Compilation issue: " + error_msg + " (basic hover still available)";
                    error_diag.severity = "warning";
                }
                error_diag.source = "cryo-lsp";
                error_diag.code = "compilation_error";
                analysis.diagnostics.push_back(error_diag);
            }
            catch (...)
            {
                Logger::instance().error("CryoAnalyzer", "Unknown exception during compilation of: {}", file_path);
                analysis.parsed_successfully = false;
                
                // Add diagnostic for unknown errors
                LSPDiagnostic unknown_diag;
                unknown_diag.start = {0, 0};
                unknown_diag.end = {0, 0};
                unknown_diag.message = "Unknown compilation error (file may have syntax issues)";
                unknown_diag.severity = "error";
                unknown_diag.source = "cryo-lsp";
                unknown_diag.code = "unknown_error";
                analysis.diagnostics.push_back(unknown_diag);
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

        // Hover request received

        // Check if the position is within a comment - if so, don't show hover
        if (file_contents_.count(file_path) && isPositionInComment(file_contents_[file_path], position))
        {
            Logger::instance().info("CryoAnalyzer", "*** COMMENT DETECTED *** Position {}:{} is within a comment, skipping hover", position.line, position.character);
            return std::nullopt;
        }

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

        // Check if the position is within a comment
        if (isPositionInComment(analysis.content, position))
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

        // Check if this is a qualified symbol (contains :: or .)
        std::string line = getLineAtPosition(analysis.content, position);
        std::string qualified_symbol = extractQualifiedSymbolAtPosition(line, position);
        
        // Check if this is a member access pattern (object.method)
        std::string member_access_context = extractMemberAccessContext(line, position);
        if (!member_access_context.empty()) {
            HoverInfo member_info = analyzeMemberAccess(analysis, member_access_context, word, file_path);
            if (!member_info.name.empty()) {
                return member_info;
            }
        }
        
        Logger::instance().debug("CryoAnalyzer", "Analyzing symbol '{}', qualified form: '{}', line: '{}'", word, qualified_symbol, line);

        // Try to find symbol in symbol table with better error handling
        try
        {
            if (analysis.compiler && analysis.compiler->symbol_table())
            {
                Cryo::Symbol *symbol = nullptr;

                // Try qualified lookup if we have a qualified symbol
                if (!qualified_symbol.empty() && qualified_symbol != word)
                {
                    symbol = findQualifiedSymbol(*analysis.compiler->symbol_table(), qualified_symbol).value_or(nullptr);
                }

                // If qualified lookup failed or we don't have a qualified symbol, try regular lookup
                if (!symbol)
                {
                    symbol = findSymbolInSymbolTable(*analysis.compiler->symbol_table(), word).value_or(nullptr);
                }

                // If still not found, try namespace-aware lookup with common imports
                if (!symbol)
                {
                    std::vector<std::string> common_imports = {"std", "io", "core", "runtime"};
                    symbol = findSymbolWithImports(*analysis.compiler->symbol_table(), word, common_imports).value_or(nullptr);
                }

                if (symbol)
                {
                    Logger::instance().debug("CryoAnalyzer", "Found symbol '{}' in symbol table", word);
                    return buildEnhancedHoverInfo(*symbol, !qualified_symbol.empty() ? qualified_symbol : word, file_path, analysis.compiler.get());
                }
                else
                {
                    Logger::instance().debug("CryoAnalyzer", "Symbol '{}' not found in symbol table", word);
                }
                
                // Try TypeChecker symbol table for more detailed function information
                if (analysis.compiler && analysis.compiler->type_checker())
                {
                    Logger::instance().debug("CryoAnalyzer", "Checking TypeChecker symbol table for word='{}', qualified='{}'", word, qualified_symbol);
                    
                    // First try looking up the simple name
                    auto typed_symbol = analysis.compiler->type_checker()->lookup_symbol(word);
                    Logger::instance().debug("CryoAnalyzer", "Simple lookup for '{}': {}", word, typed_symbol ? "found" : "not found");
                    
                    // If not found and we have a qualified symbol, try that too
                    if (!typed_symbol && !qualified_symbol.empty() && qualified_symbol != word)
                    {
                        Logger::instance().debug("CryoAnalyzer", "Trying qualified lookup in TypeChecker for '{}'", qualified_symbol);
                        typed_symbol = analysis.compiler->type_checker()->lookup_symbol(qualified_symbol);
                        Logger::instance().debug("CryoAnalyzer", "Qualified lookup for '{}': {}", qualified_symbol, typed_symbol ? "found" : "not found");
                    }
                    
                    // Also try namespace-aware lookup
                    if (!typed_symbol)
                    {
                        Logger::instance().debug("CryoAnalyzer", "Trying namespace-aware lookup in TypeChecker for '{}'", word);
                        typed_symbol = analysis.compiler->type_checker()->lookup_symbol_in_any_namespace(word);
                        Logger::instance().debug("CryoAnalyzer", "Namespace lookup for '{}': {}", word, typed_symbol ? "found" : "not found");
                    }
                    
                    // If we have qualified_symbol like "IO::println", try just the function part
                    if (!typed_symbol && !qualified_symbol.empty())
                    {
                        size_t pos = qualified_symbol.find_last_of("::");
                        if (pos != std::string::npos && pos > 1)
                        {
                            std::string function_name = qualified_symbol.substr(pos + 1);
                            Logger::instance().debug("CryoAnalyzer", "Trying function part lookup '{}' from qualified '{}'", function_name, qualified_symbol);
                            typed_symbol = analysis.compiler->type_checker()->lookup_symbol(function_name);
                            Logger::instance().debug("CryoAnalyzer", "Function part lookup for '{}': {}", function_name, typed_symbol ? "found" : "not found");
                        }
                    }
                    
                    if (typed_symbol && typed_symbol->function_node)
                    {
                        Logger::instance().debug("CryoAnalyzer", "Found function with AST node in TypeChecker: '{}'", word);
                        HoverInfo info;
                        info.name = word;
                        info.kind = "function";
                        info.signature = extractParameterNamesFromAST(typed_symbol->function_node);
                        info.type = typed_symbol->type ? typed_symbol->type->to_string() : "unknown";
                        info.qualified_name = !qualified_symbol.empty() ? qualified_symbol : word;
                        info.documentation = extractDocumentationFromASTNode(typed_symbol->function_node);
                        if (typed_symbol->declaration_location.line() > 0)
                        {
                            info.definition_location = convertSourceLocationToPosition(typed_symbol->declaration_location);
                        }
                        return info;
                    }
                    else if (typed_symbol)
                    {
                        Logger::instance().debug("CryoAnalyzer", "Found symbol in TypeChecker but no function_node, falling back to SymbolTable lookup: '{}'", word);
                        // Let the main SymbolTable lookup handle this case with the enhanced buildEnhancedHoverInfo
                    }
                    else
                    {
                        Logger::instance().debug("CryoAnalyzer", "No symbol found in TypeChecker for any lookup attempt");
                    }
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

    std::string CryoAnalyzer::extractFunctionSignatureFromSource(const std::string &file_path, const std::string &function_name)
    {
        Logger::instance().debug("CryoAnalyzer", "Attempting to extract function signature for '{}' from '{}'", function_name, file_path);
        try {
            // Fix file path - convert backslashes to forward slashes and handle drive letters
            std::string fixed_path = file_path;
            if (fixed_path.starts_with("\\") && fixed_path.length() > 3 && fixed_path[2] == ':') {
                // Handle paths like "\c:\..." - remove leading backslash
                fixed_path = fixed_path.substr(1);
            }
            // Convert all backslashes to forward slashes
            std::replace(fixed_path.begin(), fixed_path.end(), '\\', '/');
            
            Logger::instance().debug("CryoAnalyzer", "Fixed file path: '{}'", fixed_path);
            
            std::ifstream file(fixed_path);
            if (!file.is_open()) {
                Logger::instance().debug("CryoAnalyzer", "Failed to open file: {}", fixed_path);
                return "";
            }
            
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();
            
            // Pattern to match function declaration with full parameter names
            // Handle both simple function names and qualified names (e.g., IO::println -> println)
            std::string search_name = function_name;
            size_t last_colon = function_name.find_last_of(':');
            if (last_colon != std::string::npos && last_colon < function_name.length() - 1) {
                search_name = function_name.substr(last_colon + 1);
                Logger::instance().debug("CryoAnalyzer", "Extracted function name '{}' from qualified name '{}'", search_name, function_name);
            }
            
            std::regex func_pattern(R"(\bfunction\s+)" + search_name + R"(\s*\(([^)]*)\)\s*(?:->\s*([^;{]+))?)");
            std::smatch match;
            
            if (std::regex_search(content, match, func_pattern)) {
                std::string params = match[1].str();
                std::string return_type = match.size() > 2 ? match[2].str() : "";
                
                Logger::instance().debug("CryoAnalyzer", "Found function '{}' with params: '{}', return: '{}'", function_name, params, return_type);
                
                // Clean up whitespace
                if (!params.empty()) {
                    std::regex ws_pattern(R"(\s+)");
                    params = std::regex_replace(params, ws_pattern, " ");
                    size_t start = params.find_first_not_of(" \t\n\r\f\v");
                    size_t end = params.find_last_not_of(" \t\n\r\f\v");
                    if (start != std::string::npos && end != std::string::npos) {
                        params = params.substr(start, end - start + 1);
                    }
                }
                
                if (!return_type.empty()) {
                    return_type.erase(return_type.find_last_not_of(" \t\n\r\f\v") + 1);
                    std::string result = "function " + function_name + "(" + params + ") -> " + return_type;
                    Logger::instance().debug("CryoAnalyzer", "Extracted signature: '{}'", result);
                    return result;
                } else {
                    std::string result = "function " + function_name + "(" + params + ")";
                    Logger::instance().debug("CryoAnalyzer", "Extracted signature: '{}'", result);
                    return result;
                }
            } else {
                Logger::instance().debug("CryoAnalyzer", "No function pattern match found for '{}'", function_name);
            }
            
            return "";
        } catch (const std::exception &e) {
            Logger::instance().debug("CryoAnalyzer", "Error extracting function signature: {}", e.what());
            return "";
        }
    }

    std::string CryoAnalyzer::extractParameterNamesFromAST(Cryo::FunctionDeclarationNode *function_node)
    {
        if (!function_node) {
            return "";
        }
        
        std::vector<std::string> param_strs;
        const auto &parameters = function_node->parameters();
        
        for (const auto &param : parameters) {
            if (param) {
                std::string param_name = param->name();
                std::string param_type = param->type_annotation();
                
                // Format as "name: type"
                param_strs.push_back(param_name + ": " + param_type);
            }
        }
        
        // Build complete signature
        std::string params = "";
        if (!param_strs.empty()) {
            params = param_strs[0];
            for (size_t i = 1; i < param_strs.size(); ++i) {
                params += ", " + param_strs[i];
            }
        }
        
        std::string return_type = function_node->return_type_annotation();
        std::string result = "function " + function_node->name() + "(" + params + ")";
        
        if (return_type != "void") {
            result += " -> " + return_type;
        }
        
        Logger::instance().debug("CryoAnalyzer", "Extracted AST signature: '{}'", result);
        return result;
    }

    std::string CryoAnalyzer::extractParameterNamesFromAST(Cryo::FunctionDeclarationNode *function_node, const std::string &qualified_name)
    {
        if (!function_node) {
            return "";
        }
        
        std::vector<std::string> param_strs;
        const auto &parameters = function_node->parameters();
        
        for (const auto &param : parameters) {
            if (param) {
                std::string param_name = param->name();
                std::string param_type = param->type_annotation();
                
                // Format as "name: type"
                param_strs.push_back(param_name + ": " + param_type);
            }
        }
        
        // Build complete signature with qualified name
        std::string params = "";
        if (!param_strs.empty()) {
            params = param_strs[0];
            for (size_t i = 1; i < param_strs.size(); ++i) {
                params += ", " + param_strs[i];
            }
        }
        
        std::string return_type = function_node->return_type_annotation();
        std::string result = "function " + qualified_name + "(" + params + ")";
        
        if (return_type != "void") {
            result += " -> " + return_type;
        }
        
        Logger::instance().debug("CryoAnalyzer", "Extracted AST signature with qualified name: '{}'", result);
        return result;
    }

    Cryo::FunctionDeclarationNode* CryoAnalyzer::findFunctionInImportedModules(Cryo::CompilerInstance *compiler, const std::string &word, const std::string &qualified_symbol)
    {
        if (!compiler || !compiler->module_loader()) {
            return nullptr;
        }
        
        const auto& imported_asts = compiler->module_loader()->get_imported_asts();
        
        for (const auto& [module_name, ast] : imported_asts) {
            if (!ast) {
                continue;
            }
            
            auto function_node = findFunctionInAST(ast.get(), word, qualified_symbol, module_name);
            if (function_node) {
                return function_node;
            }
        }
        
        return nullptr;
    }

    Cryo::FunctionDeclarationNode* CryoAnalyzer::findFunctionInAST(Cryo::ASTNode *node, const std::string &word, const std::string &qualified_symbol, const std::string &module_name)
    {
        if (!node) {
            return nullptr;
        }
        
        // Check if this is a function declaration node
        if (auto func_decl = dynamic_cast<Cryo::FunctionDeclarationNode*>(node)) {
            std::string func_name = func_decl->name();
            Logger::instance().debug("CryoAnalyzer", "Checking function '{}' against search term '{}' (qualified: '{}')", 
                func_name, word, qualified_symbol);
            
            // Try multiple matching strategies
            bool matches = false;
            
            // 1. Direct name match
            if (func_name == word) {
                matches = true;
                Logger::instance().debug("CryoAnalyzer", "Direct name match for '{}'", func_name);
            }
            
            // 2. Qualified name match (e.g., "IO::println" matches "println" in std::IO module)
            if (!matches && !qualified_symbol.empty()) {
                size_t pos = qualified_symbol.find_last_of("::");
                if (pos != std::string::npos && pos > 1) {
                    std::string function_part = qualified_symbol.substr(pos + 1);
                    if (func_name == function_part) {
                        std::string namespace_part = qualified_symbol.substr(0, pos - 1);
                        // Check if this module corresponds to the namespace
                        if (module_name.find(namespace_part) != std::string::npos || 
                            module_name == "std::IO" && namespace_part == "IO") {
                            matches = true;
                        }
                    }
                }
            }
            
            if (matches) {
                Logger::instance().debug("CryoAnalyzer", "Found matching function: '{}' for search term '{}'", func_name, word);
                return func_decl;
            }
        }
        
        // For other node types, recursively search children
        if (auto program = dynamic_cast<Cryo::ProgramNode*>(node)) {
            for (const auto& stmt : program->statements()) {
                if (auto result = findFunctionInAST(stmt.get(), word, qualified_symbol, module_name)) {
                    return result;
                }
            }
        }
        
        return nullptr;
    }

    Cryo::StructDeclarationNode* CryoAnalyzer::findStructDeclarationInAST(Cryo::ASTNode *node, const std::string &struct_name)
    {
        if (!node) {
            return nullptr;
        }
        
        // Check if this is a struct declaration node
        if (auto struct_decl = dynamic_cast<Cryo::StructDeclarationNode*>(node)) {
            if (struct_decl->name() == struct_name) {
                return struct_decl;
            }
        }
        
        // For program nodes, recursively search children
        if (auto program = dynamic_cast<Cryo::ProgramNode*>(node)) {
            for (const auto& stmt : program->statements()) {
                if (auto result = findStructDeclarationInAST(stmt.get(), struct_name)) {
                    return result;
                }
            }
        }
        
        return nullptr;
    }

    Cryo::ClassDeclarationNode* CryoAnalyzer::findClassDeclarationInAST(Cryo::ASTNode *node, const std::string &class_name)
    {
        if (!node) {
            return nullptr;
        }
        
        // Check if this is a class declaration node
        if (auto class_decl = dynamic_cast<Cryo::ClassDeclarationNode*>(node)) {
            if (class_decl->name() == class_name) {
                return class_decl;
            }
        }
        
        // For program nodes, recursively search children
        if (auto program = dynamic_cast<Cryo::ProgramNode*>(node)) {
            for (const auto& stmt : program->statements()) {
                if (auto result = findClassDeclarationInAST(stmt.get(), class_name)) {
                    return result;
                }
            }
        }
        
        return nullptr;
    }

    std::string CryoAnalyzer::buildStructPreview(Cryo::StructDeclarationNode *struct_node, const std::string &qualified_name)
    {
        if (!struct_node) {
            return "";
        }
        
        std::string preview = "type struct " + qualified_name;
        
        // Add generic parameters if any
        const auto& generics = struct_node->generic_parameters();
        if (!generics.empty()) {
            preview += "<";
            for (size_t i = 0; i < generics.size(); ++i) {
                if (i > 0) preview += ", ";
                preview += generics[i]->name();
            }
            preview += ">";
        }
        
        preview += " {\n";
        
        // Add fields (max 5 total items)
        const auto& fields = struct_node->fields();
        const auto& methods = struct_node->methods();
        
        int item_count = 0;
        const int max_items = 5;
        
        // Prioritize constructors first
        for (const auto& method : methods) {
            if (item_count >= max_items) break;
            if (method && method->is_constructor()) {
                preview += "    " + method->name() + "(";
                const auto& params = method->parameters();
                for (size_t i = 0; i < params.size(); ++i) {
                    if (i > 0) preview += ", ";
                    preview += params[i]->name() + ": " + params[i]->type_annotation();
                }
                preview += ");\n";
                item_count++;
            }
        }
        
        // Add fields next
        for (const auto& field : fields) {
            if (item_count >= max_items) break;
            if (field) {
                preview += "    " + field->name() + ": " + field->type_annotation() + ";\n";
                item_count++;
            }
        }
        
        // Add non-constructor methods
        for (const auto& method : methods) {
            if (item_count >= max_items) break;
            if (method && !method->is_constructor()) {
                preview += "    " + method->name() + "(";
                const auto& params = method->parameters();
                for (size_t i = 0; i < params.size(); ++i) {
                    if (i > 0) preview += ", ";
                    preview += params[i]->name() + ": " + params[i]->type_annotation();
                }
                preview += ")";
                if (method->return_type_annotation() != "void") {
                    preview += " -> " + method->return_type_annotation();
                }
                preview += ";\n";
                item_count++;
            }
        }
        
        if (fields.size() + methods.size() > max_items) {
            preview += "    // ...\n";
        }
        
        preview += "}";
        
        return preview;
    }

    std::string CryoAnalyzer::buildClassPreview(Cryo::ClassDeclarationNode *class_node, const std::string &qualified_name)
    {
        if (!class_node) {
            return "";
        }
        
        std::string preview = "type class " + qualified_name;
        
        // Add generic parameters if any
        const auto& generics = class_node->generic_parameters();
        if (!generics.empty()) {
            preview += "<";
            for (size_t i = 0; i < generics.size(); ++i) {
                if (i > 0) preview += ", ";
                preview += generics[i]->name();
            }
            preview += ">";
        }
        
        // Add base class if any
        if (!class_node->base_class().empty()) {
            preview += " : " + class_node->base_class();
        }
        
        preview += " {\n";
        
        // Add fields and methods (max 5 total items)
        const auto& fields = class_node->fields();
        const auto& methods = class_node->methods();
        
        int item_count = 0;
        const int max_items = 5;
        
        // Prioritize constructors first
        for (const auto& method : methods) {
            if (item_count >= max_items) break;
            if (method && method->is_constructor()) {
                preview += "    " + method->name() + "(";
                const auto& params = method->parameters();
                for (size_t i = 0; i < params.size(); ++i) {
                    if (i > 0) preview += ", ";
                    preview += params[i]->name() + ": " + params[i]->type_annotation();
                }
                preview += ");\n";
                item_count++;
            }
        }
        
        // Add fields next
        for (const auto& field : fields) {
            if (item_count >= max_items) break;
            if (field) {
                preview += "    " + field->name() + ": " + field->type_annotation() + ";\n";
                item_count++;
            }
        }
        
        // Add non-constructor methods
        for (const auto& method : methods) {
            if (item_count >= max_items) break;
            if (method && !method->is_constructor()) {
                preview += "    " + method->name() + "(";
                const auto& params = method->parameters();
                for (size_t i = 0; i < params.size(); ++i) {
                    if (i > 0) preview += ", ";
                    preview += params[i]->name() + ": " + params[i]->type_annotation();
                }
                preview += ")";
                if (method->return_type_annotation() != "void") {
                    preview += " -> " + method->return_type_annotation();
                }
                preview += ";\n";
                item_count++;
            }
        }
        
        if (fields.size() + methods.size() > max_items) {
            preview += "    // ...\n";
        }
        
        preview += "}";
        
        return preview;
    }

    std::string CryoAnalyzer::extractMemberAccessContext(const std::string &line, const Position &position)
    {
        // Check if we're hovering over a member access pattern like "object.member"
        if (position.character >= static_cast<int>(line.length()) || position.character == 0) {
            return "";
        }
        
        // Look for a dot before the current position
        int dot_pos = -1;
        for (int i = position.character - 1; i >= 0; i--) {
            char c = line[i];
            if (c == '.') {
                dot_pos = i;
                break;
            }
            if (!std::isalnum(c) && c != '_') {
                break; // Hit a non-identifier character before finding a dot
            }
        }
        
        if (dot_pos == -1) {
            return ""; // No dot found
        }
        
        // Extract the object name before the dot
        int start = dot_pos - 1;
        while (start >= 0) {
            char c = line[start];
            if (std::isalnum(c) || c == '_') {
                start--;
            } else {
                break;
            }
        }
        start++; // Move to the first character of the identifier
        
        if (start >= dot_pos) {
            return ""; // No valid identifier before the dot
        }
        
        return line.substr(start, dot_pos - start);
    }

    HoverInfo CryoAnalyzer::analyzeMemberAccess(const FileAnalysis &analysis, const std::string &context, const std::string &member_name, const std::string &file_path)
    {
        HoverInfo info;
        
        if (!analysis.compiler || !analysis.compiler->symbol_table()) {
            return info;
        }
        
        // First, find the type of the object being accessed
        auto object_symbol = analysis.compiler->symbol_table()->lookup_symbol(context);
        if (!object_symbol) {
            return info;
        }
        
        // Get the type information
        if (!object_symbol->data_type) {
            return info;
        }
        
        std::string type_name = object_symbol->data_type->to_string();
        
        // Remove any pointer/reference markers to get the base type
        if (type_name.ends_with("*") || type_name.ends_with("&")) {
            type_name = type_name.substr(0, type_name.length() - 1);
        }
        
        // Find the struct/class declaration for this type
        Cryo::StructDeclarationNode* struct_node = nullptr;
        Cryo::ClassDeclarationNode* class_node = nullptr;
        
        // Check current file first
        if (analysis.ast) {
            struct_node = findStructDeclarationInAST(analysis.ast, type_name);
            if (!struct_node) {
                class_node = findClassDeclarationInAST(analysis.ast, type_name);
            }
        }
        
        // Check imported modules if not found locally
        if (!struct_node && !class_node && analysis.compiler->module_loader()) {
            const auto& imported_asts = analysis.compiler->module_loader()->get_imported_asts();
            for (const auto& [module_name, ast] : imported_asts) {
                if (!ast) continue;
                
                struct_node = findStructDeclarationInAST(ast.get(), type_name);
                if (struct_node) break;
                
                class_node = findClassDeclarationInAST(ast.get(), type_name);
                if (class_node) break;
            }
        }
        
        // Look for the method in the struct/class
        if (struct_node) {
            for (const auto& method : struct_node->methods()) {
                if (method && method->name() == member_name) {
                    info.name = member_name;
                    info.kind = "method";
                    info.signature = extractParameterNamesFromAST(method.get(), type_name + "::" + member_name);
                    info.qualified_name = type_name + "::" + member_name;
                    return info;
                }
            }
            
            // Check fields too
            for (const auto& field : struct_node->fields()) {
                if (field && field->name() == member_name) {
                    info.name = member_name;
                    info.kind = "field";
                    info.signature = "field " + member_name + ": " + field->type_annotation();
                    info.qualified_name = type_name + "::" + member_name;
                    return info;
                }
            }
        }
        
        if (class_node) {
            for (const auto& method : class_node->methods()) {
                if (method && method->name() == member_name) {
                    info.name = member_name;
                    info.kind = "method";
                    info.signature = extractParameterNamesFromAST(method.get(), type_name + "::" + member_name);
                    info.qualified_name = type_name + "::" + member_name;
                    return info;
                }
            }
            
            // Check fields too
            for (const auto& field : class_node->fields()) {
                if (field && field->name() == member_name) {
                    info.name = member_name;
                    info.kind = "field";
                    info.signature = "field " + member_name + ": " + field->type_annotation();
                    info.qualified_name = type_name + "::" + member_name;
                    return info;
                }
            }
        }
        
        return info;
    }

    HoverInfo CryoAnalyzer::analyzeSimplePattern(const std::string &content, const Position &position)
    {
        HoverInfo info;

        // Check if the position is within a comment first
        if (isPositionInComment(content, position))
        {
            Logger::instance().debug("CryoAnalyzer", "Position {}:{} is within a comment in simple pattern analysis, skipping", position.line, position.character);
            return info;
        }

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

    std::optional<HoverInfo> CryoAnalyzer::getHoverInfoWithRetry(const std::string &file_path, const Position &position, int max_retries)
    {
        Logger::instance().debug("CryoAnalyzer", "Getting hover info with retry for {}:{},{} (max_retries: {})", file_path, position.line, position.character, max_retries);

        // Try the normal hover info first
        auto hover_result = getHoverInfo(file_path, position);
        
        if (hover_result.has_value() && !hover_result->name.empty())
        {
            Logger::instance().debug("CryoAnalyzer", "Hover succeeded on first attempt");
            return hover_result;
        }

        // If first attempt failed, try reprocessing the document
        for (int retry = 1; retry <= max_retries; retry++)
        {
            Logger::instance().info("CryoAnalyzer", "Hover attempt {} failed, trying retry {}/{}", retry - 1, retry, max_retries);
            
            // Force reprocess the document with fresh compiler instance
            if (forceReprocessDocument(file_path))
            {
                Logger::instance().debug("CryoAnalyzer", "Document reprocessed successfully on retry {}", retry);
                
                // Try hover again after reprocessing
                hover_result = getHoverInfo(file_path, position);
                if (hover_result.has_value() && !hover_result->name.empty())
                {
                    Logger::instance().info("CryoAnalyzer", "Hover succeeded on retry {}", retry);
                    return hover_result;
                }
            }
            else
            {
                Logger::instance().warn("CryoAnalyzer", "Document reprocessing failed on retry {}", retry);
            }

            // Wait a bit before next retry (exponential backoff)
            if (retry < max_retries)
            {
                int delay_ms = 100 * retry; // 100ms, 200ms, etc.
                Logger::instance().debug("CryoAnalyzer", "Waiting {}ms before next retry", delay_ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }
        }

        Logger::instance().error("CryoAnalyzer", "All {} retry attempts failed for hover at {}:{},{}", max_retries, file_path, position.line, position.character);
        
        // Return empty result - don't provide fallback
        return std::nullopt;
    }

    bool CryoAnalyzer::forceReprocessDocument(const std::string &file_path)
    {
        Logger::instance().info("CryoAnalyzer", "Force reprocessing document: {}", file_path);

        // Check if we have the file content
        if (file_contents_.find(file_path) == file_contents_.end())
        {
            Logger::instance().error("CryoAnalyzer", "Cannot reprocess document - no content available for: {}", file_path);
            return false;
        }

        const std::string &content = file_contents_[file_path];

        try
        {
            // Clean up any existing analysis for this file
            if (analyzed_files_.count(file_path))
            {
                FileAnalysis &existing = analyzed_files_[file_path];
                if (existing.compiler)
                {
                    Logger::instance().debug("CryoAnalyzer", "Cleaning up existing compiler instance for reprocessing");
                    existing.compiler.reset();
                }
                analyzed_files_.erase(file_path);
            }

            // Force a fresh parse with a new temporary file to avoid any caching issues
            Logger::instance().debug("CryoAnalyzer", "Creating fresh temporary file for reprocessing");
            
            // Create a unique temporary file with timestamp to ensure freshness
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            std::string temp_dir;
            const char *temp_env = std::getenv("TEMP");
            if (temp_env) {
                temp_dir = std::string(temp_env);
            } else {
                temp_dir = "/tmp";
            }
            
            std::string temp_file = temp_dir + "/cryo_lsp_retry_" + std::to_string(timestamp) + "_" + 
                                   std::to_string(std::hash<std::string>{}(file_path)) + ".cryo";
            
            std::ofstream temp_out(temp_file);
            if (!temp_out)
            {
                Logger::instance().error("CryoAnalyzer", "Failed to create temporary file for reprocessing: {}", temp_file);
                return false;
            }
            temp_out << content;
            temp_out.close();

            // Create a completely fresh analysis entry
            FileAnalysis &analysis = analyzed_files_[file_path];
            analysis.content = content;
            analysis.parsed_successfully = false;
            analysis.diagnostics.clear();
            analysis.last_analyzed = std::chrono::steady_clock::now();

            // Create fresh compiler instance
            analysis.compiler = Cryo::create_compiler_instance();
            analysis.compiler->set_stdlib_linking(true);
            
            Logger::instance().debug("CryoAnalyzer", "Created fresh compiler instance for reprocessing");

            // Attempt compilation with timeout protection
            bool compilation_success = false;
            auto start_time = std::chrono::steady_clock::now();
            
            try
            {
                compilation_success = analysis.compiler->compile_frontend_only(temp_file);
                auto elapsed = std::chrono::steady_clock::now() - start_time;
                
                if (elapsed > std::chrono::seconds(5)) // 5 second timeout for retry
                {
                    Logger::instance().warn("CryoAnalyzer", "Reprocessing timeout for: {}", file_path);
                    compilation_success = false;
                }
            }
            catch (const std::exception &e)
            {
                Logger::instance().error("CryoAnalyzer", "Exception during reprocessing: {}", e.what());
                compilation_success = false;
            }

            // Clean up temporary file
            std::remove(temp_file.c_str());

            if (compilation_success)
            {
                analysis.parsed_successfully = true;
                Logger::instance().info("CryoAnalyzer", "Document reprocessing successful: {}", file_path);
                return true;
            }
            else
            {
                Logger::instance().warn("CryoAnalyzer", "Document reprocessing failed: {}", file_path);
                analysis.parsed_successfully = false;
                return false;
            }
        }
        catch (const std::exception &e)
        {
            Logger::instance().error("CryoAnalyzer", "Exception during document reprocessing: {}", e.what());
            return false;
        }
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

    bool CryoAnalyzer::isPositionInComment(const std::string &content, const Position &position)
    {
        std::istringstream stream(content);
        std::string line;
        int current_line = 0;
        bool in_block_comment = false;

        // Process all lines up to and including the target line
        while (std::getline(stream, line) && current_line <= position.line)
        {
            size_t pos = 0;
            
            while (pos < line.length())
            {
                if (!in_block_comment)
                {
                    // Check for line comment start
                    if (pos < line.length() - 1 && line[pos] == '/' && line[pos + 1] == '/')
                    {
                        // If this is the target line, check if position is at or after the comment start
                        if (current_line == position.line)
                        {
                            return position.character >= static_cast<int>(pos);
                        }
                        // Line comment continues to end of line, so skip rest of line
                        break;
                    }
                    
                    // Check for block comment start
                    if (pos < line.length() - 1 && line[pos] == '/' && line[pos + 1] == '*')
                    {
                        in_block_comment = true;
                        // If we're on the target line and position is at or after comment start
                        if (current_line == position.line && position.character >= static_cast<int>(pos))
                        {
                            return true;
                        }
                        pos += 2;
                        continue;
                    }
                }
                else
                {
                    // We're in a block comment
                    // If we're on the target line, we're definitely in a comment
                    if (current_line == position.line)
                    {
                        return true;
                    }
                    
                    // Check for block comment end
                    if (pos < line.length() - 1 && line[pos] == '*' && line[pos + 1] == '/')
                    {
                        in_block_comment = false;
                        pos += 2;
                        continue;
                    }
                }
                
                pos++;
            }
            
            current_line++;
        }

        // If we reached here and we're still in a block comment, return true
        return in_block_comment;
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

    std::string CryoAnalyzer::getKeywordDocumentation(const std::string &keyword)
    {
        static const std::unordered_map<std::string, std::string> keyword_docs = {
            // Declaration keywords
            {"const", "Declares an immutable variable that cannot be changed after initialization. Use for values that remain constant throughout their lifetime."},
            {"mut", "Declares a mutable variable that can be modified after initialization. Required for variables that need to change their value."},
            {"function", "Declares a function. Functions are reusable blocks of code that can accept parameters and return values."},
            {"type", "Declares a type alias or custom type definition. Used to create new type names or define structs, classes, enums, and traits."},
            {"struct", "Declares a structured data type with named fields. Structs are value types stored on the stack by default."},
            {"class", "Declares a class with data members and methods. Classes support inheritance and encapsulation with public/private access control."},
            {"enum", "Declares an enumeration type. Enums can be simple value lists or complex algebraic data types with associated data."},
            {"trait", "Declares a trait interface that defines a set of methods. Traits enable polymorphism and code reuse across different types."},
            {"implement", "Implements methods for a type or trait. Used to define the actual behavior for structs, classes, enums, and trait implementations."},
            {"namespace", "Declares a namespace to organize code and prevent naming conflicts. Provides logical grouping of related functionality."},
            {"import", "Imports symbols from other modules or namespaces. Makes external functionality available in the current scope."},
            {"export", "Exports symbols from the current module, making them available for import in other modules."},
            
            // Control flow keywords
            {"if", "Conditional statement that executes code based on a boolean condition. Can be followed by 'else if' and 'else' clauses."},
            {"else", "Alternative branch for 'if' statements. Executes when the if condition is false."},
            {"while", "Loop that continues executing while a condition remains true. Condition is checked before each iteration."},
            {"for", "Loop with initialization, condition, and increment. Commonly used for iterating over ranges or collections."},
            {"do", "Part of a do-while loop construct. Executes the loop body at least once before checking the condition."},
            {"break", "Exits the current loop immediately. Control transfers to the statement following the loop."},
            {"continue", "Skips the rest of the current loop iteration and jumps to the next iteration."},
            {"return", "Returns a value from a function and exits the function immediately. Functions with void return type can use 'return' without a value."},
            {"match", "Pattern matching construct for examining and destructuring values. More powerful than switch statements."},
            {"switch", "Multi-way conditional statement that compares a value against multiple cases."},
            {"case", "Individual branch in a switch statement. Specifies a value to match against."},
            {"default", "Fallback case in a switch statement. Executes when no other case matches."},
            
            // Type keywords (these are duplicated from primitive types but serve as keywords too)
            {"int", "Integer type keyword. Declares variables of 64-bit signed integer type."},
            {"i8", "8-bit signed integer type keyword."},
            {"i16", "16-bit signed integer type keyword."},
            {"i32", "32-bit signed integer type keyword."},
            {"i64", "64-bit signed integer type keyword."},
            {"uint", "Unsigned integer type keyword. Typically 64-bit unsigned integer."},
            {"u8", "8-bit unsigned integer type keyword."},
            {"u16", "16-bit unsigned integer type keyword."},
            {"u32", "32-bit unsigned integer type keyword."},
            {"u64", "64-bit unsigned integer type keyword."},
            {"float", "32-bit floating point type keyword."},
            {"f32", "32-bit floating point type keyword."},
            {"f64", "64-bit floating point type keyword."},
            {"double", "64-bit floating point type keyword."},
            {"boolean", "Boolean type keyword for true/false values."},
            {"char", "Character type keyword for single Unicode characters."},
            {"string", "String type keyword for UTF-8 text."},
            {"void", "Void type keyword indicating no return value from functions."},
            
            // Access modifier keywords
            {"public", "Access modifier that makes members visible and accessible from outside the class or module."},
            {"private", "Access modifier that restricts member access to within the same class only."},
            {"protected", "Access modifier that allows access within the class and its derived classes."},
            {"static", "Modifier for class-level members that belong to the type itself rather than instances."},
            {"extern", "Declares external linkage, typically for interfacing with C libraries or other external code."},
            {"inline", "Suggests to the compiler that function calls should be expanded inline for performance."},
            {"virtual", "Enables dynamic dispatch for methods in inheritance hierarchies."},
            {"override", "Explicitly marks a method as overriding a virtual method from a base class."},
            {"abstract", "Marks classes or methods as abstract, requiring implementation in derived classes."},
            {"final", "Prevents further inheritance of classes or overriding of methods."},
            
            // Special keywords
            {"this", "Reference to the current instance within methods. Used to access instance members and distinguish from parameters."},
            {"true", "Boolean literal value representing logical truth."},
            {"false", "Boolean literal value representing logical falsehood."},
            {"null", "Null pointer literal representing an invalid or uninitialized pointer."},
            {"sizeof", "Operator that returns the size in bytes of a type or variable at compile time."},
            {"new", "Memory allocation operator that creates objects on the heap and returns a pointer."},
            {"intrinsic", "Marks functions as compiler intrinsics that map directly to low-level operations or CPU instructions."}
        };
        
        auto it = keyword_docs.find(keyword);
        return (it != keyword_docs.end()) ? it->second : "";
    }

    std::string CryoAnalyzer::getLiteralDocumentation(const std::string &literal, const std::string &content, const Position &position)
    {
        // Get the actual literal text from the line at position
        std::string line = getLineAtPosition(content, position);
        
        // Find the literal at the current position
        size_t line_offset = 0;
        for (int i = 0; i < position.line; i++) {
            size_t next_newline = content.find('\n', line_offset);
            if (next_newline != std::string::npos) {
                line_offset = next_newline + 1;
            }
        }
        
        size_t char_pos = line_offset + position.character;
        
        // Detect string literals by looking for quotes around the cursor position
        if (char_pos < content.length()) {
            // Look backwards and forwards for string quotes
            size_t start_quote = std::string::npos;
            size_t end_quote = std::string::npos;
            
            // Find the start quote (look backwards from cursor)
            for (size_t i = char_pos; i > line_offset; --i) {
                if (content[i] == '"' && (i == 0 || content[i-1] != '\\')) {
                    start_quote = i;
                    break;
                }
            }
            
            // If we found a start quote, look for the end quote
            if (start_quote != std::string::npos) {
                for (size_t i = start_quote + 1; i < content.length() && i < line_offset + line.length(); ++i) {
                    if (content[i] == '"' && content[i-1] != '\\') {
                        end_quote = i;
                        break;
                    }
                }
                
                // If we have both quotes and cursor is between them, extract the string
                if (end_quote != std::string::npos && char_pos >= start_quote && char_pos <= end_quote) {
                    std::string string_literal = content.substr(start_quote, end_quote - start_quote + 1);
                    std::string string_content = string_literal.substr(1, string_literal.length() - 2); // Remove quotes
                    
                    return "String literal: char[" + std::to_string(string_content.length()) + "](\"" + string_content + "\")";
                }
            }
        }
        
        // Detect character literals
        if (char_pos < content.length() && content[char_pos] == '\'') {
            // Find the full character literal
            size_t start = char_pos;
            size_t end = start + 1;
            if (end < content.length() && content[end] == '\\' && end + 1 < content.length()) {
                end += 2; // Escaped character
            } else if (end < content.length()) {
                end++; // Regular character
            }
            if (end < content.length() && content[end] == '\'') end++; // Include closing quote
            
            std::string char_literal = content.substr(start, end - start);
            return "Character literal: char(" + char_literal + ")";
        }
        
        // Detect numeric literals
        std::regex integer_pattern(R"(^-?\d+$)");
        std::regex float_pattern(R"(^-?\d+\.\d*f?$|^-?\d*\.\d+f?$)");
        std::regex hex_pattern(R"(^0[xX][0-9a-fA-F]+$)");
        std::regex binary_pattern(R"(^0[bB][01]+$)");
        
        if (std::regex_match(literal, hex_pattern)) {
            try {
                unsigned long long value = std::stoull(literal, nullptr, 16);
                std::string result = "Hexadecimal integer literal: " + literal + " (decimal: " + std::to_string(value) + ")";
                
                // Add binary representation for small values
                if (value <= 0xFFFF) {
                    std::string binary = "";
                    unsigned long long temp = value;
                    if (temp == 0) {
                        binary = "0";
                    } else {
                        while (temp > 0) {
                            binary = (temp % 2 ? "1" : "0") + binary;
                            temp /= 2;
                        }
                    }
                    result += ", binary: 0b" + binary;
                }
                
                // Add information about bit representation
                if (value <= 0xFF) {
                    result += " (fits in 8-bit: u8)";
                } else if (value <= 0xFFFF) {
                    result += " (fits in 16-bit: u16)";
                } else if (value <= 0xFFFFFFFF) {
                    result += " (fits in 32-bit: u32)";
                } else {
                    result += " (requires 64-bit: u64)";
                }
                
                return result;
            } catch (...) {
                return "Hexadecimal integer literal: " + literal;
            }
        }
        
        if (std::regex_match(literal, binary_pattern)) {
            try {
                std::string binary_digits = literal.substr(2); // Remove 0b prefix
                unsigned long long value = std::stoull(binary_digits, nullptr, 2);
                std::string result = "Binary integer literal: " + literal + " (decimal: " + std::to_string(value);
                
                // Add hex representation
                std::stringstream hex_stream;
                hex_stream << "0x" << std::hex << std::uppercase << value;
                result += ", hex: " + hex_stream.str() + ")";
                
                return result;
            } catch (...) {
                return "Binary integer literal: " + literal;
            }
        }
        
        if (std::regex_match(literal, float_pattern)) {
            if (literal.back() == 'f' || literal.back() == 'F') {
                return "32-bit float literal: f32(" + literal + ")";
            } else {
                return "64-bit float literal: f64(" + literal + ")";
            }
        }
        
        if (std::regex_match(literal, integer_pattern)) {
            try {
                long long value = std::stoll(literal);
                
                // Determine the appropriate integer type based on value range
                if (value >= -128 && value <= 127) {
                    return "Integer literal: i8(" + literal + ") - fits in 8-bit signed";
                } else if (value >= -32768 && value <= 32767) {
                    return "Integer literal: i16(" + literal + ") - fits in 16-bit signed";
                } else if (value >= -2147483648LL && value <= 2147483647LL) {
                    return "Integer literal: i32(" + literal + ") - fits in 32-bit signed";
                } else {
                    return "Integer literal: i64(" + literal + ") - 64-bit signed";
                }
            } catch (...) {
                return "Integer literal: " + literal;
            }
        }
        
        // Check for boolean literals
        if (literal == "true" || literal == "false") {
            return "Boolean literal: boolean(" + literal + ")";
        }
        
        // Check for null literal
        if (literal == "null") {
            return "Null literal: null pointer value";
        }
        
        return ""; // Not a recognized literal
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
        
        // Initialize position fields to invalid values (will be overridden for literals)
        info.start_pos.line = -1;
        info.start_pos.character = -1;
        info.end_pos.line = -1;
        info.end_pos.character = -1;

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

        // Check if this is a keyword
        auto keyword_docs = getKeywordDocumentation(word);
        if (!keyword_docs.empty()) {
            info.type = "keyword";
            info.kind = "keyword";
            info.signature = word;
            info.documentation = keyword_docs;
            Logger::instance().debug("CryoAnalyzer", "Found keyword: {} -> {}", word, keyword_docs.substr(0, 50) + "...");
            return info;
        }

        // Check if this is a literal (string, number, boolean, etc.)
        // For literals, we need to check the actual character at position, not just the word
        size_t line_offset = 0;
        for (int i = 0; i < position.line; i++) {
            size_t next_newline = content.find('\n', line_offset);
            if (next_newline != std::string::npos) {
                line_offset = next_newline + 1;
            }
        }
        size_t char_pos = line_offset + position.character;
        
        // Helper function to count actual characters (treating escape sequences as single chars)
        auto countActualChars = [](const std::string& str) -> size_t {
            size_t count = 0;
            for (size_t i = 0; i < str.length(); ++i) {
                if (str[i] == '\\' && i + 1 < str.length()) {
                    // Skip the next character as it's part of the escape sequence
                    i++;
                }
                count++;
            }
            return count;
        };
        
        // Check if we're hovering inside a string literal
        if (char_pos < content.length()) {
            // Look backwards and forwards for string quotes
            size_t start_quote = std::string::npos;
            size_t end_quote = std::string::npos;
            
            // Find the start quote (look backwards from cursor)
            for (size_t i = char_pos; i > line_offset && i < content.length(); --i) {
                if (content[i] == '"' && (i == 0 || content[i-1] != '\\')) {
                    start_quote = i;
                    break;
                }
            }
            
            // If we found a start quote, look for the end quote
            if (start_quote != std::string::npos) {
                // Find the end of the current line
                size_t line_end = content.find('\n', line_offset);
                if (line_end == std::string::npos) line_end = content.length();
                
                for (size_t i = start_quote + 1; i < content.length() && i < line_end; ++i) {
                    if (content[i] == '"' && content[i-1] != '\\') {
                        end_quote = i;
                        break;
                    }
                }
                
                // If we have both quotes and cursor is between them, extract the full string
                if (end_quote != std::string::npos && char_pos >= start_quote && char_pos <= end_quote) {
                    std::string full_string = content.substr(start_quote, end_quote - start_quote + 1);
                    std::string string_content = full_string.substr(1, full_string.length() - 2); // Remove quotes for length calculation
                    size_t actual_char_count = countActualChars(string_content); // Count considering escape sequences
                    
                    // Calculate the start and end positions for the range
                    Position start_position;
                    start_position.line = position.line;
                    start_position.character = static_cast<int>(start_quote - line_offset);
                    
                    Position end_position;
                    end_position.line = position.line;
                    end_position.character = static_cast<int>(end_quote + 1 - line_offset); // +1 to include the closing quote
                    
                    info.type = "string literal";
                    info.kind = "literal";
                    info.signature = "char[" + std::to_string(actual_char_count) + "](" + full_string + ")";
                    info.documentation = "String literal containing " + std::to_string(actual_char_count) + " characters";
                    info.start_pos = start_position;
                    info.end_pos = end_position;
                    Logger::instance().debug("CryoAnalyzer", "Found full string literal: {} at range {}:{} to {}:{}", 
                                           full_string, start_position.line, start_position.character, 
                                           end_position.line, end_position.character);
                    return info;
                }
            }
        }
        
        // Check if we're in a character literal by looking for single quotes
        if (char_pos < content.length()) {
            // Look backwards and forwards for character quotes
            size_t start_quote = std::string::npos;
            size_t end_quote = std::string::npos;
            
            // Find the start quote (look backwards from cursor)
            for (size_t i = char_pos; i > line_offset && i < content.length(); --i) {
                if (content[i] == '\'' && (i == 0 || content[i-1] != '\\')) {
                    start_quote = i;
                    break;
                }
            }
            
            // If we found a start quote, look for the end quote
            if (start_quote != std::string::npos) {
                // Find the end of the current line
                size_t line_end = content.find('\n', line_offset);
                if (line_end == std::string::npos) line_end = content.length();
                
                for (size_t i = start_quote + 1; i < content.length() && i < line_end; ++i) {
                    if (content[i] == '\'' && content[i-1] != '\\') {
                        end_quote = i;
                        break;
                    }
                }
                
                // If we have both quotes and cursor is between them, extract the character
                if (end_quote != std::string::npos && char_pos >= start_quote && char_pos <= end_quote) {
                    std::string full_char = content.substr(start_quote, end_quote - start_quote + 1);
                    std::string char_content = full_char.substr(1, full_char.length() - 2); // Remove quotes
                    
                    // Calculate the start and end positions for the range
                    Position start_position;
                    start_position.line = position.line;
                    start_position.character = static_cast<int>(start_quote - line_offset);
                    
                    Position end_position;
                    end_position.line = position.line;
                    end_position.character = static_cast<int>(end_quote + 1 - line_offset); // +1 to include the closing quote
                    
                    std::string char_description;
                    std::string detailed_info;
                    unsigned char ascii_value = 0;
                    bool has_ascii_value = false;
                    
                    if (char_content == "\\n") {
                        char_description = "newline character";
                        ascii_value = 10;
                        has_ascii_value = true;
                    } else if (char_content == "\\t") {
                        char_description = "tab character";
                        ascii_value = 9;
                        has_ascii_value = true;
                    } else if (char_content == "\\r") {
                        char_description = "carriage return character";
                        ascii_value = 13;
                        has_ascii_value = true;
                    } else if (char_content == "\\\\") {
                        char_description = "backslash character";
                        ascii_value = 92;
                        has_ascii_value = true;
                    } else if (char_content == "\\'") {
                        char_description = "single quote character";
                        ascii_value = 39;
                        has_ascii_value = true;
                    } else if (char_content == "\\0") {
                        char_description = "null character";
                        ascii_value = 0;
                        has_ascii_value = true;
                    } else if (char_content.length() == 1) {
                        char_description = "character '" + char_content + "'";
                        ascii_value = static_cast<unsigned char>(char_content[0]);
                        has_ascii_value = true;
                    } else {
                        char_description = "escape sequence";
                    }
                    
                    // Build detailed documentation with ASCII/Unicode info
                    if (has_ascii_value) {
                        std::string binary_str = "";
                        for (int i = 7; i >= 0; i--) {
                            binary_str += ((ascii_value >> i) & 1) ? "1" : "0";
                        }
                        
                        // Convert to proper hex
                        std::stringstream hex_stream;
                        hex_stream << "0x" << std::hex << std::uppercase << static_cast<int>(ascii_value);
                        
                        detailed_info = "Character literal: " + char_description + "\n\n";
                        detailed_info += "**ASCII/Unicode Value:** " + std::to_string(ascii_value) + "\n";
                        detailed_info += "**Hexadecimal:** " + hex_stream.str() + "\n";
                        detailed_info += "**Binary:** 0b" + binary_str;
                    } else {
                        detailed_info = "Character literal: " + char_description;
                    }
                    
                    info.type = "character literal";
                    info.kind = "literal";
                    info.signature = "char(" + full_char + ")";
                    info.documentation = detailed_info;
                    info.start_pos = start_position;
                    info.end_pos = end_position;
                    Logger::instance().debug("CryoAnalyzer", "Found character literal: {} at range {}:{} to {}:{}", 
                                           full_char, start_position.line, start_position.character, 
                                           end_position.line, end_position.character);
                    return info;
                }
            }
        }
        
        // Check if the word itself is a numeric or boolean literal
        auto literal_docs = getLiteralDocumentation(word, content, position);
        if (!literal_docs.empty()) {
            info.type = "literal";
            info.kind = "literal";
            info.signature = word;
            info.documentation = literal_docs;
            Logger::instance().debug("CryoAnalyzer", "Found literal: {} -> {}", word, literal_docs.substr(0, 50) + "...");
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

    HoverInfo CryoAnalyzer::buildEnhancedHoverInfo(const Cryo::Symbol &symbol, const std::string &word, const std::string &file_path, Cryo::CompilerInstance *compiler)
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
        info.signature = buildQualifiedSignature(symbol, file_path, compiler);

        // Add documentation - first try to get from AST node if available
        std::string ast_documentation;
        Logger::instance().debug("CryoAnalyzer", "Trying to get documentation for symbol '{}' of kind '{}'", 
            symbol.name, static_cast<int>(symbol.kind));
            
        if (compiler && !file_path.empty())
        {
            Logger::instance().debug("CryoAnalyzer", "Compiler and file_path available, checking analyzed files");
            
            // Try to find the corresponding AST node and extract its documentation
            if (analyzed_files_.count(file_path) && analyzed_files_[file_path].ast)
            {
                Logger::instance().debug("CryoAnalyzer", "Found analyzed file with AST, searching for declaration");
                Cryo::DeclarationNode* declaration = nullptr;
                
                // Search for the declaration based on symbol kind
                switch (symbol.kind)
                {
                case Cryo::SymbolKind::Function:
                    Logger::instance().debug("CryoAnalyzer", "Searching for function declaration: '{}'", symbol.name);
                    declaration = findFunctionInAST(analyzed_files_[file_path].ast, symbol.name, info.qualified_name, "");
                    break;
                case Cryo::SymbolKind::Type:
                    Logger::instance().debug("CryoAnalyzer", "Searching for type declaration: '{}'", symbol.name);
                    // Try to find struct or class declaration
                    declaration = findStructDeclarationInAST(analyzed_files_[file_path].ast, symbol.name);
                    if (!declaration)
                    {
                        declaration = findClassDeclarationInAST(analyzed_files_[file_path].ast, symbol.name);
                    }
                    break;
                default:
                    Logger::instance().debug("CryoAnalyzer", "Symbol kind not supported for AST documentation search");
                    // For other symbol types, we don't currently search AST
                    break;
                }
                
                Logger::instance().debug("CryoAnalyzer", "Declaration search result: {}", declaration ? "found" : "not found");
                
                if (declaration)
                {
                    ast_documentation = extractDocumentationFromASTNode(declaration);
                    Logger::instance().debug("CryoAnalyzer", "Extracted AST documentation for '{}': length={}, content='{}'", 
                        symbol.name, ast_documentation.length(), ast_documentation.substr(0, 100) + (ast_documentation.length() > 100 ? "..." : ""));
                }
            }
        }
        
        // Use AST documentation if available, otherwise fall back to static documentation
        if (!ast_documentation.empty())
        {
            info.documentation = ast_documentation;
        }
        else
        {
            info.documentation = getSymbolDocumentation(symbol, word);
        }

        // Convert source location if available
        if (symbol.declaration_location.line() > 0)
        {
            info.definition_location = convertSourceLocationToPosition(symbol.declaration_location);
        }

        Logger::instance().debug("CryoAnalyzer", "Built enhanced hover info for '{}': {}", word, info.signature);
        return info;
    }

    std::string CryoAnalyzer::buildQualifiedSignature(const Cryo::Symbol &symbol, const std::string &file_path, Cryo::CompilerInstance *compiler)
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
                    // For functions, try to extract full signature with parameter names from source text
                    Logger::instance().debug("CryoAnalyzer", "Found function symbol '{}', attempting source extraction", symbol.name);
                    std::string full_signature = extractFunctionSignatureFromSource(file_path, symbol.name);
                    if (!full_signature.empty()) {
                        Logger::instance().debug("CryoAnalyzer", "Using extracted signature: '{}'", full_signature);
                        signature = full_signature;
                    } else {
                        // Try to find function in imported modules for parameter names
                        Logger::instance().debug("CryoAnalyzer", "Source extraction failed, checking imported modules for '{}'", symbol.name);
                        
                        if (compiler) {
                            auto imported_function_node = findFunctionInImportedModules(compiler, symbol.name, qualified_name);
                            if (imported_function_node) {
                                Logger::instance().debug("CryoAnalyzer", "Found function in imported modules, extracting parameter names");
                                std::string ast_signature = extractParameterNamesFromAST(imported_function_node, qualified_name);
                                if (!ast_signature.empty()) {
                                    signature = ast_signature;
                                } else {
                                    Logger::instance().debug("CryoAnalyzer", "Failed to extract parameter names from AST, using fallback");
                                    signature = "function " + qualified_name + symbol.data_type->to_string();
                                }
                            } else {
                                Logger::instance().debug("CryoAnalyzer", "Function not found in imported modules, using AST signature");
                                signature = "function " + qualified_name + symbol.data_type->to_string();
                            }
                        } else {
                            Logger::instance().debug("CryoAnalyzer", "No compiler instance available, using AST signature as fallback");
                            signature = "function " + qualified_name + symbol.data_type->to_string();
                        }
                    }
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
                
                // Try to find struct or class declaration for enhanced preview
                if (compiler) {
                    // Check current file first
                    if (analyzed_files_.count(file_path) && analyzed_files_[file_path].ast) {
                        auto struct_node = findStructDeclarationInAST(analyzed_files_[file_path].ast, symbol.name);
                        if (struct_node) {
                            signature = buildStructPreview(struct_node, qualified_name);
                            break;
                        }
                        
                        auto class_node = findClassDeclarationInAST(analyzed_files_[file_path].ast, symbol.name);
                        if (class_node) {
                            signature = buildClassPreview(class_node, qualified_name);
                            break;
                        }
                    }
                    
                    // Check imported modules
                    const auto& imported_asts = compiler->module_loader()->get_imported_asts();
                    for (const auto& [module_name, ast] : imported_asts) {
                        if (!ast) continue;
                        
                        auto struct_node = findStructDeclarationInAST(ast.get(), symbol.name);
                        if (struct_node) {
                            signature = buildStructPreview(struct_node, qualified_name);
                            break;
                        }
                        
                        auto class_node = findClassDeclarationInAST(ast.get(), symbol.name);
                        if (class_node) {
                            signature = buildClassPreview(class_node, qualified_name);
                            break;
                        }
                    }
                }
                
                // Fallback to basic type signature
                if (signature.empty()) {
                    signature = "type " + qualified_name;
                    if (symbol.data_type) {
                        signature += " = " + symbol.data_type->to_string();
                    }
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

    std::string CryoAnalyzer::extractDocumentationFromASTNode(Cryo::DeclarationNode *declaration_node)
    {
        Logger::instance().debug("CryoAnalyzer", "extractDocumentationFromASTNode called with declaration_node: {}", 
            declaration_node ? "valid" : "null");
            
        if (!declaration_node)
        {
            return "";
        }

        // Check if the declaration node has documentation
        Logger::instance().debug("CryoAnalyzer", "Checking if declaration has documentation: {}", 
            declaration_node->has_documentation() ? "yes" : "no");
            
        if (declaration_node->has_documentation())
        {
            std::string doc = declaration_node->documentation();
            Logger::instance().debug("CryoAnalyzer", "Raw documentation content: '{}'", doc);
            
            // Clean up the documentation string - preserve structure but fix spacing
            if (!doc.empty())
            {
                // Remove leading and trailing whitespace
                doc.erase(0, doc.find_first_not_of(" \t\n\r"));
                doc.erase(doc.find_last_not_of(" \t\n\r") + 1);
                
                // Process line by line, preserving content but normalizing whitespace
                std::string cleaned_doc;
                std::stringstream ss(doc);
                std::string line;
                bool first_line = true;
                
                while (std::getline(ss, line))
                {
                    // Remove leading whitespace from each line
                    std::string trimmed = line;
                    trimmed.erase(0, trimmed.find_first_not_of(" \t"));
                    
                    // Skip completely empty lines to avoid extra spacing
                    if (trimmed.empty())
                    {
                        continue;
                    }
                    
                    if (!first_line)
                    {
                        cleaned_doc += "\n";
                    }
                    first_line = false;
                    
                    cleaned_doc += trimmed;
                }
                
                Logger::instance().debug("CryoAnalyzer", "Cleaned documentation: '{}'", cleaned_doc);
                return cleaned_doc;
            }
        }

        return "";
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
            return buildEnhancedHoverInfo(*symbol.value(), qualified_name, file_path, analysis.compiler.get());
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

        const auto &analysis = analyzed_files_[file_path];
        const auto &symbol_table = *analysis.compiler->symbol_table();

        try
        {
            if (scope.empty())
            {
                // Get all symbols in global scope
                const auto &global_symbols = symbol_table.get_symbols();
                for (const auto &[name, symbol] : global_symbols)
                {
                    symbols.push_back(buildEnhancedHoverInfo(symbol, name, file_path, analysis.compiler.get()));
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
                        symbols.push_back(buildEnhancedHoverInfo(symbol, name, file_path, analysis.compiler.get()));
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