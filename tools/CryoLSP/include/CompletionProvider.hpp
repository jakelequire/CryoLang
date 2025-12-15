#pragma once

#include "LSPTypes.hpp"
#include "SymbolProvider.hpp"
#include "Compiler/CompilerInstance.hpp"
#include <vector>
#include <string>
#include <unordered_set>

namespace CryoLSP
{

    enum class CompletionContext
    {
        Unknown,
        GlobalScope,
        MemberAccess,
        ScopeResolution,
        TypeContext,
        FunctionCall,
        ImportStatement,
        TemplateArguments,
        AttributeContext
    };

    /**
     * @brief Provides intelligent code completion
     *
     * Context-aware completion with:
     * - Symbol-based completions from scope
     * - Member access completions
     * - Template argument suggestions
     * - Import path completions
     * - Keyword and primitive completions
     * - Smart ranking and filtering
     */
    class CompletionProvider
    {
    public:
        CompletionProvider(Cryo::CompilerInstance *compiler, SymbolProvider *symbol_provider);
        ~CompletionProvider() = default;

        CompletionList get_completions(const std::string &uri, const Position &position);

    private:
        Cryo::CompilerInstance *_compiler;
        SymbolProvider *_symbol_provider;

        // Static completion data
        std::unordered_set<std::string> _keywords;
        std::unordered_set<std::string> _primitives;
        std::unordered_set<std::string> _intrinsics;

        // Context analysis
        CompletionContext analyze_completion_context(const std::string &content, const Position &position);
        std::string get_prefix_at_position(const std::string &content, const Position &position);
        std::string get_line_up_to_position(const std::string &content, const Position &position);

        // Completion generators
        std::vector<CompletionItem> get_global_completions(const std::string &uri, const std::string &prefix);
        std::vector<CompletionItem> get_member_completions(const std::string &uri, const Position &position, const std::string &prefix);
        std::vector<CompletionItem> get_scope_resolution_completions(const std::string &uri, const std::string &scope, const std::string &prefix);
        std::vector<CompletionItem> get_type_completions(const std::string &prefix);
        std::vector<CompletionItem> get_keyword_completions(const std::string &prefix);
        std::vector<CompletionItem> get_primitive_completions(const std::string &prefix);
        std::vector<CompletionItem> get_intrinsic_completions(const std::string &prefix);
        std::vector<CompletionItem> get_import_path_completions(const std::string &current_path, const std::string &prefix);

        // Symbol-based completions
        std::vector<CompletionItem> get_symbols_in_scope(const std::string &uri, const Position &position, const std::string &prefix);
        std::vector<CompletionItem> get_struct_members(const std::string &struct_name, const std::string &prefix);
        std::vector<CompletionItem> get_class_members(const std::string &class_name, const std::string &prefix);
        std::vector<CompletionItem> get_enum_variants(const std::string &enum_name, const std::string &prefix);

        // Completion item creation
        CompletionItem create_symbol_completion(const SymbolInfo &symbol, const std::string &prefix);
        CompletionItem create_keyword_completion(const std::string &keyword, const std::string &description);
        CompletionItem create_primitive_completion(const std::string &primitive, const std::string &description);
        CompletionItem create_function_completion(Cryo::FunctionDeclarationNode *func_node);
        CompletionItem create_struct_completion(Cryo::StructDeclarationNode *struct_node);
        CompletionItem create_class_completion(Cryo::ClassDeclarationNode *class_node);

        // Filtering and ranking
        std::vector<CompletionItem> filter_and_rank_completions(const std::vector<CompletionItem> &items, const std::string &prefix);
        double calculate_completion_score(const CompletionItem &item, const std::string &prefix);
        bool matches_prefix(const std::string &text, const std::string &prefix);

        // Initialization
        void initialize_static_completions();
        void load_keywords();
        void load_primitives();
        void load_intrinsics();
    };

} // namespace CryoLSP
