#pragma once

#include "LSP/Protocol.hpp"
#include "LSP/AnalysisEngine.hpp"
#include "LSP/DocumentStore.hpp"
#include "AST/ASTNode.hpp"
#include "Types/SymbolTable.hpp"
#include <optional>
#include <string>
#include <vector>

namespace CryoLSP
{

    class HoverProvider
    {
    public:
        HoverProvider(AnalysisEngine &engine, DocumentStore &documents);

        std::optional<Hover> getHover(const std::string &uri, const Position &position);

    private:
        AnalysisEngine &_engine;
        DocumentStore &_documents;

        // Format hover for specific declaration types
        // type_args: concrete type arguments for generic instantiation (e.g., {"string"} for Array<string>)
        std::string formatFunctionHover(Cryo::FunctionDeclarationNode *func,
                                        const std::vector<std::string> &type_args = {});
        std::string formatStructHover(Cryo::StructDeclarationNode *decl,
                                      const std::vector<std::string> &type_args = {});
        std::string formatClassHover(Cryo::ClassDeclarationNode *decl,
                                     const std::vector<std::string> &type_args = {});
        std::string formatEnumHover(Cryo::EnumDeclarationNode *decl);
        std::string formatVariableHover(Cryo::VariableDeclarationNode *decl);
        std::string formatEnumVariantHover(Cryo::EnumVariantNode *variant, int variant_index = -1);
        std::string formatTraitHover(Cryo::TraitDeclarationNode *decl);

        // Format hover from symbol table lookup (for references)
        std::string formatSymbolRefHover(Cryo::Symbol *sym, Cryo::ASTNode *ast_root,
                                         const std::vector<std::string> &type_args = {});

        // Format hover for a module/namespace showing its exported declarations
        std::string formatNamespaceHover(const std::string &name, Cryo::ProgramNode *ast);

        // Format hover for literals (42, "hello", true, etc.)
        std::string formatLiteralHover(Cryo::LiteralNode *literal, bool is_negated = false);

        // Format hover for primitive type names (int, string, bool, etc.)
        static std::string getPrimitiveTypeHover(const std::string &type_name);

        // Format hover for language keywords (struct, class, enum, etc.)
        static std::string getKeywordHover(const std::string &keyword);

        // Extract the word at a given position from document content
        static std::string extractWordAtPosition(const std::string &content, int line, int character);

        // Find the column where the word starts at a given position
        static int findWordStartColumn(const std::string &content, int line, int character);
    };

} // namespace CryoLSP
