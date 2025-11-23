#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

namespace Cryo
{
// Cryo Error Codes. Format: E#### where #### is a zero-padded number with the following ranges:
//   E0000-E0099:   Lexical Analysis Errors
//   E0100-E0199:   Syntax Errors
//   E0200-E0399:   Type Checking Errors
//   E0400-E0599:   Control Flow Errors
//   E0600-E0799:   Code Generation Errors
//   E0800-E0999:   System/IO Errors
//   W0001-W0999:   Warnings
#define ERROR_CODE_LIST(X)                       \
    /* General Errors */                         \
    X(E0000_UNKNOWN, 0)                          \
                                                 \
    /* Lexical Analysis Errors (E0001-E0099) */  \
    X(E0001_UNEXPECTED_CHARACTER, 1)             \
    X(E0002_UNTERMINATED_STRING, 2)              \
    X(E0003_UNTERMINATED_CHAR, 3)                \
    X(E0004_INVALID_NUMBER, 4)                   \
    X(E0005_INVALID_ESCAPE, 5)                   \
    X(E0006_INVALID_UNICODE, 6)                  \
    X(E0007_INVALID_HEX, 7)                      \
    X(E0008_INVALID_BINARY, 8)                   \
    X(E0009_INVALID_OCTAL, 9)                    \
    X(E0010_NUMBER_TOO_LARGE, 10)                \
                                                 \
    /* Syntax Errors (E0100-E0199) */            \
    X(E0100_EXPECTED_TOKEN, 100)                 \
    X(E0101_UNEXPECTED_TOKEN, 101)               \
    X(E0102_EXPECTED_EXPRESSION, 102)            \
    X(E0103_EXPECTED_STATEMENT, 103)             \
    X(E0104_EXPECTED_TYPE, 104)                  \
    X(E0105_EXPECTED_IDENTIFIER, 105)            \
    X(E0106_EXPECTED_SEMICOLON, 106)             \
    X(E0107_EXPECTED_PAREN, 107)                 \
    X(E0108_EXPECTED_BRACE, 108)                 \
    X(E0109_EXPECTED_BRACKET, 109)               \
    X(E0110_MISMATCHED_DELIMITERS, 110)          \
    X(E0111_INVALID_SYNTAX, 111)                 \
    X(E0112_UNEXPECTED_EOF, 112)                 \
    X(E0113_INVALID_PATTERN, 113)                \
    X(E0114_DUPLICATE_DEFAULT, 114)              \
                                                 \
    /* Type Checking Errors (E0200-E0399) */     \
    X(E0200_TYPE_MISMATCH, 200)                  \
    X(E0201_UNDEFINED_VARIABLE, 201)             \
    X(E0202_UNDEFINED_FUNCTION, 202)             \
    X(E0203_UNDEFINED_TYPE, 203)                 \
    X(E0204_UNDEFINED_FIELD, 204)                \
    X(E0205_REDEFINED_SYMBOL, 205)               \
    X(E0206_REDEFINED_FUNCTION, 206)             \
    X(E0207_REDEFINED_TYPE, 207)                 \
    X(E0208_INVALID_CAST, 208)                   \
    X(E0209_INVALID_OPERATION, 209)              \
    X(E0210_INVALID_ASSIGNMENT, 210)             \
    X(E0211_INCOMPATIBLE_TYPES, 211)             \
    X(E0212_VOID_VALUE_USED, 212)                \
    X(E0213_NON_CALLABLE, 213)                   \
    X(E0214_ARGUMENT_MISMATCH, 214)              \
    X(E0215_TOO_MANY_ARGS, 215)                  \
    X(E0216_TOO_FEW_ARGS, 216)                   \
    X(E0217_CONST_VIOLATION, 217)                \
    X(E0218_IMMUTABLE_ASSIGNMENT, 218)           \
    X(E0219_UNINITIALIZED_VAR, 219)              \
    X(E0220_UNREACHABLE_CODE, 220)               \
    X(E0221_CIRCULAR_DEPENDENCY, 221)            \
    X(E0222_INVALID_DEREF, 222)                  \
    X(E0223_INVALID_ADDRESS_OF, 223)             \
    X(E0224_INVALID_INDEX, 224)                  \
    X(E0225_INDEX_OUT_OF_BOUNDS, 225)            \
    X(E0226_DIVISION_BY_ZERO, 226)               \
    X(E0227_OVERFLOW, 227)                       \
    X(E0228_UNDERFLOW, 228)                      \
    X(E0229_INVALID_BINARY_OP, 229)              \
    X(E0230_INVALID_UNARY_OP, 230)               \
    X(E0231_NON_CALLABLE_TYPE, 231)              \
    X(E0232_INVALID_ASSIGNMENT_TARGET, 232)      \
    X(E0233_UNDEFINED_SYMBOL, 233)               \
                                                 \
    /* Generic/Template Errors (E0300-E0349) */  \
    X(E0300_GENERIC_INSTANTIATION_FAILED, 300)   \
    X(E0301_GENERIC_TYPE_RESOLUTION_FAILED, 301) \
    X(E0302_GENERIC_PARAM_MISMATCH, 302)         \
    X(E0303_INVALID_GENERIC_CONSTRAINT, 303)     \
    X(E0304_AMBIGUOUS_GENERIC, 304)              \
    X(E0305_RECURSIVE_GENERIC, 305)              \
                                                 \
    /* Struct/Class Errors (E0350-E0399) */      \
    X(E0350_STRUCT_FIELD_NOT_FOUND, 350)         \
    X(E0351_CLASS_MEMBER_NOT_FOUND, 351)         \
    X(E0352_CONSTRUCTOR_NOT_FOUND, 352)          \
    X(E0353_PRIVATE_ACCESS, 353)                 \
    X(E0354_ABSTRACT_METHOD_CALL, 354)           \
    X(E0355_MISSING_FIELD_INIT, 355)             \
    X(E0356_DUPLICATE_FIELD, 356)                \
    X(E0357_INVALID_INSTANTIATION, 357)          \
    X(E0358_UNDEFINED_METHOD_IMPL, 358)          \
                                                 \
    /* Control Flow Errors (E0400-E0449) */      \
    X(E0400_INVALID_BREAK, 400)                  \
    X(E0401_INVALID_CONTINUE, 401)               \
    X(E0402_INVALID_RETURN, 402)                 \
    X(E0403_MISSING_RETURN, 403)                 \
    X(E0404_UNREACHABLE_PATTERN, 404)            \
    X(E0405_NON_EXHAUSTIVE_MATCH, 405)           \
                                                 \
    /* Memory Management Errors (E0450-E0499) */ \
    X(E0450_INVALID_BORROW, 450)                 \
    X(E0451_BORROW_CHECK_FAILED, 451)            \
    X(E0452_USE_AFTER_MOVE, 452)                 \
    X(E0453_DOUBLE_FREE, 453)                    \
    X(E0454_MEMORY_LEAK, 454)                    \
    X(E0455_DANGLING_POINTER, 455)               \
                                                 \
    /* Module System Errors (E0500-E0549) */     \
    X(E0500_MODULE_NOT_FOUND, 500)               \
    X(E0501_CIRCULAR_IMPORT, 501)                \
    X(E0502_INVALID_IMPORT, 502)                 \
    X(E0503_PRIVATE_SYMBOL_ACCESS, 503)          \
    X(E0504_NAMESPACE_CONFLICT, 504)             \
                                                 \
    /* Code Generation Errors (E0600-E0699) */   \
    X(E0600_CODEGEN_FAILED, 600)                 \
    X(E0601_LLVM_ERROR, 601)                     \
    X(E0602_INVALID_LLVM_TYPE, 602)              \
    X(E0603_INVALID_LLVM_VALUE, 603)             \
    X(E0604_UNIMPLEMENTED_INTRINSIC, 604)        \
    X(E0605_OPTIMIZATION_FAILED, 605)            \
    X(E0606_FUNCTION_GENERATION_ERROR, 606)      \
    X(E0607_VARIABLE_GENERATION_ERROR, 607)      \
    X(E0608_INTRINSIC_GENERATION_ERROR, 608)     \
    X(E0609_TYPE_MAPPING_ERROR, 609)             \
    X(E0610_CLASS_GENERATION_ERROR, 610)         \
    X(E0611_ENUM_GENERATION_ERROR, 611)          \
    X(E0612_STRUCT_GENERATION_ERROR, 612)        \
    X(E0613_CONTROL_FLOW_ERROR, 613)             \
    X(E0614_ASSIGNMENT_ERROR, 614)               \
    X(E0615_BINARY_OPERATION_ERROR, 615)         \
    X(E0616_UNARY_OPERATION_ERROR, 616)          \
    X(E0617_MEMORY_OPERATION_ERROR, 617)         \
    X(E0618_CONSTRUCTOR_GENERATION_ERROR, 618)   \
    X(E0619_METHOD_GENERATION_ERROR, 619)        \
    X(E0620_MODULE_CONTEXT_ERROR, 620)           \
                                                 \
    /* Linker Errors (E0700-E0799) */            \
    X(E0700_LINK_ERROR, 700)                     \
    X(E0701_UNDEFINED_SYMBOL_LINK, 701)          \
    X(E0702_DUPLICATE_SYMBOL_LINK, 702)          \
    X(E0703_LIBRARY_NOT_FOUND, 703)              \
    X(E0704_INVALID_TARGET, 704)                 \
                                                 \
    /* System/IO Errors (E0800-E0899) */         \
    X(E0800_FILE_NOT_FOUND, 800)                 \
    X(E0801_FILE_READ_ERROR, 801)                \
    X(E0802_FILE_WRITE_ERROR, 802)               \
    X(E0803_PERMISSION_DENIED, 803)              \
    X(E0804_OUT_OF_MEMORY, 804)                  \
    X(E0805_INTERNAL_ERROR, 805)                 \
                                                 \
    /* Warning Codes (W0001-W9999) */            \
    X(W0001_UNUSED_VARIABLE, 10001)              \
    X(W0002_UNUSED_FUNCTION, 10002)              \
    X(W0003_UNUSED_IMPORT, 10003)                \
    X(W0004_SHADOWED_VARIABLE, 10004)            \
    X(W0005_IMPLICIT_CONVERSION, 10005)          \
    X(W0006_LOSSY_CONVERSION, 10006)             \
    X(W0007_DEPRECATED, 10007)                   \
    X(W0008_UNNECESSARY_CAST, 10008)             \
    X(W0009_DEAD_CODE, 10009)                    \
    X(W0010_MISSING_DOCS, 10010)

    // Generate the enum using the X-macro
    enum class ErrorCode : uint32_t
    {
#define X(name, value) name = value,
        ERROR_CODE_LIST(X)
#undef X
    };

    // Error code information for rich diagnostics
    struct ErrorInfo
    {
        ErrorCode code;
        std::string short_description;
        std::string explanation;
        std::string suggestion;
        bool is_warning;

        ErrorInfo(ErrorCode c, const std::string &desc, const std::string &exp = "",
                  const std::string &sug = "", bool warn = false)
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
        static const ErrorInfo &get_error_info(ErrorCode code);
        static std::string format_error_code(ErrorCode code);
        static std::string error_code_to_string(ErrorCode code);
        static bool is_warning(ErrorCode code);

    private:
        static void register_error_info();
    };
}