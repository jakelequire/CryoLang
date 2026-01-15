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

        // Use the node's source file if available, otherwise fall back to builder's source file
        const std::string &source_file = node->source_file().empty() ? _source_file : node->source_file();

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
        return SourceSpan(start, end, source_file, true);
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

    SourceSpan BaseDiagnosticBuilder::create_type_span(TypeRef type, SourceLocation location) const
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
        case ErrorCode::E0357_INVALID_INSTANTIATION:
            diagnostic.add_help("use 'new ClassName(args)' for constructor calls or 'ClassName({field: value})' for struct literals, but not both");
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

    bool BaseDiagnosticBuilder::should_skip_error_reporting(ASTNode *node) const
    {
        // Skip errors from problematic external modules (like runtime dependencies)
        // that are being re-parsed without proper symbol context
        if (_source_file.find("std::Runtime") != std::string::npos ||
            _source_file.find("runtime") != std::string::npos)
        {
            return true;
        }

        // Skip if node is null or already has an error reported
        return !node || node->has_error();
    }

    //===================================================================
    // CodegenDiagnosticBuilder Implementation
    //===================================================================

    Diagnostic &CodegenDiagnosticBuilder::create_llvm_error(const std::string &operation,
                                                            ASTNode *node,
                                                            const std::string &llvm_message)
    {
        // Check if we should skip error reporting for this node to prevent duplicates
        if (should_skip_error_reporting(node))
        {
            // Return a dummy/placeholder diagnostic to maintain interface compatibility
            static Diagnostic dummy_diagnostic(ErrorCode::E0101_UNEXPECTED_TOKEN, DiagnosticSeverity::Error,
                                               DiagnosticCategory::CodeGen, "duplicate error suppressed",
                                               SourceRange{}, "");
            return dummy_diagnostic;
        }

        SourceSpan span = create_node_span(node);

        // Create more specific labels based on the actual error message
        std::string label = "compilation error";
        std::string message = operation;

        if (llvm_message.find("Unknown type in new expression") != std::string::npos)
        {
            // Extract the specific type name for a more informative label
            size_t colon_pos = llvm_message.rfind(": ");
            if (colon_pos != std::string::npos && colon_pos + 2 < llvm_message.length())
            {
                std::string type_name = llvm_message.substr(colon_pos + 2);
                label = "unknown type '" + type_name + "'";
            }
            else
            {
                label = "unknown type";
            }
            message = operation + ": " + llvm_message; // Combine both parts
        }
        else if (llvm_message.find("Constructor not found") != std::string::npos)
        {
            // Extract the specific type name for constructor errors
            size_t colon_pos = llvm_message.rfind(": ");
            if (colon_pos != std::string::npos && colon_pos + 2 < llvm_message.length())
            {
                std::string type_name = llvm_message.substr(colon_pos + 2);
                label = "no constructor for '" + type_name + "'";
            }
            else
            {
                label = "constructor not found";
            }
            message = operation + ": " + llvm_message;
        }
        else if (operation.find("Failed to generate generic constructor") != std::string::npos)
        {
            label = "generic instantiation failed";
            message = operation;
        }
        else if (operation.find("dereference") != std::string::npos)
        {
            label = "invalid dereference operation";
            message = operation;
        }
        else if (operation.find("address") != std::string::npos || operation.find("Address-of") != std::string::npos)
        {
            label = "invalid address-of operation";
            message = operation;
        }
        else if (llvm_message.find("Unknown struct type or field in member assignment") != std::string::npos)
        {
            // Extract the specific field name
            size_t colon_pos = llvm_message.rfind(": ");
            if (colon_pos != std::string::npos && colon_pos + 2 < llvm_message.length())
            {
                std::string field_name = llvm_message.substr(colon_pos + 2);
                label = "field '" + field_name + "' not found";
            }
            else
            {
                label = "field not found";
            }
            message = operation + ": " + llvm_message;
        }
        else if (llvm_message.find("Failed to generate binary expression") != std::string::npos)
        {
            // Extract more specific information from enhanced error messages
            if (llvm_message.find("assignment '='") != std::string::npos)
            {
                if (llvm_message.find("member access") != std::string::npos)
                {
                    label = "assignment to member failed";
                }
                else
                {
                    label = "assignment failed";
                }
            }
            else if (llvm_message.find("addition '+'") != std::string::npos)
            {
                label = "addition failed";
            }
            else if (llvm_message.find("subtraction '-'") != std::string::npos)
            {
                label = "subtraction failed";
            }
            else if (llvm_message.find("multiplication '*'") != std::string::npos)
            {
                label = "multiplication failed";
            }
            else if (llvm_message.find("division '/'") != std::string::npos)
            {
                label = "division failed";
            }
            else
            {
                label = "binary operation failed";
            }
            message = operation + ": " + llvm_message;
        }
        else if (operation.find("field") != std::string::npos || operation.find("member access") != std::string::npos)
        {
            label = "field access error";
            message = operation;
        }
        else if (llvm_message.find("Unknown struct type in implementation block") != std::string::npos)
        {
            // Extract the specific struct type name
            size_t colon_pos = llvm_message.rfind(": ");
            if (colon_pos != std::string::npos && colon_pos + 2 < llvm_message.length())
            {
                std::string remaining = llvm_message.substr(colon_pos + 2);
                size_t space_pos = remaining.find(" ");
                if (space_pos != std::string::npos)
                {
                    std::string type_name = remaining.substr(0, space_pos);
                    label = "unknown struct '" + type_name + "'";
                }
                else
                {
                    label = "unknown struct type";
                }
            }
            else
            {
                label = "unknown struct type";
            }
            message = operation + ": " + llvm_message;
        }
        else if (operation.find("struct type") != std::string::npos)
        {
            label = "unknown struct type";
            message = operation;
        }
        else if (operation.find("type") != std::string::npos)
        {
            label = "type error";
            message = operation;
        }
        else
        {
            // For any other case, use the operation as both label and message
            label = "compilation error";
            message = operation;
            if (!llvm_message.empty())
            {
                message += ": " + llvm_message;
            }
        }

        // Set the label after determining the specific type
        span.set_label(label);

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0601_LLVM_ERROR, span.to_source_range(), _source_file, message);
        diagnostic.with_primary_span(span);

        // Provide more helpful notes based on the specific error type
        if (operation.find("Unknown type in new expression") != std::string::npos)
        {
            diagnostic.add_note("The type specified in the 'new' expression was not found or is not properly defined");
            diagnostic.add_help("ensure the type is declared before use, check for typos in the type name, or verify generic type arguments are correct");
        }
        else if (operation.find("Constructor not found") != std::string::npos)
        {
            diagnostic.add_note("No constructor was found that matches the provided arguments");
            diagnostic.add_help("check that a constructor exists with the correct parameter types, or use a different constructor signature");
        }
        else if (operation.find("Failed to generate generic constructor") != std::string::npos)
        {
            diagnostic.add_note("The compiler could not instantiate the generic constructor for this type");
            diagnostic.add_help("ensure the generic type is properly defined and all generic constraints are satisfied");
        }
        else if (operation.find("type") != std::string::npos)
        {
            diagnostic.add_note("This indicates a type compatibility issue");
            diagnostic.add_help("check that all types match and variables are properly declared");
        }
        else if (operation.find("dereference") != std::string::npos || operation.find("address") != std::string::npos)
        {
            diagnostic.add_note("This indicates an incorrect pointer operation");
            diagnostic.add_help("ensure you're using pointers and references correctly");
        }
        else
        {
            diagnostic.add_note("This indicates an internal compiler error during code generation");
            diagnostic.add_help("try simplifying the expression or check for type compatibility issues");
        }

        // Mark the node as having an error to prevent duplicate reporting
        if (node)
        {
            node->mark_error();
        }

        return diagnostic;
    }

    Diagnostic &CodegenDiagnosticBuilder::report_error(ErrorCode error_code, ASTNode *node,
                                                       const std::string &message)
    {
        // Check if we should skip error reporting for this node to prevent duplicates
        if (should_skip_error_reporting(node))
        {
            // Return a dummy/placeholder diagnostic to maintain interface compatibility
            static Diagnostic dummy_diagnostic(ErrorCode::E0101_UNEXPECTED_TOKEN, DiagnosticSeverity::Error,
                                               DiagnosticCategory::CodeGen, "duplicate error suppressed",
                                               SourceRange{}, "");
            return dummy_diagnostic;
        }

        // Create source span from node or default location
        SourceSpan span = create_node_span(node);
        SourceRange range(span.start(), span.end());

        // Use custom message if provided, otherwise use error code's default message
        std::string final_message = message.empty() ? ErrorRegistry::get_error_info(error_code).short_description : message;

        // Create the diagnostic using the diagnostic manager
        Diagnostic &diagnostic = _diagnostic_manager->create_diagnostic(
            error_code, range, _source_file, final_message);

        // Add contextual information based on the node
        if (node)
        {
            std::string context = get_node_context(node);
            if (!context.empty())
            {
                diagnostic.add_note("in " + context);
            }
            node->mark_error(); // Mark node as having an error
        }

        // Add common help based on error code patterns
        add_common_help(diagnostic, error_code);

        return diagnostic;
    }

    Diagnostic &CodegenDiagnosticBuilder::report_error(ErrorCode error_code, const std::string &message,
                                                       ASTNode *node)
    {
        // Delegate to the primary report_error method
        return report_error(error_code, node, message);
    }

    Diagnostic &CodegenDiagnosticBuilder::create_type_mapping_error(TypeRef cryo_type,
                                                                    ASTNode *node,
                                                                    const std::string &reason)
    {
        return *create_diagnostic_with_error_tracking(node, [&]()
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

            return &diagnostic; });
    }

    Diagnostic &CodegenDiagnosticBuilder::create_value_generation_error(const std::string &value_type,
                                                                        ASTNode *node,
                                                                        const std::string &context)
    {
        return *create_diagnostic_with_error_tracking(node, [&]()
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

            return &diagnostic; });
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
        return *create_diagnostic_with_error_tracking(node, [&]()
                                                      {
            SourceSpan span = create_node_span(node);
            span.set_label("allocation failed");

            std::string message = "failed to allocate " + allocation_type;

            auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0600_CODEGEN_FAILED, span.to_source_range(), _source_file);
            diagnostic.with_primary_span(span);

            diagnostic.add_note("Memory allocation failed during code generation");
            diagnostic.add_help("this may indicate a compiler bug or system resource limitation");

            return &diagnostic; });
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
        return *create_diagnostic_with_error_tracking(node, [&]()
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

            return &diagnostic; });
    }

    Diagnostic &CodegenDiagnosticBuilder::create_field_access_error(const std::string &field_name,
                                                                    const std::string &type_name,
                                                                    ASTNode *node)
    {
        return *create_diagnostic_with_error_tracking(node, [&]()
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

            auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0204_UNDEFINED_FIELD, span.to_source_range(), _source_file, message);
            diagnostic.with_primary_span(span);

            // TEMPORARILY DISABLED
            // diagnostic.add_note("This field does not exist in the struct or class.");
            // diagnostic.add_help("Check the field name for typos or verify the struct/class definition.");

            return &diagnostic; });
    }

    Diagnostic &CodegenDiagnosticBuilder::create_invalid_dereference_error(const std::string &type_name,
                                                                           ASTNode *node)
    {
        return *create_diagnostic_with_error_tracking(node, [&]()
                                                      {
            SourceSpan span = create_node_span(node);
            span.set_label("cannot dereference non-pointer type '" + type_name + "'");

            std::string message = "Cannot dereference value of type '" + type_name + "'";

            auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0222_INVALID_DEREF, span.to_source_range(), _source_file, message);
            diagnostic.with_primary_span(span);

            diagnostic.add_note("Cannot dereference a non-pointer type.");
            diagnostic.add_help("Only pointer types can be dereferenced with the * operator.");

            return &diagnostic; });
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

    Diagnostic &ParserDiagnosticBuilder::create_mismatched_delimiter_error(char found, char expected,
                                                                           SourceLocation location,
                                                                           SourceLocation opening_location)
    {
        SourceSpan span(location, location, _source_file, true);
        span.set_label(std::string("found '") + found + "', expected '" + expected + "'");

        std::string message = "mismatched delimiter: found '" + std::string(1, found) +
                              "', expected '" + std::string(1, expected) + "'";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0110_MISMATCHED_DELIMITERS, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        // Add secondary span for opening delimiter if available
        if (opening_location.line() > 0)
        {
            SourceSpan opening_span(opening_location, opening_location, _source_file, false);
            opening_span.set_label("opening delimiter here");
            diagnostic.add_secondary_span(opening_span);
        }

        diagnostic.add_help("check that all opening delimiters have matching closing delimiters");

        return diagnostic;
    }

    Diagnostic &ParserDiagnosticBuilder::create_invalid_syntax_error(SourceLocation location,
                                                                     const std::string &construct,
                                                                     const std::string &suggestion)
    {
        SourceSpan span(location, location, _source_file, true);
        span.set_label("invalid syntax");

        std::string message = "invalid syntax";
        if (!construct.empty())
        {
            message += " in " + construct;
        }

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0111_INVALID_SYNTAX, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        if (!suggestion.empty())
        {
            diagnostic.add_help(suggestion);
        }
        else
        {
            diagnostic.add_help("check the syntax against the language specification");
        }

        return diagnostic;
    }

    Diagnostic &ParserDiagnosticBuilder::create_invalid_declaration_error(const std::string &declaration_type,
                                                                          SourceLocation location,
                                                                          const std::string &issue)
    {
        SourceSpan span(location, location, _source_file, true);
        span.set_label("invalid " + declaration_type + " declaration");

        std::string message = "invalid " + declaration_type + " declaration";
        if (!issue.empty())
        {
            message += ": " + issue;
        }

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0111_INVALID_SYNTAX, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_help("ensure the " + declaration_type + " declaration follows proper syntax");

        return diagnostic;
    }

    Diagnostic &ParserDiagnosticBuilder::create_invalid_pattern_error(SourceLocation location,
                                                                      const std::string &pattern_context)
    {
        SourceSpan span(location, location, _source_file, true);
        span.set_label("invalid pattern");

        std::string message = "invalid pattern";
        if (!pattern_context.empty())
        {
            message += " in " + pattern_context;
        }

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0113_INVALID_PATTERN, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_help("ensure the pattern syntax is correct for this context");

        return diagnostic;
    }

    Diagnostic &ParserDiagnosticBuilder::create_invalid_instantiation_error(const std::string &type_name,
                                                                            SourceLocation location,
                                                                            const std::string &reason)
    {
        SourceSpan span(location, location, _source_file, true);
        span.set_label("invalid instantiation");

        std::string message = "invalid instantiation of '" + type_name + "'";
        if (!reason.empty())
        {
            message += ": " + reason;
        }

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0357_INVALID_INSTANTIATION, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_help("use 'new ClassName(args)' for constructor calls or 'ClassName({field: value})' for struct literals, but not both");

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

    Diagnostic &LexerDiagnosticBuilder::create_invalid_unicode_error(SourceLocation location,
                                                                     const std::string &sequence)
    {
        SourceSpan span(location, location, _source_file, true);
        span.set_label("invalid unicode sequence");

        std::string message = "invalid unicode sequence";
        if (!sequence.empty())
        {
            message += ": '" + sequence + "'";
        }

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0006_INVALID_UNICODE, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_help("use valid unicode escape sequences like \\u0041 or \\U00000041");

        return diagnostic;
    }

    Diagnostic &LexerDiagnosticBuilder::create_unterminated_char_error(SourceLocation start_location)
    {
        SourceSpan span(start_location, start_location, _source_file, true);
        span.set_label("unterminated character literal");

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0003_UNTERMINATED_CHAR,
                                                             span.to_source_range(), _source_file);

        diagnostic.add_help("add a closing single quote (') to terminate the character literal");

        return diagnostic;
    }

    Diagnostic &LexerDiagnosticBuilder::create_invalid_number_error(const std::string &number_text,
                                                                    SourceLocation location,
                                                                    const std::string &reason)
    {
        SourceSpan span(location, location, _source_file, true);
        span.set_label("invalid number format");

        std::string message = "invalid number format: '" + number_text + "'";
        if (!reason.empty())
        {
            message += " (" + reason + ")";
        }

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0004_INVALID_NUMBER, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_help("ensure the number follows valid syntax (e.g., 123, 3.14, 0x1A, 0b1010)");

        return diagnostic;
    }

    Diagnostic &LexerDiagnosticBuilder::create_number_overflow_error(const std::string &number_text,
                                                                     SourceLocation location,
                                                                     const std::string &number_type)
    {
        SourceSpan span(location, location, _source_file, true);
        span.set_label("number overflow");

        std::string message = "number too large: '" + number_text + "'";
        if (!number_type.empty())
        {
            message += " for type " + number_type;
        }

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0010_NUMBER_TOO_LARGE, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_help("use a smaller number or a larger numeric type");

        return diagnostic;
    }

    Diagnostic &LexerDiagnosticBuilder::create_invalid_hex_error(const std::string &hex_text,
                                                                 SourceLocation location)
    {
        SourceSpan span(location, location, _source_file, true);
        span.set_label("invalid hexadecimal number");

        std::string message = "invalid hexadecimal number: '" + hex_text + "'";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0007_INVALID_HEX, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_help("hexadecimal numbers must contain only digits 0-9 and letters A-F (e.g., 0x1A2B)");

        return diagnostic;
    }

    Diagnostic &LexerDiagnosticBuilder::create_invalid_binary_error(const std::string &binary_text,
                                                                    SourceLocation location)
    {
        SourceSpan span(location, location, _source_file, true);
        span.set_label("invalid binary number");

        std::string message = "invalid binary number: '" + binary_text + "'";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0008_INVALID_BINARY, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_help("binary numbers must contain only digits 0 and 1 (e.g., 0b1010)");

        return diagnostic;
    }

    Diagnostic &LexerDiagnosticBuilder::create_invalid_octal_error(const std::string &octal_text,
                                                                   SourceLocation location)
    {
        SourceSpan span(location, location, _source_file, true);
        span.set_label("invalid octal number");

        std::string message = "invalid octal number: '" + octal_text + "'";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0009_INVALID_OCTAL, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_help("octal numbers must contain only digits 0-7 (e.g., 0o123)");

        return diagnostic;
    }

    //===================================================================
    // TypeCheckerDiagnosticBuilder Implementation
    //===================================================================

    Diagnostic &TypeCheckerDiagnosticBuilder::create_assignment_type_error(TypeRef expected,
                                                                           TypeRef actual,
                                                                           SourceLocation location,
                                                                           ASTNode *node)
    {
        // Check if we should skip error reporting for this node to prevent duplicates
        if (should_skip_error_reporting(node))
        {
            // Return a dummy/placeholder diagnostic to maintain interface compatibility
            static Diagnostic dummy_diagnostic(ErrorCode::E0101_UNEXPECTED_TOKEN, DiagnosticSeverity::Error,
                                               DiagnosticCategory::Semantic, "duplicate error suppressed",
                                               SourceRange{}, "");
            return dummy_diagnostic;
        }

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

        // Mark the node as having an error to prevent duplicate reporting
        if (node)
        {
            node->mark_error();
        }

        return diagnostic;
    }

    Diagnostic &TypeCheckerDiagnosticBuilder::create_operation_type_error(const std::string &operation,
                                                                          TypeRef left_type,
                                                                          TypeRef right_type,
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

        switch (symbol_kind)
        {
        case NodeKind::FunctionDeclaration:
            symbol_type = "function";
            error_code = ErrorCode::E0202_UNDEFINED_FUNCTION;
            break;
        case NodeKind::StructDeclaration:
            symbol_type = "type";
            error_code = ErrorCode::E0203_UNDEFINED_TYPE;
            break;
        case NodeKind::ClassDeclaration:
            symbol_type = "type";
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

        auto &diagnostic = _diagnostic_manager->create_error(error_code, span.to_source_range(), _source_file, message);

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
        switch (symbol_kind)
        {
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

    Diagnostic &TypeCheckerDiagnosticBuilder::create_redefined_symbol_error(const std::string &symbol_name,
                                                                            NodeKind symbol_kind,
                                                                            ASTNode *node)
    {
        // Use create_node_span which properly extracts source file from the node
        SourceSpan span = create_node_span(node);

        // Convert NodeKind to symbol type string
        std::string symbol_type;
        switch (symbol_kind)
        {
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

        // Get the source file - prefer node's file, fall back to builder's file
        const std::string &source_file = (node && !node->source_file().empty()) ? node->source_file() : _source_file;

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0205_REDEFINED_SYMBOL, span.to_source_range(), source_file);
        diagnostic.with_primary_span(span);

        add_common_help(diagnostic, ErrorCode::E0205_REDEFINED_SYMBOL);

        // Mark node as having error to prevent duplicates
        if (node)
        {
            node->mark_error();
        }

        return diagnostic;
    }

    Diagnostic &TypeCheckerDiagnosticBuilder::create_invalid_dereference_error(TypeRef type,
                                                                               SourceLocation location)
    {
        SourceSpan span(location, location, _source_file, true);

        std::string type_name = type ? type->to_string() : "unknown";
        span.set_label("cannot dereference non-pointer type '" + type_name + "'");

        std::string message = "Cannot dereference value of type '" + type_name + "'";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0222_INVALID_DEREF, span.to_source_range(), _source_file, message);
        diagnostic.with_primary_span(span);

        diagnostic.add_note("Cannot dereference a non-pointer type.");
        diagnostic.add_help("Only pointer types can be dereferenced with the * operator.");

        return diagnostic;
    }

    Diagnostic &TypeCheckerDiagnosticBuilder::create_invalid_member_access_error(const std::string &member_name,
                                                                                 TypeRef object_type,
                                                                                 SourceLocation location)
    {
        SourceSpan span(location, location, _source_file, true);

        std::string type_name = object_type ? object_type->to_string() : "unknown";
        span.set_label("field does not exist on type '" + type_name + "'");

        std::string message = "No field '" + member_name + "' on type '" + type_name + "'";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0204_UNDEFINED_FIELD, span.to_source_range(), _source_file, message);
        diagnostic.with_primary_span(span);

        diagnostic.add_note("This field does not exist in the struct or class.");
        diagnostic.add_help("Check the field name for typos or verify the struct/class definition.");

        return diagnostic;
    }

    Diagnostic &TypeCheckerDiagnosticBuilder::create_private_member_access_error(const std::string &member_name,
                                                                                 const std::string &type_name,
                                                                                 SourceLocation location)
    {
        // Debug logging for location info
        LOG_DEBUG(Cryo::LogComponent::DIAGNOSTIC, "Creating E0353_PRIVATE_ACCESS error: member='{}', type='{}', location=line:{}, col:{}, file='{}'",
                  member_name, type_name, location.line(), location.column(), _source_file);

        SourceSpan span(location, location, _source_file, true);

        span.set_label("cannot access private member '" + member_name + "'");

        std::string message = "Cannot access private member '" + member_name + "' of type '" + type_name + "'";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0353_PRIVATE_ACCESS, span.to_source_range(), _source_file, message);
        diagnostic.with_primary_span(span);

        diagnostic.add_note("This member is marked as private and can only be accessed from within the same class or struct.");
        diagnostic.add_help("Make the member public or access it through a public method.");

        return diagnostic;
    }

    Diagnostic &TypeCheckerDiagnosticBuilder::create_invalid_operation_error(const std::string &operation,
                                                                             TypeRef left_type, TypeRef right_type,
                                                                             SourceLocation location)
    {
        SourceSpan span(location, location, _source_file, true);

        std::string left_name = left_type ? left_type->to_string() : "unknown";
        std::string right_name = right_type ? right_type->to_string() : "unknown";

        if (right_type)
        {
            span.set_label("incompatible types for " + operation + " operation");
        }
        else
        {
            span.set_label("invalid " + operation + " operation on type '" + left_name + "'");
        }

        std::string message = right_type ? ("Cannot apply '" + operation + "' to types '" + left_name + "' and '" + right_name + "'") : ("Cannot apply '" + operation + "' to type '" + left_name + "'");

        ErrorCode error_code = right_type ? ErrorCode::E0229_INVALID_BINARY_OP : ErrorCode::E0230_INVALID_UNARY_OP;
        auto &diagnostic = _diagnostic_manager->create_error(error_code, span.to_source_range(), _source_file, message);
        diagnostic.with_primary_span(span);

        return diagnostic;
    }

    Diagnostic &TypeCheckerDiagnosticBuilder::create_invalid_operation_error(const std::string &operation,
                                                                             TypeRef left_type, TypeRef right_type,
                                                                             ASTNode *node)
    {
        // Use create_node_span which properly extracts source file from the node
        SourceSpan span = create_node_span(node);

        std::string left_name = left_type ? left_type->to_string() : "unknown";
        std::string right_name = right_type ? right_type->to_string() : "unknown";

        if (right_type)
        {
            span.set_label("incompatible types for " + operation + " operation");
        }
        else
        {
            span.set_label("invalid " + operation + " operation on type '" + left_name + "'");
        }

        std::string message = right_type ? ("Cannot apply '" + operation + "' to types '" + left_name + "' and '" + right_name + "'") : ("Cannot apply '" + operation + "' to type '" + left_name + "'");

        // Get the source file - prefer node's file, fall back to builder's file
        const std::string &source_file = (node && !node->source_file().empty()) ? node->source_file() : _source_file;

        ErrorCode error_code = right_type ? ErrorCode::E0229_INVALID_BINARY_OP : ErrorCode::E0230_INVALID_UNARY_OP;
        auto &diagnostic = _diagnostic_manager->create_error(error_code, span.to_source_range(), source_file, message);
        diagnostic.with_primary_span(span);

        // Mark node as having error to prevent duplicates
        if (node)
        {
            node->mark_error();
        }

        return diagnostic;
    }

    Diagnostic &TypeCheckerDiagnosticBuilder::create_non_callable_error(TypeRef type, SourceLocation location, ASTNode *node)
    {
        // Check for duplicate error reporting
        if (should_skip_error_reporting(node))
        {
            // Return a dummy/placeholder diagnostic to maintain interface compatibility
            static Diagnostic dummy_diagnostic(ErrorCode::E0231_NON_CALLABLE_TYPE, DiagnosticSeverity::Error,
                                               DiagnosticCategory::Semantic, "duplicate error suppressed",
                                               SourceRange{}, "");
            return dummy_diagnostic;
        }

        SourceSpan span(location, location, _source_file, true);

        std::string type_name = type ? type->to_string() : "unknown";
        span.set_label("type '" + type_name + "' cannot be called");

        std::string message = "Cannot call type '" + type_name + "' as a function";

        auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0231_NON_CALLABLE_TYPE, span.to_source_range(), _source_file);
        diagnostic.with_primary_span(span);

        diagnostic.add_note("Only functions and callable objects can be invoked with ()");

        // Mark the node as having an error
        if (node)
        {
            node->mark_error();
        }

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

    Diagnostic &TypeCheckerDiagnosticBuilder::create_invalid_assignment_error(TypeRef target_type, TypeRef value_type,
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

    Diagnostic &TypeCheckerDiagnosticBuilder::create_type_error(ErrorCode error_code, SourceLocation location,
                                                                const std::string &custom_message)
    {
        SourceSpan span(location, location, _source_file, true);
        span.set_label("type error");

        auto &diagnostic = _diagnostic_manager->create_error(error_code, span.to_source_range(), _source_file, custom_message);
        diagnostic.with_primary_span(span);

        return diagnostic;
    }

    Diagnostic &TypeCheckerDiagnosticBuilder::create_error_with_node(ErrorCode error_code, ASTNode *node,
                                                                     const std::string &message,
                                                                     const std::string &label)
    {
        // Use create_node_span which properly extracts source file from the node
        SourceSpan span = create_node_span(node);

        // Set a meaningful label
        if (!label.empty())
        {
            span.set_label(label);
        }
        else
        {
            // Generate label from error code category
            span.set_label(message.substr(0, std::min(message.size(), size_t(50))));
        }

        // Get the source file - prefer node's file, fall back to builder's file
        const std::string &source_file = (node && !node->source_file().empty()) ? node->source_file() : _source_file;

        auto &diagnostic = _diagnostic_manager->create_error(error_code, span.to_source_range(), source_file, message);
        diagnostic.with_primary_span(span);

        // Mark node as having error to prevent duplicates
        if (node)
        {
            node->mark_error();
        }

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
        switch (symbol_kind)
        {
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