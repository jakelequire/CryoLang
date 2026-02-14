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
        Transport::log("[Definition] Request for " + file_path + " at " +
                       std::to_string(position.line) + ":" + std::to_string(position.character));

        auto *instance = _engine.getCompilerInstance(file_path);
        if (!instance || !instance->ast_root())
        {
            Transport::log("[Definition] No compiler instance or AST for file");
            return std::nullopt;
        }

        // Find node at cursor
        Transport::log("[Definition] Running PositionFinder...");
        PositionFinder finder(position.line + 1, position.character + 1);
        auto found = finder.find(instance->ast_root());
        Transport::log("[Definition] PositionFinder done: identifier='" + found.identifier_name + "'");

        if (!found.node || found.identifier_name.empty())
            return std::nullopt;

        auto *symbol_table = instance->symbol_table();

        // Look up symbol in the current file's symbol table
        Cryo::Symbol *sym = nullptr;
        if (symbol_table)
        {
            Transport::log("[Definition] Looking up symbol in symbol table...");
            sym = symbol_table->lookup(found.identifier_name);
            if (!sym)
                sym = symbol_table->lookup_with_imports(found.identifier_name);
            Transport::log("[Definition] Symbol table lookup: " + std::string(sym ? "found" : "not found"));
        }

        if (sym)
        {
            // Build location from symbol
            Location loc;
            std::string sym_file = file_path;
            loc.uri = path_to_uri(sym_file);
            loc.range.start.line = static_cast<int>(sym->location.line() > 0 ? sym->location.line() - 1 : 0);
            loc.range.start.character = static_cast<int>(sym->location.column() > 0 ? sym->location.column() - 1 : 0);
            loc.range.end.line = loc.range.start.line;
            loc.range.end.character = loc.range.start.character + static_cast<int>(sym->name.size());
            return loc;
        }

        // Fallback: search intrinsics file
        auto *intrinsics = _engine.getIntrinsicsInstance();

        if (intrinsics && intrinsics->ast_root())
        {
            // Direct iteration over ProgramNode children (avoids visitor dispatch)
            Cryo::ASTNode *declNode = nullptr;
            for (const auto &child : intrinsics->ast_root()->statements())
            {
                if (!child)
                    continue;
                if (auto *decl = dynamic_cast<Cryo::IntrinsicDeclarationNode *>(child.get()))
                {
                    if (decl->name() == found.identifier_name)
                    {
                        declNode = decl;
                        break;
                    }
                }
                else if (auto *func = dynamic_cast<Cryo::FunctionDeclarationNode *>(child.get()))
                {
                    if (func->name() == found.identifier_name)
                    {
                        declNode = func;
                        break;
                    }
                }
            }

            if (declNode)
            {
                Location loc;
                loc.uri = path_to_uri(_engine.getIntrinsicsFilePath());

                // Use name_location for FunctionDeclarationNode, or location for IntrinsicDeclarationNode
                auto *funcDecl = dynamic_cast<Cryo::FunctionDeclarationNode *>(declNode);
                auto *intrinsicDecl = dynamic_cast<Cryo::IntrinsicDeclarationNode *>(declNode);

                Cryo::SourceLocation srcLoc = declNode->location();
                if (funcDecl && funcDecl->has_name_location())
                    srcLoc = funcDecl->name_location();
                else if (intrinsicDecl)
                    srcLoc = intrinsicDecl->location();

                loc.range.start.line = static_cast<int>(srcLoc.line() > 0 ? srcLoc.line() - 1 : 0);
                loc.range.start.character = static_cast<int>(srcLoc.column() > 0 ? srcLoc.column() - 1 : 0);
                loc.range.end.line = loc.range.start.line;
                loc.range.end.character = loc.range.start.character + static_cast<int>(found.identifier_name.size());
                return loc;
            }
        }

        Transport::log("[Definition] No definition found");
        return std::nullopt;
    }

} // namespace CryoLSP
