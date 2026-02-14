#pragma once

#include "LSP/Protocol.hpp"
#include "LSP/AnalysisEngine.hpp"
#include "AST/ASTNode.hpp"
#include "Types/SymbolTable.hpp"
#include <optional>
#include <string>

namespace CryoLSP
{

    class HoverProvider
    {
    public:
        HoverProvider(AnalysisEngine &engine);

        std::optional<Hover> getHover(const std::string &uri, const Position &position);

    private:
        AnalysisEngine &_engine;

        // Format hover for specific declaration types
        std::string formatFunctionHover(Cryo::FunctionDeclarationNode *func);
        std::string formatStructHover(Cryo::StructDeclarationNode *decl);
        std::string formatClassHover(Cryo::ClassDeclarationNode *decl);
        std::string formatEnumHover(Cryo::EnumDeclarationNode *decl);
        std::string formatVariableHover(Cryo::VariableDeclarationNode *decl);
        std::string formatEnumVariantHover(Cryo::EnumVariantNode *variant);
        std::string formatTraitHover(Cryo::TraitDeclarationNode *decl);

        // Format hover from symbol table lookup (for references)
        std::string formatSymbolRefHover(Cryo::Symbol *sym, Cryo::ASTNode *ast_root);

        // Format hover for literals (42, "hello", true, etc.)
        std::string formatLiteralHover(Cryo::LiteralNode *literal);

        // Format hover for primitive type names (int, string, bool, etc.)
        static std::string getPrimitiveTypeHover(const std::string &type_name);
    };

} // namespace CryoLSP
