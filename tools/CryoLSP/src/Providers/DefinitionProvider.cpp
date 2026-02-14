#include "LSP/Providers/DefinitionProvider.hpp"
#include "LSP/PositionFinder.hpp"
#include "LSP/Transport.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "Types/SymbolTable.hpp"

namespace CryoLSP
{

    DefinitionProvider::DefinitionProvider(AnalysisEngine &engine)
        : _engine(engine) {}

    std::optional<Location> DefinitionProvider::getDefinition(const std::string &uri, const Position &position)
    {
        std::string file_path = uri_to_path(uri);
        auto *instance = _engine.getCompilerInstance(file_path);
        if (!instance || !instance->ast_root())
            return std::nullopt;

        // Find node at cursor
        PositionFinder finder(position.line + 1, position.character + 1);
        auto found = finder.find(instance->ast_root());

        if (!found.node || found.identifier_name.empty())
            return std::nullopt;

        auto *symbol_table = instance->symbol_table();
        if (!symbol_table)
            return std::nullopt;

        // Look up symbol
        Cryo::Symbol *sym = symbol_table->lookup(found.identifier_name);
        if (!sym)
            sym = symbol_table->lookup_with_imports(found.identifier_name);

        if (!sym)
            return std::nullopt;

        // Build location from symbol
        Location loc;

        // Determine the file for the symbol
        // If the symbol has a source file set, use it; otherwise, use current file
        std::string sym_file = file_path; // default to current file

        // Convert to URI
        loc.uri = path_to_uri(sym_file);

        // Convert 1-based to 0-based
        loc.range.start.line = static_cast<int>(sym->location.line() > 0 ? sym->location.line() - 1 : 0);
        loc.range.start.character = static_cast<int>(sym->location.column() > 0 ? sym->location.column() - 1 : 0);
        loc.range.end.line = loc.range.start.line;
        loc.range.end.character = loc.range.start.character + static_cast<int>(sym->name.size());

        return loc;
    }

} // namespace CryoLSP
