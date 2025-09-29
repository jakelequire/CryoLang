#include "AST/ASTDumper.hpp"
#include <sstream>

namespace Cryo
{
    ASTDumper::ASTDumper(std::ostream &output, bool use_colors)
        : _output(output), _use_colors(use_colors), _current_level(0), _in_class_context(false)
    {
    }

    void ASTDumper::dump(const ASTNode *node)
    {
        if (!node)
        {
            _output << Colors::TREE << "nullptr" << Colors::RESET << std::endl;
            return;
        }

        _is_last_stack.clear();
        _current_level = 0;
        _in_class_context = false;

        // Start the dump - cast away const for visitor pattern
        const_cast<ASTNode *>(node)->accept(*this);
    }

    void ASTDumper::print_prefix()
    {
        if (_use_colors)
            _output << Colors::TREE;

        for (size_t i = 0; i < _is_last_stack.size(); ++i)
        {
            if (i == _is_last_stack.size() - 1)
            {
                // This is the current level
                _output << (_is_last_stack[i] ? TreeChars::LAST_BRANCH : TreeChars::BRANCH);
            }
            else
            {
                // This is a parent level
                _output << (_is_last_stack[i] ? TreeChars::SPACE : TreeChars::VERTICAL);
            }
        }

        if (_use_colors)
            _output << Colors::RESET;
    }

    void ASTDumper::print_location(const SourceLocation &loc)
    {
        if (_use_colors)
            _output << Colors::LOCATION;

        _output << " <line:" << loc.line() << ":" << loc.column() << ">";

        if (_use_colors)
            _output << Colors::RESET;
    }

    std::string ASTDumper::get_node_color(NodeKind kind) const
    {
        if (!_use_colors)
            return "";

        switch (kind)
        {
        case NodeKind::Program:
        case NodeKind::VariableDeclaration:
        case NodeKind::FunctionDeclaration:
        case NodeKind::StructDeclaration:
        case NodeKind::ClassDeclaration:
        case NodeKind::TypeAliasDeclaration:
        case NodeKind::ImplementationBlock:
        case NodeKind::ExternBlock:
            return Colors::DECLARATION;

        case NodeKind::BlockStatement:
        case NodeKind::ReturnStatement:
        case NodeKind::IfStatement:
        case NodeKind::WhileStatement:
        case NodeKind::ForStatement:
        case NodeKind::SwitchStatement:
        case NodeKind::CaseStatement:
        case NodeKind::BreakStatement:
        case NodeKind::ContinueStatement:
        case NodeKind::ExpressionStatement:
            return Colors::STATEMENT;

        case NodeKind::BinaryExpression:
        case NodeKind::CallExpression:
        case NodeKind::ArrayLiteral:
        case NodeKind::ArrayAccess:
            return Colors::EXPRESSION;

        case NodeKind::Literal:
            return Colors::LITERAL;

        case NodeKind::Identifier:
            return Colors::IDENTIFIER;

        default:
            return Colors::EXPRESSION;
        }
    }

    std::string ASTDumper::get_literal_node_name(TokenKind kind) const
    {
        switch (kind)
        {
        case TokenKind::TK_NUMERIC_CONSTANT:
            return "IntegerLiteral"; // Note: This will be overridden in visit(LiteralNode&) for floats
        case TokenKind::TK_STRING_LITERAL:
            return "StringLiteral";
        case TokenKind::TK_CHAR_CONSTANT:
            return "CharLiteral";
        case TokenKind::TK_BOOLEAN_LITERAL:
        case TokenKind::TK_KW_TRUE:
        case TokenKind::TK_KW_FALSE:
            return "BooleanLiteral";
        case TokenKind::TK_KW_NULL:
            return "NullLiteral";
        default:
            return "Literal";
        }
    }

    std::string ASTDumper::get_literal_type_string(TokenKind kind) const
    {
        switch (kind)
        {
        case TokenKind::TK_NUMERIC_CONSTANT:
            return "int"; // Note: This will be overridden in visit(LiteralNode&) for floats
        case TokenKind::TK_STRING_LITERAL:
            return "string";
        case TokenKind::TK_CHAR_CONSTANT:
            return "char";
        case TokenKind::TK_BOOLEAN_LITERAL:
        case TokenKind::TK_KW_TRUE:
        case TokenKind::TK_KW_FALSE:
            return "boolean";
        case TokenKind::TK_KW_NULL:
            return "null";
        default:
            return "unknown";
        }
    }

    // Helper function to determine if a numeric literal contains a decimal point
    bool ASTDumper::is_float_literal(const std::string &value) const
    {
        return value.find('.') != std::string::npos ||
               value.find('e') != std::string::npos ||
               value.find('E') != std::string::npos;
    }

    void ASTDumper::visit(ExpressionNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "Expression";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << std::endl;
    }

    void ASTDumper::visit(StatementNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "Statement";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << std::endl;
    }

    void ASTDumper::visit(DeclarationNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "Declaration";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << std::endl;
    }

    void ASTDumper::visit(LiteralNode &node)
    {
        print_prefix();

        // For numeric constants, determine if it's actually a float literal
        std::string node_name;
        std::string type_name;

        if (node.literal_kind() == TokenKind::TK_NUMERIC_CONSTANT)
        {
            if (is_float_literal(node.value()))
            {
                node_name = "FloatLiteral";
                type_name = "float";
            }
            else
            {
                node_name = "IntegerLiteral";
                type_name = "int";
            }
        }
        else
        {
            node_name = get_literal_node_name(node.literal_kind());
            type_name = get_literal_type_string(node.literal_kind());
        }

        _output << get_node_color(node.kind()) << node_name;
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " ";

        if (_use_colors)
            _output << Colors::VALUE;
        _output << node.value();
        if (_use_colors)
            _output << Colors::RESET;

        _output << " ";
        if (_use_colors)
            _output << Colors::TYPE;
        _output << "'" << type_name << "'";
        if (_use_colors)
            _output << Colors::RESET;

        _output << std::endl;
    }

    void ASTDumper::visit(IdentifierNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "DeclRefExpr";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " ";

        if (_use_colors)
            _output << Colors::VALUE;
        _output << "'" << node.name() << "'";
        if (_use_colors)
            _output << Colors::RESET;

        // Add type information if available
        if (node.type().has_value())
        {
            _output << " ";
            if (_use_colors)
                _output << Colors::TYPE;
            _output << "'" << node.type().value() << "'";
            if (_use_colors)
                _output << Colors::RESET;
        }

        _output << std::endl;
    }

    void ASTDumper::visit(BinaryExpressionNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "BinaryOperator";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " ";

        if (_use_colors)
            _output << Colors::VALUE;
        _output << "'" << node.operator_token().to_string() << "'";
        if (_use_colors)
            _output << Colors::RESET;

        _output << std::endl;

        // Dump left and right operands
        dump_child(node.left(), false);
        dump_child(node.right(), true);
    }

    void ASTDumper::visit(UnaryExpressionNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "UnaryOperator";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " ";

        if (_use_colors)
            _output << Colors::VALUE;
        _output << "'" << node.operator_token().to_string() << "'";
        if (_use_colors)
            _output << Colors::RESET;

        // Display type if available
        if (node.type().has_value())
        {
            _output << " ";
            if (_use_colors)
                _output << Colors::TYPE;
            _output << "'" << node.type().value() << "'";
            if (_use_colors)
                _output << Colors::RESET;
        }

        _output << std::endl;

        // Dump operand
        dump_child(node.operand(), true);
    }

    void ASTDumper::visit(TernaryExpressionNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "TernaryOperator";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " '?:'";
        _output << std::endl;

        // Dump condition, true expression, and false expression
        dump_child(node.condition(), false);
        dump_child(node.true_expression(), false);
        dump_child(node.false_expression(), true);
    }

    void ASTDumper::visit(ProgramNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "TranslationUnitDecl";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << std::endl;

        const auto &statements = node.statements();
        for (size_t i = 0; i < statements.size(); ++i)
        {
            bool is_last = (i == statements.size() - 1);
            dump_child(statements[i].get(), is_last);
        }
    }

    void ASTDumper::visit(BlockStatementNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "CompoundStmt";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << std::endl;

        const auto &statements = node.statements();
        for (size_t i = 0; i < statements.size(); ++i)
        {
            bool is_last = (i == statements.size() - 1);
            dump_child(statements[i].get(), is_last);
        }
    }

    void ASTDumper::visit(ReturnStatementNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "ReturnStmt";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << std::endl;

        if (node.expression())
        {
            dump_child(node.expression(), true);
        }
    }

    void ASTDumper::visit(VariableDeclarationNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "VarDecl";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " ";

        if (_use_colors)
            _output << Colors::VALUE;
        _output << "'" << node.name() << "'";
        if (_use_colors)
            _output << Colors::RESET;

        _output << std::endl;

        if (node.initializer())
        {
            dump_child(node.initializer(), true);
        }
    }

    void ASTDumper::visit(FunctionDeclarationNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "FunctionDecl";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " ";

        if (_use_colors)
            _output << Colors::VALUE;
        _output << "'" << node.name() << "'";
        if (_use_colors)
            _output << Colors::RESET;

        // Build and display function signature
        _output << " ";
        if (_use_colors)
            _output << Colors::TYPE;
        _output << "'(";

        // Add parameter types
        const auto &params = node.parameters();
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (i > 0)
                _output << ", ";
            _output << params[i]->type_annotation();
        }

        _output << ") -> " << node.return_type_annotation() << "'";
        if (_use_colors)
            _output << Colors::RESET;

        _output << std::endl;

        // Dump generic parameters
        const auto &generics = node.generic_parameters();
        const auto &parameters = node.parameters();
        bool has_body = node.body() != nullptr;

        for (size_t i = 0; i < generics.size(); ++i)
        {
            bool is_last = (i == generics.size() - 1) && parameters.empty() && !has_body;
            dump_child(generics[i].get(), is_last);
        }

        // Dump parameters
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            bool is_last = (i == parameters.size() - 1) && !has_body;
            dump_child(parameters[i].get(), is_last);
        }

        // Dump body
        if (node.body())
        {
            dump_child(node.body(), true);
        }
    }

    void ASTDumper::visit(IntrinsicDeclarationNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "IntrinsicDecl";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " ";

        if (_use_colors)
            _output << Colors::VALUE;
        _output << "'" << node.name() << "'";
        if (_use_colors)
            _output << Colors::RESET;

        // Build and display function signature
        _output << " ";
        if (_use_colors)
            _output << Colors::TYPE;
        _output << "'(";

        // Add parameter types
        const auto &params = node.parameters();
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (i > 0)
                _output << ", ";
            _output << params[i]->type_annotation();
        }

        _output << ") -> " << node.return_type_annotation() << "'";
        if (_use_colors)
            _output << Colors::RESET;

        _output << std::endl;

        // Dump parameters
        for (size_t i = 0; i < params.size(); ++i)
        {
            bool is_last = (i == params.size() - 1);
            dump_child(params[i].get(), is_last);
        }
    }

    void ASTDumper::visit(ImportDeclarationNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "ImportDecl";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " ";

        if (_use_colors)
            _output << Colors::VALUE;

        // Show import type
        _output << "'";
        if (node.import_type() == ImportDeclarationNode::ImportType::Relative)
        {
            _output << "\"" << node.path() << "\"";
        }
        else if (node.import_type() == ImportDeclarationNode::ImportType::Absolute)
        {
            _output << "<" << node.path() << ">";
        }

        if (node.has_alias())
        {
            _output << " as " << node.alias();
        }

        _output << "'";

        if (_use_colors)
            _output << Colors::RESET;

        _output << std::endl;
    }

    void ASTDumper::visit(CallExpressionNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "CallExpr";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << std::endl;

        // Dump callee
        const auto &args = node.arguments();
        bool has_args = !args.empty();
        dump_child(node.callee(), !has_args);

        // Dump arguments
        for (size_t i = 0; i < args.size(); ++i)
        {
            bool is_last = (i == args.size() - 1);
            dump_child(args[i].get(), is_last);
        }
    }

    void ASTDumper::visit(NewExpressionNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "NewExpr";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " '" << node.type_name();

        // Print generic arguments if any
        if (!node.generic_args().empty())
        {
            _output << "<";
            for (size_t i = 0; i < node.generic_args().size(); ++i)
            {
                if (i > 0)
                    _output << ", ";
                _output << node.generic_args()[i];
            }
            _output << ">";
        }

        _output << "'" << std::endl;

        // Dump arguments
        const auto &args = node.arguments();
        for (size_t i = 0; i < args.size(); ++i)
        {
            bool is_last = (i == args.size() - 1);
            dump_child(args[i].get(), is_last);
        }
    }

    void ASTDumper::visit(SizeofExpressionNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "SizeofExpr";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " '" << node.type_name() << "'" << std::endl;
    }

    void ASTDumper::visit(StructLiteralNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "StructLiteral";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " '" << node.struct_type();

        // Print generic arguments if any
        if (!node.generic_args().empty())
        {
            _output << "<";
            for (size_t i = 0; i < node.generic_args().size(); ++i)
            {
                if (i > 0)
                    _output << ", ";
                _output << node.generic_args()[i];
            }
            _output << ">";
        }

        _output << "'" << std::endl;

        // Dump field initializers
        const auto &field_inits = node.field_initializers();
        for (size_t i = 0; i < field_inits.size(); ++i)
        {
            bool is_last = (i == field_inits.size() - 1);
            if (field_inits[i] && field_inits[i]->value())
            {
                dump_child(field_inits[i]->value(), is_last);
            }
        }
    }

    void ASTDumper::visit(ArrayLiteralNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "ArrayLiteral";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " [" << node.size() << "]";

        if (!node.element_type().empty())
        {
            _output << " ";
            if (_use_colors)
                _output << Colors::TYPE;
            _output << "'" << node.element_type() << "[]'";
            if (_use_colors)
                _output << Colors::RESET;
        }

        _output << std::endl;

        // Dump array elements
        const auto &elements = node.elements();
        for (size_t i = 0; i < elements.size(); ++i)
        {
            bool is_last = (i == elements.size() - 1);
            dump_child(elements[i].get(), is_last);
        }
    }

    void ASTDumper::visit(ArrayAccessNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "ArrayAccess";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << std::endl;

        // Dump array and index
        dump_child(node.array(), false);
        dump_child(node.index(), true);
    }

    void ASTDumper::visit(MemberAccessNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "MemberAccess";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << std::endl;

        // Dump object and member name
        dump_child(node.object(), false);
        print_prefix();
        _output << "|-Member: " << node.member() << std::endl;
    }

    void ASTDumper::visit(IfStatementNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "IfStmt";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << std::endl;

        bool has_then = node.then_statement() != nullptr;
        bool has_else = node.else_statement() != nullptr;

        // Dump condition
        dump_child(node.condition(), !has_then && !has_else);

        // Dump then statement
        if (has_then)
        {
            dump_child(node.then_statement(), !has_else);
        }

        // Dump else statement
        if (has_else)
        {
            dump_child(node.else_statement(), true);
        }
    }

    void ASTDumper::visit(WhileStatementNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "WhileStmt";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << std::endl;

        bool has_body = node.body() != nullptr;

        // Dump condition
        dump_child(node.condition(), !has_body);

        // Dump body
        if (has_body)
        {
            dump_child(node.body(), true);
        }
    }

    void ASTDumper::visit(ForStatementNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "ForStmt";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << std::endl;

        bool has_init = node.init() != nullptr;
        bool has_condition = node.condition() != nullptr;
        bool has_update = node.update() != nullptr;
        bool has_body = node.body() != nullptr;

        // Count remaining children
        int remaining = (has_init ? 1 : 0) + (has_condition ? 1 : 0) +
                        (has_update ? 1 : 0) + (has_body ? 1 : 0);

        if (has_init)
        {
            dump_child(node.init(), --remaining == 0);
        }
        if (has_condition)
        {
            dump_child(node.condition(), --remaining == 0);
        }
        if (has_update)
        {
            dump_child(node.update(), --remaining == 0);
        }
        if (has_body)
        {
            dump_child(node.body(), --remaining == 0);
        }
    }

    void ASTDumper::visit(MatchStatementNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "MatchStmt";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << std::endl;

        // First dump the expression being matched
        if (node.expr())
        {
            dump_child(node.expr(), node.arms().empty());
        }

        // Then dump all the match arms
        const auto &arms = node.arms();
        for (size_t i = 0; i < arms.size(); ++i)
        {
            dump_child(arms[i].get(), i == arms.size() - 1);
        }
    }

    void ASTDumper::visit(MatchArmNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "MatchArm";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << std::endl;

        bool has_pattern = node.pattern() != nullptr;
        bool has_body = node.body() != nullptr;
        int remaining = (has_pattern ? 1 : 0) + (has_body ? 1 : 0);

        if (has_pattern)
        {
            dump_child(node.pattern(), --remaining == 0);
        }
        if (has_body)
        {
            dump_child(node.body(), --remaining == 0);
        }
    }

    void ASTDumper::visit(PatternNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "Pattern";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << std::endl;
    }

    void ASTDumper::visit(EnumPatternNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "EnumPattern "
                << node.enum_name() << "::" << node.variant_name();

        if (!node.bound_variables().empty())
        {
            _output << " (";
            for (size_t i = 0; i < node.bound_variables().size(); ++i)
            {
                if (i > 0)
                    _output << ", ";
                _output << node.bound_variables()[i];
            }
            _output << ")";
        }

        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << std::endl;
    }

    void ASTDumper::visit(BreakStatementNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "BreakStmt";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << std::endl;
    }

    void ASTDumper::visit(ContinueStatementNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "ContinueStmt";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << std::endl;
    }

    void ASTDumper::visit(ExpressionStatementNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "ExprStmt";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << std::endl;

        if (node.expression())
        {
            dump_child(node.expression(), true);
        }
    }

    void ASTDumper::visit(DeclarationStatementNode &node)
    {
        // For DeclarationStatementNode, we just delegate to the wrapped declaration
        // This makes the AST output cleaner by not showing the wrapper
        if (node.declaration())
        {
            node.declaration()->accept(*this);
        }
    }

    void ASTDumper::visit(StructFieldNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "StructField";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " ";

        if (_use_colors)
            _output << Colors::VALUE;
        _output << "'" << node.name() << "'";
        if (_use_colors)
            _output << Colors::RESET;

        _output << " ";
        if (_use_colors)
            _output << Colors::TYPE;
        _output << "'" << node.type_annotation() << "'";
        if (_use_colors)
            _output << Colors::RESET;

        _output << std::endl;
    }

    void ASTDumper::visit(StructMethodNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "StructMethod";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " ";

        if (_use_colors)
            _output << Colors::VALUE;
        _output << "'" << node.name() << "'";
        if (_use_colors)
            _output << Colors::RESET;

        // Build and display method signature (same format as FunctionDecl)
        _output << " ";
        if (_use_colors)
            _output << Colors::TYPE;
        _output << "'(";

        // Add parameter types
        const auto &params = node.parameters();
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (i > 0)
                _output << ", ";
            _output << params[i]->type_annotation();
        }

        _output << ") -> " << node.return_type_annotation() << "'";
        if (_use_colors)
            _output << Colors::RESET;

        // Add annotations for class methods only
        if (_in_class_context)
        {
            // Add static annotation first if applicable
            if (node.is_static())
            {
                _output << " ";
                if (_use_colors)
                    _output << Colors::BOLD << Colors::ANNOTATION;
                _output << "@static";
                if (_use_colors)
                    _output << Colors::RESET;
            }
            
            if (node.is_constructor())
            {
                _output << " ";
                if (_use_colors)
                    _output << Colors::BOLD << Colors::ANNOTATION;
                _output << "@constructor";
                if (_use_colors)
                    _output << Colors::RESET;
            }
            else
            {
                // Add visibility annotation for non-constructor methods
                switch (node.visibility())
                {
                case Visibility::Public:
                    _output << " ";
                    if (_use_colors)
                        _output << Colors::BOLD << Colors::ANNOTATION;
                    _output << "@public";
                    if (_use_colors)
                        _output << Colors::RESET;
                    break;
                case Visibility::Private:
                    _output << " ";
                    if (_use_colors)
                        _output << Colors::BOLD << Colors::ANNOTATION;
                    _output << "@private";
                    if (_use_colors)
                        _output << Colors::RESET;
                    break;
                case Visibility::Protected:
                    _output << " ";
                    if (_use_colors)
                        _output << Colors::BOLD << Colors::ANNOTATION;
                    _output << "@protected";
                    if (_use_colors)
                        _output << Colors::RESET;
                    break;
                }
            }
        }

        _output << std::endl;

        // Dump parameters and body
        if (!params.empty())
        {
            for (size_t i = 0; i < params.size(); ++i)
            {
                dump_child(params[i].get(), i == params.size() - 1 && !node.body());
            }
        }

        if (node.body())
        {
            dump_child(node.body(), true);
        }
    }

    void ASTDumper::visit(GenericParameterNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "GenericParam";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " ";

        if (_use_colors)
            _output << Colors::VALUE;
        _output << "'" << node.name() << "'";
        if (_use_colors)
            _output << Colors::RESET;

        const auto &constraints = node.constraints();
        if (!constraints.empty())
        {
            _output << " : ";
            for (size_t i = 0; i < constraints.size(); ++i)
            {
                if (i > 0)
                    _output << " + ";
                _output << constraints[i];
            }
        }

        _output << std::endl;
    }

    void ASTDumper::visit(StructDeclarationNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "StructDecl";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " ";

        if (_use_colors)
            _output << Colors::VALUE;
        _output << "'" << node.name() << "'";
        if (_use_colors)
            _output << Colors::RESET;

        _output << std::endl;

        // Dump generic parameters, fields, and methods
        const auto &generics = node.generic_parameters();
        const auto &fields = node.fields();
        const auto &methods = node.methods();

        size_t total_children = generics.size() + fields.size() + methods.size();
        size_t child_index = 0;

        for (const auto &generic : generics)
        {
            dump_child(generic.get(), ++child_index == total_children);
        }

        for (const auto &field : fields)
        {
            dump_child(field.get(), ++child_index == total_children);
        }

        for (const auto &method : methods)
        {
            dump_child(method.get(), ++child_index == total_children);
        }
    }

    void ASTDumper::visit(ClassDeclarationNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "ClassDecl";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " ";

        if (_use_colors)
            _output << Colors::VALUE;
        _output << "'" << node.name() << "'";
        if (_use_colors)
            _output << Colors::RESET;

        if (!node.base_class().empty())
        {
            _output << " : ";
            if (_use_colors)
                _output << Colors::TYPE;
            _output << node.base_class();
            if (_use_colors)
                _output << Colors::RESET;
        }

        _output << std::endl;

        // Set class context for method annotations
        bool previous_class_context = _in_class_context;
        _in_class_context = true;

        // Dump generic parameters, fields, and methods (similar to struct)
        const auto &generics = node.generic_parameters();
        const auto &fields = node.fields();
        const auto &methods = node.methods();

        size_t total_children = generics.size() + fields.size() + methods.size();
        size_t child_index = 0;

        for (const auto &generic : generics)
        {
            dump_child(generic.get(), ++child_index == total_children);
        }

        for (const auto &field : fields)
        {
            dump_child(field.get(), ++child_index == total_children);
        }

        for (const auto &method : methods)
        {
            dump_child(method.get(), ++child_index == total_children);
        }

        // Restore previous context
        _in_class_context = previous_class_context;
    }

    void ASTDumper::visit(TypeAliasDeclarationNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "TypeAlias";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " ";

        if (_use_colors)
            _output << Colors::VALUE;
        _output << "'" << node.alias_name() << "'";
        if (_use_colors)
            _output << Colors::RESET;

        _output << " = ";
        if (_use_colors)
            _output << Colors::TYPE;
        _output << "'" << node.target_type() << "'";
        if (_use_colors)
            _output << Colors::RESET;

        _output << std::endl;
    }

    void ASTDumper::visit(TraitDeclarationNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "TraitDeclaration";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " ";

        if (_use_colors)
            _output << Colors::VALUE;
        _output << "'" << node.name() << "'";
        if (_use_colors)
            _output << Colors::RESET;

        // Display generic parameters if any
        if (!node.generic_parameters().empty())
        {
            _output << "<";
            const auto &params = node.generic_parameters();
            for (size_t i = 0; i < params.size(); ++i)
            {
                if (i > 0)
                    _output << ", ";
                if (_use_colors)
                    _output << Colors::TYPE;
                _output << params[i]->name();
                if (_use_colors)
                    _output << Colors::RESET;
            }
            _output << ">";
        }

        // Display base traits if any
        if (!node.base_traits().empty())
        {
            _output << " : ";
            const auto &base_traits = node.base_traits();
            for (size_t i = 0; i < base_traits.size(); ++i)
            {
                if (i > 0)
                    _output << ", ";
                if (_use_colors)
                    _output << Colors::TYPE;
                _output << base_traits[i].name;
                if (!base_traits[i].type_parameters.empty())
                {
                    _output << "<";
                    for (size_t j = 0; j < base_traits[i].type_parameters.size(); ++j)
                    {
                        if (j > 0)
                            _output << ", ";
                        _output << base_traits[i].type_parameters[j];
                    }
                    _output << ">";
                }
                if (_use_colors)
                    _output << Colors::RESET;
            }
        }

        _output << std::endl;

        // Dump trait methods
        const auto &methods = node.methods();
        for (size_t i = 0; i < methods.size(); ++i)
        {
            dump_child(methods[i].get(), i == methods.size() - 1);
        }
    }

    void ASTDumper::visit(EnumDeclarationNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "EnumDeclaration";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " ";

        if (_use_colors)
            _output << Colors::VALUE;
        _output << "'" << node.name() << "'";
        if (_use_colors)
            _output << Colors::RESET;

        _output << std::endl;

        // Dump variants
        const auto &variants = node.variants();
        for (size_t i = 0; i < variants.size(); ++i)
        {
            dump_child(variants[i].get(), i == variants.size() - 1);
        }
    }

    void ASTDumper::visit(EnumVariantNode &node)
    {
        print_prefix();
        _output << get_node_color(NodeKind::Declaration) << "EnumVariant";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " ";

        if (_use_colors)
            _output << Colors::VALUE;
        _output << "'" << node.name() << "'";
        if (_use_colors)
            _output << Colors::RESET;

        if (!node.is_simple_variant())
        {
            _output << " (";
            const auto &types = node.associated_types();
            for (size_t i = 0; i < types.size(); ++i)
            {
                if (i > 0)
                    _output << ", ";
                if (_use_colors)
                    _output << Colors::TYPE;
                _output << types[i];
                if (_use_colors)
                    _output << Colors::RESET;
            }
            _output << ")";
        }

        _output << std::endl;
    }

    void ASTDumper::visit(ScopeResolutionNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "ScopeResolution";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " ";

        if (_use_colors)
            _output << Colors::TYPE;
        _output << node.scope_name();
        if (_use_colors)
            _output << Colors::RESET;

        _output << "::";

        if (_use_colors)
            _output << Colors::VALUE;
        _output << node.member_name();
        if (_use_colors)
            _output << Colors::RESET;

        _output << std::endl;
    }

    void ASTDumper::visit(ImplementationBlockNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "ImplementationBlock";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " for ";

        if (_use_colors)
            _output << Colors::TYPE;
        _output << "'" << node.target_type() << "'";
        if (_use_colors)
            _output << Colors::RESET;

        _output << std::endl;

        // Dump field implementations and method implementations
        const auto &fields = node.field_implementations();
        const auto &methods = node.method_implementations();

        size_t total_children = fields.size() + methods.size();
        size_t child_index = 0;

        for (const auto &field : fields)
        {
            dump_child(field.get(), ++child_index == total_children);
        }

        for (const auto &method : methods)
        {
            dump_child(method.get(), ++child_index == total_children);
        }
    }

    void ASTDumper::visit(ExternBlockNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "ExternBlock";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << " linkage: ";

        if (_use_colors)
            _output << Colors::TYPE;
        _output << "\"" << node.linkage_type() << "\"";
        if (_use_colors)
            _output << Colors::RESET;

        _output << std::endl;

        // Dump function declarations
        const auto &functions = node.function_declarations();

        for (size_t i = 0; i < functions.size(); ++i)
        {
            dump_child(functions[i].get(), i == functions.size() - 1);
        }
    }

    void ASTDumper::visit(SwitchStatementNode &node)
    {
        print_prefix();
        _output << get_node_color(node.kind()) << "SwitchStmt";
        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << std::endl;

        bool has_cases = !node.cases().empty();

        // First dump the expression being switched on
        dump_child(node.expression(), !has_cases);

        // Then dump all the case statements (including default cases)
        const auto &cases = node.cases();
        for (size_t i = 0; i < cases.size(); ++i)
        {
            bool is_last = (i == cases.size() - 1);
            dump_child(cases[i].get(), is_last);
        }
    }

    void ASTDumper::visit(CaseStatementNode &node)
    {
        print_prefix();
        if (node.is_default())
        {
            _output << get_node_color(node.kind()) << "DefaultCase";
        }
        else
        {
            _output << get_node_color(node.kind()) << "Case";
        }

        if (_use_colors)
            _output << Colors::RESET;
        print_location(node.location());
        _output << std::endl;

        bool has_value = !node.is_default() && node.value() != nullptr;
        bool has_body = !node.statements().empty();

        // If it's a regular case (not default), dump the case value
        if (has_value)
        {
            dump_child(node.value(), !has_body);
        }

        // Dump the case body statements
        const auto &statements = node.statements();
        for (size_t i = 0; i < statements.size(); ++i)
        {
            bool is_last = (i == statements.size() - 1);
            dump_child(statements[i].get(), is_last);
        }
    }

    void ASTDumper::dump_child(const ASTNode *child, bool is_last)
    {
        if (!child)
            return;

        push_level(is_last);
        const_cast<ASTNode *>(child)->accept(*this);
        pop_level();
    }

    void ASTDumper::push_level(bool is_last)
    {
        _is_last_stack.push_back(is_last);
        _current_level++;
    }

    void ASTDumper::pop_level()
    {
        if (!_is_last_stack.empty())
        {
            _is_last_stack.pop_back();
            _current_level--;
        }
    }
}