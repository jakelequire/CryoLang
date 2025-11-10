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
     * @brief Parse source and run type checking
     */
    bool parse_and_type_check(const std::string& source) {
        if (!parse_source(source)) {
            return false;
        }
        
        auto ast = get_ast();
        if (!ast) {
            return false;
        }
        
        // Get the built-in type checker from compiler
        auto type_checker = compiler->type_checker();
        if (!type_checker) {
            return false;
        }
        
        // NOTE: Don't clear symbols here - they were already loaded during analyze()
        // and clearing them now would remove necessary builtin/intrinsic symbols
        
        // Run type checking
        type_checker->check_program(*ast);
        
        return !has_errors();
    }
    
    /**
     * @brief Parse source and expect type checking to succeed
     */
    void expect_type_check_success(const std::string& source) {
        bool success = parse_and_type_check(source);
        if (!success || has_errors()) {
            throw TestSetupError("Expected type checking to succeed, but got errors");
        }
    }
    
    /**
     * @brief Parse source and expect type checking to fail
     */
    void expect_type_check_failure(const std::string& source) {
        parse_and_type_check(source);
        if (!has_errors()) {
            throw TestSetupError("Expected type checking to fail, but no errors were found");
        }
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
public:
    /**
     * @brief Test that source parses successfully
     */
    bool parses_successfully(const std::string& source) {
        bool success = parse_source(source);
        return success && !has_errors() && get_ast() != nullptr;
    }
    
    /**
     * @brief Parse source and expect success
     */
    void expect_parse_success(const std::string& source) {
        if (!parses_successfully(source)) {
            throw TestSetupError("Expected parsing to succeed, but failed");
        }
    }
    
    /**
     * @brief Parse source and expect failure
     */
    void expect_parse_failure(const std::string& source) {
        bool success = parse_source(source);
        if (success && !has_errors()) {
            throw TestSetupError("Expected parsing to fail, but succeeded");
        }
    }
};

/**
 * @brief Helper class for integration testing
 */
class IntegrationTestHelper : public CompilerTestHelper {
public:
    /**
     * @brief Test full compilation pipeline up to IR generation
     */
    bool compiles_to_ir(const std::string& source) {
        if (!parse_source(source)) {
            return false;
        }
        
        if (has_errors()) {
            return false;
        }
        
        // Run type checking
        auto ast = get_ast();
        if (!ast) {
            return false;
        }
        
        auto type_checker = compiler->type_checker();
        if (type_checker) {
            type_checker->check_program(*ast);
        }
        
        if (has_errors()) {
            return false;
        }
        
        // Try to generate IR
        return compiler->generate_ir();
    }
    
    /**
     * @brief Test that source compiles without errors
     */
    void expect_compilation_success(const std::string& source) {
        if (!compiles_to_ir(source)) {
            throw TestSetupError("Expected compilation to succeed, but failed");
        }
    }
    
    /**
     * @brief Test that source fails compilation
     */
    void expect_compilation_failure(const std::string& source) {
        bool success = compiles_to_ir(source);
        if (success && !has_errors()) {
            throw TestSetupError("Expected compilation to fail, but succeeded");
        }
    }
};

} // namespace CryoTest