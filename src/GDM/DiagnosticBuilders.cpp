#include "GDM/DiagnosticBuilders.hpp"
#include "GDM/ErrorAnalysis.hpp"
#include "Utils/Logger.hpp"
#include <algorithm>

namespace Cryo
{
    //===================================================================
    // BaseDiagnosticBuilder Implementation
    //===================================================================

    SourceSpan BaseDiagnosticBuilder::create_node_span(ASTNode *node) const
    {
        if (!node)
        {
            return SourceSpan(SourceLocation(1, 1), SourceLocation(1, 1), _source_file, true);
        }

        SourceLocation start = node->location();
        // Try to estimate a reasonable end location based on node type
        size_t estimated_width = 1;

        switch (node->kind())
        {
        case NodeKind::Identifier:
        case NodeKind::VariableDeclaration:
            estimated_width = 5; // Reasonable default for identifiers
            break;
        case NodeKind::FunctionDeclaration:
            estimated_width = 8; // "function"
            break;
        case NodeKind::Literal:
            estimated_width = 10; // String with quotes or number
            break;
        default:
            estimated_width = 3;
            break;
        }

        SourceLocation end(start.line(), start.column() + estimated_width);
        return SourceSpan(start, end, _source_file, true);
    }

    SourceSpan BaseDiagnosticBuilder::create_token_span(const Token &token) const
    {
        SourceLocation start = token.location();
        size_t token_length = token.text().length();
        if (token_length == 0)
            token_length = 1;

        SourceLocation end(start.line(), start.column() + token_length);
        return SourceSpan(start, end, _source_file, true);
    }

    SourceSpan BaseDiagnosticBuilder::create_type_span(Type *type, SourceLocation location) const
    {
        if (!type)
        {
            return SourceSpan(location, location, _source_file, true);
        }

        std::string type_str = type->to_string();
        SourceLocation end(location.line(), location.column() + type_str.length());
        return SourceSpan(location, end, _source_file, true);
    }

    void BaseDiagnosticBuilder::add_common_help(Diagnostic &diagnostic, ErrorCode code) const
    {
        switch (code)
        {
        case ErrorCode::E0200_TYPE_MISMATCH:
            diagnostic.add_help("ensure the types are compatible or add an explicit cast");
            break;
        case ErrorCode::E0201_UNDEFINED_VARIABLE:
            diagnostic.add_help("check the variable name spelling or ensure it's declared in scope");
            break;
        case ErrorCode::E0202_UNDEFINED_FUNCTION:
            diagnostic.add_help("verify the function name and check if it's imported or declared");
            break;
        case ErrorCode::E0106_EXPECTED_SEMICOLON:
            diagnostic.add_help("add a semicolon (';') at the end of the statement");
            break;
        case ErrorCode::E0600_CODEGEN_FAILED:
            diagnostic.add_help("this is usually caused by earlier semantic errors");
            break;
        default:
            // No specific help for this error code
            break;
        }
    }

    std::string BaseDiagnosticBuilder::get_node_context(ASTNode *node) const
    {
        if (!node)
            return "unknown";

        switch (node->kind())
        {
        case NodeKind::VariableDeclaration:
            return "variable declaration";
        case NodeKind::FunctionDeclaration:
            return "function declaration";
        case NodeKind::CallExpression:
            return "function call";
        case NodeKind::BinaryExpression:
            return "binary expression";
        case NodeKind::IfStatement:
            return "if statement";
        case NodeKind::WhileStatement:
            return "while loop";
        case NodeKind::ReturnStatement:
            return "return statement";
        default:
            return "expression";
        }
    }

    //===================================================================
    // CodegenDiagnosticBuilder Implementation
    //===================================================================

    Diagnostic &CodegenDiagnosticBuilder::create_llvm_error(const std::string &operation,
                                                            ASTNode *node,
                                                            const std::string &llvm_message)
    {
        SourceSpan span = create_node_span(node);

        // Avoid generic "LLVM code generation failed" labels
        std::string label = "compilation error";
        if (operation.find("dereference") != std::string::npos)
        {
            label = "invalid dereference operation";
        }
        else if (operation.find("address") != std::string::npos || operation.find("Address-of") != std::string::npos)
        {
            label = "invalid address-of operation";
        }
        else if (operation.find("field") != std::string::npos || operation.find("member access") != std::string::npos)
        {
            label = "field access error";
        }
        else if (operation.find("struct type") != std::string::npos)
        {
            label = "unknown struct type";
        }
        else if (operation.find("type") != std::string::npos)
        {
            label = "type error";
        }
        span.set_label(label);

        // Make the message more semantic and less technical
        std::string message;
        if (operation.find("Unknown struct type or field") != std::string::npos)
        {
            // This should be caught by field access error, but just in case
            message = operation;
        }
        else if (operation.find("Dereference operator") != std::string::npos)
        {
            message = operation;
        }
        else if (operation.find("Address-of operator") != std::string::npos)
        {
            message = operation;
        }
        else
        {
            // Generic case - try to make it less technical
            message = operation;
            if (!llvm_message.empty())
            {
                message += ": " + llvm_message;
            }
        }

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0601_LLVM_ERROR, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        // Provide more helpful notes instead of generic LLVM references
        if (operation.find("type") != std::string::npos)
        {
            diagnostic.add_note("This indicates a type compatibility issue.");
            diagnostic.add_help("check that all types match and variables are properly declared");
        }
        else if (operation.find("dereference") != std::string::npos || operation.find("address") != std::string::npos)
        {
            diagnostic.add_note("This indicates an incorrect pointer operation.");
            diagnostic.add_help("ensure you're using pointers and references correctly");
        }
        else
        {
            diagnostic.add_note("This indicates an internal compiler error during code generation");
            diagnostic.add_help("try simplifying the expression or check for type compatibility issues");
        }

        return diagnostic;
    }

    Diagnostic &CodegenDiagnosticBuilder::create_type_mapping_error(Type *cryo_type,
                                                                    ASTNode *node,
                                                                    const std::string &reason)
    {
        SourceSpan span = create_node_span(node);
        std::string type_name = cryo_type ? cryo_type->to_string() : "unknown";
        span.set_label("cannot map type '" + type_name + "' to LLVM");

        std::string message = "failed to map Cryo type '" + type_name + "' to LLVM type";
        if (!reason.empty())
        {
            message += ": " + reason;
        }

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0602_INVALID_LLVM_TYPE, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_note("Type mapping converts Cryo types to LLVM representations");
        diagnostic.add_help("ensure the type is properly defined and supported");

        return diagnostic;
    }

    Diagnostic &CodegenDiagnosticBuilder::create_value_generation_error(const std::string &value_type,
                                                                        ASTNode *node,
                                                                        const std::string &context)
    {
        SourceSpan span = create_node_span(node);
        span.set_label("failed to generate " + value_type + " value");

        std::string message = "code generation failed for " + value_type;
        if (!context.empty())
        {
            message += " in " + context;
        }

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0603_INVALID_LLVM_VALUE, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_note("Value generation creates LLVM instructions for expressions");
        add_common_help(diagnostic, ErrorCode::E0600_CODEGEN_FAILED);

        return diagnostic;
    }

    Diagnostic &CodegenDiagnosticBuilder::create_invalid_control_flow_error(const std::string &statement_type,
                                                                            ASTNode *node,
                                                                            const std::string &context)
    {
        SourceSpan span = create_node_span(node);
        span.set_label(statement_type + " used incorrectly");

        std::string message = statement_type + " statement used outside valid context";
        if (!context.empty())
        {
            message += ": " + context;
        }

        ErrorCode error_code = ErrorCode::E0600_CODEGEN_FAILED;
        if (statement_type == "break")
        {
            error_code = ErrorCode::E0400_INVALID_BREAK;
        }
        else if (statement_type == "continue")
        {
            error_code = ErrorCode::E0401_INVALID_CONTINUE;
        }
        else if (statement_type == "return")
        {
            error_code = ErrorCode::E0402_INVALID_RETURN;
        }

        auto &diagnostic = _diagnostic_manager->create_error(error_code, span.to_source_range(), _source_file);

        if (statement_type == "break" || statement_type == "continue")
        {
            diagnostic.add_help("use " + statement_type + " only inside loops or switch statements");
        }
        else if (statement_type == "return")
        {
            diagnostic.add_help("use return only inside functions");
        }

        return diagnostic;
    }

    Diagnostic &CodegenDiagnosticBuilder::create_missing_symbol_error(const std::string &symbol_name,
                                                                      const std::string &symbol_type,
                                                                      ASTNode *node)
    {
        SourceSpan span = create_node_span(node);
        span.set_label(symbol_type + " '" + symbol_name + "' not found");

        std::string message = "undefined " + symbol_type + " '" + symbol_name + "'";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0701_UNDEFINED_SYMBOL_LINK, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_note("Symbol not found during code generation phase");
        diagnostic.add_help("ensure the " + symbol_type + " is properly declared and accessible");

        return diagnostic;
    }

    Diagnostic &CodegenDiagnosticBuilder::create_allocation_error(const std::string &allocation_type,
                                                                  ASTNode *node)
    {
        SourceSpan span = create_node_span(node);
        span.set_label("allocation failed");

        std::string message = "failed to allocate " + allocation_type;

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0600_CODEGEN_FAILED, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_note("Memory allocation failed during code generation");
        diagnostic.add_help("this may indicate a compiler bug or system resource limitation");

        return diagnostic;
    }

    Diagnostic &CodegenDiagnosticBuilder::create_function_signature_error(const std::string &function_name,
                                                                          ASTNode *node,
                                                                          const std::string &issue)
    {
        SourceSpan span = create_node_span(node);
        span.set_label("function signature issue");

        std::string message = "function '" + function_name + "' signature problem";
        if (!issue.empty())
        {
            message += ": " + issue;
        }

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0600_CODEGEN_FAILED, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_note("Function signature must be consistent between declaration and definition");
        diagnostic.add_help("verify parameter types and return type match the declaration");

        return diagnostic;
    }

    Diagnostic &CodegenDiagnosticBuilder::create_array_error(const std::string &operation,
                                                             ASTNode *node,
                                                             const std::string &details)
    {
        SourceSpan span = create_node_span(node);
        span.set_label("array " + operation + " failed");

        std::string message = "array " + operation + " error";
        if (!details.empty())
        {
            message += ": " + details;
        }

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0224_INVALID_INDEX, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_note("Array operations require valid indices and compatible types");
        diagnostic.add_help("ensure array indices are integers and within bounds");

        return diagnostic;
    }

    Diagnostic &CodegenDiagnosticBuilder::create_field_access_error(const std::string &field_name,
                                                                    const std::string &type_name,
                                                                    ASTNode *node)
    {
        SourceSpan span = create_node_span(node);

        // For member access, try to create a span that covers the field name
        if (node)
        {
            SourceLocation start = node->location();
            // Create a span that matches the field name length
            SourceLocation end(start.line(), start.column() + field_name.length());
            span = SourceSpan(start, end, _source_file, true);
        }

        span.set_label("field does not exist on type '" + type_name + "'");

        std::string message = "No field '" + field_name + "' on type '" + type_name + "'";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0204_UNDEFINED_FIELD, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        // TEMPORARILY DISABLED
        // diagnostic.add_note("This field does not exist in the struct or class.");
        // diagnostic.add_help("Check the field name for typos or verify the struct/class definition.");

        return diagnostic;
    }

    Diagnostic &CodegenDiagnosticBuilder::create_invalid_dereference_error(const std::string &type_name,
                                                                           ASTNode *node)
    {
        SourceSpan span = create_node_span(node);
        span.set_label("cannot dereference non-pointer type '" + type_name + "'");

        std::string message = "Cannot dereference value of type '" + type_name + "'";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0222_INVALID_DEREF, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_note("Cannot dereference a non-pointer type.");
        diagnostic.add_help("Only pointer types can be dereferenced with the * operator.");

        return diagnostic;
    }

    Diagnostic &CodegenDiagnosticBuilder::create_invalid_address_of_error(const std::string &type_name,
                                                                          const std::string &context,
                                                                          ASTNode *node)
    {
        SourceSpan span = create_node_span(node);

        // Create a more informative label with type information
        std::string label = "invalid address-of on non-lvalue";
        if (!type_name.empty())
        {
            label = "invalid address-of on " + type_name;
        }
        span.set_label(label);

        std::string message = "Address-of operator (&) can only be applied to variables";
        if (!context.empty())
        {
            message += ": " + context;
        }

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0223_INVALID_ADDRESS_OF, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_note("The address-of operator requires an lvalue (variable or field).");
        diagnostic.add_help("Ensure you're taking the address of a variable, not an expression result.");

        return diagnostic;
    }

    //===================================================================
    // ParserDiagnosticBuilder Implementation
    //===================================================================

    Diagnostic &ParserDiagnosticBuilder::create_unexpected_token_error(const Token &found,
                                                                       TokenKind expected,
                                                                       const std::string &context)
    {
        SourceSpan span = create_token_span(found);
        span.set_label("unexpected token");

        std::string message = "expected ";
        // Convert TokenKind to string representation
        switch (expected)
        {
        case TokenKind::TK_SEMICOLON:
            message += "';'";
            break;
        case TokenKind::TK_L_PAREN:
            message += "'('";
            break;
        case TokenKind::TK_R_PAREN:
            message += "')'";
            break;
        case TokenKind::TK_L_BRACE:
            message += "'{'";
            break;
        case TokenKind::TK_R_BRACE:
            message += "'}'";
            break;
        case TokenKind::TK_IDENTIFIER:
            message += "identifier";
            break;
        default:
            message += "token";
            break;
        }
        message += ", found '" + std::string(found.text()) + "'";

        if (!context.empty())
        {
            message += " in " + context;
        }

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0101_UNEXPECTED_TOKEN, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        add_common_help(diagnostic, ErrorCode::E0101_UNEXPECTED_TOKEN);

        return diagnostic;
    }

    Diagnostic &ParserDiagnosticBuilder::create_missing_token_error(TokenKind expected,
                                                                    SourceLocation location,
                                                                    const std::string &context)
    {
        SourceSpan span(location, location, _source_file, true);

        std::string token_name;
        ErrorCode error_code = ErrorCode::E0100_EXPECTED_TOKEN;

        switch (expected)
        {
        case TokenKind::TK_SEMICOLON:
            token_name = "';'";
            error_code = ErrorCode::E0106_EXPECTED_SEMICOLON;
            break;
        case TokenKind::TK_R_PAREN:
            token_name = "')'";
            error_code = ErrorCode::E0107_EXPECTED_PAREN;
            break;
        case TokenKind::TK_R_BRACE:
            token_name = "'}'";
            error_code = ErrorCode::E0108_EXPECTED_BRACE;
            break;
        case TokenKind::TK_R_SQUARE:
            token_name = "']'";
            error_code = ErrorCode::E0109_EXPECTED_BRACKET;
            break;
        case TokenKind::TK_IDENTIFIER:
            token_name = "identifier";
            error_code = ErrorCode::E0105_EXPECTED_IDENTIFIER;
            break;
        default:
            token_name = "token";
            break;
        }

        span.set_label("expected " + token_name);

        std::string message = "missing " + token_name;
        if (!context.empty())
        {
            message += " in " + context;
        }

        auto &diagnostic = _diagnostic_manager->create_error(error_code, span.to_source_range(), _source_file);

        add_common_help(diagnostic, error_code);

        return diagnostic;
    }

    Diagnostic &ParserDiagnosticBuilder::create_missing_delimiter_error(char delimiter,
                                                                        SourceLocation location,
                                                                        SourceLocation opening_location)
    {
        SourceSpan span(location, location, _source_file, true);
        span.set_label(std::string("expected '") + delimiter + "'");

        std::string message = std::string("missing closing '") + delimiter + "'";

        ErrorCode error_code;
        switch (delimiter)
        {
        case ')':
            error_code = ErrorCode::E0107_EXPECTED_PAREN;
            break;
        case '}':
            error_code = ErrorCode::E0108_EXPECTED_BRACE;
            break;
        case ']':
            error_code = ErrorCode::E0109_EXPECTED_BRACKET;
            break;
        default:
            error_code = ErrorCode::E0110_MISMATCHED_DELIMITERS;
            break;
        }

        auto &diagnostic = _diagnostic_manager->create_error(error_code, span.to_source_range(), _source_file);

        // Add secondary span for opening delimiter if available
        if (opening_location.line() > 0 && opening_location.column() > 0)
        {
            SourceSpan opening_span(opening_location, opening_location, _source_file, false);
            opening_span.set_label("opening delimiter here");
            diagnostic.add_secondary_span(opening_span);
        }

        diagnostic.add_help("add the missing '" + std::string(1, delimiter) + "' delimiter");

        return diagnostic;
    }

    Diagnostic &ParserDiagnosticBuilder::create_expected_expression_error(SourceLocation location,
                                                                          const std::string &context)
    {
        SourceSpan span(location, location, _source_file, true);
        span.set_label("expected expression");

        std::string message = "expected expression";
        if (!context.empty())
        {
            message += " in " + context;
        }

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0102_EXPECTED_EXPRESSION, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_help("provide a valid expression (variable, literal, function call, etc.)");

        return diagnostic;
    }

    //===================================================================
    // LexerDiagnosticBuilder Implementation
    //===================================================================

    Diagnostic &LexerDiagnosticBuilder::create_unexpected_character_error(char character,
                                                                          SourceLocation location)
    {
        SourceSpan span(location, location, _source_file, true);
        span.set_label("unexpected character");

        std::string message = "unexpected character '";
        if (std::isprint(character))
        {
            message += character;
        }
        else
        {
            message += "\\x" + std::to_string(static_cast<unsigned char>(character));
        }
        message += "'";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0001_UNEXPECTED_CHARACTER, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_help("remove the invalid character or check for encoding issues");

        return diagnostic;
    }

    Diagnostic &LexerDiagnosticBuilder::create_unterminated_string_error(SourceLocation start_location)
    {
        SourceSpan span(start_location, start_location, _source_file, true);
        span.set_label("unterminated string literal");

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0002_UNTERMINATED_STRING,
                                                             span.to_source_range(), _source_file);

        diagnostic.add_help("add a closing quote (\") to terminate the string");

        return diagnostic;
    }

    Diagnostic &LexerDiagnosticBuilder::create_invalid_escape_error(char escape_char,
                                                                    SourceLocation location)
    {
        SourceSpan span(location, location, _source_file, true);
        span.set_label("invalid escape sequence");

        std::string message = "invalid escape sequence '\\";
        message += escape_char;
        message += "'";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0005_INVALID_ESCAPE, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_help("use valid escape sequences like \\n, \\t, \\r, \\\\, or \\\"");

        return diagnostic;
    }

    //===================================================================
    // TypeCheckerDiagnosticBuilder Implementation
    //===================================================================

    Diagnostic &TypeCheckerDiagnosticBuilder::create_assignment_type_error(Type *expected,
                                                                           Type *actual,
                                                                           SourceLocation location,
                                                                           ASTNode *node)
    {
        SourceSpan span = node ? create_node_span(node) : SourceSpan(location, location, _source_file, true);

        std::string expected_name = expected ? expected->to_string() : "unknown";
        std::string actual_name = actual ? actual->to_string() : "unknown";

        span.set_label("expected `" + expected_name + "`, found `" + actual_name + "`");

        std::string message = "type mismatch in assignment: expected `" + expected_name +
                              "`, found `" + actual_name + "`";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0200_TYPE_MISMATCH, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        // Add type-specific suggestions using ErrorAnalysis
        if (expected_name == "int" && actual_name == "string")
        {
            diagnostic.add_help("if you want to convert string to integer, try parsing");
            diagnostic.add_help("if you meant to create a string variable, remove the type annotation");
        }
        else if (expected_name == "string" && actual_name == "int")
        {
            diagnostic.add_help("if you want to convert integer to string, use a conversion function");
        }

        add_common_help(diagnostic, ErrorCode::E0200_TYPE_MISMATCH);

        return diagnostic;
    }

    Diagnostic &TypeCheckerDiagnosticBuilder::create_operation_type_error(const std::string &operation,
                                                                          Type *left_type,
                                                                          Type *right_type,
                                                                          SourceLocation location)
    {
        SourceSpan span(location, location, _source_file, true);

        std::string left_name = left_type ? left_type->to_string() : "unknown";
        std::string right_name = right_type ? right_type->to_string() : "unknown";

        span.set_label("incompatible types for " + operation);

        std::string message = "cannot apply " + operation + " to `" + left_name + "` and `" + right_name + "`";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0209_INVALID_OPERATION, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_help("ensure both operands have compatible types");
        if (operation == "+")
        {
            diagnostic.add_help("arithmetic operations require numeric types, or string concatenation");
        }

        return diagnostic;
    }

    Diagnostic &TypeCheckerDiagnosticBuilder::create_function_call_error(const std::string &function_name,
                                                                         size_t expected_args,
                                                                         size_t actual_args,
                                                                         SourceLocation location)
    {
        SourceSpan span(location, location, _source_file, true);

        ErrorCode error_code = (actual_args > expected_args) ? ErrorCode::E0215_TOO_MANY_ARGS : ErrorCode::E0216_TOO_FEW_ARGS;

        std::string comparison = (actual_args > expected_args) ? "too many" : "too few";
        span.set_label(comparison + " arguments");

        std::string message = "function `" + function_name + "` expects " +
                              std::to_string(expected_args) + " argument" +
                              (expected_args != 1 ? "s" : "") + ", found " +
                              std::to_string(actual_args);

        auto &diagnostic = _diagnostic_manager->create_error(error_code, span.to_source_range(), _source_file);

        if (actual_args > expected_args)
        {
            diagnostic.add_help("remove extra arguments from the function call");
        }
        else
        {
            diagnostic.add_help("provide the required arguments to the function call");
        }

        return diagnostic;
    }

    Diagnostic &TypeCheckerDiagnosticBuilder::create_undefined_symbol_error(const std::string &symbol_name,
                                                                            NodeKind symbol_kind,
                                                                            SourceLocation location,
                                                                            const std::vector<std::string> &suggestions)
    {
        SourceSpan span(location, location, _source_file, true);
        
        // Convert NodeKind to symbol type string and error code
        std::string symbol_type;
        ErrorCode error_code = ErrorCode::E0201_UNDEFINED_VARIABLE;
        
        switch (symbol_kind) {
            case NodeKind::FunctionDeclaration:
                symbol_type = "function";
                error_code = ErrorCode::E0202_UNDEFINED_FUNCTION;
                break;
            case NodeKind::StructDeclaration:
                symbol_type = "struct";
                error_code = ErrorCode::E0203_UNDEFINED_TYPE;
                break;
            case NodeKind::ClassDeclaration:
                symbol_type = "class";
                error_code = ErrorCode::E0203_UNDEFINED_TYPE;
                break;
            case NodeKind::TraitDeclaration:
                symbol_type = "trait";
                error_code = ErrorCode::E0203_UNDEFINED_TYPE;
                break;
            case NodeKind::EnumDeclaration:
                symbol_type = "enum";
                error_code = ErrorCode::E0203_UNDEFINED_TYPE;
                break;
            case NodeKind::TypeAliasDeclaration:
                symbol_type = "type alias";
                error_code = ErrorCode::E0203_UNDEFINED_TYPE;
                break;
            case NodeKind::ImplementationBlock:
                symbol_type = "implementation target type";
                error_code = ErrorCode::E0203_UNDEFINED_TYPE;
                break;
            case NodeKind::VariableDeclaration:
            default:
                symbol_type = "variable";
                error_code = ErrorCode::E0201_UNDEFINED_VARIABLE;
                break;
        }
        
        span.set_label("undefined " + symbol_type);
        std::string message = "undefined " + symbol_type + " `" + symbol_name + "`";

        auto &diagnostic = _diagnostic_manager->create_error(error_code, span.to_source_range(), _source_file);

        // Add suggestions if available
        for (const auto &suggestion : suggestions)
        {
            diagnostic.add_help("did you mean `" + suggestion + "`?");
        }

        add_common_help(diagnostic, error_code);

        return diagnostic;
    }

    Diagnostic &TypeCheckerDiagnosticBuilder::create_redefined_symbol_error(const std::string &symbol_name,
                                                                           NodeKind symbol_kind,
                                                                           SourceLocation location)
    {
        SourceSpan span(location, location, _source_file, true);
        
        // Convert NodeKind to symbol type string
        std::string symbol_type;
        switch (symbol_kind) {
            case NodeKind::FunctionDeclaration:
                symbol_type = "function";
                break;
            case NodeKind::StructDeclaration:
                symbol_type = "struct";
                break;
            case NodeKind::ClassDeclaration:
                symbol_type = "class";
                break;
            case NodeKind::TraitDeclaration:
                symbol_type = "trait";
                break;
            case NodeKind::EnumDeclaration:
                symbol_type = "enum";
                break;
            case NodeKind::TypeAliasDeclaration:
                symbol_type = "type alias";
                break;
            case NodeKind::VariableDeclaration:
                symbol_type = "variable";
                break;
            case NodeKind::Declaration: // Generic parameter
                symbol_type = "generic parameter";
                break;
            default:
                symbol_type = "symbol";
                break;
        }
        
        span.set_label("redefined " + symbol_type);
        std::string message = symbol_type + " `" + symbol_name + "` is already defined";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0205_REDEFINED_SYMBOL, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        add_common_help(diagnostic, ErrorCode::E0205_REDEFINED_SYMBOL);

        return diagnostic;
    }

    Diagnostic &TypeCheckerDiagnosticBuilder::create_invalid_dereference_error(Type *type,
                                                                               SourceLocation location)
    {
        SourceSpan span(location, location, _source_file, true);

        std::string type_name = type ? type->to_string() : "unknown";
        span.set_label("cannot dereference non-pointer type '" + type_name + "'");

        std::string message = "Cannot dereference value of type '" + type_name + "'";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0222_INVALID_DEREF, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_note("Cannot dereference a non-pointer type.");
        diagnostic.add_help("Only pointer types can be dereferenced with the * operator.");

        return diagnostic;
    }

    Diagnostic &TypeCheckerDiagnosticBuilder::create_invalid_member_access_error(const std::string &member_name,
                                                                                 Type *object_type,
                                                                                 SourceLocation location)
    {
        SourceSpan span(location, location, _source_file, true);

        std::string type_name = object_type ? object_type->to_string() : "unknown";
        span.set_label("field does not exist on type '" + type_name + "'");

        std::string message = "No field '" + member_name + "' on type '" + type_name + "'";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0204_UNDEFINED_FIELD, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_note("This field does not exist in the struct or class.");
        diagnostic.add_help("Check the field name for typos or verify the struct/class definition.");

        return diagnostic;
    }

    Diagnostic &TypeCheckerDiagnosticBuilder::create_invalid_operation_error(const std::string &operation,
                                                                             Type *left_type, Type *right_type,
                                                                             SourceLocation location)
    {
        SourceSpan span(location, location, _source_file, true);
        
        std::string left_name = left_type ? left_type->to_string() : "unknown";
        std::string right_name = right_type ? right_type->to_string() : "unknown";
        
        if (right_type) {
            span.set_label("incompatible types for " + operation + " operation");
        } else {
            span.set_label("invalid " + operation + " operation on type '" + left_name + "'");
        }

        std::string message = right_type ? 
            ("Cannot apply '" + operation + "' to types '" + left_name + "' and '" + right_name + "'") :
            ("Cannot apply '" + operation + "' to type '" + left_name + "'");

        ErrorCode error_code = right_type ? ErrorCode::E0229_INVALID_BINARY_OP : ErrorCode::E0230_INVALID_UNARY_OP;
        auto &diagnostic = _diagnostic_manager->create_error(error_code, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        return diagnostic;
    }

    Diagnostic &TypeCheckerDiagnosticBuilder::create_non_callable_error(Type *type, SourceLocation location)
    {
        SourceSpan span(location, location, _source_file, true);
        
        std::string type_name = type ? type->to_string() : "unknown";
        span.set_label("type '" + type_name + "' cannot be called");

        std::string message = "Cannot call type '" + type_name + "' as a function";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0231_NON_CALLABLE_TYPE, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_note("Only functions and callable objects can be invoked with ()");

        return diagnostic;
    }

    Diagnostic &TypeCheckerDiagnosticBuilder::create_too_many_args_error(const std::string &function_name,
                                                                         size_t expected, size_t actual,
                                                                         SourceLocation location)
    {
        SourceSpan span(location, location, _source_file, true);
        
        span.set_label("too many arguments");

        std::string message = "Function '" + function_name + "' expects " + std::to_string(expected) + 
                             " argument" + (expected == 1 ? "" : "s") + 
                             ", but " + std::to_string(actual) + " were provided";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0215_TOO_MANY_ARGS, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        return diagnostic;
    }

    Diagnostic &TypeCheckerDiagnosticBuilder::create_invalid_assignment_error(Type *target_type, Type *value_type,
                                                                              SourceLocation location)
    {
        SourceSpan span(location, location, _source_file, true);
        
        std::string target_name = target_type ? target_type->to_string() : "unknown";
        std::string value_name = value_type ? value_type->to_string() : "unknown";
        
        span.set_label("invalid assignment to '" + target_name + "'");

        std::string message = "Cannot assign value of type '" + value_name + "' to target of type '" + target_name + "'";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0232_INVALID_ASSIGNMENT_TARGET, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        return diagnostic;
    }

    Diagnostic &TypeCheckerDiagnosticBuilder::create_undefined_variable_error(const std::string &symbol_name,
                                                                             NodeKind symbol_kind,
                                                                             SourceLocation location)
    {
        SourceSpan span(location, location, _source_file, true);
        span.set_label("undefined symbol");

        // Convert NodeKind to context description
        std::string context_description;
        switch (symbol_kind) {
            case NodeKind::FunctionDeclaration:
                context_description = "function";
                break;
            case NodeKind::StructDeclaration:
                context_description = "struct";
                break;
            case NodeKind::ClassDeclaration:
                context_description = "class";
                break;
            case NodeKind::TraitDeclaration:
                context_description = "trait";
                break;
            case NodeKind::EnumDeclaration:
                context_description = "enum";
                break;
            case NodeKind::TypeAliasDeclaration:
                context_description = "type alias";
                break;
            case NodeKind::VariableDeclaration:
            default:
                context_description = "variable";
                break;
        }

        std::string message = "Undefined " + context_description + " '" + symbol_name + "'";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0233_UNDEFINED_SYMBOL, span.to_source_range(), _source_file, message);
        diagnostic.with_primary_span(span);

        return diagnostic;
    }

    //===================================================================
    // DiagnosticBuilderFactory Implementation
    //===================================================================

    std::unique_ptr<CodegenDiagnosticBuilder> DiagnosticBuilderFactory::create_codegen_builder()
    {
        return std::make_unique<CodegenDiagnosticBuilder>(_diagnostic_manager, _source_file);
    }

    std::unique_ptr<ParserDiagnosticBuilder> DiagnosticBuilderFactory::create_parser_builder()
    {
        return std::make_unique<ParserDiagnosticBuilder>(_diagnostic_manager, _source_file);
    }

    std::unique_ptr<LexerDiagnosticBuilder> DiagnosticBuilderFactory::create_lexer_builder()
    {
        return std::make_unique<LexerDiagnosticBuilder>(_diagnostic_manager, _source_file);
    }

    std::unique_ptr<TypeCheckerDiagnosticBuilder> DiagnosticBuilderFactory::create_type_checker_builder()
    {
        return std::make_unique<TypeCheckerDiagnosticBuilder>(_diagnostic_manager, _source_file);
    }

} // namespace Cryo