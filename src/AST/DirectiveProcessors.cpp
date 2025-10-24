#include "AST/DirectiveProcessors.hpp"
#include "Utils/Logger.hpp"

namespace Cryo
{

    //===----------------------------------------------------------------------===//
    // TestDirectiveProcessor Implementation
    //===----------------------------------------------------------------------===//

    std::unique_ptr<DirectiveNode> TestDirectiveProcessor::parse_directive_arguments(Parser &parser)
    {
        // Create a test directive node with the current location
        auto test_directive = std::make_unique<TestDirectiveNode>(parser.peek_current().location());

        // Parse optional arguments: test(name="...", category="...")
        if (parser.match_token(TokenKind::TK_L_PAREN))
        {
            parser.advance_token(); // consume '('

            while (!parser.match_token(TokenKind::TK_R_PAREN) && !parser.is_parser_at_end())
            {
                // Parse argument name
                if (!parser.match_token(TokenKind::TK_IDENTIFIER))
                {
                    parser.report_error("Expected argument name in test directive");
                    return nullptr;
                }

                Token arg_name = parser.consume_token(TokenKind::TK_IDENTIFIER, "Expected argument name");

                // Expect '='
                if (!parser.match_token(TokenKind::TK_EQUAL))
                {
                    parser.report_error("Expected '=' after argument name");
                    return nullptr;
                }
                parser.advance_token(); // consume '='

                // Parse string value
                if (!parser.match_token(TokenKind::TK_STRING_LITERAL))
                {
                    parser.report_error("Expected string literal for argument value");
                    return nullptr;
                }

                Token value_token = parser.consume_token(TokenKind::TK_STRING_LITERAL, "Expected string literal");
                std::string value = std::string(value_token.text());

                // Remove quotes from string literal
                if (value.length() >= 2 && value.front() == '"' && value.back() == '"')
                {
                    value = value.substr(1, value.length() - 2);
                }

                // Set the appropriate field
                std::string name = std::string(arg_name.text());
                if (name == "name")
                {
                    test_directive->set_test_name(value);
                }
                else if (name == "category")
                {
                    test_directive->set_test_category(value);
                }
                else
                {
                    parser.report_error("Unknown test directive argument: " + name);
                    return nullptr;
                }

                // Parse optional comma
                if (parser.match_token(TokenKind::TK_COMMA))
                {
                    parser.advance_token(); // consume ','
                }
                else if (!parser.match_token(TokenKind::TK_R_PAREN))
                {
                    parser.report_error("Expected ',' or ')' in test directive arguments");
                    return nullptr;
                }
            }

            parser.consume_token(TokenKind::TK_R_PAREN, "Expected ')' after test directive arguments");
        }

        return std::move(test_directive);
    }

    //===----------------------------------------------------------------------===//
    // ExpectErrorDirectiveProcessor Implementation
    //===----------------------------------------------------------------------===//

    std::unique_ptr<DirectiveNode> ExpectErrorDirectiveProcessor::parse_directive_arguments(Parser &parser)
    {
        // Parse #[expect_error("ErrorCode")]

        // Expect '('
        if (!parser.match_token(TokenKind::TK_L_PAREN))
        {
            parser.report_error("Expected '(' after expect_error");
            return nullptr;
        }
        parser.advance_token(); // consume '('

        // Parse error code (string literal)
        if (!parser.match_token(TokenKind::TK_STRING_LITERAL))
        {
            parser.report_error("Expected error code string literal");
            return nullptr;
        }

        Token error_token = parser.consume_token(TokenKind::TK_STRING_LITERAL, "Expected error code");
        std::string error_code = std::string(error_token.text());

        // Remove quotes from string literal
        if (error_code.length() >= 2 && error_code.front() == '"' && error_code.back() == '"')
        {
            error_code = error_code.substr(1, error_code.length() - 2);
        }

        // Expect ')'
        if (!parser.match_token(TokenKind::TK_R_PAREN))
        {
            parser.report_error("Expected ')' after error code");
            return nullptr;
        }
        parser.advance_token(); // consume ')'

        // Consume closing ']'
        if (!parser.match_token(TokenKind::TK_R_SQUARE))
        {
            parser.report_error("Expected ']' after expect_error directive");
            return nullptr;
        }
        parser.advance_token(); // consume ']'

        // Create directive with single error code
        std::vector<std::string> error_codes = {error_code};
        return std::make_unique<ExpectErrorDirectiveNode>(parser.peek_current().location(), error_codes);
    }

    //===----------------------------------------------------------------------===//
    // ExpectErrorsDirectiveProcessor Implementation
    //===----------------------------------------------------------------------===//

    std::unique_ptr<DirectiveNode> ExpectErrorsDirectiveProcessor::parse_directive_arguments(Parser &parser)
    {
        // Parse #[expect_errors("ErrorCode1", "ErrorCode2", ...)]

        // Expect '('
        if (!parser.match_token(TokenKind::TK_L_PAREN))
        {
            parser.report_error("Expected '(' after expect_errors");
            return nullptr;
        }
        parser.advance_token(); // consume '('

        std::vector<std::string> error_codes;

        // Parse error codes list
        while (!parser.match_token(TokenKind::TK_R_PAREN) && !parser.is_parser_at_end())
        {
            // Parse error code (string literal)
            if (!parser.match_token(TokenKind::TK_STRING_LITERAL))
            {
                parser.report_error("Expected error code string literal");
                return nullptr;
            }

            Token error_token = parser.consume_token(TokenKind::TK_STRING_LITERAL, "Expected error code");
            std::string error_code = std::string(error_token.text());

            // Remove quotes from string literal
            if (error_code.length() >= 2 && error_code.front() == '"' && error_code.back() == '"')
            {
                error_code = error_code.substr(1, error_code.length() - 2);
            }

            error_codes.push_back(error_code);

            // Check for comma or end of list
            if (parser.match_token(TokenKind::TK_COMMA))
            {
                parser.advance_token(); // consume ','
            }
            else if (!parser.match_token(TokenKind::TK_R_PAREN))
            {
                parser.report_error("Expected ',' or ')' in error codes list");
                return nullptr;
            }
        }

        if (error_codes.empty())
        {
            parser.report_error("Expected at least one error code in expect_errors directive");
            return nullptr;
        }

        // Expect ')'
        if (!parser.match_token(TokenKind::TK_R_PAREN))
        {
            parser.report_error("Expected ')' after error codes list");
            return nullptr;
        }
        parser.advance_token(); // consume ')'

        // Consume closing ']'
        if (!parser.match_token(TokenKind::TK_R_SQUARE))
        {
            parser.report_error("Expected ']' after expect_errors directive");
            return nullptr;
        }
        parser.advance_token(); // consume ']'

        return std::make_unique<ExpectErrorDirectiveNode>(parser.peek_current().location(), error_codes);
    }

} // namespace Cryo