#include "AST/DirectiveProcessors.hpp"
#include "Utils/Logger.hpp"
#include <iostream>

namespace Cryo
{

    //===----------------------------------------------------------------------===//
    // TestDirectiveProcessor Implementation
    //===----------------------------------------------------------------------===//

    std::unique_ptr<DirectiveNode> TestDirectiveProcessor::parse_directive_arguments(Parser &parser)
    {
        // Parse #[test(name = "directive_system_test", category = "directives")]

        // Expect '('
        if (!parser.match_token(TokenKind::TK_L_PAREN))
        {
            parser.report_error("Expected '(' after test");
            return nullptr;
        }

        auto test_directive = std::make_unique<TestDirectiveNode>(parser.peek_current().location());

        // Parse key-value pairs separated by commas
        bool first_arg = true;
        while (parser.peek_current().kind() != TokenKind::TK_R_PAREN && !parser.is_parser_at_end())
        {
            if (!first_arg)
            {
                // Expect comma between arguments
                if (!parser.match_token(TokenKind::TK_COMMA))
                {
                    parser.report_error("Expected ',' between test arguments");
                    return nullptr;
                }
            }
            first_arg = false;

            // Parse key = "value" pair
            Token key_token = parser.consume_token(TokenKind::TK_IDENTIFIER, "Expected argument name");
            std::string key = std::string(key_token.text());

            if (!parser.match_token(TokenKind::TK_EQUAL))
            {
                parser.report_error("Expected '=' after argument name");
                return nullptr;
            }

            Token value_token = parser.consume_token(TokenKind::TK_STRING_LITERAL, "Expected string value");
            std::string value = std::string(value_token.text());

            // Remove quotes from value
            if (value.length() >= 2 && value.front() == '"' && value.back() == '"')
            {
                value = value.substr(1, value.length() - 2);
            }

            // Store the argument
            if (key == "name")
            {
                test_directive->set_test_name(value);
            }
            else if (key == "category")
            {
                test_directive->set_test_category(value);
            }
        }

        // Consume the closing ')'
        parser.consume_token(TokenKind::TK_R_PAREN, "Expected ')' after test arguments");

        return std::move(test_directive);
    }

    //===----------------------------------------------------------------------===//
    // ExpectErrorDirectiveProcessor Implementation
    //===----------------------------------------------------------------------===//

    std::unique_ptr<DirectiveNode> ExpectErrorDirectiveProcessor::parse_directive_arguments(Parser &parser)
    {
        // Parse #[expect_error("ErrorCode")]

        // Expect '('
        parser.consume_token(TokenKind::TK_L_PAREN, "Expected '(' after expect_error");

        // Parse error code (string literal)
        Token error_token = parser.consume_token(TokenKind::TK_STRING_LITERAL, "Expected error code string literal");
        std::string error_code = std::string(error_token.text());

        // Remove quotes from string literal
        if (error_code.length() >= 2 && error_code.front() == '"' && error_code.back() == '"')
        {
            error_code = error_code.substr(1, error_code.length() - 2);
        }

        // Expect ')'
        parser.consume_token(TokenKind::TK_R_PAREN, "Expected ')' after error code");

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
        parser.consume_token(TokenKind::TK_L_PAREN, "Expected '(' after expect_errors");

        std::vector<std::string> error_codes;

        // Parse error codes list
        while (parser.peek_current().kind() != TokenKind::TK_R_PAREN && !parser.is_parser_at_end())
        {
            // Parse error code (string literal)
            Token error_token = parser.consume_token(TokenKind::TK_STRING_LITERAL, "Expected error code string literal");
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
                // Comma was consumed by match_token
                continue;
            }
            else if (parser.peek_current().kind() != TokenKind::TK_R_PAREN)
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

        // Consume the closing ')'
        parser.consume_token(TokenKind::TK_R_PAREN, "Expected ')' after error codes list");

        return std::make_unique<ExpectErrorDirectiveNode>(parser.peek_current().location(), error_codes);
    }

} // namespace Cryo