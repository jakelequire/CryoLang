#include "../include/CompletionProvider.hpp"
#include <sstream>
#include <algorithm>

namespace CryoLSP
{

    CompletionProvider::CompletionProvider(Cryo::CompilerInstance *compiler, SymbolProvider *symbol_provider)
        : _compiler(compiler), _symbol_provider(symbol_provider)
    {
        initialize_static_completions();
    }

    CompletionList CompletionProvider::get_completions(const std::string &uri, const Position &position)
    {
        CompletionList completion_list;
        completion_list.is_incomplete = false;

        if (!_compiler || !_symbol_provider)
        {
            return completion_list;
        }

        // Simple implementation - just return basic keyword completions
        CompletionItem item;
        item.label = "let";
        item.kind = CompletionItemKind::Keyword;
        completion_list.items.push_back(item);

        item.label = "fn";
        item.kind = CompletionItemKind::Keyword;
        completion_list.items.push_back(item);

        item.label = "struct";
        item.kind = CompletionItemKind::Keyword;
        completion_list.items.push_back(item);

        return completion_list;
    }

    CompletionContext CompletionProvider::analyze_completion_context(const std::string &content, const Position &position)
    {
        return CompletionContext::GlobalScope;
    }

    std::vector<CompletionItem> CompletionProvider::get_keyword_completions(const std::string &prefix)
    {
        std::vector<CompletionItem> items;
        // TODO: Implement keyword completions based on _keywords
        return items;
    }

    std::vector<CompletionItem> CompletionProvider::get_type_completions(const std::string &prefix)
    {
        std::vector<CompletionItem> items;
        // TODO: Implement type completions
        return items;
    }

    std::vector<CompletionItem> CompletionProvider::get_member_completions(const std::string &uri, const Position &position, const std::string &prefix)
    {
        std::vector<CompletionItem> items;
        // TODO: Implement member completions
        return items;
    }

    void CompletionProvider::initialize_static_completions()
    {
        // Initialize keyword set
        _keywords.insert("let");
        _keywords.insert("mut");
        _keywords.insert("fn");
        _keywords.insert("return");
        _keywords.insert("if");
        _keywords.insert("else");
        _keywords.insert("while");
        _keywords.insert("for");
        _keywords.insert("break");
        _keywords.insert("continue");
        _keywords.insert("struct");
        _keywords.insert("enum");
        _keywords.insert("impl");
        _keywords.insert("trait");
        _keywords.insert("match");
        _keywords.insert("use");
        _keywords.insert("mod");
        _keywords.insert("pub");
        _keywords.insert("const");
        _keywords.insert("static");
        _keywords.insert("extern");
        _keywords.insert("type");
        _keywords.insert("as");
        _keywords.insert("in");

        // Initialize primitive types
        _primitives.insert("i8");
        _primitives.insert("i16");
        _primitives.insert("i32");
        _primitives.insert("i64");
        _primitives.insert("u8");
        _primitives.insert("u16");
        _primitives.insert("u32");
        _primitives.insert("u64");
        _primitives.insert("f32");
        _primitives.insert("f64");
        _primitives.insert("bool");
        _primitives.insert("char");
        _primitives.insert("str");
        _primitives.insert("void");
    }

} // namespace CryoLSP