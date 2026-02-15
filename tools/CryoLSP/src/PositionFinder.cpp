#include "LSP/PositionFinder.hpp"
#include "LSP/Transport.hpp"
#include "Types/TypeAnnotation.hpp"

namespace CryoLSP
{

    PositionFinder::PositionFinder(size_t target_line, size_t target_col)
        : _target_line(target_line), _target_col(target_col) {}

    FoundNode PositionFinder::find(Cryo::ASTNode *root)
    {
        _result = FoundNode{};
        if (root)
            root->accept(*this);
        return _result;
    }

    bool PositionFinder::matchesPosition(Cryo::ASTNode &node, size_t name_length)
    {
        return matchesPosition(node.location(), name_length);
    }

    bool PositionFinder::matchesPosition(const Cryo::SourceLocation &loc, size_t name_length)
    {
        if (loc.line() == _target_line)
        {
            size_t start = loc.column();
            size_t end = (name_length > 0) ? start + name_length : start + 1;
            if (_target_col >= start && _target_col < end)
                return true;
        }
        return false;
    }

    // ============================================================================
    // Program / Container visitors
    // ============================================================================

    void PositionFinder::visit(Cryo::ProgramNode &node)
    {
        for (const auto &child : node.statements())
        {
            if (child)
                child->accept(*this);
        }
    }

    void PositionFinder::visit(Cryo::BlockStatementNode &node)
    {
        for (const auto &stmt : node.statements())
        {
            if (stmt)
                stmt->accept(*this);
        }
    }

    void PositionFinder::visit(Cryo::ExpressionStatementNode &node)
    {
        if (node.expression())
            node.expression()->accept(*this);
    }

    void PositionFinder::visit(Cryo::DeclarationStatementNode &node)
    {
        if (node.declaration())
            node.declaration()->accept(*this);
    }

    // ============================================================================
    // Identifier / Literal
    // ============================================================================

    void PositionFinder::visit(Cryo::IdentifierNode &node)
    {
        if (matchesPosition(node, node.name().size()))
        {
            _result.node = &node;
            _result.identifier_name = node.name();
            _result.kind = FoundNode::Kind::Identifier;
        }
    }

    void PositionFinder::visit(Cryo::LiteralNode &node)
    {
        // For string literals, account for the surrounding quotes in position matching
        size_t match_len = node.value().size();
        auto lk = node.literal_kind();
        if (lk == Cryo::TokenKind::TK_STRING_LITERAL || lk == Cryo::TokenKind::TK_RAW_STRING_LITERAL)
            match_len += 2; // opening and closing quotes

        if (matchesPosition(node, match_len))
        {
            _result.node = &node;
            _result.identifier_name = node.value();
            _result.kind = FoundNode::Kind::Literal;
        }
    }

    // ============================================================================
    // Type annotation helper
    // ============================================================================

    bool PositionFinder::checkTypeAnnotation(const Cryo::TypeAnnotation *ann)
    {
        if (!ann || ann->location.line() == 0)
            return false;

        if (matchesPosition(ann->location, ann->name.size()))
        {
            _result.node = nullptr; // No AST node - identified by name
            _result.identifier_name = ann->name;
            _result.kind = FoundNode::Kind::TypeReference;
            return true;
        }
        return false;
    }

    // ============================================================================
    // Declarations
    // ============================================================================

    void PositionFinder::visit(Cryo::FunctionDeclarationNode &node)
    {
        const auto &loc = node.has_name_location() ? node.name_location() : node.location();
        if (matchesPosition(loc, node.name().size()))
        {
            _result.node = &node;
            _result.identifier_name = node.name();
            _result.kind = FoundNode::Kind::FunctionDecl;
        }

        // Check return type annotation
        if (node.has_return_type_annotation())
            checkTypeAnnotation(node.return_type_annotation());

        // Visit parameters (also checks their type annotations)
        for (const auto &param : node.parameters())
        {
            if (param)
                param->accept(*this);
        }

        // Visit body
        if (node.body())
            node.body()->accept(*this);
    }

    void PositionFinder::visit(Cryo::VariableDeclarationNode &node)
    {
        const auto &loc = node.has_name_location() ? node.name_location() : node.location();
        if (matchesPosition(loc, node.name().size()))
        {
            _result.node = &node;
            _result.identifier_name = node.name();
            _result.kind = FoundNode::Kind::VariableDecl;
        }

        // Check type annotation (e.g., the `int` in `const x: int = 5`)
        if (node.has_type_annotation())
            checkTypeAnnotation(node.type_annotation());

        if (node.initializer())
            node.initializer()->accept(*this);
    }

    void PositionFinder::visit(Cryo::StructDeclarationNode &node)
    {
        const auto &loc = node.has_name_location() ? node.name_location() : node.location();
        if (matchesPosition(loc, node.name().size()))
        {
            _result.node = &node;
            _result.identifier_name = node.name();
            _result.kind = FoundNode::Kind::StructDecl;
        }

        // Visit fields (checks type annotations like `x: Point`)
        for (const auto &field : node.fields())
        {
            if (field)
                field->accept(*this);
        }

        for (const auto &method : node.methods())
        {
            if (method)
                method->accept(*this);
        }
    }

    void PositionFinder::visit(Cryo::ClassDeclarationNode &node)
    {
        const auto &loc = node.has_name_location() ? node.name_location() : node.location();
        if (matchesPosition(loc, node.name().size()))
        {
            _result.node = &node;
            _result.identifier_name = node.name();
            _result.kind = FoundNode::Kind::ClassDecl;
        }

        // Visit fields (checks type annotations)
        for (const auto &field : node.fields())
        {
            if (field)
                field->accept(*this);
        }

        for (const auto &method : node.methods())
        {
            if (method)
                method->accept(*this);
        }
    }

    void PositionFinder::visit(Cryo::EnumDeclarationNode &node)
    {
        const auto &loc = node.has_name_location() ? node.name_location() : node.location();
        if (matchesPosition(loc, node.name().size()))
        {
            _result.node = &node;
            _result.identifier_name = node.name();
            _result.kind = FoundNode::Kind::EnumDecl;
        }

        for (const auto &variant : node.variants())
        {
            if (variant)
                variant->accept(*this);
        }
    }

    void PositionFinder::visit(Cryo::EnumVariantNode &node)
    {
        // Enum variants don't have a preceding keyword, so location() is already correct
        if (matchesPosition(node, node.name().size()))
        {
            _result.node = &node;
            _result.identifier_name = node.name();
            _result.kind = FoundNode::Kind::EnumVariant;
        }
    }

    void PositionFinder::visit(Cryo::ImplementationBlockNode &node)
    {
        const auto &loc = node.has_name_location() ? node.name_location() : node.location();
        if (matchesPosition(loc, node.target_type().size()))
        {
            _result.node = &node;
            _result.identifier_name = node.target_type();
            _result.kind = FoundNode::Kind::TypeReference;
        }

        for (const auto &method : node.method_implementations())
        {
            if (method)
                method->accept(*this);
        }
    }

    void PositionFinder::visit(Cryo::StructMethodNode &node)
    {
        const auto &loc = node.has_name_location() ? node.name_location() : node.location();
        if (matchesPosition(loc, node.name().size()))
        {
            _result.node = &node;
            _result.identifier_name = node.name();
            _result.kind = FoundNode::Kind::FunctionDecl;
        }

        // Check return type annotation
        if (node.has_return_type_annotation())
            checkTypeAnnotation(node.return_type_annotation());

        for (const auto &param : node.parameters())
        {
            if (param)
                param->accept(*this);
        }

        if (node.body())
            node.body()->accept(*this);
    }

    void PositionFinder::visit(Cryo::ImportDeclarationNode &node)
    {
        const auto &loc = node.has_name_location() ? node.name_location() : node.location();
        if (matchesPosition(loc, node.module_path().size()))
        {
            _result.node = &node;
            _result.identifier_name = node.module_path();
            _result.kind = FoundNode::Kind::ImportDecl;
        }
    }

    void PositionFinder::visit(Cryo::TypeAliasDeclarationNode &node)
    {
        const auto &loc = node.has_name_location() ? node.name_location() : node.location();
        if (matchesPosition(loc, node.alias_name().size()))
        {
            _result.node = &node;
            _result.identifier_name = node.alias_name();
            _result.kind = FoundNode::Kind::TypeReference;
        }
    }

    void PositionFinder::visit(Cryo::TraitDeclarationNode &node)
    {
        const auto &loc = node.has_name_location() ? node.name_location() : node.location();
        if (matchesPosition(loc, node.name().size()))
        {
            _result.node = &node;
            _result.identifier_name = node.name();
            _result.kind = FoundNode::Kind::TypeReference;
        }

        for (const auto &method : node.methods())
        {
            if (method)
                method->accept(*this);
        }
    }

    // ============================================================================
    // Expressions
    // ============================================================================

    void PositionFinder::visit(Cryo::MemberAccessNode &node)
    {
        // First visit the object expression
        if (node.object())
            node.object()->accept(*this);

        // Check if cursor is on the member name (after the dot)
        if (node.has_member_location())
        {
            if (matchesPosition(node.member_location(), node.member().size()))
            {
                _result.node = &node;
                _result.identifier_name = node.member();
                _result.kind = FoundNode::Kind::FieldAccess;
            }
        }
    }

    void PositionFinder::visit(Cryo::ScopeResolutionNode &node)
    {
        if (node.location().line() != _target_line)
            return;

        const std::string &scope_name = node.scope_name();

        // Split scope_name by "::" to handle chained resolution
        // e.g., "Colors::Color" in "Colors::Color::Red" -> ["Colors", "Color"]
        std::vector<std::string> segments;
        size_t start = 0;
        while (true)
        {
            size_t pos = scope_name.find("::", start);
            if (pos == std::string::npos)
            {
                segments.push_back(scope_name.substr(start));
                break;
            }
            segments.push_back(scope_name.substr(start, pos - start));
            start = pos + 2;
        }

        // Check each segment of the scope name
        size_t current_col = node.location().column();
        for (const auto &segment : segments)
        {
            if (_target_col >= current_col && _target_col < current_col + segment.size())
            {
                _result.node = &node;
                _result.identifier_name = segment;
                _result.kind = FoundNode::Kind::TypeReference;
                return;
            }
            current_col += segment.size() + 2; // +2 for "::"
        }

        // Account for generic args after the last scope segment
        if (node.has_generic_args())
        {
            // Backtrack: generic args are on the last segment, before the final "::"
            // current_col is past the last "::" after scope segments
            // Actually, generics attach to the whole scope_name, positioned after scope_name
            // Recompute: generics start right after scope_name ends
            current_col = node.location().column() + scope_name.size();
            current_col += 1; // <
            for (size_t i = 0; i < node.generic_args().size(); ++i)
            {
                if (i > 0)
                    current_col += 2; // ", "
                current_col += node.generic_args()[i].size();
            }
            current_col += 1; // >
            current_col += 2; // "::" before member
        }

        // Check if cursor is on the member name (e.g., "new" in "Point::new")
        if (_target_col >= current_col && _target_col < current_col + node.member_name().size())
        {
            _result.node = &node;
            _result.identifier_name = node.member_name();
            _result.kind = FoundNode::Kind::ScopeResolution;
            return;
        }
    }

    void PositionFinder::visit(Cryo::CallExpressionNode &node)
    {
        if (node.callee())
            node.callee()->accept(*this);

        for (const auto &arg : node.arguments())
        {
            if (arg)
                arg->accept(*this);
        }
    }

    void PositionFinder::visit(Cryo::BinaryExpressionNode &node)
    {
        if (node.left())
            node.left()->accept(*this);
        if (node.right())
            node.right()->accept(*this);
    }

    void PositionFinder::visit(Cryo::UnaryExpressionNode &node)
    {
        if (node.operand())
            node.operand()->accept(*this);
    }

    void PositionFinder::visit(Cryo::SizeofExpressionNode &node)
    {
        // Check if cursor is on the 'sizeof' keyword itself
        if (matchesPosition(node.location(), 6)) // "sizeof" = 6 chars
        {
            _result.node = &node;
            _result.identifier_name = "sizeof";
            _result.kind = FoundNode::Kind::Identifier;
            return;
        }

        // Check if cursor is on the type name inside sizeof(TypeName)
        if (node.has_type_location())
        {
            if (matchesPosition(node.type_location(), node.type_name().size()))
            {
                _result.node = &node;
                _result.identifier_name = node.type_name();
                _result.kind = FoundNode::Kind::TypeReference;
            }
        }
    }

    void PositionFinder::visit(Cryo::AlignofExpressionNode &node)
    {
        // Check if cursor is on the 'alignof' keyword itself
        if (matchesPosition(node.location(), 7)) // "alignof" = 7 chars
        {
            _result.node = &node;
            _result.identifier_name = "alignof";
            _result.kind = FoundNode::Kind::Identifier;
            return;
        }

        // Check if cursor is on the type name inside alignof(TypeName)
        if (node.has_type_location())
        {
            if (matchesPosition(node.type_location(), node.type_name().size()))
            {
                _result.node = &node;
                _result.identifier_name = node.type_name();
                _result.kind = FoundNode::Kind::TypeReference;
            }
        }
    }

    void PositionFinder::visit(Cryo::CastExpressionNode &node)
    {
        // Visit the expression being cast
        if (node.expression())
            node.expression()->accept(*this);

        // Check if cursor is on the target type annotation (e.g., `x as int`)
        if (node.has_target_type_annotation())
            checkTypeAnnotation(node.target_type_annotation());
    }

    void PositionFinder::visit(Cryo::StructFieldNode &node)
    {
        // Check field name
        const auto &loc = node.has_name_location() ? node.name_location() : node.location();
        if (matchesPosition(loc, node.name().size()))
        {
            _result.node = &node;
            _result.identifier_name = node.name();
            _result.kind = FoundNode::Kind::VariableDecl;
        }

        // Check field type annotation (e.g., the `Point` in `center: Point`)
        if (node.has_type_annotation())
            checkTypeAnnotation(node.type_annotation());
    }

    void PositionFinder::visit(Cryo::StructLiteralNode &node)
    {
        // Check if cursor is on the struct type name (e.g., "Point" in "Point { x: 1, y: 2 }")
        if (matchesPosition(node.location(), node.struct_type().size()))
        {
            _result.node = &node;
            _result.identifier_name = node.struct_type();
            _result.kind = FoundNode::Kind::TypeReference;
        }

        // Visit field initializer value expressions
        for (const auto &field : node.field_initializers())
        {
            if (field && field->value())
                field->value()->accept(*this);
        }
    }

    // ============================================================================
    // Statements
    // ============================================================================

    void PositionFinder::visit(Cryo::IfStatementNode &node)
    {
        if (node.condition())
            node.condition()->accept(*this);
        if (node.then_statement())
            node.then_statement()->accept(*this);
        if (node.else_statement())
            node.else_statement()->accept(*this);
    }

    void PositionFinder::visit(Cryo::WhileStatementNode &node)
    {
        if (node.condition())
            node.condition()->accept(*this);
        if (node.body())
            node.body()->accept(*this);
    }

    void PositionFinder::visit(Cryo::ForStatementNode &node)
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

    void PositionFinder::visit(Cryo::ReturnStatementNode &node)
    {
        if (node.expression())
            node.expression()->accept(*this);
    }

    void PositionFinder::visit(Cryo::MatchStatementNode &node)
    {
        if (node.expr())
            node.expr()->accept(*this);

        for (const auto &arm : node.arms())
        {
            if (arm)
                arm->accept(*this);
        }
    }

    void PositionFinder::visit(Cryo::MatchExpressionNode &node)
    {
        if (node.expression())
            node.expression()->accept(*this);

        for (const auto &arm : node.arms())
        {
            if (arm)
                arm->accept(*this);
        }
    }

    void PositionFinder::visit(Cryo::MatchArmNode &node)
    {
        for (const auto &pattern : node.patterns())
        {
            if (pattern)
                pattern->accept(*this);
        }

        if (node.body())
            node.body()->accept(*this);
    }

    void PositionFinder::visit(Cryo::PatternNode &node)
    {
        // For literal patterns, visit the literal value
        if (node.literal_value())
            node.literal_value()->accept(*this);

        // For identifier patterns, check cursor on the identifier
        if (node.pattern_type() == Cryo::PatternNode::PatternType::Identifier)
        {
            if (matchesPosition(node, node.identifier().size()))
            {
                _result.node = &node;
                _result.identifier_name = node.identifier();
                _result.kind = FoundNode::Kind::Identifier;
            }
        }
    }

    void PositionFinder::visit(Cryo::EnumPatternNode &node)
    {
        const std::string &enum_name = node.enum_name();
        const std::string &variant_name = node.variant_name();

        if (node.location().line() != _target_line)
            return;

        // Split enum_name by "::" to get individual segments
        // e.g., "Colors::Color" -> ["Colors", "Color"]
        std::vector<std::string> segments;
        size_t start = 0;
        while (true)
        {
            size_t pos = enum_name.find("::", start);
            if (pos == std::string::npos)
            {
                segments.push_back(enum_name.substr(start));
                break;
            }
            segments.push_back(enum_name.substr(start, pos - start));
            start = pos + 2;
        }

        // Check each segment of enum_name
        size_t current_col = node.location().column();
        for (const auto &segment : segments)
        {
            if (_target_col >= current_col && _target_col < current_col + segment.size())
            {
                _result.node = &node;
                _result.identifier_name = segment;
                _result.kind = FoundNode::Kind::TypeReference;
                return;
            }
            current_col += segment.size() + 2; // +2 for "::"
        }

        // Check variant name
        if (_target_col >= current_col && _target_col < current_col + variant_name.size())
        {
            _result.node = &node;
            _result.identifier_name = variant_name;
            _result.kind = FoundNode::Kind::EnumVariant;
            return;
        }
    }

} // namespace CryoLSP
