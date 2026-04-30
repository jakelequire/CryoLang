#pragma once

#include "AST/DirectiveSystem.hpp"
#include "AST/ASTNode.hpp"
#include "Parser/Parser.hpp"

namespace Cryo
{

    //===----------------------------------------------------------------------===//
    // TestDirectiveProcessor - Handles #[test(...)] directives
    //===----------------------------------------------------------------------===//

    class TestDirectiveProcessor : public DirectiveProcessor
    {
    public:
        std::string get_directive_name() const override
        {
            return "test";
        }

        bool is_statement_level() const override
        {
            return false; // File-level directive
        }

        bool process(const DirectiveNode *directive, CompilationContext &context) override
        {
            auto test_directive = dynamic_cast<const TestDirectiveNode *>(directive);
            if (!test_directive)
            {
                return false;
            }

            // Set compilation in test mode
            context.set_context("test_mode", true);
            context.set_context("test_name", test_directive->test_name());
            context.set_context("test_category", test_directive->test_category());

            // Create and add test metadata effect
            std::unique_ptr<DirectiveEffect> effect = std::make_unique<TestMetadataEffect>(
                test_directive->test_name(),
                test_directive->test_category());

            context.add_pending_effect(std::move(effect));
            return true;
        }

        std::unique_ptr<DirectiveNode> parse_directive_arguments(Parser &parser) override;

        std::string get_help_text() const override
        {
            return "Mark a file as a test case with optional name and category";
        }

        std::vector<std::string> get_argument_names() const override
        {
            return {"name", "category"};
        }
    };

    //===----------------------------------------------------------------------===//
    // ExpectErrorDirectiveProcessor - Handles #[expect_error(...)] directives
    //===----------------------------------------------------------------------===//

    class ExpectErrorDirectiveProcessor : public DirectiveProcessor
    {
    public:
        std::string get_directive_name() const override
        {
            return "expect_error";
        }

        bool is_statement_level() const override
        {
            return true; // Statement-level directive
        }

        bool process(const DirectiveNode *directive, CompilationContext &context) override
        {
            auto expect_directive = dynamic_cast<const ExpectErrorDirectiveNode *>(directive);
            if (!expect_directive)
            {
                return false;
            }

            // Create error expectation effect for the following statement
            // The target range will be set when the effect is applied to the statement
            auto effect = std::make_unique<ErrorExpectationEffect>(
                expect_directive->expected_errors(),
                SourceRange{} // Will be filled when applied to target statement
            );

            context.add_pending_effect(std::move(effect));
            return true;
        }

        std::unique_ptr<DirectiveNode> parse_directive_arguments(Parser &parser) override;

        std::string get_help_text() const override
        {
            return "Expect a specific error to occur on the following statement";
        }

        std::vector<std::string> get_argument_names() const override
        {
            return {"error_code"};
        }
    };

    //===----------------------------------------------------------------------===//
    // ExpectErrorsDirectiveProcessor - Handles #[expect_errors(...)] directives
    //===----------------------------------------------------------------------===//

    class ExpectErrorsDirectiveProcessor : public DirectiveProcessor
    {
    public:
        std::string get_directive_name() const override
        {
            return "expect_errors";
        }

        bool is_statement_level() const override
        {
            return true; // Statement-level directive
        }

        bool process(const DirectiveNode *directive, CompilationContext &context) override
        {
            auto expect_directive = dynamic_cast<const ExpectErrorDirectiveNode *>(directive);
            if (!expect_directive)
            {
                return false;
            }

            // Create error expectation effect for the next statement
            std::unique_ptr<DirectiveEffect> effect = std::make_unique<ErrorExpectationEffect>(
                expect_directive->expected_errors(),
                SourceRange{} // Will be filled when applied to target statement
            );

            context.add_pending_effect(std::move(effect));
            return true;
        }

        std::unique_ptr<DirectiveNode> parse_directive_arguments(Parser &parser) override;

        std::string get_help_text() const override
        {
            return "Expect multiple specific errors to occur on the following statement";
        }

        std::vector<std::string> get_argument_names() const override
        {
            return {"error_codes"};
        }
    };

} // namespace Cryo