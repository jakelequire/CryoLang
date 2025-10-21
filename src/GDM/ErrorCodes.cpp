#include "GDM/ErrorCodes.hpp"
#include "GDM/GDM.hpp"
#include <unordered_map>

namespace Cryo
{
    // Static member initialization
    std::unordered_map<ErrorCode, ErrorInfo> ErrorRegistry::_error_info;
    bool ErrorRegistry::_initialized = false;

    void ErrorRegistry::initialize()
    {
        if (_initialized)
            return;
        register_error_info();
        _initialized = true;
    }

    const ErrorInfo &ErrorRegistry::get_error_info(ErrorCode code)
    {
        if (!_initialized)
            initialize();

        auto it = _error_info.find(code);
        if (it != _error_info.end())
        {
            return it->second;
        }

        // Return default error info for unknown codes
        static ErrorInfo unknown_error(ErrorCode::E0805_INTERNAL_ERROR, "Unknown error code", "", "", false);
        return unknown_error;
    }

    std::string ErrorRegistry::format_error_code(ErrorCode code)
    {
        uint32_t code_num = static_cast<uint32_t>(code);

        // Format warning codes differently
        if (code_num >= 10000)
        {
            return "W" + std::to_string(code_num - 10000).insert(0, 4 - std::to_string(code_num - 10000).length(), '0');
        }

        // Format error codes
        return "E" + std::to_string(code_num).insert(0, 4 - std::to_string(code_num).length(), '0');
    }

    std::string ErrorRegistry::error_code_to_string(ErrorCode code)
    {
        switch (code)
        {
#define X(name, value)    \
    case ErrorCode::name: \
        return #name;
            ERROR_CODE_LIST(X)
#undef X
        default:
            return "UNKNOWN_ERROR_CODE";
        }
    }

    bool ErrorRegistry::is_warning(ErrorCode code)
    {
        return static_cast<uint32_t>(code) >= 10000;
    }

    void ErrorRegistry::register_error_info()
    {
        // ==== General Errors ====
        _error_info.emplace(ErrorCode::E0000_UNKNOWN,
                            ErrorInfo(ErrorCode::E0000_UNKNOWN,
                                      "unknown error",
                                      "An unknown or unspecified error occurred.",
                                      "This is a generic error. Please check your code for syntax issues."));

        // ==== Lexical Analysis Errors ====
        _error_info.emplace(ErrorCode::E0001_UNEXPECTED_CHARACTER,
                            ErrorInfo(ErrorCode::E0001_UNEXPECTED_CHARACTER,
                                      "unexpected character",
                                      "This character is not valid in this position or context.",
                                      "Check if you meant to use a different character or if there's a typo."));

        _error_info.emplace(ErrorCode::E0002_UNTERMINATED_STRING,
                            ErrorInfo(ErrorCode::E0002_UNTERMINATED_STRING,
                                      "unterminated string literal",
                                      "String literals must be closed with a matching quote character.",
                                      "Add the missing closing quote (\") at the end of the string."));

        _error_info.emplace(ErrorCode::E0003_UNTERMINATED_CHAR,
                            ErrorInfo(ErrorCode::E0003_UNTERMINATED_CHAR,
                                      "unterminated character literal",
                                      "Character literals must be closed with a single quote.",
                                      "Add the missing closing quote (') after the character."));

        _error_info.emplace(ErrorCode::E0004_INVALID_NUMBER,
                            ErrorInfo(ErrorCode::E0004_INVALID_NUMBER,
                                      "invalid numeric literal",
                                      "The numeric literal contains invalid characters or formatting.",
                                      "Check the number format and ensure it follows valid syntax."));

        _error_info.emplace(ErrorCode::E0005_INVALID_ESCAPE,
                            ErrorInfo(ErrorCode::E0005_INVALID_ESCAPE,
                                      "invalid escape sequence",
                                      "The escape sequence is not recognized or properly formatted.",
                                      "Use a valid escape sequence like \\n, \\t, \\r, \\\\, \\\", or \\uXXXX."));

        // ==== Syntax Errors ====
        _error_info.emplace(ErrorCode::E0100_EXPECTED_TOKEN,
                            ErrorInfo(ErrorCode::E0100_EXPECTED_TOKEN,
                                      "expected token",
                                      "A specific token was expected at this position but not found.",
                                      "Add the missing token or check the syntax structure."));

        _error_info.emplace(ErrorCode::E0101_UNEXPECTED_TOKEN,
                            ErrorInfo(ErrorCode::E0101_UNEXPECTED_TOKEN,
                                      "unexpected token",
                                      "An unexpected token was encountered in this context.",
                                      "Remove the unexpected token or check if it belongs elsewhere."));

        _error_info.emplace(ErrorCode::E0106_EXPECTED_SEMICOLON,
                            ErrorInfo(ErrorCode::E0106_EXPECTED_SEMICOLON,
                                      "expected semicolon",
                                      "Statements in Cryo must end with a semicolon.",
                                      "Add a semicolon (;) at the end of the statement."));

        // ==== Type Checking Errors ====
        _error_info.emplace(ErrorCode::E0200_TYPE_MISMATCH,
                            ErrorInfo(ErrorCode::E0200_TYPE_MISMATCH,
                                      "type mismatch",
                                      "The expression type does not match what was expected.",
                                      "Ensure the types are compatible or add an explicit cast if appropriate."));

        _error_info.emplace(ErrorCode::E0201_UNDEFINED_VARIABLE,
                            ErrorInfo(ErrorCode::E0201_UNDEFINED_VARIABLE,
                                      "undefined variable",
                                      "This variable has not been declared in the current scope.",
                                      "Declare the variable before using it, or check for typos in the variable name."));

        _error_info.emplace(ErrorCode::E0202_UNDEFINED_FUNCTION,
                            ErrorInfo(ErrorCode::E0202_UNDEFINED_FUNCTION,
                                      "undefined function",
                                      "This function has not been declared or is not in scope.",
                                      "Declare the function, import the module containing it, or check for typos."));

        _error_info.emplace(ErrorCode::E0204_UNDEFINED_FIELD,
                            ErrorInfo(ErrorCode::E0204_UNDEFINED_FIELD,
                                      "undefined field",
                                      "This field does not exist in the struct or class.",
                                      "Check the field name for typos or verify the struct/class definition."));

        _error_info.emplace(ErrorCode::E0205_REDEFINED_SYMBOL,
                            ErrorInfo(ErrorCode::E0205_REDEFINED_SYMBOL,
                                      "redefined symbol",
                                      "This symbol has already been defined in the current scope.",
                                      "Use a different name or check if you meant to modify an existing definition."));

        _error_info.emplace(ErrorCode::E0213_NON_CALLABLE,
                            ErrorInfo(ErrorCode::E0213_NON_CALLABLE,
                                      "attempt to call non-function",
                                      "Only functions and function pointers can be called with parentheses.",
                                      "Check if you meant to access a field instead, or if the identifier should refer to a function."));

        _error_info.emplace(ErrorCode::E0214_ARGUMENT_MISMATCH,
                            ErrorInfo(ErrorCode::E0214_ARGUMENT_MISMATCH,
                                      "argument count mismatch",
                                      "The number of arguments provided does not match the function signature.",
                                      "Provide the correct number of arguments as defined in the function declaration."));

        _error_info.emplace(ErrorCode::E0231_NON_CALLABLE_TYPE,
                            ErrorInfo(ErrorCode::E0231_NON_CALLABLE_TYPE,
                                      "type is not callable",
                                      "Cannot call this type as a function - only functions and callable objects can be invoked.",
                                      "Check if this should be a function call, field access, or method invocation."));

        _error_info.emplace(ErrorCode::E0209_INVALID_OPERATION,
                            ErrorInfo(ErrorCode::E0209_INVALID_OPERATION,
                                      "invalid operation",
                                      "This operation cannot be performed on the given type(s).",
                                      "Check that the operands are compatible with this operation."));

        _error_info.emplace(ErrorCode::E0222_INVALID_DEREF,
                            ErrorInfo(ErrorCode::E0222_INVALID_DEREF,
                                      "invalid dereference",
                                      "Cannot dereference a non-pointer type.",
                                      "Only pointer types can be dereferenced with the * operator."));

        _error_info.emplace(ErrorCode::E0223_INVALID_ADDRESS_OF,
                            ErrorInfo(ErrorCode::E0223_INVALID_ADDRESS_OF,
                                      "invalid address-of operation",
                                      "Cannot take the address of this expression.",
                                      "The & operator can only be used with lvalue expressions and variables."));

        _error_info.emplace(ErrorCode::E0229_INVALID_BINARY_OP,
                            ErrorInfo(ErrorCode::E0229_INVALID_BINARY_OP,
                                      "invalid binary operation",
                                      "Cannot perform this operation on the given types.",
                                      "Check that the operands are compatible with this operator."));

        _error_info.emplace(ErrorCode::E0230_INVALID_UNARY_OP,
                            ErrorInfo(ErrorCode::E0230_INVALID_UNARY_OP,
                                      "invalid unary operation",
                                      "Cannot perform this unary operation on the given type.",
                                      "Check that the operand is compatible with this operator."));

        // ==== Control Flow Errors ====
        _error_info.emplace(ErrorCode::E0400_INVALID_BREAK,
                            ErrorInfo(ErrorCode::E0400_INVALID_BREAK,
                                      "break outside loop",
                                      "Break statements can only be used inside loops or match expressions.",
                                      "Move the break statement inside a loop or remove it."));

        _error_info.emplace(ErrorCode::E0401_INVALID_CONTINUE,
                            ErrorInfo(ErrorCode::E0401_INVALID_CONTINUE,
                                      "continue outside loop",
                                      "Continue statements can only be used inside loops.",
                                      "Move the continue statement inside a loop or remove it."));

        // ==== Parser Errors (missing registrations) ====
        _error_info.emplace(ErrorCode::E0111_INVALID_SYNTAX,
                            ErrorInfo(ErrorCode::E0111_INVALID_SYNTAX,
                                      "invalid syntax",
                                      "The syntax is not valid in this context.",
                                      "Check the language reference for correct syntax."));

        _error_info.emplace(ErrorCode::E0216_TOO_FEW_ARGS,
                            ErrorInfo(ErrorCode::E0216_TOO_FEW_ARGS,
                                      "too few arguments",
                                      "This function call has fewer arguments than required.",
                                      "Add the missing arguments to match the function signature."));

        _error_info.emplace(ErrorCode::E0215_TOO_MANY_ARGS,
                            ErrorInfo(ErrorCode::E0215_TOO_MANY_ARGS,
                                      "too many arguments",
                                      "This function call has more arguments than expected.",
                                      "Remove the extra arguments to match the function signature."));

        _error_info.emplace(ErrorCode::E0106_EXPECTED_SEMICOLON,
                            ErrorInfo(ErrorCode::E0106_EXPECTED_SEMICOLON,
                                      "expected semicolon",
                                      "A semicolon is required to terminate this statement.",
                                      "Add a semicolon (;) at the end of the statement."));

        _error_info.emplace(ErrorCode::E0107_EXPECTED_PAREN,
                            ErrorInfo(ErrorCode::E0107_EXPECTED_PAREN,
                                      "expected parenthesis",
                                      "A parenthesis is expected in this context.",
                                      "Add the missing opening or closing parenthesis."));

        _error_info.emplace(ErrorCode::E0108_EXPECTED_BRACE,
                            ErrorInfo(ErrorCode::E0108_EXPECTED_BRACE,
                                      "expected brace",
                                      "A brace is expected in this context.",
                                      "Add the missing opening or closing brace."));

        _error_info.emplace(ErrorCode::E0109_EXPECTED_BRACKET,
                            ErrorInfo(ErrorCode::E0109_EXPECTED_BRACKET,
                                      "expected bracket",
                                      "A bracket is expected in this context.",
                                      "Add the missing opening or closing bracket."));

        _error_info.emplace(ErrorCode::E0110_MISMATCHED_DELIMITERS,
                            ErrorInfo(ErrorCode::E0110_MISMATCHED_DELIMITERS,
                                      "mismatched delimiters",
                                      "The opening and closing delimiters don't match.",
                                      "Ensure that each opening delimiter has a matching closing delimiter."));

        // ==== System/IO Errors ====
        _error_info.emplace(ErrorCode::E0800_FILE_NOT_FOUND,
                            ErrorInfo(ErrorCode::E0800_FILE_NOT_FOUND,
                                      "file not found",
                                      "The specified source file could not be found.",
                                      "Check the file path and ensure the file exists."));

        // ==== Warnings ====
        _error_info.emplace(ErrorCode::W0001_UNUSED_VARIABLE,
                            ErrorInfo(ErrorCode::W0001_UNUSED_VARIABLE,
                                      "unused variable",
                                      "This variable is declared but never used.",
                                      "Remove the variable if not needed, or prefix with underscore to indicate intentional non-use.",
                                      true));

        _error_info.emplace(ErrorCode::W0004_SHADOWED_VARIABLE,
                            ErrorInfo(ErrorCode::W0004_SHADOWED_VARIABLE,
                                      "variable shadows another",
                                      "This variable declaration shadows a variable from an outer scope.",
                                      "Use a different variable name to avoid confusion.",
                                      true));

        _error_info.emplace(ErrorCode::W0005_IMPLICIT_CONVERSION,
                            ErrorInfo(ErrorCode::W0005_IMPLICIT_CONVERSION,
                                      "implicit type conversion",
                                      "An implicit type conversion is taking place which might not be intended.",
                                      "Add an explicit cast if the conversion is intentional.",
                                      true));
    }
}