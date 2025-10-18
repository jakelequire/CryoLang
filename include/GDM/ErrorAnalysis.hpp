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
            const Type* expected_return_type = nullptr;
            const SymbolTable* current_scope = nullptr;
            std::vector<std::string> available_symbols;
            std::unordered_map<std::string, const Type*> available_types;
            
            CompilerContext() = default;
            
            void set_function_context(const std::string& function_name, const Type* return_type);
            void set_class_context(const std::string& class_name);
            void set_namespace_context(const std::string& namespace_name);
            void update_scope(const SymbolTable* scope);
        };

        // ================================================================
        // Type Analysis
        // ================================================================

        /**
         * @brief Analyzes type mismatches and provides intelligent suggestions
         */
        struct TypeMismatchAnalysis
        {
            const Type* expected_type;
            const Type* actual_type;
            std::string context;
            std::vector<std::string> suggestions;
            std::vector<std::string> help_messages;
            bool can_convert;
            bool can_cast;
            bool likely_typo;
            
            TypeMismatchAnalysis(const Type* expected, const Type* actual, const std::string& ctx);
            
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
            
            UndefinedSymbolAnalysis(const std::string& name, const std::vector<std::string>& available);
            
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
        void update_context(const CompilerContext& context);
        void set_function_context(const std::string& function_name, const Type* return_type);
        void set_class_context(const std::string& class_name);
        void set_namespace_context(const std::string& namespace_name);
        void update_scope(const SymbolTable* scope);
        
        // Enhanced diagnostic creation
        Diagnostic create_type_mismatch_diagnostic(
            const SourceSpan& error_span,
            const Type* expected_type,
            const Type* actual_type,
            const std::string& context
        );
        
        Diagnostic create_undefined_symbol_diagnostic(
            const SourceSpan& error_span,
            const std::string& symbol_name,
            const std::string& context = ""
        );
        
        Diagnostic create_invalid_operation_diagnostic(
            const SourceSpan& error_span,
            const std::string& operation,
            const Type* left_type,
            const Type* right_type = nullptr
        );
        
        Diagnostic create_field_access_diagnostic(
            const SourceSpan& error_span,
            const std::string& field_name,
            const Type* struct_type
        );
        
        Diagnostic create_function_call_diagnostic(
            const SourceSpan& error_span,
            const std::string& function_name,
            const std::vector<const Type*>& provided_args,
            const std::vector<const Type*>& expected_args
        );

        // Advanced analysis methods
        TypeMismatchAnalysis analyze_type_mismatch(
            const Type* expected,
            const Type* actual,
            const std::string& context
        );
        
        UndefinedSymbolAnalysis analyze_undefined_symbol(
            const std::string& symbol_name,
            const std::string& context = ""
        );
        
        // Utility methods
        std::vector<std::string> find_similar_symbols(
            const std::string& target,
            const std::vector<std::string>& candidates,
            size_t max_suggestions = 3
        );
        
        std::vector<std::string> find_similar_types(
            const std::string& target_type_name,
            size_t max_suggestions = 3
        );
        
        bool can_suggest_conversion(const Type* from, const Type* to);
        bool can_suggest_cast(const Type* from, const Type* to);
        bool are_related_types(const Type* type1, const Type* type2);
        
        // String similarity for fuzzy matching
        static double calculate_similarity(const std::string& str1, const std::string& str2);
        static size_t levenshtein_distance(const std::string& str1, const std::string& str2);

    private:
        // Helper methods for generating suggestions
        std::vector<std::string> generate_conversion_suggestions(const Type* from, const Type* to);
        std::vector<std::string> generate_cast_suggestions(const Type* from, const Type* to);
        std::vector<std::string> generate_context_help(const std::string& context);
        
        // Caching for performance
        void cache_similar_symbols(const std::string& target, const std::vector<std::string>& similar);
        std::optional<std::vector<std::string>> get_cached_similar_symbols(const std::string& target);
    };

    // ================================================================
    // Enhanced Diagnostic Payload Implementations
    // ================================================================

    /**
     * @brief Enhanced type mismatch context with full analysis
     */
    class EnhancedTypeMismatchContext
    {
    private:
        const Type* _expected_type;
        const Type* _actual_type;
        std::string _context;
        ErrorAnalysis::TypeMismatchAnalysis _analysis;
        
    public:
        EnhancedTypeMismatchContext(const Type* expected, const Type* actual, const std::string& context);
        
        // Getters
        const Type* expected_type() const { return _expected_type; }
        const Type* actual_type() const { return _actual_type; }
        const std::string& context() const { return _context; }
        const ErrorAnalysis::TypeMismatchAnalysis& analysis() const { return _analysis; }
        
        // Generate smart error content
        std::string generate_primary_message() const;
        std::string generate_inline_label() const;
        std::vector<std::string> generate_suggestions() const;
        std::vector<std::string> generate_help_messages() const;
        std::vector<CodeSuggestion> generate_code_suggestions(const SourceSpan& span) const;
    };

    /**
     * @brief Enhanced undefined symbol context with suggestions
     */
    class EnhancedUndefinedSymbolContext
    {
    private:
        std::string _symbol_name;
        std::string _context;
        ErrorAnalysis::UndefinedSymbolAnalysis _analysis;
        
    public:
        EnhancedUndefinedSymbolContext(const std::string& symbol_name, 
                                     const std::vector<std::string>& available_symbols,
                                     const std::string& context = "");
        
        // Getters  
        const std::string& symbol_name() const { return _symbol_name; }
        const std::string& context() const { return _context; }
        const ErrorAnalysis::UndefinedSymbolAnalysis& analysis() const { return _analysis; }
        
        // Generate smart error content
        std::string generate_primary_message() const;
        std::string generate_inline_label() const;
        std::vector<std::string> generate_suggestions() const;
        std::vector<CodeSuggestion> generate_code_suggestions(const SourceSpan& span) const;
    };

    // ================================================================
    // Extended Diagnostic Payload System
    // ================================================================

    /**
     * @brief Extended diagnostic payload with more payload types
     */
    class ExtendedDiagnosticPayload
    {
    public:
        enum class PayloadKind 
        {
            None,
            TypeMismatch,
            UndefinedSymbol,
            InvalidOperation,
            FieldAccess,
            FunctionCall,
            Redefinition,
            ArgumentMismatch
        };
        
    private:
        PayloadKind _kind;
        std::unique_ptr<void, void(*)(void*)> _data;
        
    public:
        ExtendedDiagnosticPayload() : _kind(PayloadKind::None), _data(nullptr, [](void*){}) {}
        
        // Move-only semantics
        ExtendedDiagnosticPayload(ExtendedDiagnosticPayload&& other) noexcept;
        ExtendedDiagnosticPayload& operator=(ExtendedDiagnosticPayload&& other) noexcept;
        ExtendedDiagnosticPayload(const ExtendedDiagnosticPayload&) = delete;
        ExtendedDiagnosticPayload& operator=(const ExtendedDiagnosticPayload&) = delete;
        
        // Payload creation
        static ExtendedDiagnosticPayload create_type_mismatch(const EnhancedTypeMismatchContext& context);
        static ExtendedDiagnosticPayload create_undefined_symbol(const EnhancedUndefinedSymbolContext& context);
        
        // Payload access
        PayloadKind kind() const { return _kind; }
        bool has_type_mismatch() const { return _kind == PayloadKind::TypeMismatch; }
        bool has_undefined_symbol() const { return _kind == PayloadKind::UndefinedSymbol; }
        
        const EnhancedTypeMismatchContext* get_type_mismatch() const;
        const EnhancedUndefinedSymbolContext* get_undefined_symbol() const;
    };

} // namespace Cryo