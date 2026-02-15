#include "LSP/Providers/HoverProvider.hpp"
#include "LSP/PositionFinder.hpp"
#include "LSP/Transport.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "AST/ASTVisitor.hpp"
#include "Types/TypeAnnotation.hpp"
#include "Lexer/lexer.hpp"
#include <unordered_map>
#include <cstdint>

namespace CryoLSP
{

    // ============================================================================
    // Declaration Finder - walks top-level AST to find declarations by name
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

        void visit(Cryo::ProgramNode &node) override
        {
            for (const auto &child : node.statements())
                if (child && !_result)
                    child->accept(*this);
        }

        void visit(Cryo::DeclarationStatementNode &node) override
        {
            if (node.declaration() && !_result)
                node.declaration()->accept(*this);
        }

        void visit(Cryo::FunctionDeclarationNode &node) override
        {
            if (node.name() == _target)
                _result = &node;
        }

        void visit(Cryo::StructDeclarationNode &node) override
        {
            if (node.name() == _target)
                _result = &node;
        }

        void visit(Cryo::ClassDeclarationNode &node) override
        {
            if (node.name() == _target)
                _result = &node;
        }

        void visit(Cryo::EnumDeclarationNode &node) override
        {
            if (node.name() == _target)
                _result = &node;
        }

        void visit(Cryo::TraitDeclarationNode &node) override
        {
            if (node.name() == _target)
                _result = &node;
        }

        void visit(Cryo::TypeAliasDeclarationNode &node) override
        {
            if (node.alias_name() == _target)
                _result = &node;
        }

        void visit(Cryo::IntrinsicDeclarationNode &node) override
        {
            if (node.name() == _target)
                _result = &node;
        }

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

        void visit(Cryo::ProgramNode &node) override
        {
            for (const auto &child : node.statements())
                if (child && !_result)
                    child->accept(*this);
        }

        void visit(Cryo::DeclarationStatementNode &node) override
        {
            if (node.declaration() && !_result)
                node.declaration()->accept(*this);
        }

        void visit(Cryo::StructDeclarationNode &node) override
        {
            if (_result)
                return;

            // Search methods
            for (const auto &method : node.methods())
            {
                if (method && method->name() == _target)
                {
                    _result = method.get();
                    _owner_name = node.name();
                    return;
                }
            }

            // Search fields
            for (const auto &field : node.fields())
            {
                if (field && field->name() == _target)
                {
                    _result = field.get();
                    _owner_name = node.name();
                    return;
                }
            }
        }

        void visit(Cryo::ClassDeclarationNode &node) override
        {
            if (_result)
                return;

            for (const auto &method : node.methods())
            {
                if (method && method->name() == _target)
                {
                    _result = method.get();
                    _owner_name = node.name();
                    return;
                }
            }

            for (const auto &field : node.fields())
            {
                if (field && field->name() == _target)
                {
                    _result = field.get();
                    _owner_name = node.name();
                    return;
                }
            }
        }

        void visit(Cryo::ImplementationBlockNode &node) override
        {
            if (_result)
                return;

            for (const auto &method : node.method_implementations())
            {
                if (method && method->name() == _target)
                {
                    _result = method.get();
                    _owner_name = node.target_type();
                    return;
                }
            }

            for (const auto &field : node.field_implementations())
            {
                if (field && field->name() == _target)
                {
                    _result = field.get();
                    _owner_name = node.target_type();
                    return;
                }
            }
        }

    private:
        std::string _target;
        Cryo::ASTNode *_result = nullptr;
        std::string _owner_name;
    };

    // ============================================================================
    // Enclosing Type Finder - walks AST to find the struct/class/impl type
    // that contains the method at a given cursor line (for 'this' hover)
    // ============================================================================

    class EnclosingTypeFinder : public Cryo::BaseASTVisitor
    {
    public:
        EnclosingTypeFinder(size_t target_line) : _target_line(target_line) {}

        struct Result
        {
            std::string type_name;
            Cryo::ASTNode *type_node = nullptr;
            bool is_mutable = false;
            size_t size = 0;
            size_t alignment = 0;
        };

        Result find(Cryo::ASTNode *root)
        {
            _result = {};
            _current_type.clear();
            _current_type_node = nullptr;
            if (root)
                root->accept(*this);
            return _result;
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

        void visit(Cryo::StructDeclarationNode &node) override
        {
            std::string prev = _current_type;
            Cryo::ASTNode *prev_node = _current_type_node;
            _current_type = node.name();
            _current_type_node = &node;
            for (const auto &method : node.methods())
                if (method)
                    method->accept(*this);
            _current_type = prev;
            _current_type_node = prev_node;
        }

        void visit(Cryo::ClassDeclarationNode &node) override
        {
            std::string prev = _current_type;
            Cryo::ASTNode *prev_node = _current_type_node;
            _current_type = node.name();
            _current_type_node = &node;
            for (const auto &method : node.methods())
                if (method)
                    method->accept(*this);
            _current_type = prev;
            _current_type_node = prev_node;
        }

        void visit(Cryo::ImplementationBlockNode &node) override
        {
            std::string prev = _current_type;
            Cryo::ASTNode *prev_node = _current_type_node;
            _current_type = node.target_type();
            _current_type_node = &node;
            for (const auto &method : node.method_implementations())
                if (method)
                    method->accept(*this);
            _current_type = prev;
            _current_type_node = prev_node;
        }

        void visit(Cryo::StructMethodNode &node) override
        {
            checkMethod(node);
        }

        void visit(Cryo::FunctionDeclarationNode &node) override
        {
            checkMethod(node);
        }

    private:
        size_t _target_line;
        std::string _current_type;
        Cryo::ASTNode *_current_type_node = nullptr;
        Result _result;

        void checkMethod(Cryo::FunctionDeclarationNode &node)
        {
            if (_current_type.empty())
                return;
            if (node.location().line() > _target_line)
                return;

            for (const auto &param : node.parameters())
            {
                if (param && param->name() == "this")
                {
                    _result.type_name = _current_type;
                    _result.type_node = _current_type_node;
                    _result.is_mutable = param->is_mutable();
                    return;
                }
            }
        }
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

        void visit(Cryo::ProgramNode &node) override
        {
            for (const auto &child : node.statements())
                if (child && !_result)
                    child->accept(*this);
        }

        void visit(Cryo::DeclarationStatementNode &node) override
        {
            if (node.declaration() && !_result)
                node.declaration()->accept(*this);
        }

        void visit(Cryo::BlockStatementNode &node) override
        {
            for (const auto &stmt : node.statements())
                if (stmt && !_result)
                    stmt->accept(*this);
        }

        void visit(Cryo::ExpressionStatementNode &node) override
        {
            // Expression statements don't contain variable declarations
        }

        void visit(Cryo::VariableDeclarationNode &node) override
        {
            if (node.name() == _target)
                _result = &node;
        }

        void visit(Cryo::FunctionDeclarationNode &node) override
        {
            // Check parameters
            for (const auto &param : node.parameters())
            {
                if (param && !_result && param->name() == _target)
                    _result = param.get();
            }
            // Recurse into body
            if (node.body() && !_result)
                node.body()->accept(*this);
        }

        void visit(Cryo::StructMethodNode &node) override
        {
            for (const auto &param : node.parameters())
            {
                if (param && !_result && param->name() == _target)
                    _result = param.get();
            }
            if (node.body() && !_result)
                node.body()->accept(*this);
        }

        void visit(Cryo::StructDeclarationNode &node) override
        {
            for (const auto &method : node.methods())
                if (method && !_result)
                    method->accept(*this);
        }

        void visit(Cryo::ClassDeclarationNode &node) override
        {
            for (const auto &method : node.methods())
                if (method && !_result)
                    method->accept(*this);
        }

        void visit(Cryo::ImplementationBlockNode &node) override
        {
            for (const auto &method : node.method_implementations())
                if (method && !_result)
                    method->accept(*this);
        }

        void visit(Cryo::IfStatementNode &node) override
        {
            if (node.then_statement() && !_result)
                node.then_statement()->accept(*this);
            if (node.else_statement() && !_result)
                node.else_statement()->accept(*this);
        }

        void visit(Cryo::ForStatementNode &node) override
        {
            if (node.init() && !_result)
                node.init()->accept(*this);
            if (node.body() && !_result)
                node.body()->accept(*this);
        }

        void visit(Cryo::WhileStatementNode &node) override
        {
            if (node.body() && !_result)
                node.body()->accept(*this);
        }

    private:
        std::string _target;
        Cryo::ASTNode *_result = nullptr;
    };

    // ============================================================================
    // Static Helpers
    // ============================================================================

    HoverProvider::HoverProvider(AnalysisEngine &engine, DocumentStore &documents)
        : _engine(engine), _documents(documents) {}

    static std::string getParamTypeStr(Cryo::VariableDeclarationNode *param)
    {
        if (param->has_type_annotation())
            return param->type_annotation()->to_string();
        if (param->has_resolved_type())
            return param->get_resolved_type()->display_name();
        return "auto";
    }

    static std::string getFieldTypeStr(Cryo::StructFieldNode *field)
    {
        if (field->has_type_annotation())
            return field->type_annotation()->to_string();
        if (field->has_resolved_type())
            return field->get_resolved_type()->display_name();
        return "unknown";
    }

    static std::string getReturnTypeStr(Cryo::FunctionDeclarationNode *func)
    {
        if (func->has_return_type_annotation())
        {
            std::string ret = func->return_type_annotation()->to_string();
            if (ret != "void" && !ret.empty())
                return ret;
        }
        if (func->has_resolved_return_type())
        {
            std::string ret = func->get_resolved_return_type()->display_name();
            if (ret != "void" && !ret.empty())
                return ret;
        }
        return "";
    }

    static std::string formatGenericParams(
        const std::vector<std::unique_ptr<Cryo::GenericParameterNode>> &generics)
    {
        if (generics.empty())
            return "";
        std::string result = "<";
        for (size_t i = 0; i < generics.size(); ++i)
        {
            if (i > 0)
                result += ", ";
            result += generics[i]->name();
            const auto &constraints = generics[i]->constraints();
            if (!constraints.empty())
            {
                result += ": ";
                for (size_t j = 0; j < constraints.size(); ++j)
                {
                    if (j > 0)
                        result += " + ";
                    result += constraints[j];
                }
            }
        }
        result += ">";
        return result;
    }

    static std::string buildFunctionSignature(Cryo::FunctionDeclarationNode *func)
    {
        std::string sig;

        bool is_method = dynamic_cast<Cryo::StructMethodNode *>(func) != nullptr;
        if (is_method)
        {
            auto *method = static_cast<Cryo::StructMethodNode *>(func);
            if (method->is_static())
                sig += "static ";
        }
        else
        {
            sig += "function ";
        }
        sig += func->name();

        // Generic params
        sig += formatGenericParams(func->generic_parameters());

        // Parameters
        sig += "(";
        const auto &params = func->parameters();
        bool is_variadic = func->is_variadic();
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (i > 0)
                sig += ", ";

            // If this is the last parameter and the function is variadic,
            // display as `name...` instead of `name: void`
            if (is_variadic && i == params.size() - 1)
            {
                sig += params[i]->name() + "...";
            }
            else
            {
                sig += params[i]->name() + ": " + getParamTypeStr(params[i].get());
            }
        }
        sig += ")";

        // Return type
        std::string ret = getReturnTypeStr(func);
        if (!ret.empty())
            sig += " -> " + ret;

        return sig;
    }

    static std::string appendDocumentation(Cryo::DeclarationNode *decl, const std::string &result)
    {
        if (decl->has_documentation())
            return result + "\n\n---\n\n" + decl->documentation();
        return result;
    }

    // ============================================================================
    // Format Functions
    // ============================================================================

    std::string HoverProvider::formatFunctionHover(Cryo::FunctionDeclarationNode *func)
    {
        std::string result = "```cryo\n" + buildFunctionSignature(func) + "\n```";
        return appendDocumentation(func, result);
    }

    std::string HoverProvider::formatStructHover(Cryo::StructDeclarationNode *decl)
    {
        size_t prop_limit = 3;
        size_t method_limit = 5;
        std::string result = "```cryo\ntype struct " + decl->name();
        result += formatGenericParams(decl->generic_parameters());
        result += " {\n";

        for (size_t i = 0; i < decl->fields().size() && i < prop_limit; ++i)
        {
            const auto &field = decl->fields()[i];
            if (!field)
                continue;
            result += "    " + field->name() + ": " + getFieldTypeStr(field.get()) + ",\n";
        }

        // Show methods
        if (!decl->methods().empty())
        {
            result += "\n";
            for (size_t i = 0; i < decl->methods().size() && i < method_limit; ++i)
            {
                const auto &method = decl->methods()[i];
                if (!method)
                    continue;
                result += "\t" + buildFunctionSignature(method.get()) + "\n";
            }
        }

        result += "}\n```";

        return appendDocumentation(decl, result);
    }

    std::string HoverProvider::formatClassHover(Cryo::ClassDeclarationNode *decl)
    {
        size_t prop_limit = 3;
        size_t method_limit = 5;
        std::string result = "```cryo\ntype class " + decl->name();
        result += formatGenericParams(decl->generic_parameters());

        if (!decl->fields().empty())
        {
            result += " {\n";
            for (size_t i = 0; i < decl->fields().size() && i < prop_limit; ++i)
            {
                const auto &field = decl->fields()[i];
                if (!field)
                    continue;
                result += "    " + field->name() + ": " + getFieldTypeStr(field.get()) + ",\n";
            }
            result += "}\n```";
        }
        else
        {
            result += "\n```";
        }

        if (!decl->methods().empty())
        {
            result += "\n\n**Methods:**\n";
            for (size_t i = 0; i < decl->methods().size() && i < method_limit; ++i)
            {
                const auto &method = decl->methods()[i];
                if (!method)
                    continue;
                result += "- `" + buildFunctionSignature(method.get()) + "`\n";
            }
        }

        return appendDocumentation(decl, result);
    }

    std::string HoverProvider::formatEnumHover(Cryo::EnumDeclarationNode *decl)
    {
        std::string result = "```cryo\nenum " + decl->name();
        result += formatGenericParams(decl->generic_parameters());
        result += " {\n";

        for (const auto &variant : decl->variants())
        {
            if (!variant)
                continue;
            result += "    " + variant->name();
            if (!variant->is_simple_variant())
            {
                result += "(";
                const auto &types = variant->associated_types();
                for (size_t i = 0; i < types.size(); ++i)
                {
                    if (i > 0)
                        result += ", ";
                    result += types[i];
                }
                result += ")";
            }
            else if (variant->has_explicit_value())
            {
                result += " = " + std::to_string(variant->explicit_value());
            }
            result += ",\n";
        }

        result += "}\n```";
        return appendDocumentation(decl, result);
    }

    std::string HoverProvider::formatVariableHover(Cryo::VariableDeclarationNode *decl)
    {
        std::string result = "```cryo\n";
        result += decl->is_mutable() ? "mut " : "const ";
        result += decl->name();

        std::string type_str = getParamTypeStr(decl);
        if (type_str != "auto")
            result += ": " + type_str;

        result += "\n```";
        return appendDocumentation(decl, result);
    }

    std::string HoverProvider::formatEnumVariantHover(Cryo::EnumVariantNode *variant)
    {
        std::string result = "```cryo\n" + variant->name();

        if (!variant->is_simple_variant())
        {
            result += "(";
            const auto &types = variant->associated_types();
            for (size_t i = 0; i < types.size(); ++i)
            {
                if (i > 0)
                    result += ", ";
                result += types[i];
            }
            result += ")";
        }
        else if (variant->has_explicit_value())
        {
            result += " = " + std::to_string(variant->explicit_value());
        }

        result += "\n```";
        return appendDocumentation(variant, result);
    }

    std::string HoverProvider::formatTraitHover(Cryo::TraitDeclarationNode *decl)
    {
        std::string result = "```cryo\ntrait " + decl->name();
        result += formatGenericParams(decl->generic_parameters());
        result += "\n```";

        if (!decl->methods().empty())
        {
            result += "\n\n**Methods:**\n";
            for (const auto &method : decl->methods())
            {
                if (!method)
                    continue;
                result += "- `" + buildFunctionSignature(method.get()) + "`\n";
            }
        }

        return appendDocumentation(decl, result);
    }

    // ============================================================================
    // Symbol Reference Hover (for identifiers that reference a declaration)
    // ============================================================================

    std::string HoverProvider::formatSymbolRefHover(Cryo::Symbol *sym, Cryo::ASTNode *ast_root)
    {
        // Try to find the declaration in the AST for rich info
        DeclarationFinder declFinder(sym->name);
        Cryo::ASTNode *declNode = declFinder.find(ast_root);

        if (declNode)
        {
            if (auto *func = dynamic_cast<Cryo::FunctionDeclarationNode *>(declNode))
                return formatFunctionHover(func);
            if (auto *strct = dynamic_cast<Cryo::StructDeclarationNode *>(declNode))
                return formatStructHover(strct);
            if (auto *cls = dynamic_cast<Cryo::ClassDeclarationNode *>(declNode))
                return formatClassHover(cls);
            if (auto *enm = dynamic_cast<Cryo::EnumDeclarationNode *>(declNode))
                return formatEnumHover(enm);
            if (auto *trait = dynamic_cast<Cryo::TraitDeclarationNode *>(declNode))
                return formatTraitHover(trait);
            if (auto *alias = dynamic_cast<Cryo::TypeAliasDeclarationNode *>(declNode))
            {
                std::string result = "```cryo\ntype " + alias->alias_name() + "\n```";
                return appendDocumentation(alias, result);
            }
        }

        // Try deep AST search for variable/parameter declarations
        if (!declNode)
        {
            VariableRefResolver varResolver(sym->name);
            Cryo::ASTNode *varNode = varResolver.find(ast_root);
            if (auto *varDecl = dynamic_cast<Cryo::VariableDeclarationNode *>(varNode))
                return formatVariableHover(varDecl);
        }

        // Try MemberFinder for methods/fields inside structs, classes, and impl blocks
        // This handles cases like `Point::new` where the method isn't a top-level decl
        if (!declNode)
        {
            // For qualified names like "Point::new", search for the member part
            std::string memberName = sym->name;
            auto colonPos = memberName.rfind("::");
            if (colonPos != std::string::npos)
                memberName = memberName.substr(colonPos + 2);

            MemberFinder memberFinder(memberName);
            Cryo::ASTNode *memberNode = memberFinder.find(ast_root);
            if (memberNode)
            {
                if (auto *method = dynamic_cast<Cryo::FunctionDeclarationNode *>(memberNode))
                    return formatFunctionHover(method);
                if (auto *field = dynamic_cast<Cryo::StructFieldNode *>(memberNode))
                {
                    std::string fieldResult = "```cryo\n" + memberFinder.owner_name() + "." + field->name() + ": " + getFieldTypeStr(field) + "\n```";
                    return appendDocumentation(field, fieldResult);
                }
            }
        }

        // Fallback: format from symbol table info only
        std::string result;
        std::string type_str;
        if (sym->type.is_valid())
            type_str = sym->type->display_name();

        switch (sym->kind)
        {
        case Cryo::SymbolKind::Function:
        case Cryo::SymbolKind::Method:
        {
            // Function type display_name is "(params) -> ret"
            result = "```cryo\nfunction " + sym->name;
            if (!type_str.empty())
                result += type_str;
            result += "\n```";
            break;
        }
        case Cryo::SymbolKind::Variable:
        case Cryo::SymbolKind::Parameter:
        {
            result = "```cryo\n";
            result += sym->is_mutable ? "mut " : "const ";
            result += sym->name;
            if (!type_str.empty())
                result += ": " + type_str;
            result += "\n```";
            break;
        }
        case Cryo::SymbolKind::Type:
        case Cryo::SymbolKind::TypeAlias:
        {
            result = "```cryo\ntype " + sym->name + "\n```";
            break;
        }
        case Cryo::SymbolKind::Field:
        {
            result = "```cryo\n" + sym->name;
            if (!type_str.empty())
                result += ": " + type_str;
            result += "\n```";
            break;
        }
        case Cryo::SymbolKind::EnumVariant:
        {
            result = "```cryo\n" + sym->name;
            if (!type_str.empty())
                result += "(" + type_str + ")";
            result += "\n```";
            break;
        }
        case Cryo::SymbolKind::Constant:
        {
            result = "```cryo\nconst " + sym->name;
            if (!type_str.empty())
                result += ": " + type_str;
            result += "\n```";
            break;
        }
        default:
        {
            result = "```cryo\n" + sym->name;
            if (!type_str.empty())
                result += ": " + type_str;
            result += "\n```";
            break;
        }
        }

        if (!sym->documentation.empty())
            result += "\n\n---\n\n" + sym->documentation;

        return result;
    }

    // ============================================================================
    // Namespace / Module Hover
    // ============================================================================

    std::string HoverProvider::formatNamespaceHover(const std::string &name, Cryo::ProgramNode *ast)
    {
        std::string result = "```cryo\nnamespace " + name + "\n```\n\n---\n\n";

        int count = 0;
        int max_items = 10;

        for (const auto &stmt : ast->statements())
        {
            if (count >= max_items)
                break;
            if (!stmt)
                continue;

            Cryo::ASTNode *decl = stmt.get();
            if (auto *ds = dynamic_cast<Cryo::DeclarationStatementNode *>(decl))
                decl = ds->declaration();

            if (auto *func = dynamic_cast<Cryo::FunctionDeclarationNode *>(decl))
            {
                result += "- `" + buildFunctionSignature(func) + "`\n";
                ++count;
            }
            else if (auto *strct = dynamic_cast<Cryo::StructDeclarationNode *>(decl))
            {
                result += "- `type struct " + strct->name();
                result += formatGenericParams(strct->generic_parameters());
                result += "`\n";
                ++count;
            }
            else if (auto *cls = dynamic_cast<Cryo::ClassDeclarationNode *>(decl))
            {
                result += "- `type class " + cls->name();
                result += formatGenericParams(cls->generic_parameters());
                result += "`\n";
                ++count;
            }
            else if (auto *enm = dynamic_cast<Cryo::EnumDeclarationNode *>(decl))
            {
                result += "- `enum " + enm->name();
                result += formatGenericParams(enm->generic_parameters());
                result += "`\n";
                ++count;
            }
            else if (auto *trait = dynamic_cast<Cryo::TraitDeclarationNode *>(decl))
            {
                result += "- `trait " + trait->name();
                result += formatGenericParams(trait->generic_parameters());
                result += "`\n";
                ++count;
            }
        }

        if (count == 0)
            result += "*No exported declarations found*\n";
        else if (count >= max_items)
            result += "\n*... and more*\n";

        return result;
    }

    // ============================================================================
    // Primitive Type Documentation
    // ============================================================================

    std::string HoverProvider::getPrimitiveTypeHover(const std::string &name)
    {
        // Map of primitive type names to their documentation
        static const std::unordered_map<std::string, std::pair<std::string, std::string>> primitives = {
            // Integer types - signed
            {"int", {"int", "The default signed integer type (32-bit).\n\nSize: 4 bytes \nRange: `-2,147,483,648` to `2,147,483,647`"}},
            {"i8", {"i8", "An 8-bit signed integer type.\n\nSize: 1 byte \nRange: `-128` to `127`"}},
            {"i16", {"i16", "A 16-bit signed integer type.\n\nSize: 2 bytes \nRange: `-32,768` to `32,767`"}},
            {"i32", {"i32", "A 32-bit signed integer type.\n\nSize: 4 bytes \nRange: `-2,147,483,648` to `2,147,483,647`"}},
            {"i64", {"i64", "A 64-bit signed integer type.\n\nSize: 8 bytes \nRange: `-9,223,372,036,854,775,808` to `9,223,372,036,854,775,807`"}},
            {"i128", {"i128", "A 128-bit signed integer type.\n\nSize: 16 bytes"}},

            // Integer types - unsigned
            {"u8", {"u8", "An 8-bit unsigned integer type.\n\nSize: 1 byte \nRange: `0` to `255`"}},
            {"u16", {"u16", "A 16-bit unsigned integer type.\n\nSize: 2 bytes \nRange: `0` to `65,535`"}},
            {"u32", {"u32", "A 32-bit unsigned integer type.\n\nSize: 4 bytes \nRange: `0` to `4,294,967,295`"}},
            {"u64", {"u64", "A 64-bit unsigned integer type.\n\nSize: 8 bytes \nRange: `0` to `18,446,744,073,709,551,615`"}},
            {"u128", {"u128", "A 128-bit unsigned integer type.\n\nSize: 16 bytes"}},

            // Floating point types
            {"float", {"float", "The default floating-point type (64-bit double precision).\n\nSize: 8 bytes \nPrecision: ~15-17 significant digits"}},
            {"f32", {"f32", "A 32-bit floating-point type (single precision).\n\nSize: 4 bytes \nPrecision: ~6-9 significant digits"}},
            {"f64", {"f64", "A 64-bit floating-point type (double precision).\n\nSize: 8 bytes \nPrecision: ~15-17 significant digits"}},

            // Boolean
            {"boolean", {"boolean", "The boolean type.\n\nCan be either `true` or `false`. \nSize: 1 byte"}},

            // String and char
            {"string", {"string", "A UTF-8 encoded string type.\n\nStrings are heap-allocated and growable. \nString literals like `\"hello\"` create values of this type."}},
            {"char", {"char", "A Unicode scalar value.\n\nRepresents a single Unicode character. \nSize: 4 bytes \nRange: `U+0000` to `U+10FFFF` (excluding surrogates)"}},

            // Void and unit
            {"void", {"void", "The void type.\n\nUsed as a function return type to indicate the function returns no value."}},

            // Special types
            {"()", {"()", "The unit type.\n\nRepresents an empty value. \nUsed when a function returns no meaningful value but you want to indicate it returns successfully."}},

            // Boolean literals (in case they're parsed as identifiers)
            {"true", {"true", "The boolean type.\n\nCan be either `true` or `false`. \nSize: 1 byte\n"}},
            {"false", {"false", "The boolean type.\n\nCan be either `true` or `false`. \nSize: 1 byte\n"}},

            // Built-in operators
            {"sizeof", {"sizeof(Type) -> int", "Returns the size in bytes of the given type.\n\nEvaluated at compile time. The result is an integer representing the number of bytes the type occupies in memory.\n\n**Example:**\n```cryo\nsizeof(i32)    // 4\nsizeof(Point)  // depends on fields\n```"}},
            {"alignof", {"alignof(Type) -> int", "Returns the alignment requirement in bytes of the given type.\n\nEvaluated at compile time. The result is an integer representing the minimum alignment boundary the type requires.\n\n**Example:**\n```cryo\nalignof(i32)    // 4\nalignof(Point)  // depends on fields\n```"}},
        };

        auto it = primitives.find(name);
        if (it == primitives.end())
            return "";

        const auto &[display_name, description] = it->second;
        return "```cryo\n" + display_name + "\n```\n\n---\n\n" + description;
    }

    // ============================================================================
    // Keyword Documentation
    // ============================================================================

    std::string HoverProvider::getKeywordHover(const std::string &keyword)
    {
        static const std::unordered_map<std::string, std::pair<std::string, std::string>> keywords = {
            {"type", {"type", "Declare a data type or type alias.\n\n`type struct Foo { ... }` declares a struct of Foo type. \n`type class Bar { ... }` declares a class of Bar type. \n`type` can also *alias* types: `type Bar = Foo;` creates an alias where `Bar` is equivalent to `Foo`."}},

            {"struct", {"struct", "A struct is a data type where all of its members are **public** by default.\n\nA struct does **not** allow for inheritance but may be generic.\n\n**Example:**\n```cryo\ntype struct Point<T> {\n    x: T,\n    y: T,\n}\n```"}},

            {"class", {"class", "A class is similar to a struct, but all members must declare their visibility in a visibility block (`public:` | `private:` | `protected:`).\n\nNon-generic classes may allow for inheritance.\n\n**Example:**\n```cryo\ntype class Foo {\npublic:\n    x: int,\n    y: int,\n}\n```"}},

            {"enum", {"enum", "An enum can take on three distinct forms:\n\n- A **complex** enum with variant constructors\n- A **simple** C-style enum with integer values\n- A **mix** of both simple and complex variants\n\n**Example:**\n```cryo\nenum Color {\n    Red,\n    Green,\n    Blue,\n}\n\nenum Option<T> {\n    Some(T),\n    None,\n}\n```"}},

            {"implement", {"implement", "An implementation block is used to implement methods of an object type or enum.\n\nPrimarily used for complex enum implementations. Structs and classes generally inline their method implementations.\n\n**Example:**\n```cryo\nimplement Option<T> {\n    function is_some(this) -> boolean {\n        match (this) {\n            Option::Some(_) => { true },\n            Option::None => { false }\n        }\n    }\n}\n```"}},

            {"match", {"match", "A match expression performs pattern matching against a value.\n\nCan be used as a statement or as an expression that returns a value.\n\n**Example:**\n```cryo\nmatch (value) {\n    1 => { println(\"one\") }\n    2 => { println(\"two\") }\n    _ => { println(\"other\") }\n}\n\nconst result = match (opt) {\n    Option::Some(x) => { x }\n    Option::None => { 0 }\n};\n```"}},

            {"if", {"if", "A conditional expression.\n\nCan be used as a statement or as an expression that returns a value.\n\n**Example:**\n```cryo\nif (condition) {\n    // ...\n} else {\n    // ...\n}\n\nconst value = if (x > 0) { x } else { -x };\n```"}},

            {"for", {"for", "A loop statement.\n\nCurrently supports C-style `for(init; condition; update)` loops.\n\n**Example:**\n```cryo\nfor (mut i: int = 0; i < 10; i++) {\n    println(i);\n}\n```"}},
        };

        auto it = keywords.find(keyword);
        if (it == keywords.end())
            return "";

        const auto &[display_name, description] = it->second;
        return "```cryo\n" + display_name + "\n```\n\n---\n\n" + description;
    }

    // ============================================================================
    // Word Extraction Helper
    // ============================================================================

    std::string HoverProvider::extractWordAtPosition(const std::string &content, int line, int character)
    {
        // Find the target line in the content
        size_t pos = 0;
        int current_line = 0;
        while (current_line < line && pos < content.size())
        {
            if (content[pos] == '\n')
                ++current_line;
            ++pos;
        }

        if (current_line != line || pos >= content.size())
            return "";

        // pos now points to the start of the target line
        size_t line_start = pos;
        size_t line_end = content.find('\n', line_start);
        if (line_end == std::string::npos)
            line_end = content.size();

        size_t cursor = line_start + static_cast<size_t>(character);
        if (cursor >= line_end)
            return "";

        // Check if cursor is on a word character
        auto isWordChar = [](char c)
        { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; };

        if (!isWordChar(content[cursor]))
            return "";

        // Find word boundaries
        size_t word_start = cursor;
        while (word_start > line_start && isWordChar(content[word_start - 1]))
            --word_start;

        size_t word_end = cursor;
        while (word_end < line_end && isWordChar(content[word_end]))
            ++word_end;

        // Encode word start column offset in a way the caller can use:
        // We prefix the result with the column offset separated by a null char.
        // This is a simple way to return two values without changing the signature.
        // Actually, let's just return the word — the caller can compute the offset.
        return content.substr(word_start, word_end - word_start);
    }

    int HoverProvider::findWordStartColumn(const std::string &content, int line, int character)
    {
        // Find the target line
        size_t pos = 0;
        int current_line = 0;
        while (current_line < line && pos < content.size())
        {
            if (content[pos] == '\n')
                ++current_line;
            ++pos;
        }

        if (current_line != line || pos >= content.size())
            return character;

        size_t line_start = pos;
        size_t line_end = content.find('\n', line_start);
        if (line_end == std::string::npos)
            line_end = content.size();

        size_t cursor = line_start + static_cast<size_t>(character);
        if (cursor >= line_end)
            return character;

        auto isWordChar = [](char c)
        { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; };

        if (!isWordChar(content[cursor]))
            return character;

        size_t word_start = cursor;
        while (word_start > line_start && isWordChar(content[word_start - 1]))
            --word_start;

        return static_cast<int>(word_start - line_start);
    }

    // ============================================================================
    // Literal Hover
    // ============================================================================

    std::string sanitizeForDisplay(const std::string &input)
    {
        std::string result;
        result.reserve(input.size());
        for (char c : input)
        {
            switch (c)
            {
            case '\t':
                result += "\\t";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\0':
                result += "\\0";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\"':
                result += "\\\"";
                break;
            default:
                result += c;
                break;
            }
        }
        return result;
    }

    std::string HoverProvider::formatLiteralHover(Cryo::LiteralNode *literal, bool is_negated)
    {
        auto lk = literal->literal_kind();
        const std::string &value = literal->value();

        if (lk == Cryo::TokenKind::TK_STRING_LITERAL || lk == Cryo::TokenKind::TK_RAW_STRING_LITERAL)
        {
            std::string len_str = std::to_string(value.size());
            std::string str_value = sanitizeForDisplay(value);
            return "```cryo\nchar[" + len_str + "](\"" + str_value + "\")\n```\n\n---\n\n"
                                                                     "A UTF-8 encoded string type.\n\n";
        }

        if (lk == Cryo::TokenKind::TK_BOOLEAN_LITERAL || lk == Cryo::TokenKind::TK_KW_BOOLEAN)
        {
            return "```cryo\ntype boolean = true | false\n```\n\n---\n\n"
                   "The boolean type.\n";
        }

        if (lk == Cryo::TokenKind::TK_KW_TRUE)
        {
            return "```cryo\ntrue\n\n"
                   "type boolean = true | false\n```\n\n---\n\n"
                   "The boolean type.\n";
        }

        if (lk == Cryo::TokenKind::TK_KW_FALSE)
        {
            return "```cryo\nfalse\n\n"
                   "type boolean = true | false\n```\n\n---\n\n"
                   "The boolean type.\n";
        }

        if (lk == Cryo::TokenKind::TK_CHAR_CONSTANT)
        {
            return "```cryo\nchar\n```\n\n---\n\n"
                   "A Unicode scalar value.\n\n"
                   "Character literal: `'" +
                   value + "'`";
        }

        if (lk == Cryo::TokenKind::TK_NUMERIC_CONSTANT)
        {
            // Determine if float or int based on value content
            bool is_float = (value.find('.') != std::string::npos ||
                             value.find('e') != std::string::npos ||
                             value.find('E') != std::string::npos);

            if (is_float)
            {
                // Check for float suffix
                std::string float_type = "float";
                std::string float_desc = "The default floating-point type (64-bit double precision).";
                if (value.size() >= 3)
                {
                    std::string suffix = value.substr(value.size() - 3);
                    if (suffix == "f32")
                    {
                        float_type = "f32";
                        float_desc = "A 32-bit floating-point type (single precision).\n\nSize: 4 bytes \nPrecision: ~6-9 significant digits";
                    }
                    else if (suffix == "f64")
                    {
                        float_type = "f64";
                        float_desc = "A 64-bit floating-point type (double precision).\n\nSize: 8 bytes \nPrecision: ~15-17 significant digits";
                    }
                }

                return "```cryo\n" + float_type + "(" + value + ")" + "\n```\n\n---\n\n" + float_desc;
            }
            else
            {
                // Check for integer type suffix (e.g., 42i64, 0xFFu32)
                std::string int_type;
                std::string int_desc;

                // Try to extract suffix: scan backwards for type suffix
                static const std::vector<std::pair<std::string, std::pair<std::string, std::string>>> suffixes = {
                    {"i128", {"i128", "A 128-bit signed integer type.\n\nSize: 16 bytes"}},
                    {"u128", {"u128", "A 128-bit unsigned integer type.\n\nSize: 16 bytes"}},
                    {"i64", {"i64", "A 64-bit signed integer type.\n\nSize: 8 bytes \nRange: `-9,223,372,036,854,775,808` to `9,223,372,036,854,775,807`"}},
                    {"u64", {"u64", "A 64-bit unsigned integer type.\n\nSize: 8 bytes \nRange: `0` to `18,446,744,073,709,551,615`"}},
                    {"i32", {"i32", "A 32-bit signed integer type.\n\nSize: 4 bytes \nRange: `-2,147,483,648` to `2,147,483,647`"}},
                    {"u32", {"u32", "A 32-bit unsigned integer type.\n\nSize: 4 bytes \nRange: `0` to `4,294,967,295`"}},
                    {"i16", {"i16", "A 16-bit signed integer type.\n\nSize: 2 bytes \nRange: `-32,768` to `32,767`"}},
                    {"u16", {"u16", "A 16-bit unsigned integer type.\n\nSize: 2 bytes \nRange: `0` to `65,535`"}},
                    {"i8", {"i8", "An 8-bit signed integer type.\n\nSize: 1 byte \nRange: `-128` to `127`"}},
                    {"u8", {"u8", "An 8-bit unsigned integer type.\n\nSize: 1 byte \nRange: `0` to `255`"}},
                    {"usize", {"usize", "A pointer-sized unsigned integer type.\n\nSize: platform-dependent (4 or 8 bytes)"}},
                    {"isize", {"isize", "A pointer-sized signed integer type.\n\nSize: platform-dependent (4 or 8 bytes)"}},
                };

                for (const auto &[sfx, info] : suffixes)
                {
                    if (value.size() > sfx.size() &&
                        value.substr(value.size() - sfx.size()) == sfx)
                    {
                        int_type = info.first;
                        int_desc = info.second;
                        break;
                    }
                }

                // Fall back to resolved type if no suffix
                if (int_type.empty() && literal->has_resolved_type())
                {
                    std::string resolved = literal->get_resolved_type()->display_name();
                    if (!resolved.empty() && resolved != "auto" && resolved != "unknown")
                    {
                        // Check if resolved type matches a known integer type
                        for (const auto &[sfx, info] : suffixes)
                        {
                            if (resolved == info.first)
                            {
                                int_type = info.first;
                                int_desc = info.second;
                                break;
                            }
                        }
                        // Also match "int" which is the display name for the default i32
                        if (int_type.empty() && resolved == "int")
                        {
                            int_type = "int";
                            int_desc = "The default signed integer type (32-bit).\n\nSize: 4 bytes \nRange: `-2,147,483,648` to `2,147,483,647`";
                        }
                    }
                }

                // Default: infer minimum unsigned type from the actual value
                if (int_type.empty())
                {
                    // Strip underscores and any trailing suffix remnants for parsing
                    std::string digits;
                    for (char c : value)
                    {
                        if (c != '_')
                            digits += c;
                    }

                    // Parse the numeric value
                    uint64_t num_val = 0;
                    bool parsed = false;
                    try
                    {
                        if (digits.size() > 2 && digits[0] == '0')
                        {
                            if (digits[1] == 'x' || digits[1] == 'X')
                                num_val = std::stoull(digits, nullptr, 16);
                            else if (digits[1] == 'b' || digits[1] == 'B')
                                num_val = std::stoull(digits.substr(2), nullptr, 2);
                            else if (digits[1] == 'o' || digits[1] == 'O')
                                num_val = std::stoull(digits.substr(2), nullptr, 8);
                            else
                                num_val = std::stoull(digits, nullptr, 10);
                        }
                        else
                        {
                            num_val = std::stoull(digits, nullptr, 10);
                        }
                        parsed = true;
                    }
                    catch (...)
                    {
                        parsed = false;
                    }

                    if (parsed)
                    {
                        if (is_negated)
                        {
                            // Negated literal: use minimum signed type that fits
                            if (num_val <= 128)
                            {
                                int_type = "i8";
                                int_desc = "An 8-bit signed integer type.\n\nSize: 1 byte \nRange: `-128` to `127`";
                            }
                            else if (num_val <= 32768)
                            {
                                int_type = "i16";
                                int_desc = "A 16-bit signed integer type.\n\nSize: 2 bytes \nRange: `-32,768` to `32,767`";
                            }
                            else if (num_val <= 2147483648ULL)
                            {
                                int_type = "i32";
                                int_desc = "A 32-bit signed integer type.\n\nSize: 4 bytes \nRange: `-2,147,483,648` to `2,147,483,647`";
                            }
                            else
                            {
                                int_type = "i64";
                                int_desc = "A 64-bit signed integer type.\n\nSize: 8 bytes \nRange: `-9,223,372,036,854,775,808` to `9,223,372,036,854,775,807`";
                            }
                        }
                        else
                        {
                            // Positive literal: use minimum unsigned type that fits
                            if (num_val <= 255)
                            {
                                int_type = "u8";
                                int_desc = "An 8-bit unsigned integer type.\n\nSize: 1 byte \nRange: `0` to `255`";
                            }
                            else if (num_val <= 65535)
                            {
                                int_type = "u16";
                                int_desc = "A 16-bit unsigned integer type.\n\nSize: 2 bytes \nRange: `0` to `65,535`";
                            }
                            else if (num_val <= 4294967295ULL)
                            {
                                int_type = "u32";
                                int_desc = "A 32-bit unsigned integer type.\n\nSize: 4 bytes \nRange: `0` to `4,294,967,295`";
                            }
                            else
                            {
                                int_type = "u64";
                                int_desc = "A 64-bit unsigned integer type.\n\nSize: 8 bytes \nRange: `0` to `18,446,744,073,709,551,615`";
                            }
                        }
                    }
                    else
                    {
                        int_type = "int";
                        int_desc = "The default signed integer type (32-bit).\n\nSize: 4 bytes \nRange: `-2,147,483,648` to `2,147,483,647`";
                    }
                }

                std::string display_val = is_negated ? "-" + value : value;
                return "```cryo\n" + int_type + "(" + display_val + ")" + "\n```\n\n---\n\n" + int_desc;
            }
        }

        return "";
    }

    // ============================================================================
    // Main getHover
    // ============================================================================

    std::optional<Hover> HoverProvider::getHover(const std::string &uri, const Position &position)
    {
        std::string file_path = uri_to_path(uri);
        Transport::log("[Hover] Request for " + file_path + " at " +
                       std::to_string(position.line) + ":" + std::to_string(position.character));

        auto *instance = _engine.getCompilerInstance(file_path);
        if (!instance || !instance->ast_root())
        {
            Transport::log("[Hover] No compiler instance or AST for file");
            return std::nullopt;
        }

        // Find node at cursor position (convert 0-based LSP to 1-based compiler)
        Transport::log("[Hover] Running PositionFinder...");
        PositionFinder finder(position.line + 1, position.character + 1);
        auto found = finder.find(instance->ast_root());
        Transport::log("[Hover] PositionFinder done: kind=" + std::to_string(static_cast<int>(found.kind)) +
                       " identifier='" + found.identifier_name + "'");

        if (found.kind == FoundNode::Kind::Unknown)
        {
            // PositionFinder didn't match any AST node — check if cursor is on a keyword
            auto content = _documents.getContent(uri);
            if (content.has_value())
            {
                std::string word = extractWordAtPosition(content.value(), position.line, position.character);
                if (!word.empty())
                {
                    std::string keyword_hover = getKeywordHover(word);
                    if (!keyword_hover.empty())
                    {
                        Hover hover;
                        hover.contents.kind = "markdown";
                        hover.contents.value = keyword_hover;
                        int word_start = findWordStartColumn(content.value(), position.line, position.character);
                        Range range;
                        range.start.line = position.line;
                        range.start.character = word_start;
                        range.end.line = position.line;
                        range.end.character = word_start + static_cast<int>(word.size());
                        hover.range = range;
                        return hover;
                    }
                }
            }
            return std::nullopt;
        }

        std::string hover_text;

        // Handle literals (42, "hello", true, etc.)
        if (found.kind == FoundNode::Kind::Literal)
        {
            auto *literal = dynamic_cast<Cryo::LiteralNode *>(found.node);
            if (literal)
            {
                // Check if literal is preceded by unary minus (e.g., -42)
                bool is_negated = false;
                if (literal->literal_kind() == Cryo::TokenKind::TK_NUMERIC_CONSTANT)
                {
                    auto content = _documents.getContent(uri);
                    if (content.has_value())
                    {
                        int col = static_cast<int>(found.node->location().column()) - 1; // 0-based
                        int line = static_cast<int>(found.node->location().line()) - 1;
                        // Scan backwards from the literal to find a '-' (skipping whitespace)
                        if (col > 0)
                        {
                            // Find start of the line in the content
                            size_t pos = 0;
                            int cur_line = 0;
                            while (cur_line < line && pos < content->size())
                            {
                                if ((*content)[pos] == '\n')
                                    ++cur_line;
                                ++pos;
                            }
                            // pos is now at start of the target line
                            int check_col = col - 1;
                            while (check_col >= 0 && ((*content)[pos + check_col] == ' ' || (*content)[pos + check_col] == '\t'))
                                --check_col;
                            if (check_col >= 0 && (*content)[pos + check_col] == '-')
                                is_negated = true;
                        }
                    }
                }
                hover_text = formatLiteralHover(literal, is_negated);
            }

            if (!hover_text.empty())
            {
                Hover hover;
                hover.contents.kind = "markdown";
                hover.contents.value = hover_text;
                Range range;
                range.start.line = position.line;
                range.start.character = static_cast<int>(found.node->location().column()) - 1;
                range.end.line = position.line;

                // For string literals, compute source-text length from document content
                // (processed value is shorter than source due to escape sequences)
                int source_len = static_cast<int>(found.identifier_name.size());
                if (literal->literal_kind() == Cryo::TokenKind::TK_STRING_LITERAL ||
                    literal->literal_kind() == Cryo::TokenKind::TK_RAW_STRING_LITERAL)
                {
                    auto content = _documents.getContent(uri);
                    if (content.has_value())
                    {
                        // Find the start of this line
                        size_t pos = 0;
                        int cur_line = 0;
                        while (cur_line < static_cast<int>(found.node->location().line()) - 1 && pos < content->size())
                        {
                            if ((*content)[pos] == '\n')
                                ++cur_line;
                            ++pos;
                        }
                        size_t str_start = pos + found.node->location().column() - 1;
                        // Scan forward from opening quote to find closing quote
                        if (str_start < content->size() && (*content)[str_start] == '"')
                        {
                            size_t i = str_start + 1;
                            while (i < content->size() && (*content)[i] != '"')
                            {
                                if ((*content)[i] == '\\')
                                    ++i; // skip escaped char
                                ++i;
                            }
                            if (i < content->size())
                                source_len = static_cast<int>(i - str_start + 1); // include closing quote
                        }
                    }
                }

                range.end.character = range.start.character + source_len;
                hover.range = range;
                return hover;
            }
            return std::nullopt;
        }

        // Handle 'this' keyword (both identifier references and parameter declarations)
        if (found.identifier_name == "this")
        {
            EnclosingTypeFinder typeFinder(position.line + 1);
            auto typeResult = typeFinder.find(instance->ast_root());
            if (!typeResult.type_name.empty())
            {
                std::string prefix = typeResult.is_mutable ? "mut &this" : "&this";
                hover_text = "```cryo\n" + prefix + ": " + typeResult.type_name + "\n```";

                // Add the type definition below the separator
                Cryo::ASTNode *resolved_type_node = typeResult.type_node;

                // For impl blocks, resolve to the actual type declaration
                if (dynamic_cast<Cryo::ImplementationBlockNode *>(resolved_type_node))
                {
                    DeclarationFinder declFinder(typeResult.type_name);
                    resolved_type_node = declFinder.find(instance->ast_root());
                }

                if (auto *strct = dynamic_cast<Cryo::StructDeclarationNode *>(resolved_type_node))
                    hover_text += "\n\n--- \n\n \n" + formatStructHover(strct);
                else if (auto *cls = dynamic_cast<Cryo::ClassDeclarationNode *>(resolved_type_node))
                    hover_text += "\n\n--- \n\n \n" + formatClassHover(cls);

                Hover hover;
                hover.contents.kind = "markdown";
                hover.contents.value = hover_text;
                Range range;
                range.start.line = position.line;
                range.start.character = found.node ? static_cast<int>(found.node->location().column()) - 1 : position.character;
                range.end.line = position.line;
                range.end.character = range.start.character + 4; // "this" is 4 chars
                hover.range = range;
                return hover;
            }
        }

        // Handle type references - check for primitive types first
        if (found.kind == FoundNode::Kind::TypeReference && !found.identifier_name.empty())
        {
            hover_text = getPrimitiveTypeHover(found.identifier_name);
            // If it's a primitive, return immediately
            if (!hover_text.empty())
            {
                Hover hover;
                hover.contents.kind = "markdown";
                hover.contents.value = hover_text;
                Range range;
                range.start.line = position.line;
                // Type annotations have no AST node - find actual word start from document
                {
                    auto content = _documents.getContent(uri);
                    if (content.has_value())
                        range.start.character = findWordStartColumn(content.value(), position.line, position.character);
                    else
                        range.start.character = position.character;
                }
                range.end.line = position.line;
                range.end.character = range.start.character + static_cast<int>(found.identifier_name.size());
                hover.range = range;
                return hover;
            }
            // Fall through to symbol lookup for non-primitive type references
        }

        // Handle identifiers that might be primitive type names
        if (found.kind == FoundNode::Kind::Identifier && !found.identifier_name.empty())
        {
            hover_text = getPrimitiveTypeHover(found.identifier_name);
            if (!hover_text.empty())
            {
                Hover hover;
                hover.contents.kind = "markdown";
                hover.contents.value = hover_text;
                Range range;
                range.start.line = position.line;
                range.start.character = static_cast<int>(found.node->location().column()) - 1;
                range.end.line = position.line;
                range.end.character = range.start.character + static_cast<int>(found.identifier_name.size());
                hover.range = range;
                return hover;
            }
            // Fall through to normal identifier handling
        }

        if (found.identifier_name.empty())
            return std::nullopt;

        Transport::log("[Hover] Entering switch for kind=" + std::to_string(static_cast<int>(found.kind)));

        // Handle based on found node kind
        switch (found.kind)
        {
        case FoundNode::Kind::FunctionDecl:
        {
            auto *func = dynamic_cast<Cryo::FunctionDeclarationNode *>(found.node);
            if (func)
                hover_text = formatFunctionHover(func);
            break;
        }
        case FoundNode::Kind::StructDecl:
        {
            auto *decl = dynamic_cast<Cryo::StructDeclarationNode *>(found.node);
            if (decl)
                hover_text = formatStructHover(decl);
            break;
        }
        case FoundNode::Kind::ClassDecl:
        {
            auto *decl = dynamic_cast<Cryo::ClassDeclarationNode *>(found.node);
            if (decl)
                hover_text = formatClassHover(decl);
            break;
        }
        case FoundNode::Kind::EnumDecl:
        {
            auto *decl = dynamic_cast<Cryo::EnumDeclarationNode *>(found.node);
            if (decl)
                hover_text = formatEnumHover(decl);
            break;
        }
        case FoundNode::Kind::VariableDecl:
        {
            auto *decl = dynamic_cast<Cryo::VariableDeclarationNode *>(found.node);
            if (decl)
                hover_text = formatVariableHover(decl);
            break;
        }
        case FoundNode::Kind::EnumVariant:
        {
            auto *variant = dynamic_cast<Cryo::EnumVariantNode *>(found.node);
            if (variant)
            {
                hover_text = formatEnumVariantHover(variant);
            }
            else if (auto *pattern = dynamic_cast<Cryo::EnumPatternNode *>(found.node))
            {
                // Enum variant referenced from a match pattern (e.g., "Red" in "Color::Red =>")
                // Find the actual enum declaration and the variant within it
                std::string enum_name = pattern->enum_name();
                // Get the last segment as the actual enum type name
                auto last_sep = enum_name.rfind("::");
                std::string actual_enum = (last_sep != std::string::npos)
                                              ? enum_name.substr(last_sep + 2)
                                              : enum_name;

                DeclarationFinder declFinder(actual_enum);
                Cryo::ASTNode *typeNode = declFinder.find(instance->ast_root());
                if (auto *enm = dynamic_cast<Cryo::EnumDeclarationNode *>(typeNode))
                {
                    for (const auto &v : enm->variants())
                    {
                        if (v && v->name() == found.identifier_name)
                        {
                            hover_text = formatEnumVariantHover(v.get());
                            break;
                        }
                    }
                }

                // Also search other loaded modules if not found locally
                if (hover_text.empty())
                {
                    auto *moduleInstance = _engine.findModuleInstance(
                        (last_sep != std::string::npos) ? enum_name.substr(0, last_sep) : "");
                    if (moduleInstance && moduleInstance->ast_root())
                    {
                        DeclarationFinder modDeclFinder(actual_enum);
                        Cryo::ASTNode *modTypeNode = modDeclFinder.find(moduleInstance->ast_root());
                        if (auto *enm = dynamic_cast<Cryo::EnumDeclarationNode *>(modTypeNode))
                        {
                            for (const auto &v : enm->variants())
                            {
                                if (v && v->name() == found.identifier_name)
                                {
                                    hover_text = formatEnumVariantHover(v.get());
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            break;
        }
        case FoundNode::Kind::Literal:
            // Already handled above
            break;
        case FoundNode::Kind::FieldAccess:
        {
            // Search all structs, classes, and impl blocks for a method/field with this name
            MemberFinder memberFinder(found.identifier_name);
            Cryo::ASTNode *memberNode = memberFinder.find(instance->ast_root());
            if (memberNode)
            {
                if (auto *method = dynamic_cast<Cryo::FunctionDeclarationNode *>(memberNode))
                    hover_text = formatFunctionHover(method);
                else if (auto *field = dynamic_cast<Cryo::StructFieldNode *>(memberNode))
                {
                    std::string type_str = getFieldTypeStr(field);
                    hover_text = "```cryo\n" + memberFinder.owner_name() + "." + field->name() + ": " + type_str + "\n```";
                }
            }

            // Fall back to symbol table if MemberFinder found nothing
            if (hover_text.empty())
            {
                auto *symbol_table = instance->symbol_table();
                Cryo::Symbol *sym = nullptr;
                if (symbol_table)
                {
                    sym = symbol_table->lookup(found.identifier_name);
                    if (!sym)
                        sym = symbol_table->lookup_with_imports(found.identifier_name);
                }
                if (sym)
                    hover_text = formatSymbolRefHover(sym, instance->ast_root());
            }
            break;
        }
        case FoundNode::Kind::ScopeResolution:
        {
            // Cursor is on the member name of a scope resolution (e.g., "new" in "Point::new")
            auto *scope_node = dynamic_cast<Cryo::ScopeResolutionNode *>(found.node);
            if (scope_node)
            {
                std::string scope_name = scope_node->scope_name();
                std::string member_name = found.identifier_name;
                Transport::log("[Hover] ScopeResolution: " + scope_name + "::" + member_name);

                // For chained scope (e.g., "Colors::Color"), the last segment is the type name
                std::string actual_type = scope_name;
                std::string namespace_prefix;
                auto last_sep = scope_name.rfind("::");
                if (last_sep != std::string::npos)
                {
                    actual_type = scope_name.substr(last_sep + 2);
                    namespace_prefix = scope_name.substr(0, last_sep);
                }

                // Find the type declaration for the scope (try local first)
                DeclarationFinder typeFinder(actual_type);
                Cryo::ASTNode *typeNode = typeFinder.find(instance->ast_root());

                // If not found locally and there's a namespace prefix, search the module
                if (!typeNode && !namespace_prefix.empty())
                {
                    auto *moduleInstance = _engine.findModuleInstance(namespace_prefix);
                    if (moduleInstance && moduleInstance->ast_root())
                    {
                        DeclarationFinder modFinder(actual_type);
                        typeNode = modFinder.find(moduleInstance->ast_root());
                    }
                }

                // Search for the member in the type's methods/fields
                if (typeNode)
                {
                    if (auto *strct = dynamic_cast<Cryo::StructDeclarationNode *>(typeNode))
                    {
                        for (const auto &method : strct->methods())
                        {
                            if (method && method->name() == member_name)
                            {
                                hover_text = formatFunctionHover(method.get());
                                break;
                            }
                        }
                        if (hover_text.empty())
                        {
                            for (const auto &field : strct->fields())
                            {
                                if (field && field->name() == member_name)
                                {
                                    hover_text = "```cryo\n" + scope_name + "." + field->name() + ": " + getFieldTypeStr(field.get()) + "\n```";
                                    break;
                                }
                            }
                        }
                    }
                    else if (auto *cls = dynamic_cast<Cryo::ClassDeclarationNode *>(typeNode))
                    {
                        for (const auto &method : cls->methods())
                        {
                            if (method && method->name() == member_name)
                            {
                                hover_text = formatFunctionHover(method.get());
                                break;
                            }
                        }
                        if (hover_text.empty())
                        {
                            for (const auto &field : cls->fields())
                            {
                                if (field && field->name() == member_name)
                                {
                                    hover_text = "```cryo\n" + scope_name + "." + field->name() + ": " + getFieldTypeStr(field.get()) + "\n```";
                                    break;
                                }
                            }
                        }
                    }
                    else if (auto *enm = dynamic_cast<Cryo::EnumDeclarationNode *>(typeNode))
                    {
                        for (const auto &variant : enm->variants())
                        {
                            if (variant && variant->name() == member_name)
                            {
                                hover_text = formatEnumVariantHover(variant.get());
                                break;
                            }
                        }
                    }
                }

                // Also search impl blocks for the member
                if (hover_text.empty())
                {
                    // Search all impl blocks that target the scope type
                    class ImplMemberFinder : public Cryo::BaseASTVisitor
                    {
                    public:
                        ImplMemberFinder(const std::string &type_name, const std::string &member)
                            : _type(type_name), _member(member) {}

                        Cryo::ASTNode *find(Cryo::ASTNode *root)
                        {
                            _result = nullptr;
                            if (root)
                                root->accept(*this);
                            return _result;
                        }

                        void visit(Cryo::ProgramNode &node) override
                        {
                            for (const auto &child : node.statements())
                                if (child && !_result)
                                    child->accept(*this);
                        }

                        void visit(Cryo::DeclarationStatementNode &node) override
                        {
                            if (node.declaration() && !_result)
                                node.declaration()->accept(*this);
                        }

                        void visit(Cryo::ImplementationBlockNode &node) override
                        {
                            if (_result || node.target_type() != _type)
                                return;
                            for (const auto &method : node.method_implementations())
                            {
                                if (method && method->name() == _member)
                                {
                                    _result = method.get();
                                    return;
                                }
                            }
                        }

                    private:
                        std::string _type;
                        std::string _member;
                        Cryo::ASTNode *_result = nullptr;
                    };

                    ImplMemberFinder implFinder(actual_type, member_name);
                    Cryo::ASTNode *implNode = implFinder.find(instance->ast_root());
                    if (auto *method = dynamic_cast<Cryo::FunctionDeclarationNode *>(implNode))
                        hover_text = formatFunctionHover(method);

                    // Also search impl blocks in the module if not found locally
                    if (!implNode && !namespace_prefix.empty())
                    {
                        auto *moduleInstance = _engine.findModuleInstance(namespace_prefix);
                        if (moduleInstance && moduleInstance->ast_root())
                        {
                            ImplMemberFinder modImplFinder(actual_type, member_name);
                            Cryo::ASTNode *modImplNode = modImplFinder.find(moduleInstance->ast_root());
                            if (auto *method = dynamic_cast<Cryo::FunctionDeclarationNode *>(modImplNode))
                                hover_text = formatFunctionHover(method);
                        }
                    }
                }
            }

            // If still empty, fall through to generic lookup
            if (!hover_text.empty())
                break;
            [[fallthrough]];
        }
        case FoundNode::Kind::Identifier:
        case FoundNode::Kind::TypeReference:
        case FoundNode::Kind::ImportDecl:
        case FoundNode::Kind::Parameter:
        default:
        {
            // For type references from annotations, try primitive docs first (already done above)
            // Then try symbol table + AST declaration search
            auto *symbol_table = instance->symbol_table();
            Transport::log("[Hover] Symbol table: " + std::string(symbol_table ? "valid" : "null"));

            Cryo::Symbol *sym = nullptr;
            if (symbol_table)
            {
                Transport::log("[Hover] Looking up '" + found.identifier_name + "' in symbol table...");
                sym = symbol_table->lookup(found.identifier_name);
                Transport::log("[Hover] lookup: " + std::string(sym ? "found" : "not found"));
                if (!sym)
                {
                    Transport::log("[Hover] Trying lookup_with_imports...");
                    sym = symbol_table->lookup_with_imports(found.identifier_name);
                    Transport::log("[Hover] lookup_with_imports: " + std::string(sym ? "found" : "not found"));
                }
            }

            if (sym)
            {
                Transport::log("[Hover] Formatting symbol ref hover...");
                hover_text = formatSymbolRefHover(sym, instance->ast_root());
            }
            else
            {
                // No symbol found - try DeclarationFinder directly on AST
                Transport::log("[Hover] Trying DeclarationFinder...");
                DeclarationFinder declFinder(found.identifier_name);
                Cryo::ASTNode *declNode = declFinder.find(instance->ast_root());
                Transport::log("[Hover] DeclarationFinder: " + std::string(declNode ? "found" : "not found"));
                if (auto *func = dynamic_cast<Cryo::FunctionDeclarationNode *>(declNode))
                    hover_text = formatFunctionHover(func);
                else if (auto *strct = dynamic_cast<Cryo::StructDeclarationNode *>(declNode))
                    hover_text = formatStructHover(strct);
                else if (auto *cls = dynamic_cast<Cryo::ClassDeclarationNode *>(declNode))
                    hover_text = formatClassHover(cls);
                else if (auto *enm = dynamic_cast<Cryo::EnumDeclarationNode *>(declNode))
                    hover_text = formatEnumHover(enm);
                else if (auto *trait = dynamic_cast<Cryo::TraitDeclarationNode *>(declNode))
                    hover_text = formatTraitHover(trait);
                else
                {
                    // Try deep AST search for variable/parameter declarations
                    Transport::log("[Hover] Trying VariableRefResolver...");
                    VariableRefResolver varResolver(found.identifier_name);
                    Cryo::ASTNode *varNode = varResolver.find(instance->ast_root());
                    Transport::log("[Hover] VariableRefResolver: " + std::string(varNode ? "found" : "not found"));
                    if (auto *varDecl = dynamic_cast<Cryo::VariableDeclarationNode *>(varNode))
                        hover_text = formatVariableHover(varDecl);
                }

                // If still not found, try searching other loaded module instances
                // This handles cases like "Color" in "Colors::Color::Red" where Color is defined in Colors module
                if (hover_text.empty() && found.node)
                {
                    std::string module_prefix;

                    // Extract module prefix from the scope resolution or enum pattern context
                    if (auto *scope_node = dynamic_cast<Cryo::ScopeResolutionNode *>(found.node))
                    {
                        // For "Colors::Color::Red", scope_name = "Colors::Color"
                        // If we're looking up "Color", the prefix is "Colors"
                        const std::string &sn = scope_node->scope_name();
                        auto sep = sn.find("::");
                        if (sep != std::string::npos)
                        {
                            // Check if our identifier is a segment after the first "::"
                            size_t search_pos = 0;
                            while (search_pos < sn.size())
                            {
                                auto next_sep = sn.find("::", search_pos);
                                std::string segment = (next_sep != std::string::npos)
                                                          ? sn.substr(search_pos, next_sep - search_pos)
                                                          : sn.substr(search_pos);
                                if (segment == found.identifier_name && search_pos > 0)
                                {
                                    module_prefix = sn.substr(0, search_pos - 2); // before the "::"
                                    break;
                                }
                                if (next_sep == std::string::npos)
                                    break;
                                search_pos = next_sep + 2;
                            }
                        }
                    }
                    else if (auto *pattern = dynamic_cast<Cryo::EnumPatternNode *>(found.node))
                    {
                        const std::string &en = pattern->enum_name();
                        auto sep = en.find("::");
                        if (sep != std::string::npos)
                        {
                            size_t search_pos = 0;
                            while (search_pos < en.size())
                            {
                                auto next_sep = en.find("::", search_pos);
                                std::string segment = (next_sep != std::string::npos)
                                                          ? en.substr(search_pos, next_sep - search_pos)
                                                          : en.substr(search_pos);
                                if (segment == found.identifier_name && search_pos > 0)
                                {
                                    module_prefix = en.substr(0, search_pos - 2);
                                    break;
                                }
                                if (next_sep == std::string::npos)
                                    break;
                                search_pos = next_sep + 2;
                            }
                        }
                    }

                    if (!module_prefix.empty())
                    {
                        auto *moduleInstance = _engine.findModuleInstance(module_prefix);
                        if (moduleInstance && moduleInstance->ast_root())
                        {
                            DeclarationFinder modDeclFinder(found.identifier_name);
                            Cryo::ASTNode *modDeclNode = modDeclFinder.find(moduleInstance->ast_root());
                            if (auto *func = dynamic_cast<Cryo::FunctionDeclarationNode *>(modDeclNode))
                                hover_text = formatFunctionHover(func);
                            else if (auto *strct = dynamic_cast<Cryo::StructDeclarationNode *>(modDeclNode))
                                hover_text = formatStructHover(strct);
                            else if (auto *cls = dynamic_cast<Cryo::ClassDeclarationNode *>(modDeclNode))
                                hover_text = formatClassHover(cls);
                            else if (auto *enm = dynamic_cast<Cryo::EnumDeclarationNode *>(modDeclNode))
                                hover_text = formatEnumHover(enm);
                            else if (auto *trait = dynamic_cast<Cryo::TraitDeclarationNode *>(modDeclNode))
                                hover_text = formatTraitHover(trait);
                        }
                    }
                }
            }
            break;
        }
        }

        // Fallback: try to find identifier as a module/namespace name
        Transport::log("[Hover] After switch, hover_text empty=" + std::string(hover_text.empty() ? "true" : "false"));
        if (hover_text.empty() && !found.identifier_name.empty())
        {
            auto *moduleInstance = _engine.findModuleInstance(found.identifier_name);
            if (moduleInstance && moduleInstance->ast_root())
            {
                hover_text = formatNamespaceHover(found.identifier_name, moduleInstance->ast_root());
            }
        }

        // Fallback: search intrinsics file for the identifier
        if (hover_text.empty() && !found.identifier_name.empty())
        {
            Transport::log("[Hover] Entering intrinsics fallback for '" + found.identifier_name + "'");
            auto *intrinsics = _engine.getIntrinsicsInstance();
            Transport::log("[Hover] getIntrinsicsInstance returned: " + std::string(intrinsics ? "valid" : "null"));
            if (intrinsics)
            {
                auto *intrinsics_ast = intrinsics->ast_root();
                Transport::log("[Hover] intrinsics ast_root: " + std::string(intrinsics_ast ? "valid" : "null"));
                if (intrinsics_ast)
                {
                    Transport::log("[Hover] intrinsics statements count: " + std::to_string(intrinsics_ast->statements().size()));
                    // Direct iteration over ProgramNode children (avoids visitor dispatch)
                    Cryo::ASTNode *intrinsicNode = nullptr;
                    for (const auto &child : intrinsics_ast->statements())
                    {
                        if (!child)
                            continue;
                        if (auto *decl = dynamic_cast<Cryo::IntrinsicDeclarationNode *>(child.get()))
                        {
                            if (decl->name() == found.identifier_name)
                            {
                                intrinsicNode = decl;
                                break;
                            }
                        }
                        else if (auto *func = dynamic_cast<Cryo::FunctionDeclarationNode *>(child.get()))
                        {
                            if (func->name() == found.identifier_name)
                            {
                                intrinsicNode = func;
                                break;
                            }
                        }
                    }

                    if (auto *func = dynamic_cast<Cryo::FunctionDeclarationNode *>(intrinsicNode))
                    {
                        std::string sig = "intrinsic " + buildFunctionSignature(func);
                        hover_text = "```cryo\n" + sig + "\n```";
                        hover_text = appendDocumentation(func, hover_text);
                    }
                    else if (auto *intrinsicDecl = dynamic_cast<Cryo::IntrinsicDeclarationNode *>(intrinsicNode))
                    {
                        // Build signature from IntrinsicDeclarationNode
                        std::string sig = "intrinsic function " + intrinsicDecl->name() + "(";
                        const auto &params = intrinsicDecl->parameters();
                        for (size_t i = 0; i < params.size(); ++i)
                        {
                            if (!params[i])
                                continue;
                            if (i > 0)
                                sig += ", ";
                            // Detect variadic: last param with void type is the parser placeholder for `args...`
                            std::string ptype = getParamTypeStr(params[i].get());
                            if (i == params.size() - 1 && (ptype == "void" || ptype == "..."))
                                sig += params[i]->name() + "...";
                            else
                                sig += params[i]->name() + ": " + ptype;
                        }
                        sig += ")";
                        std::string ret = intrinsicDecl->return_type_annotation();
                        if (!ret.empty() && ret != "void" && ret != "undefined")
                            sig += " -> " + ret;
                        hover_text = "```cryo\n" + sig + "\n```";
                        hover_text = appendDocumentation(intrinsicDecl, hover_text);
                    }
                }
            }
        }

        if (hover_text.empty())
            return std::nullopt;

        Hover hover;
        hover.contents.kind = "markdown";
        hover.contents.value = hover_text;

        // Set range at the found node (node may be nullptr for type annotations)
        Range range;
        range.start.line = position.line;
        if (found.node)
        {
            // For member access, use member_location instead of node location
            auto *member_node = dynamic_cast<Cryo::MemberAccessNode *>(found.node);
            if (member_node && member_node->has_member_location())
                range.start.character = static_cast<int>(member_node->member_location().column()) - 1;
            else if (found.kind == FoundNode::Kind::ScopeResolution)
            {
                // For scope resolution member hover, compute member start column
                auto *scope_node = dynamic_cast<Cryo::ScopeResolutionNode *>(found.node);
                if (scope_node)
                {
                    size_t scope_display_len = scope_node->scope_name().size();
                    if (scope_node->has_generic_args())
                    {
                        scope_display_len += 1;
                        for (size_t i = 0; i < scope_node->generic_args().size(); ++i)
                        {
                            if (i > 0)
                                scope_display_len += 2;
                            scope_display_len += scope_node->generic_args()[i].size();
                        }
                        scope_display_len += 1;
                    }
                    range.start.character = static_cast<int>(scope_node->location().column() - 1 + scope_display_len + 2);
                }
                else
                {
                    range.start.character = position.character;
                }
            }
            else if (found.kind == FoundNode::Kind::EnumVariant && !dynamic_cast<Cryo::EnumVariantNode *>(found.node))
            {
                // Enum variant from a match pattern - find word start from document
                auto content = _documents.getContent(uri);
                if (content.has_value())
                    range.start.character = findWordStartColumn(content.value(), position.line, position.character);
                else
                    range.start.character = position.character;
            }
            else if (found.kind == FoundNode::Kind::TypeReference && !dynamic_cast<Cryo::DeclarationNode *>(found.node))
            {
                // Type reference inside an expression (sizeof, alignof, struct literal, scope resolution, etc.)
                // The node's location points to the expression start, not the type name
                auto content = _documents.getContent(uri);
                if (content.has_value())
                    range.start.character = findWordStartColumn(content.value(), position.line, position.character);
                else
                    range.start.character = position.character;
            }
            else
            {
                // Prefer name_location for declarations (location() points to keywords like 'function')
                Cryo::SourceLocation loc = found.node->location();
                if (auto *decl = dynamic_cast<Cryo::DeclarationNode *>(found.node))
                {
                    if (decl->has_name_location())
                        loc = decl->name_location();
                }
                range.start.character = static_cast<int>(loc.column()) - 1;
            }
        }
        else
        {
            // No AST node (e.g., type annotations) - find actual word start from document
            auto content = _documents.getContent(uri);
            if (content.has_value())
                range.start.character = findWordStartColumn(content.value(), position.line, position.character);
            else
                range.start.character = position.character;
        }
        range.end.line = position.line;
        range.end.character = range.start.character + static_cast<int>(found.identifier_name.size());
        hover.range = range;

        return hover;
    }

} // namespace CryoLSP
