#include "GDM/ErrorAnalysis.hpp"
#include "Utils/Logger.hpp"
#include <algorithm>
#include <sstream>
#include <cctype>
#include <cmath>

namespace Cryo
{
    // ================================================================
    // CompilerContext Implementation
    // ================================================================

    void ErrorAnalysis::CompilerContext::set_function_context(const std::string &function_name, TypeRef return_type)
    {
        current_function = function_name;
        expected_return_type = return_type;
    }

    void ErrorAnalysis::CompilerContext::set_class_context(const std::string &class_name)
    {
        current_class = class_name;
    }

    void ErrorAnalysis::CompilerContext::set_namespace_context(const std::string &namespace_name)
    {
        current_namespace = namespace_name;
    }

    void ErrorAnalysis::CompilerContext::update_scope(const SymbolTable *scope)
    {
        current_scope = scope;
        // TODO: Extract available symbols from scope
        // This would require integration with the SymbolTable implementation
    }

    // ================================================================
    // TypeMismatchAnalysis Implementation
    // ================================================================

    ErrorAnalysis::TypeMismatchAnalysis::TypeMismatchAnalysis(TypeRef expected, TypeRef actual, const std::string &ctx)
        : expected_type(expected), actual_type(actual), context(ctx), can_convert(false), can_cast(false), likely_typo(false)
    {
        analyze();
    }

    void ErrorAnalysis::TypeMismatchAnalysis::analyze()
    {
        if (!expected_type || !actual_type)
        {
            return;
        }

        std::string expected_name = expected_type->display_name();
        std::string actual_name = actual_type->display_name();

        // Check for common conversion patterns
        if (expected_name == "int" && actual_name == "string")
        {
            can_convert = true;
            suggestions.push_back("try parsing with `int.parse(value)`");
            suggestions.push_back("if you meant to create a string variable, remove the type annotation");
            help_messages.push_back("string literals cannot be implicitly converted to integers");
        }
        else if (expected_name == "string" && actual_name == "int")
        {
            can_convert = true;
            suggestions.push_back("try `value.to_string()` to convert the integer");
            suggestions.push_back("use string interpolation: `\"${value}\"`");
        }
        else if (expected_name == "float" && actual_name == "int")
        {
            can_convert = true;
            suggestions.push_back("integer values can be implicitly converted to float");
            help_messages.push_back("this conversion should work automatically");
        }
        else if (expected_name == "boolean" && (actual_name == "int" || actual_name == "float"))
        {
            suggestions.push_back("use comparison operators like `value != 0` or `value > 0`");
            help_messages.push_back("numeric types don't implicitly convert to boolean in Cryo");
        }
        else if (expected_name.find("*") != std::string::npos && actual_name.find("*") == std::string::npos)
        {
            can_convert = true;
            suggestions.push_back("use address-of operator `&` to get a pointer");
            help_messages.push_back("pointer and value types are not directly compatible");
        }
        else if (expected_name.find("*") == std::string::npos && actual_name.find("*") != std::string::npos)
        {
            can_convert = true;
            suggestions.push_back("use dereference operator `*` to get the value");
            help_messages.push_back("use address-of (&) or dereference (*) operators as appropriate");
        }

        // Check for potential typos in type names
        if (ErrorAnalysis::calculate_similarity(expected_name, actual_name) > 0.7)
        {
            likely_typo = true;
            suggestions.push_back("did you mean `" + expected_name + "`?");
        }

        // Generate context-specific help
        if (context == "variable initialization")
        {
            help_messages.push_back("the variable type annotation must match the initializer type");
        }
        else if (context == "assignment")
        {
            help_messages.push_back("assignment requires compatible types");
        }
        else if (context == "function argument")
        {
            help_messages.push_back("function arguments must match the parameter types");
        }
        else if (context == "return statement")
        {
            help_messages.push_back("return value must match the function's return type");
        }
    }

    std::string ErrorAnalysis::TypeMismatchAnalysis::generate_primary_message() const
    {
        if (!expected_type || !actual_type)
        {
            return "type mismatch in " + context;
        }

        return "type mismatch in " + context + ": expected `" +
               expected_type->display_name() + "`, found `" + actual_type->display_name() + "`";
    }

    std::string ErrorAnalysis::TypeMismatchAnalysis::generate_inline_label() const
    {
        if (!expected_type || !actual_type)
        {
            return "type mismatch";
        }

        return "expected `" + expected_type->display_name() + "`, found `" + actual_type->display_name() + "`";
    }

    // ================================================================
    // UndefinedSymbolAnalysis Implementation
    // ================================================================

    ErrorAnalysis::UndefinedSymbolAnalysis::UndefinedSymbolAnalysis(const std::string &name, const std::vector<std::string> &available)
        : symbol_name(name), available_symbols(available)
    {
        analyze();
    }

    void ErrorAnalysis::UndefinedSymbolAnalysis::analyze()
    {
        // Find similar symbols using fuzzy matching
        for (const auto &symbol : available_symbols)
        {
            double similarity = ErrorAnalysis::calculate_similarity(symbol_name, symbol);
            if (similarity > 0.6)
            { // Threshold for similarity
                similar_symbols.push_back(symbol);
            }
        }

        // Sort by similarity (this is a simplified version - we'd need to track similarity scores)
        std::sort(similar_symbols.begin(), similar_symbols.end());

        // Limit to top 3 suggestions
        if (similar_symbols.size() > 3)
        {
            similar_symbols.resize(3);
        }
    }

    std::string ErrorAnalysis::UndefinedSymbolAnalysis::generate_primary_message() const
    {
        std::string base_message = "cannot find value `" + symbol_name + "` in this scope";

        if (!context.empty())
        {
            base_message += " (" + context + ")";
        }

        return base_message;
    }

    std::vector<std::string> ErrorAnalysis::UndefinedSymbolAnalysis::generate_suggestions() const
    {
        std::vector<std::string> suggestions;

        if (!similar_symbols.empty())
        {
            if (similar_symbols.size() == 1)
            {
                suggestions.push_back("did you mean `" + similar_symbols[0] + "`?");
            }
            else
            {
                std::string suggestion = "did you mean one of: ";
                for (size_t i = 0; i < similar_symbols.size(); ++i)
                {
                    if (i > 0)
                        suggestion += ", ";
                    suggestion += "`" + similar_symbols[i] + "`";
                }
                suggestion += "?";
                suggestions.push_back(suggestion);
            }
        }
        else
        {
            suggestions.push_back("make sure the variable is declared and in scope");
            suggestions.push_back("check for typos in the variable name");
        }

        return suggestions;
    }

    // ================================================================
    // ErrorAnalysis Main Implementation
    // ================================================================

    void ErrorAnalysis::update_context(const CompilerContext &context)
    {
        _current_context = context;
    }

    void ErrorAnalysis::set_function_context(const std::string &function_name, TypeRef return_type)
    {
        _current_context.set_function_context(function_name, return_type);
    }

    void ErrorAnalysis::set_class_context(const std::string &class_name)
    {
        _current_context.set_class_context(class_name);
    }

    void ErrorAnalysis::set_namespace_context(const std::string &namespace_name)
    {
        _current_context.set_namespace_context(namespace_name);
    }

    void ErrorAnalysis::update_scope(const SymbolTable *scope)
    {
        _current_context.update_scope(scope);
    }

    Diagnostic ErrorAnalysis::create_type_mismatch_diagnostic(
        const SourceSpan &error_span,
        TypeRef expected_type,
        TypeRef actual_type,
        const std::string &context)
    {
        auto analysis = analyze_type_mismatch(expected_type, actual_type, context);

        // Create enhanced diagnostic with structured analysis
        Diagnostic diagnostic(ErrorCode::E0200_TYPE_MISMATCH,
                              DiagnosticSeverity::Error,
                              DiagnosticCategory::Semantic,
                              analysis.generate_primary_message(),
                              SourceRange(error_span.start(), error_span.end()),
                              error_span.filename());

        // Add primary span with inline label
        SourceSpan primary_span = error_span;
        primary_span.set_label(analysis.generate_inline_label());
        diagnostic.with_primary_span(primary_span);

        // Add suggestions from analysis
        for (const auto &suggestion : analysis.suggestions)
        {
            diagnostic.add_help(suggestion);
        }

        // Add help messages from analysis
        for (const auto &help : analysis.help_messages)
        {
            diagnostic.add_help(help);
        }

        return diagnostic;
    }

    Diagnostic ErrorAnalysis::create_undefined_symbol_diagnostic(
        const SourceSpan &error_span,
        const std::string &symbol_name,
        const std::string &context)
    {
        auto analysis = analyze_undefined_symbol(symbol_name, context);

        // Create enhanced diagnostic
        Diagnostic diagnostic(ErrorCode::E0201_UNDEFINED_VARIABLE,
                              DiagnosticSeverity::Error,
                              DiagnosticCategory::Semantic,
                              analysis.generate_primary_message(),
                              SourceRange(error_span.start(), error_span.end()),
                              error_span.filename());

        // Add primary span
        SourceSpan primary_span = error_span;
        primary_span.set_label("not found in this scope");
        diagnostic.with_primary_span(primary_span);

        // Add suggestions
        for (const auto &suggestion : analysis.generate_suggestions())
        {
            diagnostic.add_help(suggestion);
        }

        return diagnostic;
    }

    ErrorAnalysis::TypeMismatchAnalysis ErrorAnalysis::analyze_type_mismatch(
        TypeRef expected,
        TypeRef actual,
        const std::string &context)
    {
        return TypeMismatchAnalysis(expected, actual, context);
    }

    ErrorAnalysis::UndefinedSymbolAnalysis ErrorAnalysis::analyze_undefined_symbol(
        const std::string &symbol_name,
        const std::string &context)
    {
        return UndefinedSymbolAnalysis(symbol_name, _current_context.available_symbols);
    }

    std::vector<std::string> ErrorAnalysis::find_similar_symbols(
        const std::string &target,
        const std::vector<std::string> &candidates,
        size_t max_suggestions)
    {
        std::vector<std::pair<std::string, double>> scored_candidates;

        for (const auto &candidate : candidates)
        {
            double similarity = calculate_similarity(target, candidate);
            if (similarity > 0.5)
            { // Minimum similarity threshold
                scored_candidates.emplace_back(candidate, similarity);
            }
        }

        // Sort by similarity (descending)
        std::sort(scored_candidates.begin(), scored_candidates.end(),
                  [](const auto &a, const auto &b)
                  { return a.second > b.second; });

        // Extract top suggestions
        std::vector<std::string> result;
        for (size_t i = 0; i < std::min(max_suggestions, scored_candidates.size()); ++i)
        {
            result.push_back(scored_candidates[i].first);
        }

        return result;
    }

    /*static*/ double ErrorAnalysis::calculate_similarity(const std::string &str1, const std::string &str2)
    {
        if (str1.empty() || str2.empty())
        {
            return 0.0;
        }

        size_t distance = levenshtein_distance(str1, str2);
        size_t max_len = std::max(str1.length(), str2.length());

        return 1.0 - (static_cast<double>(distance) / max_len);
    }

    /*static*/ size_t ErrorAnalysis::levenshtein_distance(const std::string &str1, const std::string &str2)
    {
        const size_t len1 = str1.length();
        const size_t len2 = str2.length();

        std::vector<std::vector<size_t>> matrix(len1 + 1, std::vector<size_t>(len2 + 1));

        for (size_t i = 0; i <= len1; ++i)
        {
            matrix[i][0] = i;
        }

        for (size_t j = 0; j <= len2; ++j)
        {
            matrix[0][j] = j;
        }

        for (size_t i = 1; i <= len1; ++i)
        {
            for (size_t j = 1; j <= len2; ++j)
            {
                size_t cost = (str1[i - 1] == str2[j - 1]) ? 0 : 1;

                matrix[i][j] = std::min({
                    matrix[i - 1][j] + 1,       // deletion
                    matrix[i][j - 1] + 1,       // insertion
                    matrix[i - 1][j - 1] + cost // substitution
                });
            }
        }

        return matrix[len1][len2];
    }

    bool ErrorAnalysis::can_suggest_conversion(TypeRef from, TypeRef to)
    {
        if (!from || !to)
            return false;

        std::string from_name = from->display_name();
        std::string to_name = to->display_name();

        // Common conversions
        if ((from_name == "int" && to_name == "float") ||
            (from_name == "int" && to_name == "string") ||
            (from_name == "float" && to_name == "string") ||
            (from_name == "boolean" && to_name == "string"))
        {
            return true;
        }

        return false;
    }

    bool ErrorAnalysis::can_suggest_cast(TypeRef from, TypeRef to)
    {
        if (!from || !to)
            return false;

        // Most types can be cast with explicit casting
        return true; // Simplified - would need more sophisticated type system analysis
    }

    bool ErrorAnalysis::are_related_types(TypeRef type1, TypeRef type2)
    {
        if (!type1 || !type2)
            return false;

        std::string name1 = type1->display_name();
        std::string name2 = type2->display_name();

        // Check for pointer vs value relationships
        if ((name1.find("*") != std::string::npos && name2.find("*") == std::string::npos) ||
            (name1.find("*") == std::string::npos && name2.find("*") != std::string::npos))
        {
            return true;
        }

        // Check for numeric type relationships
        std::vector<std::string> numeric_types = {"int", "float", "double", "i32", "i64", "f32", "f64"};
        bool type1_numeric = std::find(numeric_types.begin(), numeric_types.end(), name1) != numeric_types.end();
        bool type2_numeric = std::find(numeric_types.begin(), numeric_types.end(), name2) != numeric_types.end();

        return type1_numeric && type2_numeric;
    }

    // ================================================================
    // TypeMismatchContext Implementation
    // ================================================================

    TypeMismatchContext::TypeMismatchContext(TypeRef expected, TypeRef actual, const std::string &context)
        : _expected_type(expected), _actual_type(actual), _context(context), _analysis(expected, actual, context)
    {
    }

    std::string TypeMismatchContext::generate_primary_message() const
    {
        return _analysis.generate_primary_message();
    }

    std::string TypeMismatchContext::generate_inline_label() const
    {
        return _analysis.generate_inline_label();
    }

    std::vector<std::string> TypeMismatchContext::generate_suggestions() const
    {
        return _analysis.suggestions;
    }

    std::vector<std::string> TypeMismatchContext::generate_help_messages() const
    {
        return _analysis.help_messages;
    }

    std::vector<CodeSuggestion> TypeMismatchContext::generate_code_suggestions(const SourceSpan &span) const
    {
        std::vector<CodeSuggestion> suggestions;

        if (_analysis.can_convert && _expected_type && _actual_type)
        {
            std::string expected_name = _expected_type->display_name();
            std::string actual_name = _actual_type->display_name();

            if (expected_name == "int" && actual_name == "string")
            {
                suggestions.emplace_back(
                    "parse the string to integer",
                    span,
                    "int.parse(" + std::string(span.start().column(), ' ') + ")",
                    SuggestionApplicability::MaybeIncorrect);
            }
        }

        return suggestions;
    }

    // Compatibility methods from old TypeMismatchContext
    bool TypeMismatchContext::can_suggest_parsing() const
    {
        return _analysis.can_convert && _expected_type && _actual_type &&
               _expected_type->display_name() == "int" && _actual_type->display_name() == "string";
    }

    bool TypeMismatchContext::can_suggest_cast() const
    {
        return _analysis.can_cast;
    }

    bool TypeMismatchContext::are_related_types() const
    {
        if (!_expected_type || !_actual_type)
            return false;

        std::string expected_str = _expected_type->display_name();
        std::string actual_str = _actual_type->display_name();

        // Check for related numeric types
        std::vector<std::string> numeric_types = {"int", "i32", "i64", "u32", "u64", "float", "f32", "f64", "double"};
        bool expected_numeric = std::find(numeric_types.begin(), numeric_types.end(), expected_str) != numeric_types.end();
        bool actual_numeric = std::find(numeric_types.begin(), numeric_types.end(), actual_str) != numeric_types.end();

        return expected_numeric && actual_numeric;
    }

    // ================================================================
    // UndefinedSymbolContext Implementation
    // ================================================================

    UndefinedSymbolContext::UndefinedSymbolContext(const std::string &symbol_name,
                                                   const std::vector<std::string> &available_symbols,
                                                   const std::string &context)
        : _symbol_name(symbol_name), _context(context), _analysis(symbol_name, available_symbols)
    {
    }

    std::string UndefinedSymbolContext::generate_primary_message() const
    {
        return _analysis.generate_primary_message();
    }

    std::string UndefinedSymbolContext::generate_inline_label() const
    {
        return "not found in this scope";
    }

    std::vector<std::string> UndefinedSymbolContext::generate_suggestions() const
    {
        return _analysis.generate_suggestions();
    }

    std::vector<CodeSuggestion> UndefinedSymbolContext::generate_code_suggestions(const SourceSpan &span) const
    {
        std::vector<CodeSuggestion> suggestions;

        if (!_analysis.similar_symbols.empty())
        {
            // Suggest the most similar symbol as a replacement
            const std::string &best_match = _analysis.similar_symbols[0];
            suggestions.emplace_back(
                "replace with `" + best_match + "`",
                span,
                best_match,
                SuggestionApplicability::MaybeIncorrect);
        }

        return suggestions;
    }

    // ================================================================
    // DiagnosticPayload Implementation - ErrorCode-based
    // ================================================================

    DiagnosticPayload::DiagnosticPayload(DiagnosticPayload &&other) noexcept
        : _error_code(other._error_code), _data(std::move(other._data))
    {
        other._error_code = ErrorCode::E0000_UNKNOWN;
    }

    DiagnosticPayload &DiagnosticPayload::operator=(DiagnosticPayload &&other) noexcept
    {
        if (this != &other)
        {
            _error_code = other._error_code;
            _data = std::move(other._data);
            other._error_code = ErrorCode::E0000_UNKNOWN;
        }
        return *this;
    }

    // ErrorCode-based payload creation
    DiagnosticPayload DiagnosticPayload::create_from_error_code(ErrorCode error_code, const std::any &context)
    {
        DiagnosticPayload payload;
        payload._error_code = error_code;

        // Create appropriate payload data based on ErrorCode
        if (is_type_mismatch_error(error_code))
        {
            // Try to extract TypeMismatchContext from std::any
            try
            {
                auto context_data = std::any_cast<std::pair<TypeRef, TypeRef>>(context);
                TypeMismatchContext *heap_context = new TypeMismatchContext(
                    context_data.first, context_data.second, "");
                payload._data = std::unique_ptr<void, void (*)(void *)>(
                    heap_context,
                    [](void *ptr)
                    { delete static_cast<TypeMismatchContext *>(ptr); });
            }
            catch (const std::bad_any_cast &)
            {
                // Context not provided or wrong type - create minimal context
                TypeMismatchContext *heap_context = new TypeMismatchContext(TypeRef{}, TypeRef{}, "");
                payload._data = std::unique_ptr<void, void (*)(void *)>(
                    heap_context,
                    [](void *ptr)
                    { delete static_cast<TypeMismatchContext *>(ptr); });
            }
        }
        else if (is_undefined_symbol_error(error_code))
        {
            try
            {
                auto context_data = std::any_cast<std::pair<std::string, std::vector<std::string>>>(context);
                UndefinedSymbolContext *heap_context = new UndefinedSymbolContext(
                    context_data.first, context_data.second, "");
                payload._data = std::unique_ptr<void, void (*)(void *)>(
                    heap_context,
                    [](void *ptr)
                    { delete static_cast<UndefinedSymbolContext *>(ptr); });
            }
            catch (const std::bad_any_cast &)
            {
                // Context not provided or wrong type - create minimal context
                UndefinedSymbolContext *heap_context = new UndefinedSymbolContext("", {}, "");
                payload._data = std::unique_ptr<void, void (*)(void *)>(
                    heap_context,
                    [](void *ptr)
                    { delete static_cast<UndefinedSymbolContext *>(ptr); });
            }
        }

        return payload;
    }

    // Payload access based on ErrorCode
    bool DiagnosticPayload::has_type_mismatch() const
    {
        return is_type_mismatch_error(_error_code);
    }

    bool DiagnosticPayload::has_undefined_symbol() const
    {
        return is_undefined_symbol_error(_error_code);
    }

    bool DiagnosticPayload::has_invalid_operation() const
    {
        return is_invalid_operation_error(_error_code);
    }

    bool DiagnosticPayload::has_argument_mismatch() const
    {
        return is_argument_mismatch_error(_error_code);
    }

    // Helper to determine if ErrorCode represents specific error types
    bool DiagnosticPayload::is_type_mismatch_error(ErrorCode code)
    {
        return code == ErrorCode::E0200_TYPE_MISMATCH ||
               code == ErrorCode::E0208_INVALID_CAST ||
               code == ErrorCode::E0211_INCOMPATIBLE_TYPES;
    }

    bool DiagnosticPayload::is_undefined_symbol_error(ErrorCode code)
    {
        return code == ErrorCode::E0201_UNDEFINED_VARIABLE ||
               code == ErrorCode::E0202_UNDEFINED_FUNCTION ||
               code == ErrorCode::E0203_UNDEFINED_TYPE ||
               code == ErrorCode::E0204_UNDEFINED_FIELD;
    }

    bool DiagnosticPayload::is_invalid_operation_error(ErrorCode code)
    {
        return code == ErrorCode::E0209_INVALID_OPERATION ||
               code == ErrorCode::E0210_INVALID_ASSIGNMENT ||
               code == ErrorCode::E0222_INVALID_DEREF ||
               code == ErrorCode::E0223_INVALID_ADDRESS_OF ||
               code == ErrorCode::E0224_INVALID_INDEX ||
               code == ErrorCode::E0229_INVALID_BINARY_OP ||
               code == ErrorCode::E0230_INVALID_UNARY_OP ||
               code == ErrorCode::E0231_NON_CALLABLE_TYPE ||
               code == ErrorCode::E0232_INVALID_ASSIGNMENT_TARGET ||
               code == ErrorCode::E0233_UNDEFINED_SYMBOL;
    }

    bool DiagnosticPayload::is_argument_mismatch_error(ErrorCode code)
    {
        return code == ErrorCode::E0214_ARGUMENT_MISMATCH ||
               code == ErrorCode::E0215_TOO_MANY_ARGS ||
               code == ErrorCode::E0216_TOO_FEW_ARGS;
    }

    const TypeMismatchContext *DiagnosticPayload::get_type_mismatch() const
    {
        if (!has_type_mismatch())
            return nullptr;
        return static_cast<const TypeMismatchContext *>(_data.get());
    }

    const UndefinedSymbolContext *DiagnosticPayload::get_undefined_symbol() const
    {
        if (!has_undefined_symbol())
            return nullptr;
        return static_cast<const UndefinedSymbolContext *>(_data.get());
    }

} // namespace Cryo