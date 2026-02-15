/******************************************************************************
 * @file ASTCloner.cpp
 * @brief Implementation of AST deep cloning functionality
 ******************************************************************************/

#include "AST/ASTCloner.hpp"

namespace Cryo
{

//=============================================================================
// Helper functions
//=============================================================================

std::unique_ptr<TypeAnnotation> ASTCloner::clone_type_annotation(const TypeAnnotation *annotation)
{
    if (!annotation)
        return nullptr;

    auto cloned = std::make_unique<TypeAnnotation>(*annotation);
    return cloned;
}

std::unique_ptr<ExpressionNode> ASTCloner::clone_expression(ExpressionNode *node)
{
    if (!node)
        return nullptr;

    _result.reset();
    node->accept(*this);
    return std::unique_ptr<ExpressionNode>(static_cast<ExpressionNode *>(_result.release()));
}

std::unique_ptr<StatementNode> ASTCloner::clone_statement(StatementNode *node)
{
    if (!node)
        return nullptr;

    _result.reset();
    node->accept(*this);
    return std::unique_ptr<StatementNode>(static_cast<StatementNode *>(_result.release()));
}

//=============================================================================
// Expression visitors
//=============================================================================

void ASTCloner::visit(IdentifierNode &node)
{
    auto cloned = std::make_unique<IdentifierNode>(
        NodeKind::Identifier,
        node.location(),
        node.name());

    cloned->set_global_reference(node.is_global_reference());
    cloned->set_resolved_type(node.get_resolved_type());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(LiteralNode &node)
{
    auto cloned = std::make_unique<LiteralNode>(
        NodeKind::Literal,
        node.location(),
        node.value(),
        node.literal_kind());

    cloned->set_resolved_type(node.get_resolved_type());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(ExpressionNode &node)
{
    // Base expression node - should not be directly instantiated
    // This is a fallback that creates a generic expression
    auto cloned = std::make_unique<ExpressionNode>(node.kind(), node.location());
    cloned->set_resolved_type(node.get_resolved_type());
    set_result(std::move(cloned), node);
}

void ASTCloner::visit(BinaryExpressionNode &node)
{
    auto cloned = std::make_unique<BinaryExpressionNode>(
        NodeKind::BinaryExpression,
        node.location(),
        node.operator_token(),
        clone_expression(node.left()),
        clone_expression(node.right()));

    cloned->set_resolved_type(node.get_resolved_type());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(UnaryExpressionNode &node)
{
    auto cloned = std::make_unique<UnaryExpressionNode>(
        NodeKind::UnaryExpression,
        node.location(),
        node.operator_token(),
        clone_expression(node.operand()));

    cloned->set_resolved_type(node.get_resolved_type());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(TernaryExpressionNode &node)
{
    auto cloned = std::make_unique<TernaryExpressionNode>(
        NodeKind::TernaryExpression,
        node.location(),
        clone_expression(node.condition()),
        clone_expression(node.true_expression()),
        clone_expression(node.false_expression()));

    cloned->set_resolved_type(node.get_resolved_type());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(IfExpressionNode &node)
{
    auto cloned = std::make_unique<IfExpressionNode>(
        node.location(),
        clone_expression(node.condition()),
        clone_expression(node.then_expression()),
        clone_expression(node.else_expression()));

    cloned->set_resolved_type(node.get_resolved_type());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(MatchExpressionNode &node)
{
    auto cloned = std::make_unique<MatchExpressionNode>(
        node.location(),
        clone_expression(node.expression()));

    for (const auto &arm : node.arms())
    {
        cloned->add_arm(clone<MatchArmNode>(arm.get()));
    }

    cloned->set_resolved_type(node.get_resolved_type());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(CallExpressionNode &node)
{
    auto cloned = std::make_unique<CallExpressionNode>(
        node.location(),
        clone_expression(node.callee()));

    for (const auto &arg : node.arguments())
    {
        cloned->add_argument(clone_expression(arg.get()));
    }

    for (const auto &generic_arg : node.generic_args())
    {
        cloned->add_generic_arg(generic_arg);
    }

    cloned->set_resolved_type(node.get_resolved_type());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(NewExpressionNode &node)
{
    auto cloned = std::make_unique<NewExpressionNode>(
        node.location(),
        node.type_name());

    for (const auto &generic_arg : node.generic_args())
    {
        cloned->add_generic_arg(generic_arg);
    }

    for (const auto &arg : node.arguments())
    {
        cloned->add_argument(clone_expression(arg.get()));
    }

    if (node.is_array_allocation() && node.array_size())
    {
        cloned->set_array_size(clone_expression(node.array_size()));
    }

    cloned->set_resolved_type(node.get_resolved_type());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(SizeofExpressionNode &node)
{
    auto cloned = std::make_unique<SizeofExpressionNode>(
        node.location(),
        node.type_name());

    cloned->set_resolved_type(node.get_resolved_type());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(AlignofExpressionNode &node)
{
    auto cloned = std::make_unique<AlignofExpressionNode>(
        node.location(),
        node.type_name());

    cloned->set_resolved_type(node.get_resolved_type());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(CastExpressionNode &node)
{
    std::unique_ptr<CastExpressionNode> cloned;

    if (node.has_target_type_annotation())
    {
        cloned = std::make_unique<CastExpressionNode>(
            node.location(),
            clone_expression(node.expression()),
            clone_type_annotation(node.target_type_annotation()));
    }
    else
    {
        cloned = std::make_unique<CastExpressionNode>(
            node.location(),
            clone_expression(node.expression()),
            node.get_resolved_target_type());
    }

    cloned->set_resolved_type(node.get_resolved_type());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(StructLiteralNode &node)
{
    auto cloned = std::make_unique<StructLiteralNode>(
        node.location(),
        node.struct_type());

    for (const auto &generic_arg : node.generic_args())
    {
        cloned->add_generic_arg(generic_arg);
    }

    for (const auto &init : node.field_initializers())
    {
        auto field_init = std::make_unique<FieldInitializerNode>(
            init->field_name(),
            clone_expression(init->value()),
            init->location());
        cloned->add_field_initializer(std::move(field_init));
    }

    cloned->set_resolved_type(node.get_resolved_type());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(ArrayLiteralNode &node)
{
    auto cloned = std::make_unique<ArrayLiteralNode>(node.location());

    for (const auto &elem : node.elements())
    {
        cloned->add_element(clone_expression(elem.get()));
    }

    if (!node.element_type().empty())
    {
        cloned->set_element_type(node.element_type());
    }

    if (node.repeat_count() > 0)
    {
        cloned->set_repeat_count(node.repeat_count());
    }

    if (node.has_dynamic_count())
    {
        cloned->set_repeat_count_expr(clone_expression(node.repeat_count_expr()));
    }

    cloned->set_resolved_type(node.get_resolved_type());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(TupleLiteralNode &node)
{
    auto cloned = std::make_unique<TupleLiteralNode>(node.location());

    for (const auto &elem : node.elements())
    {
        cloned->add_element(clone_expression(elem.get()));
    }

    cloned->set_resolved_type(node.get_resolved_type());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(LambdaExpressionNode &node)
{
    auto cloned = std::make_unique<LambdaExpressionNode>(node.location());

    for (const auto &[name, type] : node.parameters())
    {
        cloned->add_parameter(name, type);
    }

    cloned->set_return_type(node.return_type());

    if (node.body())
    {
        cloned->set_body(clone_statement(node.body()));
    }

    cloned->set_resolved_type(node.get_resolved_type());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(ArrayAccessNode &node)
{
    auto cloned = std::make_unique<ArrayAccessNode>(
        node.location(),
        clone_expression(node.array()),
        clone_expression(node.index()));

    cloned->set_resolved_type(node.get_resolved_type());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(MemberAccessNode &node)
{
    auto cloned = std::make_unique<MemberAccessNode>(
        node.location(),
        clone_expression(node.object()),
        node.member());

    cloned->set_resolved_type(node.get_resolved_type());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(ScopeResolutionNode &node)
{
    auto cloned = std::make_unique<ScopeResolutionNode>(
        node.location(),
        node.scope_name(),
        node.member_name(),
        node.generic_args());

    cloned->set_resolved_type(node.get_resolved_type());

    set_result(std::move(cloned), node);
}

//=============================================================================
// Statement visitors
//=============================================================================

void ASTCloner::visit(StatementNode &node)
{
    // Base statement node - fallback
    auto cloned = std::make_unique<StatementNode>(node.kind(), node.location());
    set_result(std::move(cloned), node);
}

void ASTCloner::visit(ProgramNode &node)
{
    auto cloned = std::make_unique<ProgramNode>(node.location());

    for (const auto &stmt : node.statements())
    {
        _result.reset();
        stmt->accept(*this);
        cloned->add_statement(std::move(_result));
    }

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(BlockStatementNode &node)
{
    auto cloned = std::make_unique<BlockStatementNode>(node.location());

    for (const auto &stmt : node.statements())
    {
        cloned->add_statement(clone_statement(stmt.get()));
    }

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(UnsafeBlockStatementNode &node)
{
    auto cloned = std::make_unique<UnsafeBlockStatementNode>(
        node.location(),
        clone<BlockStatementNode>(node.block()));

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(ReturnStatementNode &node)
{
    auto cloned = std::make_unique<ReturnStatementNode>(
        node.location(),
        clone_expression(node.expression()));

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(IfStatementNode &node)
{
    auto cloned = std::make_unique<IfStatementNode>(
        node.location(),
        clone_expression(node.condition()),
        clone_statement(node.then_statement()),
        clone_statement(node.else_statement()));

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(WhileStatementNode &node)
{
    auto cloned = std::make_unique<WhileStatementNode>(
        node.location(),
        clone_expression(node.condition()),
        clone_statement(node.body()));

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(ForStatementNode &node)
{
    auto cloned = std::make_unique<ForStatementNode>(
        node.location(),
        clone<VariableDeclarationNode>(node.init()),
        clone_expression(node.condition()),
        clone_expression(node.update()),
        clone_statement(node.body()));

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(MatchStatementNode &node)
{
    auto cloned = std::make_unique<MatchStatementNode>(
        node.location(),
        clone_expression(node.expr()));

    for (const auto &arm : node.arms())
    {
        cloned->add_arm(clone<MatchArmNode>(arm.get()));
    }

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(SwitchStatementNode &node)
{
    std::vector<std::unique_ptr<CaseStatementNode>> cloned_cases;
    for (const auto &case_stmt : node.cases())
    {
        cloned_cases.push_back(clone<CaseStatementNode>(case_stmt.get()));
    }

    auto cloned = std::make_unique<SwitchStatementNode>(
        node.location(),
        clone_expression(node.expression()),
        std::move(cloned_cases));

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(CaseStatementNode &node)
{
    std::vector<std::unique_ptr<StatementNode>> cloned_stmts;
    for (const auto &stmt : node.statements())
    {
        cloned_stmts.push_back(clone_statement(stmt.get()));
    }

    std::unique_ptr<CaseStatementNode> cloned;
    if (node.is_default())
    {
        cloned = std::make_unique<CaseStatementNode>(
            node.location(),
            std::move(cloned_stmts));
    }
    else
    {
        cloned = std::make_unique<CaseStatementNode>(
            node.location(),
            clone_expression(node.value()),
            std::move(cloned_stmts));
    }

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(MatchArmNode &node)
{
    std::vector<std::unique_ptr<PatternNode>> cloned_patterns;
    for (const auto &pattern : node.patterns())
    {
        cloned_patterns.push_back(clone<PatternNode>(pattern.get()));
    }

    auto cloned = std::make_unique<MatchArmNode>(
        node.location(),
        std::move(cloned_patterns),
        clone_statement(node.body()));

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(PatternNode &node)
{
    auto cloned = std::make_unique<PatternNode>(node.location());

    switch (node.pattern_type())
    {
    case PatternNode::PatternType::Literal:
        if (node.literal_value())
        {
            cloned->set_literal_value(clone<LiteralNode>(node.literal_value()));
        }
        break;
    case PatternNode::PatternType::Identifier:
        cloned->set_identifier(node.identifier());
        break;
    case PatternNode::PatternType::Wildcard:
        cloned->set_wildcard(true);
        break;
    case PatternNode::PatternType::Range:
        cloned->set_range(
            clone<LiteralNode>(node.range_start()),
            clone<LiteralNode>(node.range_end()));
        break;
    case PatternNode::PatternType::Enum:
        // Enum patterns are handled by EnumPatternNode
        break;
    }

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(EnumPatternNode &node)
{
    auto cloned = std::make_unique<EnumPatternNode>(
        node.location(),
        node.enum_name(),
        node.variant_name());

    for (const auto &elem : node.pattern_elements())
    {
        cloned->add_pattern_element(elem);
    }

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(BreakStatementNode &node)
{
    auto cloned = std::make_unique<BreakStatementNode>(node.location());
    set_result(std::move(cloned), node);
}

void ASTCloner::visit(ContinueStatementNode &node)
{
    auto cloned = std::make_unique<ContinueStatementNode>(node.location());
    set_result(std::move(cloned), node);
}

void ASTCloner::visit(ExpressionStatementNode &node)
{
    auto cloned = std::make_unique<ExpressionStatementNode>(
        node.location(),
        clone_expression(node.expression()));

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(DeclarationStatementNode &node)
{
    auto cloned = std::make_unique<DeclarationStatementNode>(
        node.location(),
        clone<DeclarationNode>(node.declaration()));

    set_result(std::move(cloned), node);
}

//=============================================================================
// Declaration visitors
//=============================================================================

void ASTCloner::visit(DeclarationNode &node)
{
    // Base declaration node - fallback
    auto cloned = std::make_unique<DeclarationNode>(node.kind(), node.location());
    cloned->set_documentation(node.documentation());
    cloned->set_source_module(node.source_module());
    set_result(std::move(cloned), node);
}

void ASTCloner::visit(VariableDeclarationNode &node)
{
    std::unique_ptr<VariableDeclarationNode> cloned;

    if (node.has_type_annotation())
    {
        cloned = std::make_unique<VariableDeclarationNode>(
            node.location(),
            node.name(),
            clone_type_annotation(node.type_annotation()),
            clone_expression(node.initializer()),
            node.is_mutable(),
            node.is_global());
    }
    else
    {
        cloned = std::make_unique<VariableDeclarationNode>(
            node.location(),
            node.name(),
            node.get_resolved_type(),
            clone_expression(node.initializer()),
            node.is_mutable(),
            node.is_global());
    }

    cloned->set_documentation(node.documentation());
    cloned->set_source_module(node.source_module());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(FunctionDeclarationNode &node)
{
    std::unique_ptr<FunctionDeclarationNode> cloned;

    if (node.has_return_type_annotation())
    {
        cloned = std::make_unique<FunctionDeclarationNode>(
            node.location(),
            node.name(),
            clone_type_annotation(node.return_type_annotation()),
            node.is_public());
    }
    else
    {
        cloned = std::make_unique<FunctionDeclarationNode>(
            node.location(),
            node.name(),
            node.get_resolved_return_type(),
            node.is_public());
    }

    // Clone parameters
    for (const auto &param : node.parameters())
    {
        cloned->add_parameter(clone<VariableDeclarationNode>(param.get()));
    }

    // Clone generic parameters
    for (const auto &gen_param : node.generic_parameters())
    {
        cloned->add_generic_parameter(clone<GenericParameterNode>(gen_param.get()));
    }

    // Clone trait bounds
    for (const auto &bound : node.trait_bounds())
    {
        cloned->add_trait_bound(bound);
    }

    // Clone body
    if (node.body())
    {
        cloned->set_body(clone<BlockStatementNode>(node.body()));
    }

    cloned->set_visibility(node.is_public());
    cloned->set_static(node.is_static());
    cloned->set_inline(node.is_inline());
    cloned->set_variadic(node.is_variadic());

    cloned->set_documentation(node.documentation());
    cloned->set_source_module(node.source_module());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(IntrinsicDeclarationNode &node)
{
    auto cloned = std::make_unique<IntrinsicDeclarationNode>(
        node.location(),
        node.name(),
        node.get_resolved_return_type());

    for (const auto &param : node.parameters())
    {
        cloned->add_parameter(clone<VariableDeclarationNode>(param.get()));
    }

    if (node.body())
    {
        cloned->set_body(clone<BlockStatementNode>(node.body()));
    }

    cloned->set_documentation(node.documentation());
    cloned->set_source_module(node.source_module());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(IntrinsicConstDeclarationNode &node)
{
    auto cloned = std::make_unique<IntrinsicConstDeclarationNode>(
        node.location(),
        node.name(),
        node.get_resolved_type());

    cloned->set_documentation(node.documentation());
    cloned->set_source_module(node.source_module());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(ImportDeclarationNode &node)
{
    std::unique_ptr<ImportDeclarationNode> cloned;

    if (node.is_specific_import())
    {
        cloned = std::make_unique<ImportDeclarationNode>(
            node.location(),
            node.specific_imports(),
            node.module_path());
    }
    else if (node.has_alias())
    {
        cloned = std::make_unique<ImportDeclarationNode>(
            node.location(),
            node.module_path(),
            node.alias());
    }
    else
    {
        cloned = std::make_unique<ImportDeclarationNode>(
            node.location(),
            node.module_path());
    }

    cloned->set_documentation(node.documentation());
    cloned->set_source_module(node.source_module());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(ModuleDeclarationNode &node)
{
    auto cloned = std::make_unique<ModuleDeclarationNode>(
        node.location(),
        node.module_path(),
        node.is_public());

    cloned->set_documentation(node.documentation());
    cloned->set_source_module(node.source_module());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(StructDeclarationNode &node)
{
    auto cloned = std::make_unique<StructDeclarationNode>(
        node.location(),
        node.name());

    // Clone generic parameters
    for (const auto &gen_param : node.generic_parameters())
    {
        cloned->add_generic_parameter(clone<GenericParameterNode>(gen_param.get()));
    }

    // Clone fields
    for (const auto &field : node.fields())
    {
        cloned->add_field(clone<StructFieldNode>(field.get()));
    }

    // Clone methods
    for (const auto &method : node.methods())
    {
        cloned->add_method(clone<StructMethodNode>(method.get()));
    }

    cloned->set_documentation(node.documentation());
    cloned->set_source_module(node.source_module());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(ClassDeclarationNode &node)
{
    auto cloned = std::make_unique<ClassDeclarationNode>(
        node.location(),
        node.name());

    // Clone generic parameters
    for (const auto &gen_param : node.generic_parameters())
    {
        cloned->add_generic_parameter(clone<GenericParameterNode>(gen_param.get()));
    }

    // Clone fields
    for (const auto &field : node.fields())
    {
        cloned->add_field(clone<StructFieldNode>(field.get()));
    }

    // Clone methods
    for (const auto &method : node.methods())
    {
        cloned->add_method(clone<StructMethodNode>(method.get()));
    }

    if (!node.base_class().empty())
    {
        cloned->set_base_class(node.base_class());
    }

    cloned->set_documentation(node.documentation());
    cloned->set_source_module(node.source_module());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(TraitDeclarationNode &node)
{
    auto cloned = std::make_unique<TraitDeclarationNode>(
        node.location(),
        node.name());

    // Clone generic parameters
    for (const auto &gen_param : node.generic_parameters())
    {
        cloned->add_generic_parameter(clone<GenericParameterNode>(gen_param.get()));
    }

    // Clone methods
    for (const auto &method : node.methods())
    {
        cloned->add_method(clone<FunctionDeclarationNode>(method.get()));
    }

    // Clone base traits
    for (const auto &base_trait : node.base_traits())
    {
        cloned->add_base_trait(base_trait);
    }

    cloned->set_documentation(node.documentation());
    cloned->set_source_module(node.source_module());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(EnumDeclarationNode &node)
{
    auto cloned = std::make_unique<EnumDeclarationNode>(
        node.location(),
        node.name());

    // Clone generic parameters
    for (const auto &gen_param : node.generic_parameters())
    {
        cloned->add_generic_parameter(clone<GenericParameterNode>(gen_param.get()));
    }

    // Clone variants
    for (const auto &variant : node.variants())
    {
        cloned->add_variant(clone<EnumVariantNode>(variant.get()));
    }

    cloned->set_documentation(node.documentation());
    cloned->set_source_module(node.source_module());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(EnumVariantNode &node)
{
    std::unique_ptr<EnumVariantNode> cloned;

    if (node.has_explicit_value())
    {
        cloned = std::make_unique<EnumVariantNode>(
            node.location(),
            node.name(),
            node.explicit_value());
    }
    else if (!node.associated_types().empty())
    {
        cloned = std::make_unique<EnumVariantNode>(
            node.location(),
            node.name(),
            node.associated_types());
    }
    else
    {
        cloned = std::make_unique<EnumVariantNode>(
            node.location(),
            node.name());
    }

    cloned->set_documentation(node.documentation());
    cloned->set_source_module(node.source_module());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(TypeAliasDeclarationNode &node)
{
    std::unique_ptr<TypeAliasDeclarationNode> cloned;

    if (node.has_target_type_annotation())
    {
        cloned = std::make_unique<TypeAliasDeclarationNode>(
            node.location(),
            node.alias_name(),
            clone_type_annotation(node.target_type_annotation()),
            node.generic_params());
    }
    else
    {
        cloned = std::make_unique<TypeAliasDeclarationNode>(
            node.location(),
            node.alias_name(),
            node.get_resolved_target_type(),
            node.generic_params());
    }

    cloned->set_documentation(node.documentation());
    cloned->set_source_module(node.source_module());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(ImplementationBlockNode &node)
{
    auto cloned = std::make_unique<ImplementationBlockNode>(
        node.location(),
        node.target_type());

    for (const auto &field : node.field_implementations())
    {
        cloned->add_field_implementation(clone<StructFieldNode>(field.get()));
    }

    for (const auto &method : node.method_implementations())
    {
        cloned->add_method_implementation(clone<StructMethodNode>(method.get()));
    }

    cloned->set_documentation(node.documentation());
    cloned->set_source_module(node.source_module());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(ExternBlockNode &node)
{
    auto cloned = std::make_unique<ExternBlockNode>(
        node.location(),
        node.linkage_type());

    for (const auto &func : node.function_declarations())
    {
        cloned->add_function_declaration(clone<FunctionDeclarationNode>(func.get()));
    }

    cloned->set_documentation(node.documentation());
    cloned->set_source_module(node.source_module());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(GenericParameterNode &node)
{
    auto cloned = std::make_unique<GenericParameterNode>(
        node.location(),
        node.name());

    for (const auto &constraint : node.constraints())
    {
        cloned->add_constraint(constraint);
    }

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(StructFieldNode &node)
{
    std::unique_ptr<StructFieldNode> cloned;

    if (node.has_type_annotation())
    {
        cloned = std::make_unique<StructFieldNode>(
            node.location(),
            node.name(),
            clone_type_annotation(node.type_annotation()),
            node.visibility());
    }
    else
    {
        cloned = std::make_unique<StructFieldNode>(
            node.location(),
            node.name(),
            node.get_resolved_type(),
            node.visibility());
    }

    if (node.default_value())
    {
        cloned->set_default_value(clone_expression(node.default_value()));
    }

    cloned->set_documentation(node.documentation());
    cloned->set_source_module(node.source_module());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(StructMethodNode &node)
{
    std::unique_ptr<StructMethodNode> cloned;

    if (node.has_return_type_annotation())
    {
        cloned = std::make_unique<StructMethodNode>(
            node.location(),
            node.name(),
            clone_type_annotation(node.return_type_annotation()),
            node.visibility(),
            node.is_constructor(),
            node.is_destructor(),
            node.is_static(),
            node.is_default_destructor());
    }
    else
    {
        cloned = std::make_unique<StructMethodNode>(
            node.location(),
            node.name(),
            node.get_resolved_return_type(),
            node.visibility(),
            node.is_constructor(),
            node.is_destructor(),
            node.is_static(),
            node.is_default_destructor());
    }

    // Clone parameters
    for (const auto &param : node.parameters())
    {
        cloned->add_parameter(clone<VariableDeclarationNode>(param.get()));
    }

    // Clone generic parameters
    for (const auto &gen_param : node.generic_parameters())
    {
        cloned->add_generic_parameter(clone<GenericParameterNode>(gen_param.get()));
    }

    // Clone trait bounds
    for (const auto &bound : node.trait_bounds())
    {
        cloned->add_trait_bound(bound);
    }

    // Clone body
    if (node.body())
    {
        cloned->set_body(clone<BlockStatementNode>(node.body()));
    }

    cloned->set_visibility(node.is_public());
    cloned->set_inline(node.is_inline());
    cloned->set_variadic(node.is_variadic());

    cloned->set_documentation(node.documentation());
    cloned->set_source_module(node.source_module());

    set_result(std::move(cloned), node);
}

void ASTCloner::visit(DirectiveNode &node)
{
    auto cloned = std::make_unique<DirectiveNode>(
        node.location(),
        node.name());

    for (const auto &[key, value] : node.arguments())
    {
        cloned->add_argument(key, value);
    }

    set_result(std::move(cloned), node);
}

} // namespace Cryo
