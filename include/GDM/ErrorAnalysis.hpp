#pragma once

#include "GDM/GDM.hpp"
#include "AST/Type.hpp"
#include "AST/ASTNode.hpp"
#include "AST/SymbolTable.hpp"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <optional>

namespace Cryo
{
    // Forward declarations
    class ASTNode;
    class Type;
    class SymbolTable;
    class TypeChecker;
    class SymbolTable;
    class TypeChecker;

    // ================================================================
    // Advanced Error Analysis System
    // ================================================================

    /**
     * @brief Advanced error analysis system that provides context-aware diagnostics
     *        and intelligent suggestions based on compiler state and AST information.
     *
     * This system is inspired by Rust's sophisticated error handling and provides:
     * - Type-aware error messages with smart suggestions
     * - Context-sensitive help based on compiler state
     * - Fuzzy matching for typos and similar names
     * - Rich diagnostic payloads with structured data
     */
    class ErrorAnalysis
    {
    public:
        // ================================================================
        // Error Context Tracking
        // ================================================================

        /**
         * @brief Tracks the current context in the compiler for better error messages
         */
        struct CompilerContext
        {
            std::string current_function;
            std::string current_namespace;
            std::string current_class;
            TypeRef expected_return_type = nullptr;
            const SymbolTable *current_scope = nullptr;
            std::vector<std::string> available_symbols;
            std::unordered_map<std::string, TypeRef> available_types;

            CompilerContext() = default;

            void set_function_context(const std::string &function_name, TypeRef return_type);
            void set_class_context(const std::string &class_name);
            void set_namespace_context(const std::string &namespace_name);
            void update_scope(const SymbolTable *scope);
        };

        // ================================================================
        // Type Analysis
        // ================================================================

        /**
         * @brief Analyzes type mismatches and provides intelligent suggestions
         */
        struct TypeMismatchAnalysis
        {
            TypeRef expected_type;
            TypeRef actual_type;
            std::string context;
            std::vector<std::string> suggestions;
            std::vector<std::string> help_messages;
            bool can_convert;
            bool can_cast;
            bool likely_typo;

            TypeMismatchAnalysis(TypeRef expected, TypeRef actual, const std::string &ctx);

            // Analyze and generate suggestions
            void analyze();
            std::string generate_primary_message() const;
            std::string generate_inline_label() const;
        };

        /**
         * @brief Analyzes undefined symbol errors and suggests alternatives
         */
        struct UndefinedSymbolAnalysis
        {
            std::string symbol_name;
            std::vector<std::string> available_symbols;
            std::vector<std::string> similar_symbols;
            std::string context;

            UndefinedSymbolAnalysis(const std::string &name, const std::vector<std::string> &available);

            void analyze();
            std::string generate_primary_message() const;
            std::vector<std::string> generate_suggestions() const;
        };

        // ================================================================
        // Main Error Analysis Interface
        // ================================================================

    private:
        CompilerContext _current_context;
        std::unordered_map<std::string, std::vector<std::string>> _typo_cache;

    public:
        ErrorAnalysis() = default;

        // Context management
        void update_context(const CompilerContext &context);
        void set_function_context(const std::string &function_name, TypeRef return_type);
        void set_class_context(const std::string &class_name);
        void set_namespace_context(const std::string &namespace_name);
        void update_scope(const SymbolTable *scope);

        // Advanced diagnostic creation
        Diagnostic create_type_mismatch_diagnostic(
            const SourceSpan &error_span,
            TypeRef expected_type,
            TypeRef actual_type,
            const std::string &context);

        Diagnostic create_undefined_symbol_diagnostic(
            const SourceSpan &error_span,
            const std::string &symbol_name,
            const std::string &context = "");

        Diagnostic create_invalid_operation_diagnostic(
            const SourceSpan &error_span,
            const std::string &operation,
            TypeRef left_type,
            TypeRef right_type = nullptr);

        Diagnostic create_field_access_diagnostic(
            const SourceSpan &error_span,
            const std::string &field_name,
            TypeRef struct_type);

        Diagnostic create_function_call_diagnostic(
            const SourceSpan &error_span,
            const std::string &function_name,
            const std::vector<TypeRef> &provided_args,
            const std::vector<TypeRef> &expected_args);

        // Advanced analysis methods
        TypeMismatchAnalysis analyze_type_mismatch(
            TypeRef expected,
            TypeRef actual,
            const std::string &context);

        UndefinedSymbolAnalysis analyze_undefined_symbol(
            const std::string &symbol_name,
            const std::string &context = "");

        // Utility methods
        std::vector<std::string> find_similar_symbols(
            const std::string &target,
            const std::vector<std::string> &candidates,
            size_t max_suggestions = 3);

        std::vector<std::string> find_similar_types(
            const std::string &target_type_name,
            size_t max_suggestions = 3);

        bool can_suggest_conversion(TypeRef from, TypeRef to);
        bool can_suggest_cast(TypeRef from, TypeRef to);
        bool are_related_types(TypeRef type1, TypeRef type2);

        // String similarity for fuzzy matching
        static double calculate_similarity(const std::string &str1, const std::string &str2);
        static size_t levenshtein_distance(const std::string &str1, const std::string &str2);

    private:
        // Helper methods for generating suggestions
        std::vector<std::string> generate_conversion_suggestions(TypeRef from, TypeRef to);
        std::vector<std::string> generate_cast_suggestions(TypeRef from, TypeRef to);
        std::vector<std::string> generate_context_help(const std::string &context);

        // Caching for performance
        void cache_similar_symbols(const std::string &target, const std::vector<std::string> &similar);
        std::optional<std::vector<std::string>> get_cached_similar_symbols(const std::string &target);
    };

    // ================================================================
    // Advanced Diagnostic Payload Implementations
    // ================================================================

    /**
     * @brief Type mismatch context with full analysis
     */
    class TypeMismatchContext
    {
    private:
        TypeRef _expected_type;
        TypeRef _actual_type;
        std::string _context;
        ErrorAnalysis::TypeMismatchAnalysis _analysis;

    public:
        TypeMismatchContext(TypeRef expected, TypeRef actual, const std::string &context);

        // Getters
        TypeRef expected_type() const { return _expected_type; }
        TypeRef actual_type() const { return _actual_type; }
        const std::string &context() const { return _context; }
        const ErrorAnalysis::TypeMismatchAnalysis &analysis() const { return _analysis; }

        // Generate smart error content
        std::string generate_primary_message() const;
        std::string generate_inline_label() const;
        std::vector<std::string> generate_suggestions() const;
        std::vector<std::string> generate_help_messages() const;
        std::vector<CodeSuggestion> generate_code_suggestions(const SourceSpan &span) const;

        // Compatibility methods from old TypeMismatchContext
        bool can_suggest_parsing() const;
        bool can_suggest_cast() const;
        bool are_related_types() const;
    };

    /**
     * @brief Undefined symbol context with suggestions
     */
    class UndefinedSymbolContext
    {
    private:
        std::string _symbol_name;
        std::string _context;
        ErrorAnalysis::UndefinedSymbolAnalysis _analysis;

    public:
        UndefinedSymbolContext(const std::string &symbol_name,
                               const std::vector<std::string> &available_symbols,
                               const std::string &context = "");

        // Getters
        const std::string &symbol_name() const { return _symbol_name; }
        const std::string &context() const { return _context; }
        const ErrorAnalysis::UndefinedSymbolAnalysis &analysis() const { return _analysis; }

        // Generate smart error content
        std::string generate_primary_message() const;
        std::string generate_inline_label() const;
        std::vector<std::string> generate_suggestions() const;
        std::vector<CodeSuggestion> generate_code_suggestions(const SourceSpan &span) const;
    };

    // ================================================================
    // Advanced Diagnostic Payload System
    // ================================================================

    /**
     * @brief Diagnostic payload with ErrorCode-based type determination
     */
    class DiagnosticPayload
    {
    private:
        ErrorCode _error_code;
        std::unique_ptr<void, void (*)(void *)> _data;

    public:
        DiagnosticPayload() : _error_code(ErrorCode::E0000_UNKNOWN), _data(nullptr, [](void *) {}) {}

        // Move-only semantics
        DiagnosticPayload(DiagnosticPayload &&other) noexcept;
        DiagnosticPayload &operator=(DiagnosticPayload &&other) noexcept;
        DiagnosticPayload(const DiagnosticPayload &) = delete;
        DiagnosticPayload &operator=(const DiagnosticPayload &) = delete;

        // ErrorCode-based payload creation
        static DiagnosticPayload create_from_error_code(ErrorCode error_code, const std::any &context = {});

        // Payload access based on ErrorCode
        ErrorCode error_code() const { return _error_code; }
        bool has_type_mismatch() const;
        bool has_undefined_symbol() const;
        bool has_invalid_operation() const;
        bool has_argument_mismatch() const;

        const TypeMismatchContext *get_type_mismatch() const;
        const UndefinedSymbolContext *get_undefined_symbol() const;

        // Helper to determine if ErrorCode represents specific error types
        static bool is_type_mismatch_error(ErrorCode code);
        static bool is_undefined_symbol_error(ErrorCode code);
        static bool is_invalid_operation_error(ErrorCode code);
        static bool is_argument_mismatch_error(ErrorCode code);
    };

} // namespace Cryo