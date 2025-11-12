#pragma once

/**
 * @file test_helpers.hpp
 * @brief Clean, professional test helper classes for CryoLang compiler testing
 */

#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <sstream>
#include <iomanip>

#include "Compiler/CompilerInstance.hpp"
#include "AST/ASTContext.hpp"
#include "AST/TypeChecker.hpp"
#include "AST/ASTNode.hpp"
#include "Parser/Parser.hpp"
#include "Lexer/lexer.hpp"

namespace CryoTest {

/**
 * @brief Exception for test setup failures
 */
class TestSetupError : public std::runtime_error {
public:
    TestSetupError(const std::string& message) : std::runtime_error("Test setup failed: " + message) {}
};

/**
 * @brief Base helper class providing compiler setup for testing
 */
class CompilerTestHelper {
protected:
    std::unique_ptr<Cryo::CompilerInstance> compiler;
    
public:
    virtual ~CompilerTestHelper() = default;
    
    /**
     * @brief Initialize compiler instance for testing
     */
    virtual void setup() {
        // Always create a fresh compiler instance for complete isolation
        compiler = Cryo::create_compiler_instance();
        if (!compiler) {
            throw TestSetupError("Failed to create compiler instance");
        }
        
        // Configure for unit testing - disable auto-imports to avoid 
        // circular dependencies with stdlib modules
        compiler->set_auto_imports_enabled(false);
    }
    
    /**
     * @brief Reset the helper for a new test (ensures clean state)
     */
    virtual void reset() {
        // Simply create a fresh compiler instance
        setup();
    }
    
    /**
     * @brief Parse source code and return success status
     */
    bool parse_source(const std::string& source) {
        if (!compiler) {
            throw TestSetupError("Compiler not initialized");
        }
        
        return compiler->parse_source(source);
    }
    
    /**
     * @brief Parse and analyze source code (includes import resolution and symbol table population)
     */
    bool parse_and_analyze(const std::string& source) {
        if (!compiler) {
            throw TestSetupError("Compiler not initialized");
        }
        
        // First parse the source
        if (!compiler->parse_source(source)) {
            return false;
        }
        
        // Then run analysis phase (symbol table population, imports, etc.)
        return compiler->analyze();
    }
    
    /**
     * @brief Check if compilation has errors
     */
    bool has_errors() const {
        return compiler && compiler->has_errors();
    }
    
    /**
     * @brief Get the AST root (may return nullptr if parsing failed)
     */
    Cryo::ProgramNode* get_ast() const {
        return compiler ? compiler->ast_root() : nullptr;
    }
    
    /**
     * @brief Get formatted diagnostics for test output
     */
    std::string get_diagnostic_summary() const {
        if (!compiler || !compiler->diagnostic_manager()) {
            return "No diagnostic information available";
        }
        
        auto* diag_manager = compiler->diagnostic_manager();
        if (!diag_manager->has_errors() && !diag_manager->has_warnings()) {
            return "No errors or warnings";
        }
        
        std::stringstream ss;
        ss << "Compilation diagnostics:\n";
        
        // Get all diagnostics and format them concisely
        const auto& diagnostics = diag_manager->diagnostics();
        size_t error_count = 0, warning_count = 0;
        size_t displayed_count = 0;
        const size_t max_errors_to_show = 10; // Limit output
        
        for (const auto& diag : diagnostics) {
            // Skip stdlib diagnostics to reduce noise
            if (diag_manager->is_stdlib_diagnostic(diag)) {
                continue;
            }
            
            // Stop showing errors after a reasonable number to avoid overwhelming output
            if (displayed_count >= max_errors_to_show) {
                ss << "  ... and " << (diagnostics.size() - displayed_count) << " more errors\n";
                break;
            }
            
            std::string severity;
            switch (diag.severity()) {
                case Cryo::DiagnosticSeverity::Error:
                case Cryo::DiagnosticSeverity::Fatal:
                    severity = "error";
                    error_count++;
                    break;
                case Cryo::DiagnosticSeverity::Warning:
                    severity = "warning";
                    warning_count++;
                    break;
                case Cryo::DiagnosticSeverity::Note:
                    severity = "note";
                    break;
            }
            
            // Format location if available
            std::string location = "";
            if (diag.range().is_valid()) {
                location = " at line " + std::to_string(diag.range().start.line()) + 
                          ", column " + std::to_string(diag.range().start.column());
            }
            
            ss << "  " << severity << location << ": " << diag.message() << "\n";
            displayed_count++;
        }
        
        // Add summary
        if (error_count > 0 || warning_count > 0) {
            ss << "Summary: " << error_count << " error(s), " << warning_count << " warning(s)";
        }
        
        return ss.str();
    }
    
    /**
     * @brief Get Cryo source context around error location
     */
    std::string get_source_context(const std::string& source, size_t error_line, size_t context_lines = 2) const {
        std::istringstream iss(source);
        std::vector<std::string> lines;
        std::string line;
        
        while (std::getline(iss, line)) {
            lines.push_back(line);
        }
        
        if (error_line == 0 || error_line > lines.size()) {
            return "Source context unavailable";
        }
        
        std::stringstream ss;
        ss << "Cryo source context:\n";
        
        size_t start_line = (error_line > context_lines) ? error_line - context_lines : 1;
        size_t end_line = std::min(error_line + context_lines, lines.size());
        
        for (size_t i = start_line; i <= end_line; ++i) {
            std::string prefix = (i == error_line) ? " -> " : "    ";
            ss << prefix << std::setw(3) << i << " | " << lines[i-1] << "\n";
        }
        
        return ss.str();
    }
    
    /**
     * @brief Get compiler components for advanced testing
     */
    Cryo::CompilerInstance* get_compiler() const { return compiler.get(); }
    Cryo::ASTContext* get_ast_context() const { return compiler ? compiler->ast_context() : nullptr; }
    Cryo::SymbolTable* get_symbol_table() const { return compiler ? compiler->symbol_table() : nullptr; }
    Cryo::DiagnosticManager* get_diagnostics() const { return compiler ? compiler->diagnostic_manager() : nullptr; }
};

/**
 * @brief Helper class for type checker testing
 */
class TypeCheckerTestHelper : public CompilerTestHelper {
private:
    std::string _last_source;
    
public:
    /**
     * @brief Setup with fresh compiler and clear TypeChecker symbols
     */
    void setup() override {
        CompilerTestHelper::setup();
        
        // Clear TypeChecker symbols to ensure test isolation
        auto type_checker = compiler->type_checker();
        if (type_checker) {
            type_checker->clear_symbols();
        }
    }
    
    /**
     * @brief Parse source and run type checking (includes full analysis pipeline)
     */
    bool parse_and_type_check(const std::string& source) {
        _last_source = source;
        
        // Use parse_and_analyze instead of just parse_source to ensure
        // imports are processed and symbol table is properly populated
        if (!parse_and_analyze(source)) {
            return false;
        }
        
        auto ast = get_ast();
        if (!ast) {
            return false;
        }
        
        // Type checking should already be done as part of analyze(),
        // but we can run it explicitly if needed for testing
        auto type_checker = compiler->type_checker();
        if (!type_checker) {
            return false;
        }
        
        // The analyze() phase should have already run type checking,
        // but we can check if there are any additional type errors
        return !has_errors();
    }
    
    /**
     * @brief Parse source and expect type checking to succeed with enhanced error reporting
     */
    void expect_type_check_success(const std::string& source) {
        _last_source = source;
        bool success = parse_and_type_check(source);
        if (!success || has_errors()) {
            std::string diagnostics = get_diagnostic_summary();
            std::string context = get_source_context(source, 1, 3);
            throw TestSetupError("Expected type checking to succeed, but got errors.\n" + 
                               diagnostics + "\n" + context);
        }
    }
    
    /**
     * @brief Parse source and expect type checking to fail
     */
    void expect_type_check_failure(const std::string& source) {
        _last_source = source;
        parse_and_type_check(source);
        if (!has_errors()) {
            throw TestSetupError("Expected type checking to fail, but no errors were found");
        }
    }
    
    /**
     * @brief Get the last tested source code
     */
    const std::string& get_last_source() const {
        return _last_source;
    }
};

/**
 * @brief Helper class for lexer testing
 */
class LexerTestHelper : public CompilerTestHelper {
public:
    /**
     * @brief Test that source can be tokenized without errors
     */
    bool tokenizes_successfully(const std::string& source) {
        try {
            if (!compiler) {
                setup();
            }
            
            // Create lexer for the source
            Cryo::Lexer lexer(source, get_diagnostics(), "test.cryo");
            
            // Try to get all tokens
            while (lexer.has_more_tokens()) {
                auto token = lexer.next_token();
                if (token.kind() == Cryo::TokenKind::TK_EOF) {
                    break;
                }
            }
            
            return !has_errors();
        } catch (...) {
            return false;
        }
    }
    
    /**
     * @brief Count tokens in source code
     */
    size_t count_tokens(const std::string& source) {
        if (!compiler) {
            setup();
        }
        
        Cryo::Lexer lexer(source, get_diagnostics(), "test.cryo");
        size_t count = 0;
        
        while (lexer.has_more_tokens()) {
            auto token = lexer.next_token();
            if (token.kind() == Cryo::TokenKind::TK_EOF) {
                break;
            }
            count++;
        }
        
        return count;
    }
};

/**
 * @brief Helper class for parser testing
 */
class ParserTestHelper : public CompilerTestHelper {
private:
    std::string _last_source;
    
public:
    /**
     * @brief Test that source parses successfully (parsing only, no analysis)
     */
    bool parses_successfully(const std::string& source) {
        _last_source = source;
        bool success = parse_source(source);
        return success && !has_errors() && get_ast() != nullptr;
    }
    
    /**
     * @brief Parse source and expect success with enhanced error reporting
     */
    void expect_parse_success(const std::string& source) {
        _last_source = source;
        if (!parses_successfully(source)) {
            std::string diagnostics = get_diagnostic_summary();
            std::string context = get_source_context(source, 1, 3);
            throw TestSetupError("Expected parsing to succeed, but failed.\n" + 
                               diagnostics + "\n" + context);
        }
    }
    
    /**
     * @brief Parse source and expect failure
     */
    void expect_parse_failure(const std::string& source) {
        _last_source = source;
        bool success = parse_source(source);
        if (success && !has_errors()) {
            throw TestSetupError("Expected parsing to fail, but succeeded");
        }
    }
    
    /**
     * @brief Get the last tested source code
     */
    const std::string& get_last_source() const {
        return _last_source;
    }
};

/**
 * @brief Helper class for integration testing
 */
class IntegrationTestHelper : public CompilerTestHelper {
private:
    std::string _last_source;
    
public:
    /**
     * @brief Setup for integration tests with stdlib support
     */
    void setup() override {
        CompilerTestHelper::setup();
        
        // Enable auto-imports for integration tests that need stdlib
        compiler->set_auto_imports_enabled(true);
        
        // Set stdlib root to ensure imports work
        if (auto* module_loader = compiler->module_loader()) {
            if (!module_loader->auto_detect_stdlib_root()) {
                module_loader->set_stdlib_root("./stdlib");
            }
        }
    }
    
    /**
     * @brief Test full compilation pipeline up to IR generation
     */
    bool compiles_to_ir(const std::string& source) {
        _last_source = source;
        
        // Use full parse_and_analyze pipeline to ensure imports are processed
        if (!parse_and_analyze(source)) {
            return false;
        }
        
        if (has_errors()) {
            return false;
        }
        
        // Type checking should already be done by analyze(), but verify AST exists
        auto ast = get_ast();
        if (!ast) {
            return false;
        }
        
        // Try to generate IR
        return compiler->generate_ir();
    }
    
    /**
     * @brief Test that source compiles without errors with enhanced error reporting
     */
    void expect_compilation_success(const std::string& source) {
        _last_source = source;
        if (!compiles_to_ir(source)) {
            std::string diagnostics = get_diagnostic_summary();
            std::string context = get_source_context(source, 1, 3);
            std::string stage = has_errors() ? "compilation" : "IR generation";
            throw TestSetupError("Expected compilation to succeed, but failed at " + stage + ".\n" +
                               diagnostics + "\n" + context);
        }
    }
    
    /**
     * @brief Test that source fails compilation
     */
    void expect_compilation_failure(const std::string& source) {
        _last_source = source;
        bool success = compiles_to_ir(source);
        if (success && !has_errors()) {
            throw TestSetupError("Expected compilation to fail, but succeeded");
        }
    }
    
    /**
     * @brief Get the last tested source code
     */
    const std::string& get_last_source() const {
        return _last_source;
    }
};

/**
 * @brief Specialized helper for tests requiring standard library generics (Array<T>, etc.)
 */
class StdlibIntegrationTestHelper : public IntegrationTestHelper {
public:
    /**
     * @brief Setup with full stdlib support including generics
     */
    void setup() override {
        // Call parent setup first
        IntegrationTestHelper::setup();
        
        // Ensure stdlib compilation is NOT enabled (we want to use stdlib, not compile it)
        compiler->set_stdlib_compilation_mode(false);
        
        // Enable stdlib linking for final executable generation
        compiler->set_stdlib_linking(true);
        
        // Pre-load core modules to avoid import issues during testing
        preload_stdlib_modules();
    }

private:
    /**
     * @brief Pre-load essential stdlib modules for testing
     */
    void preload_stdlib_modules() {
        // This method can be used to explicitly load stdlib modules if needed
        // For now, we rely on auto-imports during compilation
        // Updated: Force recompilation
    }
};

/**
 * @brief Helper for unit tests that should NOT use stdlib (pure unit testing)
 */
class UnitTestHelper : public CompilerTestHelper {
public:
    /**
     * @brief Setup with minimal configuration (no stdlib imports)
     */
    void setup() override {
        CompilerTestHelper::setup();
        
        // Explicitly disable auto-imports for pure unit tests
        compiler->set_auto_imports_enabled(false);
        compiler->set_stdlib_compilation_mode(false);
        compiler->set_stdlib_linking(false);
    }
};

} // namespace CryoTest