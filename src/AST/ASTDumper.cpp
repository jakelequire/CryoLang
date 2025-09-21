#include "AST/ASTDumper.hpp"
#include <sstream>

namespace Cryo
{
    ASTDumper::ASTDumper(std::ostream &output, bool use_colors)
        : _output(output), _use_colors(use_colors), _current_level(0)
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
            return Colors::DECLARATION;

        case NodeKind::BlockStatement:
        case NodeKind::ReturnStatement:
        case NodeKind::IfStatement:
        case NodeKind::WhileStatement:
        case NodeKind::ForStatement:
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

        // Dump parameters
        bool has_body = node.body() != nullptr;

        for (size_t i = 0; i < params.size(); ++i)
        {
            bool is_last = (i == params.size() - 1) && !has_body;
            dump_child(params[i].get(), is_last);
        }

        // Dump body
        if (node.body())
        {
            dump_child(node.body(), true);
        }
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
                if (i > 0) _output << " + ";
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