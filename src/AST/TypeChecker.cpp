#include "AST/TypeChecker.hpp"
#include "Lexer/lexer.hpp"
#include <sstream>
#include <algorithm>

namespace Cryo
{
    //===----------------------------------------------------------------------===//
    // TypeError Implementation
    //===----------------------------------------------------------------------===//

    std::string TypeError::to_string() const
    {
        std::ostringstream oss;
        oss << "Type error at line " << location.line() << ", column " << location.column() << ": ";
        oss << message;

        if (expected_type && actual_type)
        {
            oss << " (expected '" << expected_type->to_string()
                << "', got '" << actual_type->to_string() << "')";
        }

        return oss.str();
    }

    //===----------------------------------------------------------------------===//
    // TypedSymbolTable Implementation
    //===----------------------------------------------------------------------===//

    bool TypedSymbolTable::declare_symbol(const std::string &name, Type *type,
                                          SourceLocation loc, bool is_mutable)
    {
        // Check for redefinition in current scope only
        if (_symbols.find(name) != _symbols.end())
        {
            return false; // Symbol already exists
        }

        TypedSymbol symbol(name, type, loc);
        symbol.is_mutable = is_mutable;
        symbol.is_initialized = true; // Assume initialized for now

        _symbols[name] = std::move(symbol);
        return true;
    }

    TypedSymbol *TypedSymbolTable::lookup_symbol(const std::string &name)
    {
        // Search current scope
        auto it = _symbols.find(name);
        if (it != _symbols.end())
        {
            return &it->second;
        }

        // Search parent scopes
        if (_parent_scope)
        {
            return _parent_scope->lookup_symbol(name);
        }

        return nullptr;
    }

    bool TypedSymbolTable::is_symbol_defined(const std::string &name)
    {
        return lookup_symbol(name) != nullptr;
    }

    std::unique_ptr<TypedSymbolTable> TypedSymbolTable::enter_scope()
    {
        // Create a new symbol table with the current table as parent
        auto current_table = std::make_unique<TypedSymbolTable>();
        current_table->_symbols = std::move(this->_symbols);
        current_table->_parent_scope = std::move(this->_parent_scope);

        return std::make_unique<TypedSymbolTable>(std::move(current_table));
    }

    std::unique_ptr<TypedSymbolTable> TypedSymbolTable::exit_scope()
    {
        if (_parent_scope)
        {
            return std::move(_parent_scope);
        }
        else
        {
            // If no parent scope, return a new empty table to avoid null pointer
            return std::make_unique<TypedSymbolTable>();
        }
    }

    //===----------------------------------------------------------------------===//
    // TypeChecker Implementation
    //===----------------------------------------------------------------------===//

    TypeChecker::TypeChecker(TypeContext &type_ctx)
        : _type_context(type_ctx)
    {
        _symbol_table = std::make_unique<TypedSymbolTable>();
    }

    void TypeChecker::check_program(ProgramNode &program)
    {
        // Clear any previous state
        _errors.clear();
        _current_function_return_type = nullptr;
        _in_function = false;
        _in_loop = false;

        // Visit all top-level declarations and statements
        for (const auto &stmt : program.statements())
        {
            if (stmt)
            {
                stmt->accept(*this);
            }
        }
    }

    //===----------------------------------------------------------------------===//
    // Program Visitors
    //===----------------------------------------------------------------------===//

    void TypeChecker::visit(ProgramNode &node)
    {
        // Visit all top-level declarations and statements
        for (const auto &stmt : node.statements())
        {
            if (stmt)
            {
                stmt->accept(*this);
            }
        }
    }

    //===----------------------------------------------------------------------===//
    // Declaration Visitors
    //===----------------------------------------------------------------------===//

    void TypeChecker::visit(VariableDeclarationNode &node)
    {
        const std::string &var_name = node.name();
        Type *declared_type = nullptr;
        Type *inferred_type = nullptr;

        // Parse type annotation from node
        if (!node.type_annotation().empty() && node.type_annotation() != "auto")
        {
            declared_type = _type_context.parse_type_from_string(node.type_annotation());
        }

        if (node.initializer())
        {
            // Visit the initializer to determine its type
            node.initializer()->accept(*this);
            inferred_type = node.initializer()->type().has_value()
                                ? _type_context.parse_type_from_string(node.initializer()->type().value())
                                : nullptr;
        }

        // Determine final type
        Type *final_type = nullptr;
        if (declared_type && inferred_type)
        {
            // Both declared and inferred - check compatibility
            if (!check_assignment_compatibility(declared_type, inferred_type, node.location()))
            {
                report_type_mismatch(node.location(), declared_type, inferred_type,
                                     "variable initialization");
                final_type = declared_type; // Use declared type for error recovery
            }
            else
            {
                final_type = declared_type;
            }
        }
        else if (declared_type)
        {
            final_type = declared_type;
        }
        else if (inferred_type)
        {
            final_type = inferred_type;
        }
        else
        {
            report_error(TypeError::ErrorKind::TypeMismatch, node.location(),
                         "Cannot infer type for variable '" + var_name + "'");
            final_type = _type_context.get_unknown_type();
        }

        // Declare the variable in symbol table
        if (!declare_variable(var_name, final_type, node.location(), node.is_mutable()))
        {
            report_redefined_symbol(node.location(), var_name);
        }
    }

    void TypeChecker::visit(FunctionDeclarationNode &node)
    {
        const std::string &func_name = node.name();

        // Parse return type from node annotation
        const std::string &return_type_str = node.return_type_annotation();
        Type *return_type = _type_context.parse_type_from_string(return_type_str);

        if (!return_type)
        {
            report_error(TypeError::ErrorKind::InvalidOperation, node.location(),
                         "Unable to parse return type: " + return_type_str);
            return_type = _type_context.get_unknown_type();
        }

        // Collect parameter types
        std::vector<Type *> param_types;
        for (const auto &param : node.parameters())
        {
            if (param)
            {
                const std::string &param_type_str = param->type_annotation();
                Type *param_type = _type_context.parse_type_from_string(param_type_str);
                if (!param_type)
                {
                    report_error(TypeError::ErrorKind::InvalidOperation, param->location(),
                                 "Unable to parse parameter type: " + param_type_str);
                    param_type = _type_context.get_unknown_type();
                }
                param_types.push_back(param_type);
            }
        }

        // Create function type
        FunctionType *func_type = static_cast<FunctionType *>(
            _type_context.create_function_type(return_type, param_types));

        // Declare function in current scope
        if (!declare_variable(func_name, func_type, node.location(), false))
        {
            report_redefined_symbol(node.location(), func_name);
        }

        // Enter function scope for parameter and body checking
        enter_scope();
        _in_function = true;
        _current_function_return_type = return_type;

        // Declare parameters in function scope
        for (const auto &param : node.parameters())
        {
            if (param)
            {
                param->accept(*this);
            }
        }

        // Check function body
        if (node.body())
        {
            node.body()->accept(*this);
        }

        // Exit function scope
        _current_function_return_type = nullptr;
        _in_function = false;
        exit_scope();
    }

    //===----------------------------------------------------------------------===//
    // Statement Visitors
    //===----------------------------------------------------------------------===//

    void TypeChecker::visit(BlockStatementNode &node)
    {
        enter_scope();

        for (const auto &stmt : node.statements())
        {
            if (stmt)
            {
                stmt->accept(*this);
            }
        }

        exit_scope();
    }

    void TypeChecker::visit(ReturnStatementNode &node)
    {
        if (!_in_function)
        {
            report_error(TypeError::ErrorKind::InvalidOperation, node.location(),
                         "Return statement outside of function");
            return;
        }

        if (node.expression())
        {
            // Check return expression type
            node.expression()->accept(*this);

            if (node.expression()->type().has_value())
            {
                Type *expr_type = _type_context.parse_type_from_string(
                    node.expression()->type().value());

                if (_current_function_return_type &&
                    !check_assignment_compatibility(_current_function_return_type, expr_type, node.location()))
                {
                    report_type_mismatch(node.location(), _current_function_return_type, expr_type,
                                         "return statement");
                }
            }
        }
        else
        {
            // Void return - check if function expects void
            if (_current_function_return_type && !_current_function_return_type->is_void())
            {
                report_error(TypeError::ErrorKind::TypeMismatch, node.location(),
                             "Function must return a value of type '" +
                                 _current_function_return_type->to_string() + "'");
            }
        }
    }

    void TypeChecker::visit(IfStatementNode &node)
    {
        // Check condition type
        if (node.condition())
        {
            node.condition()->accept(*this);

            if (node.condition()->type().has_value())
            {
                Type *cond_type = _type_context.parse_type_from_string(
                    node.condition()->type().value());

                if (!is_boolean_context_valid(cond_type))
                {
                    report_type_mismatch(node.location(), _type_context.get_boolean_type(),
                                         cond_type, "if condition");
                }
            }
        }

        // Check then and else branches
        if (node.then_statement())
        {
            node.then_statement()->accept(*this);
        }

        if (node.else_statement())
        {
            node.else_statement()->accept(*this);
        }
    }

    void TypeChecker::visit(WhileStatementNode &node)
    {
        // Check condition type
        if (node.condition())
        {
            node.condition()->accept(*this);

            if (node.condition()->type().has_value())
            {
                Type *cond_type = _type_context.parse_type_from_string(
                    node.condition()->type().value());

                if (!is_boolean_context_valid(cond_type))
                {
                    report_type_mismatch(node.location(), _type_context.get_boolean_type(),
                                         cond_type, "while condition");
                }
            }
        }

        // Check loop body
        bool was_in_loop = _in_loop;
        _in_loop = true;

        if (node.body())
        {
            node.body()->accept(*this);
        }

        _in_loop = was_in_loop;
    }

    void TypeChecker::visit(ForStatementNode &node)
    {
        enter_scope(); // For loop creates its own scope

        // Check initialization
        if (node.init())
        {
            node.init()->accept(*this);
        }

        // Check condition
        if (node.condition())
        {
            node.condition()->accept(*this);

            if (node.condition()->type().has_value())
            {
                Type *cond_type = _type_context.parse_type_from_string(
                    node.condition()->type().value());

                if (!is_boolean_context_valid(cond_type))
                {
                    report_type_mismatch(node.location(), _type_context.get_boolean_type(),
                                         cond_type, "for condition");
                }
            }
        }

        // Check update expression
        if (node.update())
        {
            node.update()->accept(*this);
        }

        // Check loop body
        bool was_in_loop = _in_loop;
        _in_loop = true;

        if (node.body())
        {
            node.body()->accept(*this);
        }

        _in_loop = was_in_loop;
        exit_scope();
    }

    void TypeChecker::visit(ExpressionStatementNode &node)
    {
        // Visit the expression
        if (node.expression())
        {
            node.expression()->accept(*this);
        }
    }

    //===----------------------------------------------------------------------===//
    // Expression Visitors
    //===----------------------------------------------------------------------===//

    void TypeChecker::visit(LiteralNode &node)
    {
        Type *literal_type = infer_literal_type(node);
        if (literal_type)
        {
            node.set_type(literal_type->to_string());
        }
    }

    void TypeChecker::visit(IdentifierNode &node)
    {
        const std::string &name = node.name();

        TypedSymbol *symbol = _symbol_table->lookup_symbol(name);
        if (!symbol)
        {
            report_undefined_symbol(node.location(), name);
            node.set_type(_type_context.get_unknown_type()->to_string());
        }
        else
        {
            node.set_type(symbol->type->to_string());
        }
    }

    void TypeChecker::visit(BinaryExpressionNode &node)
    {
        // Visit the left and right operands
        if (node.left())
        {
            node.left()->accept(*this);
        }
        if (node.right())
        {
            node.right()->accept(*this);
        }

        // Type check the binary operation
        if (node.left() && node.right())
        {
            Type *left_type = nullptr;
            Type *right_type = nullptr;

            if (node.left()->type().has_value())
            {
                left_type = _type_context.parse_type_from_string(node.left()->type().value());
            }

            if (node.right()->type().has_value())
            {
                right_type = _type_context.parse_type_from_string(node.right()->type().value());
            }

            if (left_type && right_type)
            {
                TokenKind op = node.operator_token().kind();

                // Check for assignment operations
                if (op == TokenKind::TK_EQUAL || op == TokenKind::TK_PLUSEQUAL || op == TokenKind::TK_MINUSEQUAL ||
                    op == TokenKind::TK_STAREQUAL || op == TokenKind::TK_SLASHEQUAL)
                {
                    // For assignment, check if right is assignable to left
                    if (!left_type->is_assignable_from(*right_type))
                    {
                        report_error(TypeError::ErrorKind::InvalidAssignment, node.location(),
                                     "Type mismatch in assignment: cannot assign " +
                                         right_type->name() + " to " + left_type->name());
                    }
                    // Assignment result has the type of the left operand
                    node.set_type(left_type->name());
                }
                else
                {
                    // For other operations, types should be compatible
                    Type *result_type = nullptr;

                    // Arithmetic operations
                    if (op == TokenKind::TK_PLUS || op == TokenKind::TK_MINUS || op == TokenKind::TK_STAR || op == TokenKind::TK_SLASH)
                    {
                        if (left_type == right_type &&
                            (left_type->name() == "int" || left_type->name() == "double"))
                        {
                            result_type = left_type;
                        }
                        else
                        {
                            report_error(TypeError::ErrorKind::InvalidOperation, node.location(),
                                         "Type mismatch in arithmetic operation: " +
                                             left_type->name() + " and " + right_type->name());
                        }
                    }
                    // Comparison operations
                    else if (op == TokenKind::TK_EQUALEQUAL || op == TokenKind::TK_EXCLAIMEQUAL ||
                             op == TokenKind::TK_L_ANGLE || op == TokenKind::TK_R_ANGLE ||
                             op == TokenKind::TK_LESSEQUAL || op == TokenKind::TK_GREATEREQUAL)
                    {
                        if (left_type->is_assignable_from(*right_type) || right_type->is_assignable_from(*left_type))
                        {
                            result_type = _type_context.get_boolean_type();
                        }
                        else
                        {
                            report_error(TypeError::ErrorKind::InvalidOperation, node.location(),
                                         "Cannot compare incompatible types: " +
                                             left_type->name() + " and " + right_type->name());
                        }
                    }

                    if (result_type)
                    {
                        node.set_type(result_type->name());
                    }
                }
            }
        }
    }

    void TypeChecker::visit(CallExpressionNode &node)
    {
        // Visit callee
        if (node.callee())
        {
            node.callee()->accept(*this);
        }

        // Visit arguments
        std::vector<Type *> arg_types;
        for (const auto &arg : node.arguments())
        {
            if (arg)
            {
                arg->accept(*this);
                if (arg->type().has_value())
                {
                    Type *arg_type = _type_context.parse_type_from_string(arg->type().value());
                    arg_types.push_back(arg_type);
                }
                else
                {
                    arg_types.push_back(_type_context.get_unknown_type());
                }
            }
        }

        // Check if callee is callable
        if (node.callee() && node.callee()->type().has_value())
        {
            Type *callee_type = _type_context.parse_type_from_string(
                node.callee()->type().value());

            if (callee_type->kind() != TypeKind::Function)
            {
                report_error(TypeError::ErrorKind::NonCallableType, node.location(),
                             "Expression is not callable");
                node.set_type(_type_context.get_unknown_type()->to_string());
                return;
            }

            // Check function call compatibility
            FunctionType *func_type = static_cast<FunctionType *>(callee_type);
            if (check_function_call_compatibility(func_type, arg_types, node.location()))
            {
                node.set_type(func_type->return_type()->to_string());
            }
            else
            {
                node.set_type(_type_context.get_unknown_type()->to_string());
            }
        }
        else
        {
            node.set_type(_type_context.get_unknown_type()->to_string());
        }
    }

    //===----------------------------------------------------------------------===//
    // Type Inference Helpers
    //===----------------------------------------------------------------------===//

    Type *TypeChecker::infer_literal_type(const LiteralNode &node)
    {
        switch (node.literal_kind())
        {
        case TokenKind::TK_KW_TRUE:
        case TokenKind::TK_KW_FALSE:
            return _type_context.get_boolean_type();

        case TokenKind::TK_NUMERIC_CONSTANT:
        {
            const std::string &value = node.value();
            // Simple heuristic: if contains '.', it's a float
            if (value.find('.') != std::string::npos)
            {
                return _type_context.get_default_float_type();
            }
            else
            {
                return _type_context.get_int_type();
            }
        }

        case TokenKind::TK_STRING_LITERAL:
            return _type_context.get_string_type();

        case TokenKind::TK_CHAR_CONSTANT:
            return _type_context.get_char_type();

        default:
            return _type_context.get_unknown_type();
        }
    }

    Type *TypeChecker::infer_binary_expression_type(const BinaryExpressionNode &node)
    {
        // Get operand types
        Type *lhs_type = nullptr;
        Type *rhs_type = nullptr;

        if (node.left() && node.left()->type().has_value())
        {
            lhs_type = _type_context.parse_type_from_string(node.left()->type().value());
        }

        if (node.right() && node.right()->type().has_value())
        {
            rhs_type = _type_context.parse_type_from_string(node.right()->type().value());
        }

        if (!lhs_type || !rhs_type)
            return _type_context.get_unknown_type();

        TokenKind op = node.operator_token().kind();

        // Comparison operators always return boolean
        if (op == TokenKind::TK_EQUALEQUAL || op == TokenKind::TK_EXCLAIMEQUAL ||
            op == TokenKind::TK_L_ANGLE || op == TokenKind::TK_R_ANGLE ||
            op == TokenKind::TK_LESSEQUAL || op == TokenKind::TK_GREATEREQUAL)
        {
            return _type_context.get_boolean_type();
        }

        // Logical operators
        if (op == TokenKind::TK_AMPAMP || op == TokenKind::TK_PIPEPIPE)
        {
            return _type_context.get_boolean_type();
        }

        // Arithmetic operators - return common type
        if (op == TokenKind::TK_PLUS || op == TokenKind::TK_MINUS ||
            op == TokenKind::TK_STAR || op == TokenKind::TK_SLASH ||
            op == TokenKind::TK_PERCENT)
        {
            return _type_context.get_common_type(lhs_type, rhs_type);
        }

        return _type_context.get_unknown_type();
    }

    //===----------------------------------------------------------------------===//
    // Type Compatibility Checking
    //===----------------------------------------------------------------------===//

    bool TypeChecker::check_assignment_compatibility(Type *lhs_type, Type *rhs_type, SourceLocation loc)
    {
        if (!lhs_type || !rhs_type)
            return false;

        return lhs_type->is_assignable_from(*rhs_type);
    }

    bool TypeChecker::check_binary_operation_compatibility(TokenKind op, Type *lhs_type, Type *rhs_type, SourceLocation loc)
    {
        if (!lhs_type || !rhs_type)
            return false;

        // Comparison operators
        if (op == TokenKind::TK_EQUALEQUAL || op == TokenKind::TK_EXCLAIMEQUAL)
        {
            return _type_context.are_types_compatible(lhs_type, rhs_type);
        }

        if (op == TokenKind::TK_L_ANGLE || op == TokenKind::TK_R_ANGLE ||
            op == TokenKind::TK_LESSEQUAL || op == TokenKind::TK_GREATEREQUAL)
        {
            return is_comparable_type(lhs_type) && is_comparable_type(rhs_type) &&
                   _type_context.are_types_compatible(lhs_type, rhs_type);
        }

        // Logical operators
        if (op == TokenKind::TK_AMPAMP || op == TokenKind::TK_PIPEPIPE)
        {
            return is_boolean_context_valid(lhs_type) && is_boolean_context_valid(rhs_type);
        }

        // Arithmetic operators
        if (op == TokenKind::TK_PLUS || op == TokenKind::TK_MINUS ||
            op == TokenKind::TK_STAR || op == TokenKind::TK_SLASH)
        {
            return is_numeric_type(lhs_type) && is_numeric_type(rhs_type);
        }

        // Modulo operator - integers only
        if (op == TokenKind::TK_PERCENT)
        {
            return is_integral_type(lhs_type) && is_integral_type(rhs_type);
        }

        return false;
    }

    bool TypeChecker::check_function_call_compatibility(FunctionType *func_type,
                                                        const std::vector<Type *> &arg_types,
                                                        SourceLocation loc)
    {
        if (!func_type)
            return false;

        const auto &param_types = func_type->parameter_types();

        // Check argument count
        if (arg_types.size() < param_types.size())
        {
            report_error(TypeError::ErrorKind::TooFewArguments, loc,
                         "Too few arguments in function call");
            return false;
        }

        if (arg_types.size() > param_types.size() && !func_type->is_variadic())
        {
            report_error(TypeError::ErrorKind::TooManyArguments, loc,
                         "Too many arguments in function call");
            return false;
        }

        // Check argument types
        for (size_t i = 0; i < param_types.size(); ++i)
        {
            if (!check_assignment_compatibility(param_types[i].get(), arg_types[i], loc))
            {
                report_type_mismatch(loc, param_types[i].get(), arg_types[i],
                                     "function argument " + std::to_string(i + 1));
                return false;
            }
        }

        return true;
    }

    //===----------------------------------------------------------------------===//
    // Error Reporting
    //===----------------------------------------------------------------------===//

    void TypeChecker::report_error(TypeError::ErrorKind kind, SourceLocation loc, const std::string &message)
    {
        _errors.emplace_back(kind, loc, message);
    }

    void TypeChecker::report_type_mismatch(SourceLocation loc, Type *expected, Type *actual, const std::string &context)
    {
        std::string message = "Type mismatch in " + context;
        _errors.emplace_back(TypeError::ErrorKind::TypeMismatch, loc, message, expected, actual);
    }

    void TypeChecker::report_undefined_symbol(SourceLocation loc, const std::string &symbol_name)
    {
        std::string message = "Undefined symbol '" + symbol_name + "'";
        _errors.emplace_back(TypeError::ErrorKind::UndefinedVariable, loc, message);
    }

    void TypeChecker::report_redefined_symbol(SourceLocation loc, const std::string &symbol_name)
    {
        std::string message = "Symbol '" + symbol_name + "' is already defined";
        _errors.emplace_back(TypeError::ErrorKind::RedefinedSymbol, loc, message);
    }

    //===----------------------------------------------------------------------===//
    // Symbol Table Helpers
    //===----------------------------------------------------------------------===//

    void TypeChecker::enter_scope()
    {
        _symbol_table = _symbol_table->enter_scope();
    }

    void TypeChecker::exit_scope()
    {
        _symbol_table = _symbol_table->exit_scope();
    }

    bool TypeChecker::declare_variable(const std::string &name, Type *type, SourceLocation loc, bool is_mutable)
    {
        return _symbol_table->declare_symbol(name, type, loc, is_mutable);
    }

    Type *TypeChecker::lookup_variable_type(const std::string &name)
    {
        TypedSymbol *symbol = _symbol_table->lookup_symbol(name);
        return symbol ? symbol->type : nullptr;
    }

    //===----------------------------------------------------------------------===//
    // Utility Methods
    //===----------------------------------------------------------------------===//

    bool TypeChecker::is_numeric_type(Type *type)
    {
        return type && type->is_numeric();
    }

    bool TypeChecker::is_comparable_type(Type *type)
    {
        return type && (type->is_numeric() || type->kind() == TypeKind::String ||
                        type->kind() == TypeKind::Char);
    }

    bool TypeChecker::is_integral_type(Type *type)
    {
        return type && type->is_integral();
    }

    bool TypeChecker::is_boolean_context_valid(Type *type)
    {
        return type && (type->kind() == TypeKind::Boolean ||
                        type->is_numeric() ||
                        type->kind() == TypeKind::Pointer);
    }

    std::string TypeChecker::format_type_error(const std::string &context, Type *expected, Type *actual)
    {
        std::ostringstream oss;
        oss << "Type error in " << context << ": expected '"
            << (expected ? expected->to_string() : "unknown") << "', got '"
            << (actual ? actual->to_string() : "unknown") << "'";
        return oss.str();
    }
}