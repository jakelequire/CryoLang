#include "LSP/Providers/ReferencesProvider.hpp"
#include "LSP/PositionFinder.hpp"
#include "LSP/Transport.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "Types/SymbolTable.hpp"
#include "AST/ASTNode.hpp"
#include "AST/ASTVisitor.hpp"

namespace CryoLSP
{

    // AST walker that collects all references to a given name
    class ReferenceCollector : public Cryo::BaseASTVisitor
    {
    public:
        std::string target_name;
        std::string file_uri;
        std::vector<Location> locations;

        void addLocation(const Cryo::SourceLocation &loc, size_t name_length)
        {
            Location l;
            l.uri = file_uri;
            l.range.start.line = static_cast<int>(loc.line() > 0 ? loc.line() - 1 : 0);
            l.range.start.character = static_cast<int>(loc.column() > 0 ? loc.column() - 1 : 0);
            l.range.end.line = l.range.start.line;
            l.range.end.character = l.range.start.character + static_cast<int>(name_length);
            locations.push_back(l);
        }

        void visit(Cryo::ProgramNode &node) override
        {
            for (const auto &child : node.statements())
                if (child)
                    child->accept(*this);
        }

        void visit(Cryo::BlockStatementNode &node) override
        {
            for (const auto &stmt : node.statements())
                if (stmt)
                    stmt->accept(*this);
        }

        void visit(Cryo::ExpressionStatementNode &node) override
        {
            if (node.expression())
                node.expression()->accept(*this);
        }

        void visit(Cryo::DeclarationStatementNode &node) override
        {
            if (node.declaration())
                node.declaration()->accept(*this);
        }

        void visit(Cryo::IdentifierNode &node) override
        {
            if (node.name() == target_name)
                addLocation(node.location(), node.name().size());
        }

        void visit(Cryo::FunctionDeclarationNode &node) override
        {
            if (node.name() == target_name)
                addLocation(node.location(), node.name().size());
            for (const auto &param : node.parameters())
                if (param)
                    param->accept(*this);
            if (node.body())
                node.body()->accept(*this);
        }

        void visit(Cryo::VariableDeclarationNode &node) override
        {
            if (node.name() == target_name)
                addLocation(node.location(), node.name().size());
            if (node.initializer())
                node.initializer()->accept(*this);
        }

        void visit(Cryo::StructDeclarationNode &node) override
        {
            if (node.name() == target_name)
                addLocation(node.location(), node.name().size());
            for (const auto &method : node.methods())
                if (method)
                    method->accept(*this);
        }

        void visit(Cryo::ClassDeclarationNode &node) override
        {
            if (node.name() == target_name)
                addLocation(node.location(), node.name().size());
            for (const auto &method : node.methods())
                if (method)
                    method->accept(*this);
        }

        void visit(Cryo::EnumDeclarationNode &node) override
        {
            if (node.name() == target_name)
                addLocation(node.location(), node.name().size());
            for (const auto &v : node.variants())
                if (v)
                    v->accept(*this);
        }

        void visit(Cryo::EnumVariantNode &node) override
        {
            if (node.name() == target_name)
                addLocation(node.location(), node.name().size());
        }

        void visit(Cryo::CallExpressionNode &node) override
        {
            if (node.callee())
                node.callee()->accept(*this);
            for (const auto &arg : node.arguments())
                if (arg)
                    arg->accept(*this);
        }

        void visit(Cryo::BinaryExpressionNode &node) override
        {
            if (node.left())
                node.left()->accept(*this);
            if (node.right())
                node.right()->accept(*this);
        }

        void visit(Cryo::UnaryExpressionNode &node) override
        {
            if (node.operand())
                node.operand()->accept(*this);
        }

        void visit(Cryo::IfStatementNode &node) override
        {
            if (node.condition())
                node.condition()->accept(*this);
            if (node.then_statement())
                node.then_statement()->accept(*this);
            if (node.else_statement())
                node.else_statement()->accept(*this);
        }

        void visit(Cryo::WhileStatementNode &node) override
        {
            if (node.condition())
                node.condition()->accept(*this);
            if (node.body())
                node.body()->accept(*this);
        }

        void visit(Cryo::ForStatementNode &node) override
        {
            if (node.init())
                node.init()->accept(*this);
            if (node.condition())
                node.condition()->accept(*this);
            if (node.update())
                node.update()->accept(*this);
            if (node.body())
                node.body()->accept(*this);
        }

        void visit(Cryo::ReturnStatementNode &node) override
        {
            if (node.expression())
                node.expression()->accept(*this);
        }

        void visit(Cryo::StructMethodNode &node) override
        {
            if (node.name() == target_name)
                addLocation(node.location(), node.name().size());
            for (const auto &param : node.parameters())
                if (param)
                    param->accept(*this);
            if (node.body())
                node.body()->accept(*this);
        }

        void visit(Cryo::ImplementationBlockNode &node) override
        {
            if (node.target_type() == target_name)
                addLocation(node.location(), node.target_type().size());
            for (const auto &method : node.method_implementations())
                if (method)
                    method->accept(*this);
        }

        void visit(Cryo::MemberAccessNode &node) override
        {
            if (node.object())
                node.object()->accept(*this);
        }

        void visit(Cryo::ScopeResolutionNode &node) override
        {
            if (node.scope_name() == target_name || node.member_name() == target_name)
                addLocation(node.location(), node.scope_name().size());
        }
    };

    ReferencesProvider::ReferencesProvider(AnalysisEngine &engine)
        : _engine(engine) {}

    std::vector<Location> ReferencesProvider::getReferences(const std::string &uri, const Position &position, bool includeDeclaration)
    {
        std::string file_path = uri_to_path(uri);
        auto *instance = _engine.getCompilerInstance(file_path);
        if (!instance || !instance->ast_root())
            return {};

        // Find the symbol at cursor
        PositionFinder finder(position.line + 1, position.character + 1);
        auto found = finder.find(instance->ast_root());

        if (!found.node || found.identifier_name.empty())
            return {};

        // Collect all references to this name in the AST
        ReferenceCollector collector;
        collector.target_name = found.identifier_name;
        collector.file_uri = uri;
        instance->ast_root()->accept(collector);

        return std::move(collector.locations);
    }

} // namespace CryoLSP
