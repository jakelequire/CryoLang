#include "AST/ASTBuilder.hpp"
#include "AST/ASTNode.hpp"
#include "Utils/Logger.hpp"

namespace Cryo
{
    std::unique_ptr<BinaryExpressionNode> ASTBuilder::create_binary_expression(Token op, std::unique_ptr<ExpressionNode> lhs, std::unique_ptr<ExpressionNode> rhs)
    {
        auto node = std::make_unique<BinaryExpressionNode>(
            NodeKind::BinaryExpression,
            op.location(),
            op,
            std::move(lhs),
            std::move(rhs));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<UnaryExpressionNode> ASTBuilder::create_unary_expression(Token op, std::unique_ptr<ExpressionNode> operand)
    {
        auto node = std::make_unique<UnaryExpressionNode>(
            NodeKind::UnaryExpression,
            op.location(),
            op,
            std::move(operand));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<TernaryExpressionNode> ASTBuilder::create_ternary_expression(SourceLocation loc, std::unique_ptr<ExpressionNode> condition, std::unique_ptr<ExpressionNode> true_expr, std::unique_ptr<ExpressionNode> false_expr)
    {
        auto node = std::make_unique<TernaryExpressionNode>(
            NodeKind::TernaryExpression,
            loc,
            std::move(condition),
            std::move(true_expr),
            std::move(false_expr));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<StatementNode> ASTBuilder::create_statement_node(NodeKind kind)
    {
        SourceLocation default_loc; // You'll need to handle this properly
        auto node = std::make_unique<StatementNode>(kind, default_loc);
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<DeclarationNode> ASTBuilder::create_declaration_node(Token identifier, std::unique_ptr<ExpressionNode> init)
    {
        auto node = std::make_unique<DeclarationNode>(NodeKind::Declaration, identifier.location());
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<LiteralNode> ASTBuilder::create_literal_node(Token literal)
    {
        validate_literal_token(literal);
        auto node = std::make_unique<LiteralNode>(
            NodeKind::Literal,
            literal.location(),
            std::string(literal.text()), // Convert string_view to string
            literal.kind());
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<IdentifierNode> ASTBuilder::create_identifier_node(Token identifier)
    {
        validate_identifier_token(identifier);
        auto node = std::make_unique<IdentifierNode>(
            NodeKind::Identifier,
            identifier.location(),
            std::string(identifier.text()) // Convert string_view to string
        );
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<ProgramNode> ASTBuilder::create_program_node(SourceLocation loc)
    {
        auto node = std::make_unique<ProgramNode>(loc);
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<BlockStatementNode> ASTBuilder::create_block_statement(SourceLocation loc)
    {
        auto node = std::make_unique<BlockStatementNode>(loc);
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<ReturnStatementNode> ASTBuilder::create_return_statement(SourceLocation loc, std::unique_ptr<ExpressionNode> expr)
    {
        auto node = std::make_unique<ReturnStatementNode>(loc, std::move(expr));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<VariableDeclarationNode> ASTBuilder::create_variable_declaration(SourceLocation loc,
                                                                                     std::string name,
                                                                                     TypeRef resolved_type,
                                                                                     std::unique_ptr<ExpressionNode> init,
                                                                                     bool is_mutable,
                                                                                     bool is_global)
    {
        LOG_DEBUG(Cryo::LogComponent::AST, "ASTBUILDER_DEBUG: Creating VariableDeclarationNode with name='{}', resolved type, location={}:{}",
                  name, loc.line(), loc.column());

        auto node = std::make_unique<VariableDeclarationNode>(loc, std::move(name), resolved_type, std::move(init), is_mutable, is_global);
        node->set_source_file(_source_file);

        LOG_DEBUG(Cryo::LogComponent::AST, "ASTBUILDER_DEBUG: Created VariableDeclarationNode node_ptr={}, stored_name='{}'",
                  static_cast<void *>(node.get()), node->name());

        return node;
    }

    std::unique_ptr<VariableDeclarationNode> ASTBuilder::create_variable_declaration(SourceLocation loc,
                                                                                     std::string name,
                                                                                     std::unique_ptr<TypeAnnotation> type_annotation,
                                                                                     std::unique_ptr<ExpressionNode> init,
                                                                                     bool is_mutable,
                                                                                     bool is_global)
    {
        LOG_DEBUG(Cryo::LogComponent::AST, "ASTBUILDER_DEBUG: Creating VariableDeclarationNode with name='{}', type annotation, location={}:{}",
                  name, loc.line(), loc.column());

        auto node = std::make_unique<VariableDeclarationNode>(loc, std::move(name), std::move(type_annotation), std::move(init), is_mutable, is_global);
        node->set_source_file(_source_file);

        LOG_DEBUG(Cryo::LogComponent::AST, "ASTBUILDER_DEBUG: Created VariableDeclarationNode node_ptr={}, stored_name='{}'",
                  static_cast<void *>(node.get()), node->name());

        return node;
    }

    std::unique_ptr<FunctionDeclarationNode> ASTBuilder::create_function_declaration(SourceLocation loc,
                                                                                     std::string name,
                                                                                     TypeRef return_type,
                                                                                     bool is_public)
    {
        auto node = std::make_unique<FunctionDeclarationNode>(loc, std::move(name), return_type, is_public);
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<FunctionDeclarationNode> ASTBuilder::create_function_declaration(SourceLocation loc,
                                                                                     std::string name,
                                                                                     std::unique_ptr<TypeAnnotation> return_type_annotation,
                                                                                     bool is_public)
    {
        auto node = std::make_unique<FunctionDeclarationNode>(loc, std::move(name), std::move(return_type_annotation), is_public);
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<CallExpressionNode> ASTBuilder::create_call_expression(SourceLocation loc, std::unique_ptr<ExpressionNode> callee)
    {
        auto node = std::make_unique<CallExpressionNode>(loc, std::move(callee));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<NewExpressionNode> ASTBuilder::create_new_expression(SourceLocation loc, std::string type_name)
    {
        auto node = std::make_unique<NewExpressionNode>(loc, std::move(type_name));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<SizeofExpressionNode> ASTBuilder::create_sizeof_expression(SourceLocation loc, std::string type_name)
    {
        auto node = std::make_unique<SizeofExpressionNode>(loc, std::move(type_name));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<AlignofExpressionNode> ASTBuilder::create_alignof_expression(SourceLocation loc, std::string type_name)
    {
        auto node = std::make_unique<AlignofExpressionNode>(loc, std::move(type_name));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<CastExpressionNode> ASTBuilder::create_cast_expression(SourceLocation loc, std::unique_ptr<ExpressionNode> expression, std::unique_ptr<TypeAnnotation> target_type_annotation)
    {
        auto node = std::make_unique<CastExpressionNode>(loc, std::move(expression), std::move(target_type_annotation));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<StructLiteralNode> ASTBuilder::create_struct_literal(SourceLocation loc, std::string struct_type)
    {
        auto node = std::make_unique<StructLiteralNode>(loc, std::move(struct_type));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<ArrayLiteralNode> ASTBuilder::create_array_literal(SourceLocation loc)
    {
        auto node = std::make_unique<ArrayLiteralNode>(loc);
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<TupleLiteralNode> ASTBuilder::create_tuple_literal(SourceLocation loc)
    {
        auto node = std::make_unique<TupleLiteralNode>(loc);
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<LambdaExpressionNode> ASTBuilder::create_lambda_expression(SourceLocation loc)
    {
        auto node = std::make_unique<LambdaExpressionNode>(loc);
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<ArrayAccessNode> ASTBuilder::create_array_access(SourceLocation loc, std::unique_ptr<ExpressionNode> array, std::unique_ptr<ExpressionNode> index)
    {
        auto node = std::make_unique<ArrayAccessNode>(loc, std::move(array), std::move(index));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<MemberAccessNode> ASTBuilder::create_member_access(SourceLocation loc, std::unique_ptr<ExpressionNode> object, std::string member)
    {
        auto node = std::make_unique<MemberAccessNode>(loc, std::move(object), std::move(member));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<ScopeResolutionNode> ASTBuilder::create_scope_resolution(SourceLocation loc, std::string scope_name, std::string member_name)
    {
        auto node = std::make_unique<ScopeResolutionNode>(loc, std::move(scope_name), std::move(member_name));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<IfStatementNode> ASTBuilder::create_if_statement(SourceLocation loc, std::unique_ptr<ExpressionNode> condition, std::unique_ptr<StatementNode> then_stmt, std::unique_ptr<StatementNode> else_stmt)
    {
        auto node = std::make_unique<IfStatementNode>(loc, std::move(condition), std::move(then_stmt), std::move(else_stmt));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<IfExpressionNode> ASTBuilder::create_if_expression(SourceLocation loc, std::unique_ptr<ExpressionNode> condition, std::unique_ptr<ExpressionNode> then_expr, std::unique_ptr<ExpressionNode> else_expr)
    {
        auto node = std::make_unique<IfExpressionNode>(loc, std::move(condition), std::move(then_expr), std::move(else_expr));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<MatchExpressionNode> ASTBuilder::create_match_expression(SourceLocation loc, std::unique_ptr<ExpressionNode> expr)
    {
        auto node = std::make_unique<MatchExpressionNode>(loc, std::move(expr));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<WhileStatementNode> ASTBuilder::create_while_statement(SourceLocation loc, std::unique_ptr<ExpressionNode> condition, std::unique_ptr<StatementNode> body)
    {
        auto node = std::make_unique<WhileStatementNode>(loc, std::move(condition), std::move(body));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<ForStatementNode> ASTBuilder::create_for_statement(SourceLocation loc, std::unique_ptr<VariableDeclarationNode> init, std::unique_ptr<ExpressionNode> condition, std::unique_ptr<ExpressionNode> update, std::unique_ptr<StatementNode> body)
    {
        auto node = std::make_unique<ForStatementNode>(loc, std::move(init), std::move(condition), std::move(update), std::move(body));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<BreakStatementNode> ASTBuilder::create_break_statement(SourceLocation loc)
    {
        auto node = std::make_unique<BreakStatementNode>(loc);
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<ContinueStatementNode> ASTBuilder::create_continue_statement(SourceLocation loc)
    {
        auto node = std::make_unique<ContinueStatementNode>(loc);
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<UnsafeBlockStatementNode> ASTBuilder::create_unsafe_block_statement(SourceLocation loc, std::unique_ptr<BlockStatementNode> block)
    {
        auto node = std::make_unique<UnsafeBlockStatementNode>(loc, std::move(block));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<ExpressionStatementNode> ASTBuilder::create_expression_statement(SourceLocation loc, std::unique_ptr<ExpressionNode> expr)
    {
        auto node = std::make_unique<ExpressionStatementNode>(loc, std::move(expr));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<DeclarationStatementNode> ASTBuilder::create_declaration_statement(SourceLocation loc, std::unique_ptr<DeclarationNode> decl)
    {
        auto node = std::make_unique<DeclarationStatementNode>(loc, std::move(decl));
        node->set_source_file(_source_file);
        return node;
    }

    // Struct and Class creation methods
    std::unique_ptr<GenericParameterNode> ASTBuilder::create_generic_parameter(SourceLocation loc, std::string name)
    {
        auto node = std::make_unique<GenericParameterNode>(loc, std::move(name));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<StructFieldNode> ASTBuilder::create_struct_field(SourceLocation loc, std::string name, TypeRef resolved_type, Visibility visibility)
    {
        auto node = std::make_unique<StructFieldNode>(loc, std::move(name), resolved_type, visibility);
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<StructFieldNode> ASTBuilder::create_struct_field(SourceLocation loc, std::string name, std::unique_ptr<TypeAnnotation> type_annotation, Visibility visibility)
    {
        auto node = std::make_unique<StructFieldNode>(loc, std::move(name), std::move(type_annotation), visibility);
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<StructMethodNode> ASTBuilder::create_struct_method(SourceLocation loc, std::string name, TypeRef return_type, Visibility visibility, bool is_constructor, bool is_destructor, bool is_static, bool is_default_destructor)
    {
        auto node = std::make_unique<StructMethodNode>(loc, std::move(name), return_type, visibility, is_constructor, is_destructor, is_static, is_default_destructor);
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<StructMethodNode> ASTBuilder::create_struct_method(SourceLocation loc, std::string name, std::unique_ptr<TypeAnnotation> return_type_annotation, Visibility visibility, bool is_constructor, bool is_destructor, bool is_static, bool is_default_destructor)
    {
        auto node = std::make_unique<StructMethodNode>(loc, std::move(name), std::move(return_type_annotation), visibility, is_constructor, is_destructor, is_static, is_default_destructor);
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<StructDeclarationNode> ASTBuilder::create_struct_declaration(SourceLocation loc, std::string name)
    {
        auto node = std::make_unique<StructDeclarationNode>(loc, std::move(name));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<ClassDeclarationNode> ASTBuilder::create_class_declaration(SourceLocation loc, std::string name)
    {
        auto node = std::make_unique<ClassDeclarationNode>(loc, std::move(name));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<TraitDeclarationNode> ASTBuilder::create_trait_declaration(SourceLocation loc, std::string name)
    {
        auto node = std::make_unique<TraitDeclarationNode>(loc, std::move(name));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<TypeAliasDeclarationNode> ASTBuilder::create_type_alias_declaration(SourceLocation loc, std::string alias_name, std::string target_type_str, std::vector<std::string> generic_params)
    {
        // Handle forward declarations (empty target type string)
        if (target_type_str.empty())
        {
            // Forward declaration - no target type to resolve
            auto node = std::make_unique<TypeAliasDeclarationNode>(loc, std::move(alias_name), TypeRef{}, std::move(generic_params));
            node->set_source_file(_source_file);
            return node;
        }

        // Try to resolve the target type string to a Type* object using the TypeArena
        TypeRef resolved_target_type = lookup_type_by_name(target_type_str);

        // If resolution succeeded, use the resolved type
        if (resolved_target_type.is_valid() && !resolved_target_type.is_error())
        {
            auto node = std::make_unique<TypeAliasDeclarationNode>(loc, std::move(alias_name), resolved_target_type, std::move(generic_params));
            node->set_source_file(_source_file);
            return node;
        }

        // Resolution failed - store as TypeAnnotation for deferred resolution
        // This handles complex types like Result<void*, AllocError> that can't be resolved at parse time
        auto annotation = std::make_unique<TypeAnnotation>(TypeAnnotation::named(target_type_str, loc));
        auto node = std::make_unique<TypeAliasDeclarationNode>(loc, std::move(alias_name), std::move(annotation), std::move(generic_params));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<EnumDeclarationNode> ASTBuilder::create_enum_declaration(SourceLocation loc, std::string name)
    {
        auto node = std::make_unique<EnumDeclarationNode>(loc, std::move(name));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<EnumVariantNode> ASTBuilder::create_enum_variant(SourceLocation loc, std::string name)
    {
        auto node = std::make_unique<EnumVariantNode>(loc, std::move(name));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<EnumVariantNode> ASTBuilder::create_enum_variant(SourceLocation loc, std::string name, std::vector<std::string> associated_types)
    {
        auto node = std::make_unique<EnumVariantNode>(loc, std::move(name), std::move(associated_types));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<EnumVariantNode> ASTBuilder::create_enum_variant_with_value(SourceLocation loc, std::string name, int64_t explicit_value)
    {
        auto node = std::make_unique<EnumVariantNode>(loc, std::move(name), explicit_value);
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<ImplementationBlockNode> ASTBuilder::create_implementation_block(SourceLocation loc, std::string target_type)
    {
        auto node = std::make_unique<ImplementationBlockNode>(loc, std::move(target_type));
        node->set_source_file(_source_file);
        return node;
    }

    std::unique_ptr<ExternBlockNode> ASTBuilder::create_extern_block(SourceLocation loc, std::string linkage_type)
    {
        auto node = std::make_unique<ExternBlockNode>(loc, std::move(linkage_type));
        node->set_source_file(_source_file);
        return node;
    }

    // Helper methods
    bool ASTBuilder::is_literal_token(TokenKind kind) const
    {
        return kind == TokenKind::TK_STRING_LITERAL ||
               kind == TokenKind::TK_NUMERIC_CONSTANT ||
               kind == TokenKind::TK_CHAR_CONSTANT ||
               kind == TokenKind::TK_BOOLEAN_LITERAL ||
               kind == TokenKind::TK_KW_TRUE ||
               kind == TokenKind::TK_KW_FALSE ||
               kind == TokenKind::TK_KW_NULL ||
               kind == TokenKind::TK_KW_VOID;
    }

    void ASTBuilder::validate_identifier_token(const Token &token) const
    {
        if (token.kind() != TokenKind::TK_IDENTIFIER)
        {
            throw std::runtime_error("Expected identifier token");
        }
    }

    void ASTBuilder::validate_literal_token(const Token &token) const
    {
        if (!is_literal_token(token.kind()))
        {
            throw std::runtime_error("Expected literal token");
        }
    }

    TypeRef ASTBuilder::lookup_type_by_name(const std::string &type_name)
    {
        auto &type_context = _context.types();

        // Handle basic types using TypeContext specific methods
        if (type_name == "void")
            return type_context.get_void();
        if (type_name == "boolean")
            return type_context.get_bool();
        if (type_name == "char")
            return type_context.get_char();
        if (type_name == "string")
            return type_context.get_string();

        // Integer types
        if (type_name == "i8")
            return type_context.get_i8();
        if (type_name == "i16")
            return type_context.get_i16();
        if (type_name == "i32")
            return type_context.get_i32();
        if (type_name == "i64")
            return type_context.get_i64();
        if (type_name == "int")
            return type_context.get_int();

        // Unsigned integer types
        if (type_name == "u8")
            return type_context.get_u8();
        if (type_name == "u16")
            return type_context.get_u16();
        if (type_name == "u32")
            return type_context.get_u32();
        if (type_name == "u64")
            return type_context.get_u64();

        // Float types
        if (type_name == "f32")
            return type_context.get_f32();
        if (type_name == "f64")
            return type_context.get_f64();
        if (type_name == "float")
            return type_context.get_f64(); // Default float is f64
        if (type_name == "double")
            return type_context.get_f64();

        // Check for pointer types (ends with '*')
        if (!type_name.empty() && type_name.back() == '*')
        {
            std::string pointee_type = type_name.substr(0, type_name.length() - 1);
            TypeRef pointee = lookup_type_by_name(pointee_type);
            if (pointee.is_valid())
            {
                return type_context.get_pointer_to(pointee);
            }
        }

        // Check for reference types (ends with '&')
        if (!type_name.empty() && type_name.back() == '&')
        {
            std::string referent_type = type_name.substr(0, type_name.length() - 1);
            TypeRef referent = lookup_type_by_name(referent_type);
            if (referent.is_valid())
            {
                return type_context.get_reference_to(referent);
            }
        }

        // For user-defined types (struct, class, enum, alias), lookup via symbol table
        SymbolTable &symbols = _context.symbols();
        auto resolved = symbols.resolve_type(type_name);
        if (resolved.has_value())
        {
            return resolved.value();
        }

        // Type not found - return invalid TypeRef (type checker will handle)
        return TypeRef{};
    }
}
