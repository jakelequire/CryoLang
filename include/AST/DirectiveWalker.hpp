#pragma once

#include "AST/ASTVisitor.hpp"
#include "AST/DirectiveSystem.hpp"
#include "Utils/Logger.hpp"

namespace Cryo
{

    //===----------------------------------------------------------------------===//
    // DirectiveWalker - AST visitor for processing directives during compilation
    //===----------------------------------------------------------------------===//

    class DirectiveWalker : public BaseASTVisitor
    {
    private:
        DirectiveRegistry *_registry;
        CompilationContext &_context;
        bool _has_errors = false;

    public:
        DirectiveWalker(DirectiveRegistry *registry, CompilationContext &context)
            : _registry(registry), _context(context) {}

        bool has_errors() const { return _has_errors; }

        // Process directives on any AST node
        bool process_node_directives(ASTNode *node)
        {
            if (!node || !node->has_directives())
            {
                return true;
            }

            bool success = true;

            // First, process any pending effects from previous directives
            _context.process_pending_effects(node);

            // Then process directives attached to this node
            for (const auto &directive : node->get_directives())
            {
                if (!process_directive(directive.get(), node))
                {
                    success = false;
                    _has_errors = true;
                }
            }

            return success;
        }

        // Override base visitor methods to process directives on each node type
        void visit(ProgramNode &node) override
        {
            process_node_directives(&node);
            BaseASTVisitor::visit(node);
        }

        void visit(FunctionDeclarationNode &node) override
        {
            process_node_directives(&node);
            BaseASTVisitor::visit(node);
        }

        void visit(VariableDeclarationNode &node) override
        {
            process_node_directives(&node);
            BaseASTVisitor::visit(node);
        }

        void visit(StructDeclarationNode &node) override
        {
            process_node_directives(&node);
            BaseASTVisitor::visit(node);
        }

        void visit(ClassDeclarationNode &node) override
        {
            process_node_directives(&node);
            BaseASTVisitor::visit(node);
        }

        void visit(IfStatementNode &node) override
        {
            process_node_directives(&node);
            BaseASTVisitor::visit(node);
        }

        void visit(WhileStatementNode &node) override
        {
            process_node_directives(&node);
            BaseASTVisitor::visit(node);
        }

        void visit(ForStatementNode &node) override
        {
            process_node_directives(&node);
            BaseASTVisitor::visit(node);
        }

        void visit(ReturnStatementNode &node) override
        {
            process_node_directives(&node);
            BaseASTVisitor::visit(node);
        }

        void visit(ExpressionStatementNode &node) override
        {
            process_node_directives(&node);
            BaseASTVisitor::visit(node);
        }

        void visit(BlockStatementNode &node) override
        {
            process_node_directives(&node);
            BaseASTVisitor::visit(node);
        }

        void visit(CallExpressionNode &node) override
        {
            process_node_directives(&node);
            BaseASTVisitor::visit(node);
        }

        void visit(BinaryExpressionNode &node) override
        {
            process_node_directives(&node);
            BaseASTVisitor::visit(node);
        }

        void visit(UnaryExpressionNode &node) override
        {
            process_node_directives(&node);
            BaseASTVisitor::visit(node);
        }

        void visit(IdentifierNode &node) override
        {
            process_node_directives(&node);
            BaseASTVisitor::visit(node);
        }

        void visit(LiteralNode &node) override
        {
            process_node_directives(&node);
            BaseASTVisitor::visit(node);
        }

        // Add more visit methods as needed for specific node types

    private:
        bool process_directive(const DirectiveNode *directive, ASTNode *target_node)
        {
            if (!directive || !_registry)
            {
                return false;
            }

            auto processor = _registry->get_processor(directive->name());
            if (!processor)
            {
                LOG_ERROR(LogComponent::GENERAL, "No processor found for directive: {}", directive->name());
                return false;
            }

            // Validate the directive first
            if (!processor->validate_directive(directive))
            {
                LOG_ERROR(LogComponent::GENERAL, "Directive validation failed: {}", directive->name());
                return false;
            }

            // Process the directive
            bool success = processor->process(directive, _context);
            if (!success)
            {
                LOG_ERROR(LogComponent::GENERAL, "Failed to process directive: {}", directive->name());
            }
            else
            {
                LOG_DEBUG(LogComponent::GENERAL, "Successfully processed directive: {} on node type: {}",
                          directive->name(), NodeKindToString(target_node->kind()));
            }

            return success;
        }
    };

} // namespace Cryo