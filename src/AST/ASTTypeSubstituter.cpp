/******************************************************************************
 * @file ASTTypeSubstituter.cpp
 * @brief Implementation of AST type substitution for monomorphization
 ******************************************************************************/

#include "AST/ASTTypeSubstituter.hpp"
#include "Types/CompoundTypes.hpp"
#include "Utils/Logger.hpp"

namespace Cryo
{

//=============================================================================
// Core substitution methods
//=============================================================================

void ASTTypeSubstituter::substitute(ASTNode *node)
{
    if (node)
    {
        node->accept(*this);
    }
}

TypeRef ASTTypeSubstituter::substitute_type(TypeRef type)
{
    return _substitution.apply(type, _arena);
}

void ASTTypeSubstituter::substitute_expression_type(ExpressionNode &node)
{
    if (node.has_resolved_type())
    {
        node.set_resolved_type(substitute_type(node.get_resolved_type()));
    }
}

TypeRef ASTTypeSubstituter::lookup_type_param(const std::string &name)
{
    // First, check the name-to-type map (populated when using template-aware constructor)
    auto name_it = _param_name_map.find(name);
    if (name_it != _param_name_map.end())
    {
        LOG_DEBUG(LogComponent::GENERAL,
                  "ASTTypeSubstituter::lookup_type_param: Found '{}' in param_name_map -> {}",
                  name, name_it->second.is_valid() ? name_it->second->display_name() : "<invalid>");
        return name_it->second;
    }

    LOG_DEBUG(LogComponent::GENERAL,
              "ASTTypeSubstituter::lookup_type_param: '{}' NOT in param_name_map (size={})",
              name, _param_name_map.size());

    // Next, try to look up the type by name from the arena
    TypeRef param_type = _arena.lookup_type_by_name(name);
    if (param_type.is_valid() && param_type->is_generic_param())
    {
        // Check if we have a substitution for this type parameter
        auto sub = _substitution.get(param_type);
        if (sub)
        {
            return *sub;
        }
    }

    return TypeRef{};
}

TypeRef ASTTypeSubstituter::resolve_type_string_with_modifiers(const std::string &type_str)
{
    if (type_str.empty())
        return TypeRef{};

    std::string remaining = type_str;

    // Track modifiers to apply in reverse order (innermost first)
    std::vector<char> modifiers;

    // Parse modifiers from right to left
    while (!remaining.empty())
    {
        char last = remaining.back();
        if (last == '*')
        {
            modifiers.push_back('*');
            remaining.pop_back();
        }
        else if (last == '&')
        {
            modifiers.push_back('&');
            remaining.pop_back();
        }
        else if (last == ']')
        {
            // Handle array suffix []
            size_t bracket_pos = remaining.rfind('[');
            if (bracket_pos != std::string::npos)
            {
                modifiers.push_back('[');
                remaining = remaining.substr(0, bracket_pos);
            }
            else
            {
                break; // Malformed, stop parsing
            }
        }
        else if (last == '?')
        {
            modifiers.push_back('?');
            remaining.pop_back();
        }
        else
        {
            break; // No more modifiers
        }
    }

    // Trim any whitespace from the base type name
    while (!remaining.empty() && std::isspace(remaining.back()))
        remaining.pop_back();
    while (!remaining.empty() && std::isspace(remaining.front()))
        remaining.erase(0, 1);

    if (remaining.empty())
        return TypeRef{};

    LOG_DEBUG(LogComponent::GENERAL,
              "ASTTypeSubstituter::resolve_type_string_with_modifiers: type_str='{}', base='{}', modifiers count={}",
              type_str, remaining, modifiers.size());

    // Resolve the base type
    TypeRef base_type = lookup_type_param(remaining);
    if (!base_type.is_valid())
    {
        // Not a type parameter, try arena lookup
        base_type = _arena.lookup_type_by_name(remaining);
    }

    // Handle generic type expressions like "Bucket<K,V>" where K,V are type parameters
    if (!base_type.is_valid())
    {
        size_t angle_pos = remaining.find('<');
        if (angle_pos != std::string::npos && remaining.back() == '>')
        {
            std::string generic_name = remaining.substr(0, angle_pos);
            std::string args_str = remaining.substr(angle_pos + 1, remaining.size() - angle_pos - 2);

            // Look up the generic base type
            TypeRef generic_base = _arena.lookup_type_by_name(generic_name);
            if (generic_base.is_valid())
            {
                // Parse and resolve each type argument, handling nested generics
                std::vector<TypeRef> type_args;
                bool args_valid = true;
                size_t start = 0;
                int depth = 0;
                for (size_t i = 0; i <= args_str.size(); ++i)
                {
                    if (i == args_str.size() || (args_str[i] == ',' && depth == 0))
                    {
                        std::string arg = args_str.substr(start, i - start);
                        // Trim whitespace
                        while (!arg.empty() && std::isspace(arg.front()))
                            arg.erase(0, 1);
                        while (!arg.empty() && std::isspace(arg.back()))
                            arg.pop_back();

                        // Recursively resolve the argument (handles nested generics and modifiers)
                        TypeRef resolved_arg = resolve_type_string_with_modifiers(arg);
                        if (!resolved_arg.is_valid())
                        {
                            resolved_arg = lookup_type_param(arg);
                        }
                        if (!resolved_arg.is_valid())
                        {
                            resolved_arg = _arena.lookup_type_by_name(arg);
                        }
                        if (!resolved_arg.is_valid())
                        {
                            args_valid = false;
                            break;
                        }
                        type_args.push_back(resolved_arg);
                        start = i + 1;
                    }
                    else if (args_str[i] == '<')
                    {
                        depth++;
                    }
                    else if (args_str[i] == '>')
                    {
                        depth--;
                    }
                }

                if (args_valid && !type_args.empty())
                {
                    base_type = _arena.create_instantiation(generic_base, std::move(type_args));
                    if (base_type.is_valid())
                    {
                        _arena.register_instantiated_by_name(base_type);
                        LOG_DEBUG(LogComponent::GENERAL,
                                  "ASTTypeSubstituter::resolve_type_string_with_modifiers: Resolved generic '{}' -> {}",
                                  remaining, base_type->display_name());
                    }
                }
            }
        }
    }

    if (!base_type.is_valid())
    {
        LOG_DEBUG(LogComponent::GENERAL,
                  "ASTTypeSubstituter::resolve_type_string_with_modifiers: Failed to resolve base type '{}'",
                  remaining);
        return TypeRef{};
    }

    // Apply modifiers in reverse order (so they apply innermost-first)
    for (auto it = modifiers.rbegin(); it != modifiers.rend(); ++it)
    {
        char mod = *it;
        switch (mod)
        {
        case '*':
            base_type = _arena.get_pointer_to(base_type);
            LOG_DEBUG(LogComponent::GENERAL,
                      "ASTTypeSubstituter::resolve_type_string_with_modifiers: Applied pointer -> {}",
                      base_type.is_valid() ? base_type->display_name() : "<failed>");
            break;
        case '&':
            base_type = _arena.get_reference_to(base_type, RefMutability::Immutable);
            LOG_DEBUG(LogComponent::GENERAL,
                      "ASTTypeSubstituter::resolve_type_string_with_modifiers: Applied reference -> {}",
                      base_type.is_valid() ? base_type->display_name() : "<failed>");
            break;
        case '[':
            base_type = _arena.get_array_of(base_type, std::nullopt);
            LOG_DEBUG(LogComponent::GENERAL,
                      "ASTTypeSubstituter::resolve_type_string_with_modifiers: Applied array -> {}",
                      base_type.is_valid() ? base_type->display_name() : "<failed>");
            break;
        case '?':
            base_type = _arena.get_optional_of(base_type);
            LOG_DEBUG(LogComponent::GENERAL,
                      "ASTTypeSubstituter::resolve_type_string_with_modifiers: Applied optional -> {}",
                      base_type.is_valid() ? base_type->display_name() : "<failed>");
            break;
        }

        if (!base_type.is_valid())
        {
            return TypeRef{};
        }
    }

    return base_type;
}

TypeRef ASTTypeSubstituter::resolve_from_annotation(const TypeAnnotation *annotation)
{
    if (!annotation)
        return TypeRef{};

    switch (annotation->kind)
    {
    case TypeAnnotationKind::Primitive:
    case TypeAnnotationKind::Named:
    {
        // Check if this is a type parameter
        TypeRef param_replacement = lookup_type_param(annotation->name);
        if (param_replacement.is_valid())
        {
            LOG_DEBUG(LogComponent::GENERAL,
                      "ASTTypeSubstituter::resolve_from_annotation: Named/Primitive '{}' -> type param {}",
                      annotation->name, param_replacement->display_name());
            return param_replacement;
        }

        // Otherwise, look up the type by name
        TypeRef result = _arena.lookup_type_by_name(annotation->name);
        if (result.is_valid())
        {
            LOG_DEBUG(LogComponent::GENERAL,
                      "ASTTypeSubstituter::resolve_from_annotation: Named/Primitive '{}' -> arena lookup {}",
                      annotation->name, result->display_name());
            return result;
        }

        // If direct lookup failed, try parsing as a type with modifiers
        // This handles cases like "T*" stored as Named annotation instead of Pointer annotation
        result = resolve_type_string_with_modifiers(annotation->name);
        if (result.is_valid())
        {
            LOG_DEBUG(LogComponent::GENERAL,
                      "ASTTypeSubstituter::resolve_from_annotation: Named/Primitive '{}' -> modifier parsing {}",
                      annotation->name, result->display_name());
            return result;
        }

        LOG_DEBUG(LogComponent::GENERAL,
                  "ASTTypeSubstituter::resolve_from_annotation: Named/Primitive '{}' -> <not found>",
                  annotation->name);
        return result;
    }

    case TypeAnnotationKind::Pointer:
    {
        LOG_DEBUG(LogComponent::GENERAL,
                  "ASTTypeSubstituter::resolve_from_annotation: Processing Pointer type");
        TypeRef inner = resolve_from_annotation(annotation->inner.get());
        if (inner.is_valid())
        {
            TypeRef result = _arena.get_pointer_to(inner);
            LOG_DEBUG(LogComponent::GENERAL,
                      "ASTTypeSubstituter::resolve_from_annotation: Pointer to {} -> {}",
                      inner->display_name(), result.is_valid() ? result->display_name() : "<failed>");
            return result;
        }
        LOG_DEBUG(LogComponent::GENERAL,
                  "ASTTypeSubstituter::resolve_from_annotation: Pointer inner type invalid");
        return TypeRef{};
    }

    case TypeAnnotationKind::Reference:
    {
        TypeRef inner = resolve_from_annotation(annotation->inner.get());
        if (inner.is_valid())
        {
            return _arena.get_reference_to(inner, annotation->is_mutable ? RefMutability::Mutable : RefMutability::Immutable);
        }
        return TypeRef{};
    }

    case TypeAnnotationKind::Array:
    {
        TypeRef element = resolve_from_annotation(annotation->inner.get());
        if (element.is_valid())
        {
            return _arena.get_array_of(element, annotation->array_size);
        }
        return TypeRef{};
    }

    case TypeAnnotationKind::Optional:
    {
        TypeRef inner = resolve_from_annotation(annotation->inner.get());
        if (inner.is_valid())
        {
            return _arena.get_optional_of(inner);
        }
        return TypeRef{};
    }

    case TypeAnnotationKind::Generic:
    {
        // Resolve base type and type arguments
        TypeRef base = resolve_from_annotation(annotation->inner.get());
        if (!base.is_valid())
            return TypeRef{};

        std::vector<TypeRef> type_args;
        for (const auto &arg : annotation->elements)
        {
            TypeRef resolved_arg = resolve_from_annotation(&arg);
            if (!resolved_arg.is_valid())
                return TypeRef{};
            type_args.push_back(resolved_arg);
        }

        // Create instantiated type
        return _arena.create_instantiation(base, std::move(type_args));
    }

    case TypeAnnotationKind::Tuple:
    {
        std::vector<TypeRef> elements;
        for (const auto &elem : annotation->elements)
        {
            TypeRef resolved = resolve_from_annotation(&elem);
            if (!resolved.is_valid())
                return TypeRef{};
            elements.push_back(resolved);
        }
        return _arena.get_tuple(std::move(elements));
    }

    case TypeAnnotationKind::Function:
    {
        std::vector<TypeRef> params;
        for (const auto &param : annotation->elements)
        {
            TypeRef resolved = resolve_from_annotation(&param);
            if (!resolved.is_valid())
                return TypeRef{};
            params.push_back(resolved);
        }
        TypeRef return_type = resolve_from_annotation(annotation->return_type.get());
        if (!return_type.is_valid())
            return_type = _arena.get_void();

        return _arena.get_function(return_type, std::move(params), annotation->is_variadic);
    }

    case TypeAnnotationKind::Qualified:
    {
        // For qualified names, join with ::
        std::string full_name;
        for (size_t i = 0; i < annotation->qualified_path.size(); ++i)
        {
            if (i > 0)
                full_name += "::";
            full_name += annotation->qualified_path[i];
        }
        return _arena.lookup_type_by_name(full_name);
    }

    default:
        return TypeRef{};
    }
}

//=============================================================================
// Expression visitors
//=============================================================================

void ASTTypeSubstituter::visit(IdentifierNode &node)
{
    substitute_expression_type(node);
}

void ASTTypeSubstituter::visit(LiteralNode &node)
{
    substitute_expression_type(node);
}

void ASTTypeSubstituter::visit(ExpressionNode &node)
{
    substitute_expression_type(node);
}

void ASTTypeSubstituter::visit(BinaryExpressionNode &node)
{
    // Visit children first (depth-first)
    if (node.left())
        node.left()->accept(*this);
    if (node.right())
        node.right()->accept(*this);

    substitute_expression_type(node);
}

void ASTTypeSubstituter::visit(UnaryExpressionNode &node)
{
    if (node.operand())
        node.operand()->accept(*this);

    substitute_expression_type(node);
}

void ASTTypeSubstituter::visit(TernaryExpressionNode &node)
{
    if (node.condition())
        node.condition()->accept(*this);
    if (node.true_expression())
        node.true_expression()->accept(*this);
    if (node.false_expression())
        node.false_expression()->accept(*this);

    substitute_expression_type(node);
}

void ASTTypeSubstituter::visit(IfExpressionNode &node)
{
    if (node.condition())
        node.condition()->accept(*this);
    if (node.then_expression())
        node.then_expression()->accept(*this);
    if (node.else_expression())
        node.else_expression()->accept(*this);

    substitute_expression_type(node);
}

void ASTTypeSubstituter::visit(MatchExpressionNode &node)
{
    if (node.expression())
        node.expression()->accept(*this);

    for (const auto &arm : node.arms())
    {
        arm->accept(*this);
    }

    substitute_expression_type(node);
}

void ASTTypeSubstituter::visit(CallExpressionNode &node)
{
    if (node.callee())
        node.callee()->accept(*this);

    for (const auto &arg : node.arguments())
    {
        arg->accept(*this);
    }

    substitute_expression_type(node);
}

void ASTTypeSubstituter::visit(NewExpressionNode &node)
{
    for (const auto &arg : node.arguments())
    {
        arg->accept(*this);
    }

    if (node.array_size())
        node.array_size()->accept(*this);

    substitute_expression_type(node);
}

void ASTTypeSubstituter::visit(SizeofExpressionNode &node)
{
    substitute_expression_type(node);
}

void ASTTypeSubstituter::visit(AlignofExpressionNode &node)
{
    substitute_expression_type(node);
}

void ASTTypeSubstituter::visit(CastExpressionNode &node)
{
    if (node.expression())
        node.expression()->accept(*this);

    // Substitute target type
    if (node.has_resolved_target_type())
    {
        node.set_resolved_target_type(substitute_type(node.get_resolved_target_type()));
    }

    substitute_expression_type(node);
}

void ASTTypeSubstituter::visit(StructLiteralNode &node)
{
    // Substitute generic args (e.g., HashMapIter<K, V> -> HashMapIter<string, i32>)
    if (!node.generic_args().empty() && !_param_name_map.empty())
    {
        std::vector<std::string> substituted_args;
        bool changed = false;
        for (const auto &arg : node.generic_args())
        {
            auto it = _param_name_map.find(arg);
            if (it != _param_name_map.end() && it->second.is_valid())
            {
                substituted_args.push_back(it->second->display_name());
                changed = true;
            }
            else
            {
                substituted_args.push_back(arg);
            }
        }
        if (changed)
        {
            node.set_generic_args(std::move(substituted_args));
        }
    }

    for (const auto &init : node.field_initializers())
    {
        if (init->value())
            init->value()->accept(*this);
    }

    substitute_expression_type(node);
}

void ASTTypeSubstituter::visit(ArrayLiteralNode &node)
{
    for (const auto &elem : node.elements())
    {
        elem->accept(*this);
    }

    if (node.repeat_count_expr())
        node.repeat_count_expr()->accept(*this);

    substitute_expression_type(node);
}

void ASTTypeSubstituter::visit(TupleLiteralNode &node)
{
    for (const auto &elem : node.elements())
    {
        elem->accept(*this);
    }

    substitute_expression_type(node);
}

void ASTTypeSubstituter::visit(LambdaExpressionNode &node)
{
    if (node.body())
        node.body()->accept(*this);

    // Note: Lambda parameters and return types are stored as TypeRef directly
    // They would need setter methods to update them, which the current API doesn't expose
    // For now, we handle the body and expression type

    substitute_expression_type(node);
}

void ASTTypeSubstituter::visit(ArrayAccessNode &node)
{
    if (node.array())
        node.array()->accept(*this);
    if (node.index())
        node.index()->accept(*this);

    substitute_expression_type(node);
}

void ASTTypeSubstituter::visit(MemberAccessNode &node)
{
    if (node.object())
        node.object()->accept(*this);

    substitute_expression_type(node);
}

void ASTTypeSubstituter::visit(ScopeResolutionNode &node)
{
    substitute_expression_type(node);
}

//=============================================================================
// Statement visitors
//=============================================================================

void ASTTypeSubstituter::visit(StatementNode &node)
{
    // Base statement - no type to substitute
}

void ASTTypeSubstituter::visit(ProgramNode &node)
{
    for (const auto &stmt : node.statements())
    {
        stmt->accept(*this);
    }
}

void ASTTypeSubstituter::visit(BlockStatementNode &node)
{
    for (const auto &stmt : node.statements())
    {
        stmt->accept(*this);
    }
}

void ASTTypeSubstituter::visit(UnsafeBlockStatementNode &node)
{
    if (node.block())
        node.block()->accept(*this);
}

void ASTTypeSubstituter::visit(ReturnStatementNode &node)
{
    if (node.expression())
        node.expression()->accept(*this);
}

void ASTTypeSubstituter::visit(IfStatementNode &node)
{
    if (node.condition())
        node.condition()->accept(*this);
    if (node.then_statement())
        node.then_statement()->accept(*this);
    if (node.else_statement())
        node.else_statement()->accept(*this);
}

void ASTTypeSubstituter::visit(WhileStatementNode &node)
{
    if (node.condition())
        node.condition()->accept(*this);
    if (node.body())
        node.body()->accept(*this);
}

void ASTTypeSubstituter::visit(ForStatementNode &node)
{
    if (node.init())
        node.init()->accept(*this);
    if (node.condition())
        node.condition()->accept(*this);
    if (node.update())
        node.update()->accept(*this);
    if (node.body())
        node.body()->accept(*this);
}

void ASTTypeSubstituter::visit(MatchStatementNode &node)
{
    if (node.expr())
        node.expr()->accept(*this);

    for (const auto &arm : node.arms())
    {
        arm->accept(*this);
    }
}

void ASTTypeSubstituter::visit(SwitchStatementNode &node)
{
    if (node.expression())
        node.expression()->accept(*this);

    for (const auto &case_stmt : node.cases())
    {
        case_stmt->accept(*this);
    }
}

void ASTTypeSubstituter::visit(CaseStatementNode &node)
{
    if (node.value())
        node.value()->accept(*this);

    for (const auto &stmt : node.statements())
    {
        stmt->accept(*this);
    }
}

void ASTTypeSubstituter::visit(MatchArmNode &node)
{
    for (const auto &pattern : node.patterns())
    {
        pattern->accept(*this);
    }

    if (node.body())
        node.body()->accept(*this);
}

void ASTTypeSubstituter::visit(PatternNode &node)
{
    if (node.literal_value())
        node.literal_value()->accept(*this);
    if (node.range_start())
        node.range_start()->accept(*this);
    if (node.range_end())
        node.range_end()->accept(*this);
}

void ASTTypeSubstituter::visit(EnumPatternNode &node)
{
    // No child nodes with types
}

void ASTTypeSubstituter::visit(BreakStatementNode &node)
{
    // No children
}

void ASTTypeSubstituter::visit(ContinueStatementNode &node)
{
    // No children
}

void ASTTypeSubstituter::visit(ExpressionStatementNode &node)
{
    if (node.expression())
        node.expression()->accept(*this);
}

void ASTTypeSubstituter::visit(DeclarationStatementNode &node)
{
    if (node.declaration())
        node.declaration()->accept(*this);
}

//=============================================================================
// Declaration visitors
//=============================================================================

void ASTTypeSubstituter::visit(DeclarationNode &node)
{
    // Base declaration - no type to substitute
}

void ASTTypeSubstituter::visit(VariableDeclarationNode &node)
{
    LOG_DEBUG(LogComponent::GENERAL,
              "ASTTypeSubstituter::visit(VariableDeclarationNode): Processing variable '{}'",
              node.name());

    // Substitute initializer first
    if (node.initializer())
        node.initializer()->accept(*this);

    // Substitute the variable's type
    if (node.has_resolved_type())
    {
        TypeRef old_type = node.get_resolved_type();
        TypeRef new_type = substitute_type(old_type);
        LOG_DEBUG(LogComponent::GENERAL,
                  "ASTTypeSubstituter: Variable '{}' has resolved type {} -> {}",
                  node.name(),
                  old_type.is_valid() ? old_type->display_name() : "<invalid>",
                  new_type.is_valid() ? new_type->display_name() : "<invalid>");
        node.set_resolved_type(new_type);
    }
    else if (node.has_type_annotation())
    {
        // Resolved type not set - common in generic method bodies
        // Resolve from type annotation with substitution
        LOG_DEBUG(LogComponent::GENERAL,
                  "ASTTypeSubstituter: Variable '{}' has no resolved type, resolving from annotation '{}'",
                  node.name(), node.type_annotation()->to_string());
        TypeRef resolved = resolve_from_annotation(node.type_annotation());
        if (resolved.is_valid())
        {
            LOG_DEBUG(LogComponent::GENERAL,
                      "ASTTypeSubstituter: Variable '{}' resolved to {}",
                      node.name(), resolved->display_name());
            node.set_resolved_type(resolved);
        }
        else
        {
            LOG_WARN(LogComponent::GENERAL,
                     "ASTTypeSubstituter: Variable '{}' failed to resolve from annotation",
                     node.name());
        }
    }
    else
    {
        LOG_WARN(LogComponent::GENERAL,
                 "ASTTypeSubstituter: Variable '{}' has neither resolved type nor type annotation",
                 node.name());
    }
}

void ASTTypeSubstituter::visit(FunctionDeclarationNode &node)
{
    // Substitute parameters
    for (const auto &param : node.parameters())
    {
        param->accept(*this);
    }

    // Substitute body
    if (node.body())
        node.body()->accept(*this);

    // Substitute return type
    if (node.has_resolved_return_type())
    {
        node.set_resolved_return_type(substitute_type(node.get_resolved_return_type()));
    }
    else if (node.has_return_type_annotation())
    {
        // Resolved return type not set - resolve from annotation
        TypeRef resolved = resolve_from_annotation(node.return_type_annotation());
        if (resolved.is_valid())
        {
            node.set_resolved_return_type(resolved);
        }
    }
}

void ASTTypeSubstituter::visit(IntrinsicDeclarationNode &node)
{
    for (const auto &param : node.parameters())
    {
        param->accept(*this);
    }

    if (node.body())
        node.body()->accept(*this);

    if (node.has_resolved_return_type())
    {
        node.set_resolved_return_type(substitute_type(node.get_resolved_return_type()));
    }
}

void ASTTypeSubstituter::visit(IntrinsicConstDeclarationNode &node)
{
    if (node.has_resolved_type())
    {
        node.set_resolved_type(substitute_type(node.get_resolved_type()));
    }
}

void ASTTypeSubstituter::visit(ImportDeclarationNode &node)
{
    // No types to substitute
}

void ASTTypeSubstituter::visit(ModuleDeclarationNode &node)
{
    // No types to substitute
}

void ASTTypeSubstituter::visit(StructDeclarationNode &node)
{
    // Substitute fields
    for (const auto &field : node.fields())
    {
        field->accept(*this);
    }

    // Substitute methods
    for (const auto &method : node.methods())
    {
        method->accept(*this);
    }
}

void ASTTypeSubstituter::visit(ClassDeclarationNode &node)
{
    for (const auto &field : node.fields())
    {
        field->accept(*this);
    }

    for (const auto &method : node.methods())
    {
        method->accept(*this);
    }
}

void ASTTypeSubstituter::visit(TraitDeclarationNode &node)
{
    for (const auto &method : node.methods())
    {
        method->accept(*this);
    }
}

void ASTTypeSubstituter::visit(EnumDeclarationNode &node)
{
    for (const auto &variant : node.variants())
    {
        variant->accept(*this);
    }
}

void ASTTypeSubstituter::visit(EnumVariantNode &node)
{
    // Associated types are stored as strings, not TypeRefs
    // They would need separate handling if they contain type parameters
}

void ASTTypeSubstituter::visit(TypeAliasDeclarationNode &node)
{
    if (node.has_resolved_target_type())
    {
        node.set_resolved_target_type(substitute_type(node.get_resolved_target_type()));
    }
}

void ASTTypeSubstituter::visit(ImplementationBlockNode &node)
{
    for (const auto &field : node.field_implementations())
    {
        field->accept(*this);
    }

    for (const auto &method : node.method_implementations())
    {
        method->accept(*this);
    }
}

void ASTTypeSubstituter::visit(ExternBlockNode &node)
{
    for (const auto &func : node.function_declarations())
    {
        func->accept(*this);
    }
}

void ASTTypeSubstituter::visit(GenericParameterNode &node)
{
    // Generic parameters don't carry types to substitute
}

void ASTTypeSubstituter::visit(StructFieldNode &node)
{
    // Substitute default value if present
    if (node.default_value())
        node.default_value()->accept(*this);

    // Substitute field type
    if (node.has_resolved_type())
    {
        node.set_resolved_type(substitute_type(node.get_resolved_type()));
    }
    else if (node.has_type_annotation())
    {
        // Resolved type not set - resolve from type annotation
        TypeRef resolved = resolve_from_annotation(node.type_annotation());
        if (resolved.is_valid())
        {
            node.set_resolved_type(resolved);
        }
    }
}

void ASTTypeSubstituter::visit(StructMethodNode &node)
{
    // StructMethodNode extends FunctionDeclarationNode
    // Substitute parameters
    for (const auto &param : node.parameters())
    {
        param->accept(*this);
    }

    // Substitute body
    if (node.body())
        node.body()->accept(*this);

    // Substitute return type
    if (node.has_resolved_return_type())
    {
        node.set_resolved_return_type(substitute_type(node.get_resolved_return_type()));
    }
    else if (node.has_return_type_annotation())
    {
        // Resolved return type not set - resolve from annotation
        TypeRef resolved = resolve_from_annotation(node.return_type_annotation());
        if (resolved.is_valid())
        {
            node.set_resolved_return_type(resolved);
        }
    }
}

void ASTTypeSubstituter::visit(DirectiveNode &node)
{
    // Directives don't carry types
}

} // namespace Cryo
