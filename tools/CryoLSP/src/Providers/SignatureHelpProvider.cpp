#include "LSP/Providers/SignatureHelpProvider.hpp"
#include "LSP/Transport.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "Types/SymbolTable.hpp"
#include "LSP/PositionFinder.hpp"
#include "AST/ASTNode.hpp"

namespace CryoLSP
{

    SignatureHelpProvider::SignatureHelpProvider(AnalysisEngine &engine)
        : _engine(engine) {}

    std::optional<SignatureHelp> SignatureHelpProvider::getSignatureHelp(const std::string &uri, const Position &position)
    {
        std::string file_path = uri_to_path(uri);
        auto *instance = _engine.getCompilerInstance(file_path);
        if (!instance || !instance->symbol_table())
            return std::nullopt;

        // Get the document content to find what function call we're in
        // For now, use a simplified approach: find the nearest call expression
        // by looking at the AST near the cursor position

        if (!instance->ast_root())
            return std::nullopt;

        // Walk backward from cursor to find a call context
        // Try positions to the left of cursor to find the function name
        for (int col = position.character; col >= 0; --col)
        {
            PositionFinder finder(position.line + 1, col + 1);
            auto found = finder.find(instance->ast_root());

            if (!found.node || found.identifier_name.empty())
                continue;

            // Look up the function
            Cryo::Symbol *sym = instance->symbol_table()->lookup(found.identifier_name);
            if (!sym)
                sym = instance->symbol_table()->lookup_with_imports(found.identifier_name);

            if (sym && (sym->kind == Cryo::SymbolKind::Function || sym->kind == Cryo::SymbolKind::Method))
            {
                SignatureHelp help;
                SignatureInformation sig;

                // Build signature label
                std::string label = sym->name + "(";

                // We can get parameter info from function overloads
                auto overloads = instance->symbol_table()->get_overloads(sym->name);
                if (!overloads.empty())
                {
                    // Use first overload for now
                    // In a more complete implementation, we'd match based on argument count
                }

                // Basic signature from the symbol
                sig.label = label + ")";
                if (sym->type.is_valid())
                {
                    sig.label += " -> " + sym->type->display_name();
                }

                if (!sym->documentation.empty())
                {
                    sig.documentation = sym->documentation;
                }

                help.signatures.push_back(std::move(sig));

                // Estimate active parameter by counting commas before cursor
                // This is a simplified heuristic
                help.activeParameter = 0;

                return help;
            }
        }

        return std::nullopt;
    }

} // namespace CryoLSP
