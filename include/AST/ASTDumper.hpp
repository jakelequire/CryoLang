#pragma once
#include "ASTNode.hpp"
#include "ASTVisitor.hpp"
#include <iostream>
#include <string>
#include <vector>

namespace Cryo
{
    // ANSI color codes for terminal output
    namespace Colors
    {
        const std::string RESET = "\033[0m";
        const std::string BOLD = "\033[1m";

        // Node type colors (similar to Clang)
        const std::string DECLARATION = "\033[1;35m"; // Bold Magenta
        const std::string STATEMENT = "\033[1;34m";   // Bold Blue
        const std::string EXPRESSION = "\033[1;32m";  // Bold Green
        const std::string LITERAL = "\033[1;33m";     // Bold Yellow
        const std::string IDENTIFIER = "\033[1;36m";  // Bold Cyan
        const std::string TYPE = "\033[1;31m";        // Bold Red
        const std::string LOCATION = "\033[90m";      // Dark Gray
        const std::string VALUE = "\033[1;37m";       // Bold White

        // Tree drawing colors
        const std::string TREE = "\033[37m"; // Light Gray
    }

    // Tree drawing characters
    namespace TreeChars
    {
        const std::string BRANCH = "|-";      // |-
        const std::string LAST_BRANCH = "`-"; // `-
        const std::string VERTICAL = "| ";    // |
        const std::string SPACE = "  ";       // Two spaces
    }

    class ASTDumper : public ASTVisitor
    {
    private:
        std::ostream &_output;
        std::vector<bool> _is_last_stack;
        bool _use_colors;
        int _current_level;

        // Helper methods
        void print_prefix();
        void print_location(const SourceLocation &loc);
        std::string get_node_color(NodeKind kind) const;
        std::string get_literal_node_name(TokenKind kind) const;
        std::string get_literal_type_string(TokenKind kind) const;
        bool is_float_literal(const std::string &value) const;

    public:
        ASTDumper(std::ostream &output, bool use_colors = true);

        // Main dump method
        void dump(const ASTNode *node);

        // Visitor pattern methods
        void visit(ExpressionNode &node) override;
        void visit(StatementNode &node) override;
        void visit(DeclarationNode &node) override;
        void visit(LiteralNode &node) override;
        void visit(IdentifierNode &node) override;
        void visit(BinaryExpressionNode &node) override;
        void visit(TernaryExpressionNode &node) override;
        void visit(ProgramNode &node) override;
        void visit(BlockStatementNode &node) override;
        void visit(ReturnStatementNode &node) override;
        void visit(VariableDeclarationNode &node) override;
        void visit(FunctionDeclarationNode &node) override;
        void visit(CallExpressionNode &node) override;
        void visit(NewExpressionNode &node) override;
        void visit(ArrayLiteralNode &node) override;
        void visit(ArrayAccessNode &node) override;
        void visit(MemberAccessNode &node) override;
        void visit(IfStatementNode &node) override;
        void visit(WhileStatementNode &node) override;
        void visit(ForStatementNode &node) override;
        void visit(BreakStatementNode &node) override;
        void visit(ContinueStatementNode &node) override;
        void visit(ExpressionStatementNode &node) override;
        void visit(DeclarationStatementNode &node) override;
        void visit(StructDeclarationNode &node) override;
        void visit(ClassDeclarationNode &node) override;
        void visit(EnumDeclarationNode &node) override;
        void visit(EnumVariantNode &node) override;
        void visit(TypeAliasDeclarationNode &node) override;
        void visit(ScopeResolutionNode &node) override;
        void visit(ImplementationBlockNode &node) override;
        void visit(GenericParameterNode &node) override;
        void visit(StructFieldNode &node) override;
        void visit(StructMethodNode &node) override;

    private:
        // Helper methods for traversing children
        void dump_child(const ASTNode *child, bool is_last);
        void push_level(bool is_last);
        void pop_level();
    };
}