#include "LSP/Providers/SemanticTokensProvider.hpp"
#include "LSP/Transport.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "AST/ASTNode.hpp"
#include "AST/ASTVisitor.hpp"

namespace CryoLSP
{

    // Token type indices (must match getLegend() order)
    enum TokenType : int
    {
        TT_Namespace = 0,
        TT_Type = 1,
        TT_Class = 2,
        TT_Enum = 3,
        TT_Interface = 4,
        TT_Struct = 5,
        TT_TypeParameter = 6,
        TT_Parameter = 7,
        TT_Variable = 8,
        TT_Property = 9,
        TT_EnumMember = 10,
        TT_Function = 11,
        TT_Method = 12,
        TT_Keyword = 13,
        TT_Comment = 14,
        TT_String = 15,
        TT_Number = 16,
    };

    // Token modifier bits
    enum TokenModifier : int
    {
        TM_Declaration = 0x01,
        TM_Definition = 0x02,
        TM_Readonly = 0x04,
        TM_Static = 0x08,
    };

    struct RawToken
    {
        int line;      // 0-based
        int startChar; // 0-based
        int length;
        int tokenType;
        int modifiers;
    };

    // Collector that walks AST and produces raw tokens
    class TokenCollector : public Cryo::BaseASTVisitor
    {
    public:
        std::vector<RawToken> tokens;

        void addToken(const Cryo::SourceLocation &loc, size_t length, int type, int mods = 0)
        {
            RawToken t;
            t.line = static_cast<int>(loc.line() > 0 ? loc.line() - 1 : 0);
            t.startChar = static_cast<int>(loc.column() > 0 ? loc.column() - 1 : 0);
            t.length = static_cast<int>(length);
            t.tokenType = type;
            t.modifiers = mods;
            tokens.push_back(t);
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
            if (node.name() == "this")
                addToken(node.location(), node.name().size(), TT_Parameter);
            else
                addToken(node.location(), node.name().size(), TT_Variable);
        }

        void visit(Cryo::LiteralNode &node) override
        {
            auto kind = node.literal_kind();
            if (kind == Cryo::TokenKind::TK_NUMERIC_CONSTANT)
                addToken(node.location(), node.value().size(), TT_Number);
            else if (kind == Cryo::TokenKind::TK_STRING_LITERAL)
                addToken(node.location(), node.value().size() + 2, TT_String); // +2 for quotes
        }

        void visit(Cryo::FunctionDeclarationNode &node) override
        {
            const auto &loc = node.has_name_location() ? node.name_location() : node.location();
            addToken(loc, node.name().size(), TT_Function, TM_Declaration | TM_Definition);
            for (const auto &param : node.parameters())
                if (param)
                    param->accept(*this);
            if (node.body())
                node.body()->accept(*this);
        }

        void visit(Cryo::VariableDeclarationNode &node) override
        {
            int mods = TM_Declaration;
            if (!node.is_mutable())
                mods |= TM_Readonly;
            const auto &loc = node.has_name_location() ? node.name_location() : node.location();
            int type = (node.name() == "this") ? TT_Parameter : TT_Variable;
            addToken(loc, node.name().size(), type, mods);
            if (node.initializer())
                node.initializer()->accept(*this);
        }

        void visit(Cryo::StructDeclarationNode &node) override
        {
            const auto &loc = node.has_name_location() ? node.name_location() : node.location();
            addToken(loc, node.name().size(), TT_Struct, TM_Declaration | TM_Definition);
            for (const auto &field : node.fields())
            {
                if (field)
                {
                    const auto &floc = field->has_name_location() ? field->name_location() : field->location();
                    addToken(floc, field->name().size(), TT_Property, TM_Declaration);
                }
            }
            for (const auto &method : node.methods())
                if (method)
                    method->accept(*this);
        }

        void visit(Cryo::ClassDeclarationNode &node) override
        {
            const auto &loc = node.has_name_location() ? node.name_location() : node.location();
            addToken(loc, node.name().size(), TT_Class, TM_Declaration | TM_Definition);
            for (const auto &method : node.methods())
                if (method)
                    method->accept(*this);
        }

        void visit(Cryo::EnumDeclarationNode &node) override
        {
            const auto &loc = node.has_name_location() ? node.name_location() : node.location();
            addToken(loc, node.name().size(), TT_Enum, TM_Declaration | TM_Definition);
            for (const auto &v : node.variants())
                if (v)
                    v->accept(*this);
        }

        void visit(Cryo::EnumVariantNode &node) override
        {
            addToken(node.location(), node.name().size(), TT_EnumMember, TM_Declaration);
        }

        void visit(Cryo::StructMethodNode &node) override
        {
            const auto &loc = node.has_name_location() ? node.name_location() : node.location();
            addToken(loc, node.name().size(), TT_Method, TM_Declaration | TM_Definition);
            for (const auto &param : node.parameters())
                if (param)
                    param->accept(*this);
            if (node.body())
                node.body()->accept(*this);
        }

        void visit(Cryo::ImplementationBlockNode &node) override
        {
            const auto &loc = node.has_name_location() ? node.name_location() : node.location();
            addToken(loc, node.target_type().size(), TT_Type);
            for (const auto &method : node.method_implementations())
                if (method)
                    method->accept(*this);
        }

        void visit(Cryo::CallExpressionNode &node) override
        {
            if (node.callee())
            {
                // Color function/method calls appropriately
                if (auto *ident = dynamic_cast<Cryo::IdentifierNode *>(node.callee()))
                {
                    // Direct function call: foo()
                    addToken(ident->location(), ident->name().size(), TT_Function);
                }
                else if (auto *member = dynamic_cast<Cryo::MemberAccessNode *>(node.callee()))
                {
                    // Method call: obj.method()
                    if (member->object())
                        member->object()->accept(*this);
                    if (member->has_member_location())
                        addToken(member->member_location(), member->member().size(), TT_Method);
                }
                else if (auto *scope = dynamic_cast<Cryo::ScopeResolutionNode *>(node.callee()))
                {
                    // Static call: Type::method()
                    addToken(scope->location(), scope->scope_name().size(), TT_Type);
                    // member_name location not stored on ScopeResolutionNode, skip for now
                }
                else
                {
                    node.callee()->accept(*this);
                }
            }
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

        void visit(Cryo::MemberAccessNode &node) override
        {
            if (node.object())
                node.object()->accept(*this);
            if (node.has_member_location())
                addToken(node.member_location(), node.member().size(), TT_Property);
        }

        void visit(Cryo::ScopeResolutionNode &node) override
        {
            addToken(node.location(), node.scope_name().size(), TT_Type);
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

        void visit(Cryo::TraitDeclarationNode &node) override
        {
            const auto &loc = node.has_name_location() ? node.name_location() : node.location();
            addToken(loc, node.name().size(), TT_Interface, TM_Declaration | TM_Definition);
            for (const auto &method : node.methods())
                if (method)
                    method->accept(*this);
        }

        void visit(Cryo::TypeAliasDeclarationNode &node) override
        {
            const auto &loc = node.has_name_location() ? node.name_location() : node.location();
            addToken(loc, node.alias_name().size(), TT_Type, TM_Declaration);
        }

        void visit(Cryo::ImportDeclarationNode &node) override
        {
            const auto &loc = node.has_name_location() ? node.name_location() : node.location();
            addToken(loc, node.module_path().size(), TT_Namespace);
        }
    };

    SemanticTokensProvider::SemanticTokensProvider(AnalysisEngine &engine)
        : _engine(engine) {}

    SemanticTokensLegend SemanticTokensProvider::getLegend()
    {
        SemanticTokensLegend legend;
        legend.tokenTypes = {
            "namespace",     // 0
            "type",          // 1
            "class",         // 2
            "enum",          // 3
            "interface",     // 4
            "struct",        // 5
            "typeParameter", // 6
            "parameter",     // 7
            "variable",      // 8
            "property",      // 9
            "enumMember",    // 10
            "function",      // 11
            "method",        // 12
            "keyword",       // 13
            "comment",       // 14
            "string",        // 15
            "number",        // 16
        };
        legend.tokenModifiers = {
            "declaration",
            "definition",
            "readonly",
            "static",
        };
        return legend;
    }

    SemanticTokens SemanticTokensProvider::getSemanticTokens(const std::string &uri)
    {
        SemanticTokens result;
        std::string file_path = uri_to_path(uri);
        auto *instance = _engine.getCompilerInstance(file_path);
        if (!instance || !instance->ast_root())
            return result;

        TokenCollector collector;
        instance->ast_root()->accept(collector);

        // Sort tokens by position
        auto &raw = collector.tokens;
        std::sort(raw.begin(), raw.end(), [](const RawToken &a, const RawToken &b)
                  {
            if (a.line != b.line) return a.line < b.line;
            return a.startChar < b.startChar; });

        // Delta-encode per LSP spec:
        // Each token is 5 integers: deltaLine, deltaStartChar, length, tokenType, tokenModifiers
        int prevLine = 0;
        int prevChar = 0;
        for (const auto &t : raw)
        {
            int deltaLine = t.line - prevLine;
            int deltaChar = (deltaLine == 0) ? (t.startChar - prevChar) : t.startChar;

            result.data.push_back(deltaLine);
            result.data.push_back(deltaChar);
            result.data.push_back(t.length);
            result.data.push_back(t.tokenType);
            result.data.push_back(t.modifiers);

            prevLine = t.line;
            prevChar = t.startChar;
        }

        return result;
    }

} // namespace CryoLSP
