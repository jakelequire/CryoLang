#pragma once

#include "GDM/GDM.hpp"
#include "GDM/ErrorCodes.hpp"
#include "AST/ASTNode.hpp"
#include "AST/Type.hpp"
#include "Lexer/lexer.hpp"
#include <memory>
#include <string>

namespace Cryo
{
    // Forward declarations
    class DiagnosticManager;
    class ASTNode;
    class Type;
    class Token;

    /**
     * Base diagnostic builder providing common functionality for all component-specific builders.
     * Handles source span extraction, node context, and basic diagnostic creation patterns.
     */
    class BaseDiagnosticBuilder
    {
    protected:
        DiagnosticManager *_diagnostic_manager;
        std::string _source_file;

    public:
        BaseDiagnosticBuilder(DiagnosticManager *manager, const std::string &source_file)
            : _diagnostic_manager(manager), _source_file(source_file) {}

        virtual ~BaseDiagnosticBuilder() = default;

    protected:
        // Helper methods for common diagnostic patterns
        SourceSpan create_node_span(ASTNode *node) const;
        SourceSpan create_token_span(const Token &token) const;
        SourceSpan create_type_span(Type *type, SourceLocation location) const;

        // Add contextual help based on error patterns
        void add_common_help(Diagnostic &diagnostic, ErrorCode code) const;

        // Extract meaningful context from AST nodes
        std::string get_node_context(ASTNode *node) const;

        // Error tracking helper - check if we should skip reporting on this node
        bool should_skip_error_reporting(ASTNode *node) const;

        // Helper to create diagnostics with automatic error flag management
        template <typename Func>
        Diagnostic *create_diagnostic_with_error_tracking(ASTNode *node, Func &&create_func) const
        {
            if (should_skip_error_reporting(node))
            {
                return nullptr; // Skip duplicate error reporting
            }

            auto *diagnostic = create_func();
            if (node)
            {
                node->mark_error(); // Mark this node as having an error
            }
            return diagnostic;
        }
    };

    /**
     * Specialized diagnostic builder for CodegenVisitor.
     * Handles LLVM-specific errors, type mapping issues, and code generation failures.
     */
    class CodegenDiagnosticBuilder : public BaseDiagnosticBuilder
    {
    public:
        CodegenDiagnosticBuilder(DiagnosticManager *manager, const std::string &source_file)
            : BaseDiagnosticBuilder(manager, source_file) {}

        // LLVM-specific error builders
        Diagnostic &create_llvm_error(const std::string &operation, ASTNode *node,
                                      const std::string &llvm_message = "");
        
        // Primary error reporting method with error code support
        Diagnostic &report_error(ErrorCode error_code, ASTNode *node = nullptr, 
                                const std::string &message = "");
        
        // Overload for custom message with error code
        Diagnostic &report_error(ErrorCode error_code, const std::string &message, 
                                ASTNode *node = nullptr);

        Diagnostic &create_type_mapping_error(Type *cryo_type, ASTNode *node,
                                              const std::string &reason = "");

        Diagnostic &create_value_generation_error(const std::string &value_type, ASTNode *node,
                                                  const std::string &context = "");

        // Code structure errors
        Diagnostic &create_invalid_control_flow_error(const std::string &statement_type,
                                                      ASTNode *node, const std::string &context = "");

        Diagnostic &create_missing_symbol_error(const std::string &symbol_name,
                                                const std::string &symbol_type, ASTNode *node);

        // Memory and allocation errors
        Diagnostic &create_allocation_error(const std::string &allocation_type, ASTNode *node);

        // Function and method errors
        Diagnostic &create_function_signature_error(const std::string &function_name,
                                                    ASTNode *node, const std::string &issue);

        // Array and indexing errors
        Diagnostic &create_array_error(const std::string &operation, ASTNode *node,
                                       const std::string &details = "");

        // Field access errors
        Diagnostic &create_field_access_error(const std::string &field_name,
                                              const std::string &type_name, ASTNode *node);

        // Pointer operation errors
        Diagnostic &create_invalid_dereference_error(const std::string &type_name, ASTNode *node);
        Diagnostic &create_invalid_address_of_error(const std::string &type_name,
                                                    const std::string &context, ASTNode *node);
    };

    /**
     * Specialized diagnostic builder for Parser.
     * Handles syntax errors, token mismatches, and parsing recovery suggestions.
     */
    class ParserDiagnosticBuilder : public BaseDiagnosticBuilder
    {
    public:
        ParserDiagnosticBuilder(DiagnosticManager *manager, const std::string &source_file)
            : BaseDiagnosticBuilder(manager, source_file) {}

        // Token-related errors
        Diagnostic &create_unexpected_token_error(const Token &found, TokenKind expected,
                                                  const std::string &context = "");

        Diagnostic &create_missing_token_error(TokenKind expected, SourceLocation location,
                                               const std::string &context = "");

        // Delimiter and bracket errors
        Diagnostic &create_missing_delimiter_error(char delimiter, SourceLocation location,
                                                   SourceLocation opening_location = SourceLocation());

        Diagnostic &create_mismatched_delimiter_error(char found, char expected,
                                                      SourceLocation location,
                                                      SourceLocation opening_location = SourceLocation());

        // Expression and statement errors
        Diagnostic &create_expected_expression_error(SourceLocation location,
                                                     const std::string &context = "");

        Diagnostic &create_invalid_syntax_error(SourceLocation location,
                                                const std::string &construct = "",
                                                const std::string &suggestion = "");

        // Declaration errors
        Diagnostic &create_invalid_declaration_error(const std::string &declaration_type,
                                                     SourceLocation location,
                                                     const std::string &issue = "");

        // Pattern and match errors
        Diagnostic &create_invalid_pattern_error(SourceLocation location,
                                                 const std::string &pattern_context = "");

        // Instantiation errors
        Diagnostic &create_invalid_instantiation_error(const std::string &type_name,
                                                      SourceLocation location,
                                                      const std::string &reason = "");
    };

    /**
     * Specialized diagnostic builder for Lexer.
     * Handles tokenization errors, invalid characters, and lexical analysis issues.
     */
    class LexerDiagnosticBuilder : public BaseDiagnosticBuilder
    {
    public:
        LexerDiagnosticBuilder(DiagnosticManager *manager, const std::string &source_file)
            : BaseDiagnosticBuilder(manager, source_file) {}

        // Character and encoding errors
        Diagnostic &create_unexpected_character_error(char character, SourceLocation location);

        Diagnostic &create_invalid_unicode_error(SourceLocation location,
                                                 const std::string &sequence = "");

        // String and character literal errors
        Diagnostic &create_unterminated_string_error(SourceLocation start_location);

        Diagnostic &create_unterminated_char_error(SourceLocation start_location);

        Diagnostic &create_invalid_escape_error(char escape_char, SourceLocation location);

        // Numeric literal errors
        Diagnostic &create_invalid_number_error(const std::string &number_text,
                                                SourceLocation location,
                                                const std::string &reason = "");

        Diagnostic &create_number_overflow_error(const std::string &number_text,
                                                 SourceLocation location,
                                                 const std::string &number_type = "");

        // Base conversion errors
        Diagnostic &create_invalid_hex_error(const std::string &hex_text, SourceLocation location);

        Diagnostic &create_invalid_binary_error(const std::string &binary_text, SourceLocation location);

        Diagnostic &create_invalid_octal_error(const std::string &octal_text, SourceLocation location);
    };

    /**
     * Specialized diagnostic builder for TypeChecker.
     * Extends the existing sophisticated type checking diagnostics with additional patterns.
     */
    class TypeCheckerDiagnosticBuilder : public BaseDiagnosticBuilder
    {
    public:
        TypeCheckerDiagnosticBuilder(DiagnosticManager *manager, const std::string &source_file)
            : BaseDiagnosticBuilder(manager, source_file) {}

        // Enhanced type mismatch errors with better context
        Diagnostic &create_assignment_type_error(Type *expected, Type *actual,
                                                 SourceLocation location, ASTNode *node = nullptr);

        Diagnostic &create_operation_type_error(const std::string &operation,
                                                Type *left_type, Type *right_type,
                                                SourceLocation location);

        Diagnostic &create_function_call_error(const std::string &function_name,
                                               size_t expected_args, size_t actual_args,
                                               SourceLocation location);

        // Symbol resolution errors
        Diagnostic &create_undefined_symbol_error(const std::string &symbol_name,
                                                  NodeKind symbol_kind,
                                                  SourceLocation location,
                                                  const std::vector<std::string> &suggestions = {});

        Diagnostic &create_redefinition_error(const std::string &symbol_name,
                                              SourceLocation new_location,
                                              SourceLocation original_location);

        // NEW: Redefined symbol error with NodeKind
        Diagnostic &create_redefined_symbol_error(const std::string &symbol_name,
                                                  NodeKind symbol_kind,
                                                  SourceLocation location);

        // Access and dereference errors
        Diagnostic &create_invalid_member_access_error(const std::string &member_name,
                                                       Type *object_type,
                                                       SourceLocation location);

        Diagnostic &create_private_member_access_error(const std::string &member_name,
                                                       const std::string &type_name,
                                                       SourceLocation location);

        Diagnostic &create_invalid_dereference_error(Type *type, SourceLocation location);

        Diagnostic &create_invalid_address_of_error(SourceLocation location,
                                                    const std::string &reason = "");

        // Generic and template errors
        Diagnostic &create_generic_instantiation_error(const std::string &generic_name,
                                                       SourceLocation location,
                                                       const std::string &reason = "");

        // Constraint and validation errors
        Diagnostic &create_constraint_violation_error(const std::string &constraint,
                                                      Type *type, SourceLocation location);

        // Additional methods for TypeError migration
        Diagnostic &create_invalid_operation_error(const std::string &operation,
                                                   Type *left_type, Type *right_type,
                                                   SourceLocation location);

        // Overload with node for proper source context
        Diagnostic &create_invalid_operation_error(const std::string &operation,
                                                   Type *left_type, Type *right_type,
                                                   ASTNode *node);

        Diagnostic &create_non_callable_error(Type *type, SourceLocation location, ASTNode *node = nullptr);

        Diagnostic &create_too_many_args_error(const std::string &function_name,
                                               size_t expected, size_t actual,
                                               SourceLocation location);

        Diagnostic &create_undefined_variable_error(const std::string &symbol_name,
                                                    NodeKind symbol_kind,
                                                    SourceLocation location);

        Diagnostic &create_invalid_assignment_error(Type *target_type, Type *value_type,
                                                    SourceLocation location);

        // Generic type error with custom message
        Diagnostic &create_type_error(ErrorCode error_code, SourceLocation location,
                                     const std::string &custom_message);

        // Generic type error with node context - this provides full source code context
        Diagnostic &create_error_with_node(ErrorCode error_code, ASTNode *node,
                                          const std::string &message, const std::string &label = "");
    };

    /**
     * Factory class for creating appropriate diagnostic builders based on compiler component.
     * Provides centralized access to all diagnostic builders with proper initialization.
     */
    class DiagnosticBuilderFactory
    {
    private:
        DiagnosticManager *_diagnostic_manager;
        std::string _source_file;

    public:
        DiagnosticBuilderFactory(DiagnosticManager *manager, const std::string &source_file)
            : _diagnostic_manager(manager), _source_file(source_file) {}

        // Factory methods for each component
        std::unique_ptr<CodegenDiagnosticBuilder> create_codegen_builder();
        std::unique_ptr<ParserDiagnosticBuilder> create_parser_builder();
        std::unique_ptr<LexerDiagnosticBuilder> create_lexer_builder();
        std::unique_ptr<TypeCheckerDiagnosticBuilder> create_type_checker_builder();

        // Convenience method for getting the right builder based on context
        template <typename BuilderType>
        std::unique_ptr<BuilderType> create_builder();
    };

} // namespace Cryo