#pragma once

#include "AST/ASTVisitor.hpp"
#include "AST/ASTNode.hpp"
#include <string>
#include <vector>

namespace CryoLSP
{

    // ============================================================================
    // Declaration Finder - walks top-level AST to find declarations by name
    // (functions, structs, classes, enums, traits, type aliases, intrinsics)
    // ============================================================================

    class DeclarationFinder : public Cryo::BaseASTVisitor
    {
    public:
        DeclarationFinder(const std::string &name) : _target(name) {}

        Cryo::ASTNode *find(Cryo::ASTNode *root)
        {
            _result = nullptr;
            if (root)
                root->accept(*this);
            return _result;
        }

        void visit(Cryo::ProgramNode &node) override;
        void visit(Cryo::DeclarationStatementNode &node) override;
        void visit(Cryo::FunctionDeclarationNode &node) override;
        void visit(Cryo::StructDeclarationNode &node) override;
        void visit(Cryo::ClassDeclarationNode &node) override;
        void visit(Cryo::EnumDeclarationNode &node) override;
        void visit(Cryo::TraitDeclarationNode &node) override;
        void visit(Cryo::TypeAliasDeclarationNode &node) override;
        void visit(Cryo::IntrinsicDeclarationNode &node) override;

    private:
        std::string _target;
        Cryo::ASTNode *_result = nullptr;
    };

    // ============================================================================
    // Member Finder - searches methods and fields across structs, classes, and
    // impl blocks for a given member name
    // ============================================================================

    class MemberFinder : public Cryo::BaseASTVisitor
    {
    public:
        MemberFinder(const std::string &member_name) : _target(member_name) {}

        Cryo::ASTNode *find(Cryo::ASTNode *root)
        {
            _result = nullptr;
            _owner_name.clear();
            if (root)
                root->accept(*this);
            return _result;
        }

        const std::string &owner_name() const { return _owner_name; }

        void visit(Cryo::ProgramNode &node) override;
        void visit(Cryo::DeclarationStatementNode &node) override;
        void visit(Cryo::StructDeclarationNode &node) override;
        void visit(Cryo::ClassDeclarationNode &node) override;
        void visit(Cryo::ImplementationBlockNode &node) override;

    private:
        std::string _target;
        Cryo::ASTNode *_result = nullptr;
        std::string _owner_name;
    };

    // ============================================================================
    // Scoped Member Finder - searches for a member within a specific named type
    // (struct, class, or impl block matching the given owner name)
    // ============================================================================

    class ScopedMemberFinder : public Cryo::BaseASTVisitor
    {
    public:
        ScopedMemberFinder(const std::string &owner_name, const std::string &member_name)
            : _owner(owner_name), _member(member_name) {}

        Cryo::ASTNode *find(Cryo::ASTNode *root)
        {
            _result = nullptr;
            if (root)
                root->accept(*this);
            return _result;
        }

        void visit(Cryo::ProgramNode &node) override;
        void visit(Cryo::DeclarationStatementNode &node) override;
        void visit(Cryo::StructDeclarationNode &node) override;
        void visit(Cryo::ClassDeclarationNode &node) override;
        void visit(Cryo::EnumDeclarationNode &node) override;
        void visit(Cryo::ImplementationBlockNode &node) override;

    private:
        std::string _owner;
        std::string _member;
        Cryo::ASTNode *_result = nullptr;
    };

    // ============================================================================
    // Variable Reference Resolver - deep AST search for variable/parameter
    // declarations by name (recurses into function bodies, blocks, etc.)
    // ============================================================================

    class VariableRefResolver : public Cryo::BaseASTVisitor
    {
    public:
        VariableRefResolver(const std::string &name) : _target(name) {}

        Cryo::ASTNode *find(Cryo::ASTNode *root)
        {
            _result = nullptr;
            if (root)
                root->accept(*this);
            return _result;
        }

        void visit(Cryo::ProgramNode &node) override;
        void visit(Cryo::DeclarationStatementNode &node) override;
        void visit(Cryo::BlockStatementNode &node) override;
        void visit(Cryo::ExpressionStatementNode &node) override;
        void visit(Cryo::VariableDeclarationNode &node) override;
        void visit(Cryo::FunctionDeclarationNode &node) override;
        void visit(Cryo::StructMethodNode &node) override;
        void visit(Cryo::StructDeclarationNode &node) override;
        void visit(Cryo::ClassDeclarationNode &node) override;
        void visit(Cryo::ImplementationBlockNode &node) override;
        void visit(Cryo::IfStatementNode &node) override;
        void visit(Cryo::ForStatementNode &node) override;
        void visit(Cryo::WhileStatementNode &node) override;

    private:
        std::string _target;
        Cryo::ASTNode *_result = nullptr;
    };

    // ============================================================================
    // Utility functions
    // ============================================================================

    // Strip generic arguments from a type name: "Array<Token>" -> "Array"
    std::string stripGenericArgs(const std::string &name);

    // Parse generic type arguments: "Array<Token, i32>" -> {"Token", "i32"}
    std::vector<std::string> parseGenericArgs(const std::string &name);

    // Strip pointer/reference modifiers: "FloatType*" -> "FloatType", "&mut string" -> "string"
    std::string stripTypeModifiers(const std::string &name);

} // namespace CryoLSP
