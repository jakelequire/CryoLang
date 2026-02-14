#include "LSP/Providers/SymbolProvider.hpp"
#include "LSP/Transport.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "AST/ASTNode.hpp"
#include "AST/ASTVisitor.hpp"

namespace CryoLSP
{

    // Helper to create a range from a source location and name length
    static Range makeRange(const Cryo::SourceLocation &loc, size_t name_length)
    {
        Range r;
        r.start.line = static_cast<int>(loc.line() > 0 ? loc.line() - 1 : 0);
        r.start.character = static_cast<int>(loc.column() > 0 ? loc.column() - 1 : 0);
        r.end.line = r.start.line;
        r.end.character = r.start.character + static_cast<int>(name_length);
        return r;
    }

    // AST walker that collects document symbols
    class SymbolCollector : public Cryo::BaseASTVisitor
    {
    public:
        std::vector<DocumentSymbol> symbols;

        void visit(Cryo::ProgramNode &node) override
        {
            for (const auto &child : node.statements())
            {
                if (child)
                    child->accept(*this);
            }
        }

        void visit(Cryo::DeclarationStatementNode &node) override
        {
            if (node.declaration())
                node.declaration()->accept(*this);
        }

        void visit(Cryo::FunctionDeclarationNode &node) override
        {
            DocumentSymbol sym;
            sym.name = node.name();
            sym.kind = SymbolKind::Function;
            sym.range = makeRange(node.location(), node.name().size());
            sym.selectionRange = sym.range;

            // Add parameters as children
            for (const auto &param : node.parameters())
            {
                if (param)
                {
                    DocumentSymbol child;
                    child.name = param->name();
                    child.kind = SymbolKind::Variable;
                    child.range = makeRange(param->location(), param->name().size());
                    child.selectionRange = child.range;
                    sym.children.push_back(std::move(child));
                }
            }

            symbols.push_back(std::move(sym));
        }

        void visit(Cryo::VariableDeclarationNode &node) override
        {
            DocumentSymbol sym;
            sym.name = node.name();
            sym.kind = node.is_mutable() ? SymbolKind::Variable : SymbolKind::Constant;
            sym.range = makeRange(node.location(), node.name().size());
            sym.selectionRange = sym.range;
            symbols.push_back(std::move(sym));
        }

        void visit(Cryo::StructDeclarationNode &node) override
        {
            DocumentSymbol sym;
            sym.name = node.name();
            sym.kind = SymbolKind::Struct;
            sym.range = makeRange(node.location(), node.name().size());
            sym.selectionRange = sym.range;

            // Add fields
            for (const auto &field : node.fields())
            {
                if (field)
                {
                    DocumentSymbol child;
                    child.name = field->name();
                    child.kind = SymbolKind::Field;
                    child.range = makeRange(field->location(), field->name().size());
                    child.selectionRange = child.range;
                    sym.children.push_back(std::move(child));
                }
            }

            // Add methods
            for (const auto &method : node.methods())
            {
                if (method)
                {
                    DocumentSymbol child;
                    child.name = method->name();
                    child.kind = SymbolKind::Method;
                    child.range = makeRange(method->location(), method->name().size());
                    child.selectionRange = child.range;
                    sym.children.push_back(std::move(child));
                }
            }

            symbols.push_back(std::move(sym));
        }

        void visit(Cryo::ClassDeclarationNode &node) override
        {
            DocumentSymbol sym;
            sym.name = node.name();
            sym.kind = SymbolKind::Class;
            sym.range = makeRange(node.location(), node.name().size());
            sym.selectionRange = sym.range;

            for (const auto &method : node.methods())
            {
                if (method)
                {
                    DocumentSymbol child;
                    child.name = method->name();
                    child.kind = SymbolKind::Method;
                    child.range = makeRange(method->location(), method->name().size());
                    child.selectionRange = child.range;
                    sym.children.push_back(std::move(child));
                }
            }

            symbols.push_back(std::move(sym));
        }

        void visit(Cryo::EnumDeclarationNode &node) override
        {
            DocumentSymbol sym;
            sym.name = node.name();
            sym.kind = SymbolKind::Enum;
            sym.range = makeRange(node.location(), node.name().size());
            sym.selectionRange = sym.range;

            for (const auto &variant : node.variants())
            {
                if (variant)
                {
                    DocumentSymbol child;
                    child.name = variant->name();
                    child.kind = SymbolKind::EnumMember;
                    child.range = makeRange(variant->location(), variant->name().size());
                    child.selectionRange = child.range;
                    sym.children.push_back(std::move(child));
                }
            }

            symbols.push_back(std::move(sym));
        }

        void visit(Cryo::ImplementationBlockNode &node) override
        {
            DocumentSymbol sym;
            sym.name = "impl " + node.target_type();
            sym.kind = SymbolKind::Namespace;
            sym.range = makeRange(node.location(), node.target_type().size() + 5); // "impl " + name
            sym.selectionRange = sym.range;

            for (const auto &method : node.method_implementations())
            {
                if (method)
                {
                    DocumentSymbol child;
                    child.name = method->name();
                    child.kind = SymbolKind::Method;
                    child.range = makeRange(method->location(), method->name().size());
                    child.selectionRange = child.range;
                    sym.children.push_back(std::move(child));
                }
            }

            symbols.push_back(std::move(sym));
        }

        void visit(Cryo::TypeAliasDeclarationNode &node) override
        {
            DocumentSymbol sym;
            sym.name = node.alias_name();
            sym.kind = SymbolKind::Class; // TypeParameter would also work
            sym.range = makeRange(node.location(), node.alias_name().size());
            sym.selectionRange = sym.range;
            symbols.push_back(std::move(sym));
        }

        void visit(Cryo::TraitDeclarationNode &node) override
        {
            DocumentSymbol sym;
            sym.name = node.name();
            sym.kind = SymbolKind::Interface;
            sym.range = makeRange(node.location(), node.name().size());
            sym.selectionRange = sym.range;

            for (const auto &method : node.methods())
            {
                if (method)
                {
                    DocumentSymbol child;
                    child.name = method->name();
                    child.kind = SymbolKind::Method;
                    child.range = makeRange(method->location(), method->name().size());
                    child.selectionRange = child.range;
                    sym.children.push_back(std::move(child));
                }
            }

            symbols.push_back(std::move(sym));
        }

        void visit(Cryo::ImportDeclarationNode &node) override
        {
            DocumentSymbol sym;
            sym.name = "import " + node.module_path();
            sym.kind = SymbolKind::Module;
            sym.range = makeRange(node.location(), node.module_path().size() + 7);
            sym.selectionRange = sym.range;
            symbols.push_back(std::move(sym));
        }
    };

    SymbolProvider::SymbolProvider(AnalysisEngine &engine)
        : _engine(engine) {}

    std::vector<DocumentSymbol> SymbolProvider::getDocumentSymbols(const std::string &uri)
    {
        std::string file_path = uri_to_path(uri);
        auto *instance = _engine.getCompilerInstance(file_path);
        if (!instance || !instance->ast_root())
            return {};

        SymbolCollector collector;
        instance->ast_root()->accept(collector);

        return std::move(collector.symbols);
    }

} // namespace CryoLSP
