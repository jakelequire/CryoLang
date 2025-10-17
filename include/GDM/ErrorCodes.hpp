#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

namespace Cryo
{
    // Systematic error codes for precise error identification and documentation
    enum class ErrorCode : uint32_t
    {
        // ==== General Errors ====
        E0000_UNKNOWN = 0,                     // Unknown/generic error

        // ==== Lexical Analysis Errors (E0001-E0099) ====
        E0001_UNEXPECTED_CHARACTER = 1,        // Unexpected character in source
        E0002_UNTERMINATED_STRING = 2,         // String literal not terminated
        E0003_UNTERMINATED_CHAR = 3,           // Character literal not terminated
        E0004_INVALID_NUMBER = 4,              // Malformed numeric literal
        E0005_INVALID_ESCAPE = 5,              // Invalid escape sequence
        E0006_INVALID_UNICODE = 6,             // Invalid Unicode escape
        E0007_INVALID_HEX = 7,                 // Invalid hexadecimal literal
        E0008_INVALID_BINARY = 8,              // Invalid binary literal
        E0009_INVALID_OCTAL = 9,               // Invalid octal literal
        E0010_NUMBER_TOO_LARGE = 10,           // Numeric literal too large

        // ==== Syntax Errors (E0100-E0199) ====
        E0100_EXPECTED_TOKEN = 100,            // Expected specific token
        E0101_UNEXPECTED_TOKEN = 101,          // Unexpected token encountered
        E0102_EXPECTED_EXPRESSION = 102,       // Expected expression
        E0103_EXPECTED_STATEMENT = 103,        // Expected statement
        E0104_EXPECTED_TYPE = 104,             // Expected type annotation
        E0105_EXPECTED_IDENTIFIER = 105,       // Expected identifier
        E0106_EXPECTED_SEMICOLON = 106,        // Missing semicolon
        E0107_EXPECTED_PAREN = 107,            // Missing closing parenthesis
        E0108_EXPECTED_BRACE = 108,            // Missing closing brace
        E0109_EXPECTED_BRACKET = 109,          // Missing closing bracket
        E0110_MISMATCHED_DELIMITERS = 110,     // Mismatched delimiters
        E0111_INVALID_SYNTAX = 111,            // Invalid syntax structure
        E0112_UNEXPECTED_EOF = 112,            // Unexpected end of file
        E0113_INVALID_PATTERN = 113,           // Invalid pattern in match/destructuring
        E0114_DUPLICATE_DEFAULT = 114,         // Duplicate default case in match

        // ==== Type Checking Errors (E0200-E0399) ====
        E0200_TYPE_MISMATCH = 200,             // Type mismatch in assignment/operation
        E0201_UNDEFINED_VARIABLE = 201,        // Use of undefined variable
        E0202_UNDEFINED_FUNCTION = 202,        // Call to undefined function
        E0203_UNDEFINED_TYPE = 203,            // Reference to undefined type
        E0204_UNDEFINED_FIELD = 204,           // Access to undefined struct field
        E0205_REDEFINED_SYMBOL = 205,          // Redefinition of symbol
        E0206_REDEFINED_FUNCTION = 206,        // Function redefinition
        E0207_REDEFINED_TYPE = 207,            // Type redefinition
        E0208_INVALID_CAST = 208,              // Invalid type cast
        E0209_INVALID_OPERATION = 209,         // Invalid operation for type(s)
        E0210_INVALID_ASSIGNMENT = 210,        // Invalid assignment operation
        E0211_INCOMPATIBLE_TYPES = 211,        // Incompatible types in operation
        E0212_VOID_VALUE_USED = 212,           // Void value used in expression
        E0213_NON_CALLABLE = 213,              // Attempt to call non-callable type
        E0214_ARGUMENT_MISMATCH = 214,         // Function argument count mismatch
        E0215_TOO_MANY_ARGS = 215,             // Too many arguments provided
        E0216_TOO_FEW_ARGS = 216,              // Too few arguments provided
        E0217_CONST_VIOLATION = 217,           // Attempt to modify const value
        E0218_IMMUTABLE_ASSIGNMENT = 218,      // Assignment to immutable variable
        E0219_UNINITIALIZED_VAR = 219,         // Use of uninitialized variable
        E0220_UNREACHABLE_CODE = 220,          // Code after return/break is unreachable
        E0221_CIRCULAR_DEPENDENCY = 221,       // Circular type/module dependency
        E0222_INVALID_DEREF = 222,             // Invalid dereference operation
        E0223_INVALID_ADDRESS_OF = 223,        // Invalid address-of operation
        E0224_INVALID_INDEX = 224,             // Invalid array/container indexing
        E0225_INDEX_OUT_OF_BOUNDS = 225,       // Array index out of bounds (compile-time)
        E0226_DIVISION_BY_ZERO = 226,          // Division by zero (compile-time)
        E0227_OVERFLOW = 227,                  // Arithmetic overflow (compile-time)
        E0228_UNDERFLOW = 228,                 // Arithmetic underflow (compile-time)

        // ==== Generic/Template Errors (E0300-E0349) ====
        E0300_GENERIC_INSTANTIATION_FAILED = 300,  // Generic instantiation failed
        E0301_GENERIC_TYPE_RESOLUTION_FAILED = 301, // Generic type resolution failed
        E0302_GENERIC_PARAM_MISMATCH = 302,        // Generic parameter mismatch
        E0303_INVALID_GENERIC_CONSTRAINT = 303,    // Invalid generic constraint
        E0304_AMBIGUOUS_GENERIC = 304,             // Ambiguous generic resolution
        E0305_RECURSIVE_GENERIC = 305,             // Recursive generic instantiation

        // ==== Struct/Class Errors (E0350-E0399) ====
        E0350_STRUCT_FIELD_NOT_FOUND = 350,    // Struct field not found
        E0351_CLASS_MEMBER_NOT_FOUND = 351,    // Class member not found
        E0352_CONSTRUCTOR_NOT_FOUND = 352,     // Constructor not found
        E0353_PRIVATE_ACCESS = 353,            // Access to private member
        E0354_ABSTRACT_METHOD_CALL = 354,      // Call to abstract method
        E0355_MISSING_FIELD_INIT = 355,        // Missing field initialization
        E0356_DUPLICATE_FIELD = 356,           // Duplicate field in struct literal

        // ==== Control Flow Errors (E0400-E0449) ====
        E0400_INVALID_BREAK = 400,             // Break outside loop
        E0401_INVALID_CONTINUE = 401,          // Continue outside loop
        E0402_INVALID_RETURN = 402,            // Return outside function
        E0403_MISSING_RETURN = 403,            // Missing return in non-void function
        E0404_UNREACHABLE_PATTERN = 404,       // Unreachable pattern in match
        E0405_NON_EXHAUSTIVE_MATCH = 405,      // Non-exhaustive match patterns

        // ==== Memory Management Errors (E0450-E0499) ====
        E0450_INVALID_BORROW = 450,            // Invalid borrow operation
        E0451_BORROW_CHECK_FAILED = 451,       // Borrow checker violation
        E0452_USE_AFTER_MOVE = 452,            // Use after move
        E0453_DOUBLE_FREE = 453,               // Double free detected
        E0454_MEMORY_LEAK = 454,               // Potential memory leak
        E0455_DANGLING_POINTER = 455,          // Dangling pointer access

        // ==== Module System Errors (E0500-E0549) ====
        E0500_MODULE_NOT_FOUND = 500,          // Module not found
        E0501_CIRCULAR_IMPORT = 501,           // Circular module import
        E0502_INVALID_IMPORT = 502,            // Invalid import statement
        E0503_PRIVATE_SYMBOL_ACCESS = 503,     // Access to private symbol
        E0504_NAMESPACE_CONFLICT = 504,        // Namespace conflict

        // ==== Code Generation Errors (E0600-E0699) ====
        E0600_CODEGEN_FAILED = 600,            // General code generation failure
        E0601_LLVM_ERROR = 601,                // LLVM backend error
        E0602_INVALID_LLVM_TYPE = 602,         // Invalid LLVM type mapping
        E0603_INVALID_LLVM_VALUE = 603,        // Invalid LLVM value
        E0604_UNIMPLEMENTED_INTRINSIC = 604,   // Unimplemented intrinsic function
        E0605_OPTIMIZATION_FAILED = 605,       // Optimization pass failed

        // ==== Linker Errors (E0700-E0799) ====
        E0700_LINK_ERROR = 700,                // General linking error
        E0701_UNDEFINED_SYMBOL_LINK = 701,     // Undefined symbol at link time
        E0702_DUPLICATE_SYMBOL_LINK = 702,     // Duplicate symbol at link time
        E0703_LIBRARY_NOT_FOUND = 703,         // Required library not found
        E0704_INVALID_TARGET = 704,            // Invalid target architecture

        // ==== System/IO Errors (E0800-E0899) ====
        E0800_FILE_NOT_FOUND = 800,            // Source file not found
        E0801_FILE_READ_ERROR = 801,           // Error reading file
        E0802_FILE_WRITE_ERROR = 802,          // Error writing file
        E0803_PERMISSION_DENIED = 803,         // File permission denied
        E0804_OUT_OF_MEMORY = 804,             // System out of memory
        E0805_INTERNAL_ERROR = 805,            // Internal compiler error

        // ==== Warning Codes (W0001-W9999) ====
        W0001_UNUSED_VARIABLE = 10001,         // Unused variable
        W0002_UNUSED_FUNCTION = 10002,         // Unused function
        W0003_UNUSED_IMPORT = 10003,           // Unused import
        W0004_SHADOWED_VARIABLE = 10004,       // Variable shadows another
        W0005_IMPLICIT_CONVERSION = 10005,     // Implicit type conversion
        W0006_LOSSY_CONVERSION = 10006,        // Lossy type conversion
        W0007_DEPRECATED = 10007,              // Use of deprecated feature
        W0008_UNNECESSARY_CAST = 10008,        // Unnecessary explicit cast
        W0009_DEAD_CODE = 10009,               // Dead/unreachable code
        W0010_MISSING_DOCS = 10010,            // Missing documentation
    };

    // Error code information for rich diagnostics
    struct ErrorInfo
    {
        ErrorCode code;
        std::string short_description;
        std::string explanation;
        std::string suggestion;
        bool is_warning;
        
        ErrorInfo(ErrorCode c, const std::string& desc, const std::string& exp = "", 
                  const std::string& sug = "", bool warn = false)
            : code(c), short_description(desc), explanation(exp), suggestion(sug), is_warning(warn) {}
    };

    // Registry of all error codes with their information
    class ErrorRegistry
    {
    private:
        static std::unordered_map<ErrorCode, ErrorInfo> _error_info;
        static bool _initialized;
        
    public:
        static void initialize();
        static const ErrorInfo& get_error_info(ErrorCode code);
        static std::string format_error_code(ErrorCode code);
        static bool is_warning(ErrorCode code);
        
    private:
        static void register_error_info();
    };
}