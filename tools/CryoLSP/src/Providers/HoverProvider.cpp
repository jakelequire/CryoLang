#include "LSP/Providers/HoverProvider.hpp"
#include "LSP/PositionFinder.hpp"
#include "LSP/Transport.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "AST/ASTVisitor.hpp"
#include "Types/TypeAnnotation.hpp"
#include "Lexer/lexer.hpp"
#include <unordered_map>

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

    HoverProvider::HoverProvider(AnalysisEngine &engine)
        : _engine(engine) {}

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
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (i > 0)
                sig += ", ";
            sig += params[i]->name() + ": " + getParamTypeStr(params[i].get());
        }
        if (func->is_variadic())
        {
            if (!params.empty())
                sig += ", ";
            sig += "...";
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
        return result;
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
            {"boolean", {"bool", "The boolean type.\n\nCan be either `true` or `false`. \nSize: 1 byte"}},

            // String and char
            {"string", {"string", "A UTF-8 encoded string type.\n\nStrings are heap-allocated and growable. \nString literals like `\"hello\"` create values of this type."}},
            {"char", {"char", "A Unicode scalar value.\n\nRepresents a single Unicode character. \nSize: 4 bytes \nRange: `U+0000` to `U+10FFFF` (excluding surrogates)"}},

            // Void and unit
            {"void", {"void", "The void type.\n\nUsed as a function return type to indicate the function returns no value."}},

            // Special types
            {"()", {"()", "The unit type.\n\nRepresents an empty value. \nUsed when a function returns no meaningful value but you want to indicate it returns successfully."}},

            // Boolean literals (in case they're parsed as identifiers)
            {"true", {"boolean", "The boolean type.\n\nCan be either `true` or `false`. \nSize: 1 byte\n\nValue: `true`"}},
            {"false", {"boolean", "The boolean type.\n\nCan be either `true` or `false`. \nSize: 1 byte\n\nValue: `false`"}},
        };

        auto it = primitives.find(name);
        if (it == primitives.end())
            return "";

        const auto &[display_name, description] = it->second;
        return "```cryo\n" + display_name + "\n```\n\n---\n\n" + description;
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

    std::string HoverProvider::formatLiteralHover(Cryo::LiteralNode *literal)
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

        if (lk == Cryo::TokenKind::TK_BOOLEAN_LITERAL || lk == Cryo::TokenKind::TK_KW_BOOLEAN ||
            lk == Cryo::TokenKind::TK_KW_TRUE || lk == Cryo::TokenKind::TK_KW_FALSE)
        {
            return "```cryo\nboolean\n```\n\n---\n\n"
                   "The boolean type.\n\n"
                   "Value: `" +
                   value + "`";
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
                return "```cryo\nfloat\n```\n\n---\n\n"
                       "The default floating-point type (64-bit double precision).\n\n"
                       "Numeric literal: `" +
                       value + "`";
            }
            else
            {
                return "```cryo\nint\n```\n\n---\n\n"
                       "The default signed integer type (32-bit).\n\n"
                       "Numeric literal: `" +
                       value + "`";
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
        auto *instance = _engine.getCompilerInstance(file_path);
        if (!instance || !instance->ast_root())
            return std::nullopt;

        // Find node at cursor position (convert 0-based LSP to 1-based compiler)
        PositionFinder finder(position.line + 1, position.character + 1);
        auto found = finder.find(instance->ast_root());

        if (found.kind == FoundNode::Kind::Unknown)
            return std::nullopt;

        std::string hover_text;

        // Handle literals (42, "hello", true, etc.)
        if (found.kind == FoundNode::Kind::Literal)
        {
            auto *literal = dynamic_cast<Cryo::LiteralNode *>(found.node);
            if (literal)
                hover_text = formatLiteralHover(literal);

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
                range.start.character = position.character;
                range.end.line = position.line;
                range.end.character = position.character + static_cast<int>(found.identifier_name.size());
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
                hover_text = formatEnumVariantHover(variant);
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
        case FoundNode::Kind::Identifier:
        case FoundNode::Kind::ScopeResolution:
        case FoundNode::Kind::TypeReference:
        case FoundNode::Kind::ImportDecl:
        case FoundNode::Kind::Parameter:
        default:
        {
            // For type references from annotations, try primitive docs first (already done above)
            // Then try symbol table + AST declaration search
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
            else
            {
                // No symbol found - try DeclarationFinder directly on AST
                DeclarationFinder declFinder(found.identifier_name);
                Cryo::ASTNode *declNode = declFinder.find(instance->ast_root());
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
                    VariableRefResolver varResolver(found.identifier_name);
                    Cryo::ASTNode *varNode = varResolver.find(instance->ast_root());
                    if (auto *varDecl = dynamic_cast<Cryo::VariableDeclarationNode *>(varNode))
                        hover_text = formatVariableHover(varDecl);
                }
            }
            break;
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
            else
                range.start.character = static_cast<int>(found.node->location().column()) - 1;
        }
        else
        {
            range.start.character = position.character;
        }
        range.end.line = position.line;
        range.end.character = range.start.character + static_cast<int>(found.identifier_name.size());
        hover.range = range;

        return hover;
    }

} // namespace CryoLSP
