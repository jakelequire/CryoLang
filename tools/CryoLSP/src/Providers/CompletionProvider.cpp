#include "LSP/Providers/CompletionProvider.hpp"
#include "LSP/Transport.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "Types/SymbolTable.hpp"

namespace CryoLSP
{

    CompletionProvider::CompletionProvider(AnalysisEngine &engine)
        : _engine(engine) {}

    CompletionList CompletionProvider::getCompletions(const std::string &uri, const Position &position)
    {
        CompletionList result;
        result.isIncomplete = false;

        std::string file_path = uri_to_path(uri);
        auto *instance = _engine.getCompilerInstance(file_path);

        // Add symbols from the compiler's symbol table
        if (instance && instance->symbol_table())
        {
            auto symbols = instance->symbol_table()->get_all_symbols_for_lsp();

            for (const auto &sym : symbols)
            {
                CompletionItem item;
                item.label = sym.name;

                // Map SymbolKind to CompletionItemKind
                switch (sym.kind)
                {
                case Cryo::SymbolKind::Variable:
                    item.kind = CompletionItemKind::Variable;
                    break;
                case Cryo::SymbolKind::Parameter:
                    item.kind = CompletionItemKind::Variable;
                    break;
                case Cryo::SymbolKind::Function:
                    item.kind = CompletionItemKind::Function;
                    break;
                case Cryo::SymbolKind::Method:
                    item.kind = CompletionItemKind::Method;
                    break;
                case Cryo::SymbolKind::Type:
                    item.kind = CompletionItemKind::Class;
                    break;
                case Cryo::SymbolKind::TypeAlias:
                    item.kind = CompletionItemKind::Class;
                    break;
                case Cryo::SymbolKind::Constant:
                    item.kind = CompletionItemKind::Constant;
                    break;
                case Cryo::SymbolKind::Field:
                    item.kind = CompletionItemKind::Field;
                    break;
                case Cryo::SymbolKind::EnumVariant:
                    item.kind = CompletionItemKind::EnumMember;
                    break;
                case Cryo::SymbolKind::GenericParam:
                    item.kind = CompletionItemKind::TypeParameter;
                    break;
                case Cryo::SymbolKind::Namespace:
                    item.kind = CompletionItemKind::Module;
                    break;
                case Cryo::SymbolKind::Import:
                    item.kind = CompletionItemKind::Module;
                    break;
                case Cryo::SymbolKind::Intrinsic:
                    item.kind = CompletionItemKind::Function;
                    break;
                default:
                    item.kind = CompletionItemKind::Text;
                    break;
                }

                // Add type info as detail
                if (sym.type.is_valid())
                {
                    item.detail = sym.type->display_name();
                }

                // Add documentation
                if (!sym.documentation.empty())
                {
                    item.documentation = sym.documentation;
                }

                result.items.push_back(std::move(item));
            }
        }

        // Always add keywords
        addKeywords(result.items);

        // Add built-in types
        addBuiltinTypes(result.items);

        return result;
    }

    void CompletionProvider::addKeywords(std::vector<CompletionItem> &items)
    {
        static const std::vector<std::string> keywords = {
            "fn", "function", "let", "const", "mut", "if", "else", "while",
            "for", "return", "struct", "class", "enum", "impl", "trait",
            "match", "import", "from", "pub", "public", "private",
            "true", "false", "null", "new", "self", "type", "as",
            "break", "continue", "do", "switch", "case", "default",
            "unsafe", "extern", "static", "inline", "where",
        };

        for (const auto &kw : keywords)
        {
            CompletionItem item;
            item.label = kw;
            item.kind = CompletionItemKind::Keyword;
            items.push_back(std::move(item));
        }
    }

    void CompletionProvider::addBuiltinTypes(std::vector<CompletionItem> &items)
    {
        static const std::vector<std::string> types = {
            "int", "i8", "i16", "i32", "i64", "i128",
            "uint", "u8", "u16", "u32", "u64", "u128",
            "float", "f32", "f64",
            "bool", "char", "string", "void",
        };

        for (const auto &t : types)
        {
            CompletionItem item;
            item.label = t;
            item.kind = CompletionItemKind::Class;
            item.detail = "built-in type";
            items.push_back(std::move(item));
        }
    }

} // namespace CryoLSP
