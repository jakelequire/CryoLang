#pragma once

#include "AST/ASTVisitor.hpp"
#include "AST/ASTNode.hpp"

#include <string>
#include <vector>

namespace CryoLSP
{

    /// Collects ALL impl block methods/fields for a given target type name.
    /// Shared between CompletionProvider and HoverProvider.
    class ImplBlockCollector : public Cryo::BaseASTVisitor
    {
    public:
        ImplBlockCollector(const std::string &target) : _target(target) {}

        struct MethodEntry
        {
            Cryo::StructMethodNode *method = nullptr;
        };

        struct FieldEntry
        {
            Cryo::StructFieldNode *field = nullptr;
        };

        /// Collect from a single AST root (clears previous results).
        void collect(Cryo::ASTNode *root)
        {
            methods.clear();
            fields.clear();
            if (root)
                root->accept(*this);
        }

        /// Append results from an additional AST root (does NOT clear).
        void collect_additional(Cryo::ASTNode *root)
        {
            if (root)
                root->accept(*this);
        }

        void visit(Cryo::ProgramNode &node) override
        {
            for (const auto &child : node.statements())
                if (child)
                    child->accept(*this);
        }

        void visit(Cryo::DeclarationStatementNode &node) override
        {
            if (node.declaration())
                node.declaration()->accept(*this);
        }

        void visit(Cryo::ImplementationBlockNode &node) override
        {
            // Strip generic params from target_type for matching (e.g., "Option<T>" -> "Option")
            std::string target = node.target_type();
            auto angle = target.find('<');
            if (angle != std::string::npos)
                target = target.substr(0, angle);

            if (target != _target)
                return;

            for (const auto &method : node.method_implementations())
            {
                if (method)
                    methods.push_back({method.get()});
            }
            for (const auto &field : node.field_implementations())
            {
                if (field)
                    fields.push_back({field.get()});
            }
        }

        std::vector<MethodEntry> methods;
        std::vector<FieldEntry> fields;

    private:
        std::string _target;
    };

} // namespace CryoLSP
