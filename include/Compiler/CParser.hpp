#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

namespace Cryo
{
    /**
     * @brief Lightweight C declaration parser for preprocessed C code.
     *
     * Parses preprocessed C (output of clang -E -P) and extracts function
     * prototypes, typedef aliases, enum names, and struct names.
     * Skips anything it can't handle (function pointers, complex declarators)
     * by advancing to the next ';' or '}'.
     *
     * Inspired by Zig's Aro parser — minimal, resilient, and focused
     * on extracting just what Cryo needs for C interop.
     */

    struct CParam
    {
        std::string name;
        std::string c_type; // e.g., "const char *"
    };

    struct CFunctionDecl
    {
        std::string name;
        std::string return_type; // e.g., "void", "int", "const char *"
        std::vector<CParam> params;
        bool is_variadic = false;
    };

    struct CEnumConstant
    {
        std::string name;
        int64_t value;
    };

    struct CEnumDecl
    {
        std::string name; // May be empty for anonymous enums
        std::vector<CEnumConstant> constants;
    };

    class CParser
    {
    public:
        // ====================================================================
        // Embedded C Lexer (public for keyword map initialization)
        // ====================================================================

        enum class CTokKind
        {
            // Keywords
            Void,
            Int,
            Char,
            Short,
            Long,
            Float,
            Double,
            Unsigned,
            Signed,
            Const,
            Volatile,
            Static,
            Extern,
            Inline,
            Struct,
            Union,
            Enum,
            Typedef,
            Bool,  // _Bool
            SizeT, // size_t
            Int8T, // int8_t, uint8_t, etc.
            Int16T,
            Int32T,
            Int64T,
            UInt8T,
            UInt16T,
            UInt32T,
            UInt64T,

            // Punctuation
            Star,      // *
            LParen,    // (
            RParen,    // )
            LBrace,    // {
            RBrace,    // }
            LBracket,  // [
            RBracket,  // ]
            Comma,     // ,
            Semicolon, // ;
            Ellipsis,  // ...
            Equals,    // =

            // Other
            Identifier,
            NumericConstant,
            StringLiteral,
            Eof,
            Unknown,
        };

        struct CToken
        {
            CTokKind kind;
            std::string text;
        };

        /**
         * @brief Parse preprocessed C source and extract declarations.
         * @param source Preprocessed C text (output of clang -E -P)
         * @return Vector of parsed function declarations
         */
        std::vector<CFunctionDecl> parse(const std::string &source);

        // ====================================================================
        // Post-parse accessors (available after parse() returns)
        // ====================================================================

        /// Typedef aliases: name -> resolved C type string (e.g., "FooRef" -> "struct Foo *")
        const std::unordered_map<std::string, std::string> &typedefs() const { return _typedefs; }

        /// Enum type names (both tagged and typedef'd)
        const std::unordered_set<std::string> &enum_names() const { return _enum_names; }

        /// Struct/union type names (both tagged and typedef'd)
        const std::unordered_set<std::string> &struct_names() const { return _struct_names; }

        /// Parsed enum declarations with their constants
        const std::vector<CEnumDecl> &enum_decls() const { return _enum_decls; }

    private:
        // Lexer state
        std::string _source;
        size_t _pos = 0;
        std::vector<CToken> _tokens;
        size_t _tok_pos = 0;

        // Type tracking (populated during parse)
        std::unordered_map<std::string, std::string> _typedefs;
        std::unordered_set<std::string> _enum_names;
        std::unordered_set<std::string> _struct_names;
        std::vector<CEnumDecl> _enum_decls;

        void tokenize();
        void skip_whitespace_and_comments();
        CToken next_token();
        CToken lex_identifier_or_keyword();
        CToken lex_number();
        CToken lex_string_literal();

        // ====================================================================
        // Parser
        // ====================================================================

        const CToken &current() const;
        const CToken &peek(size_t offset = 1) const;
        void advance();
        bool match(CTokKind kind);
        bool check(CTokKind kind) const;
        void skip_to_semicolon_or_brace();
        void skip_balanced_braces();
        void skip_balanced_parens();

        // Parse a type specifier (qualifiers + base type + pointers)
        std::string parse_type_specifier();

        // Parse a declarator (optional name, possibly with pointer prefix)
        std::string parse_declarator_name();

        // Try to parse a function declaration; returns true on success
        bool try_parse_function_decl(CFunctionDecl &out);

        // Parse parameter list between ( and )
        bool parse_parameter_list(std::vector<CParam> &params, bool &is_variadic);

        // Typedef, enum, struct parsing
        void parse_typedef();
        void parse_enum_body(CEnumDecl &out);

        // Check if a token kind is a type keyword
        bool is_type_keyword(CTokKind kind) const;
        bool is_type_qualifier(CTokKind kind) const;

        // Check if an identifier is a known typedef name (acts as a type)
        bool is_typedef_name(const std::string &name) const;
    };

} // namespace Cryo
