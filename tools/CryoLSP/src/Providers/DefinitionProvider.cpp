#include "LSP/Providers/DefinitionProvider.hpp"
#include "LSP/PositionFinder.hpp"
#include "LSP/Transport.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "Types/SymbolTable.hpp"
#include "AST/ASTVisitor.hpp"

namespace CryoLSP
{

    DefinitionProvider::DefinitionProvider(AnalysisEngine &engine)
        : _engine(engine) {}

    std::optional<Location> DefinitionProvider::getDefinition(const std::string &uri, const Position &position)
    {
        std::string file_path = uri_to_path(uri);
        Transport::log("[Definition] Request for " + file_path + " at " +
                       std::to_string(position.line) + ":" + std::to_string(position.character));

        auto *instance = _engine.getCompilerInstance(file_path);
        if (!instance || !instance->ast_root())
        {
            Transport::log("[Definition] No compiler instance or AST for file");
            return std::nullopt;
        }

        // Find node at cursor
        Transport::log("[Definition] Running PositionFinder...");
        PositionFinder finder(position.line + 1, position.character + 1);
        auto found = finder.find(instance->ast_root());
        Transport::log("[Definition] PositionFinder done: identifier='" + found.identifier_name + "'");

        if (!found.node || found.identifier_name.empty())
            return std::nullopt;

        auto *symbol_table = instance->symbol_table();

        // Look up symbol in the current file's symbol table
        Cryo::Symbol *sym = nullptr;
        if (symbol_table)
        {
            Transport::log("[Definition] Looking up symbol in symbol table...");
            sym = symbol_table->lookup(found.identifier_name);
            if (!sym)
                sym = symbol_table->lookup_with_imports(found.identifier_name);
            Transport::log("[Definition] Symbol table lookup: " + std::string(sym ? "found" : "not found"));
        }

        if (sym)
        {
            // Build location from symbol
            Location loc;
            std::string sym_file = file_path;
            loc.uri = path_to_uri(sym_file);
            loc.range.start.line = static_cast<int>(sym->location.line() > 0 ? sym->location.line() - 1 : 0);
            loc.range.start.character = static_cast<int>(sym->location.column() > 0 ? sym->location.column() - 1 : 0);
            loc.range.end.line = loc.range.start.line;
            loc.range.end.character = loc.range.start.character + static_cast<int>(sym->name.size());
            return loc;
        }

        // AST-based search: find declarations directly in the AST
        {
            Transport::log("[Definition] Trying AST-based declaration search...");
            Cryo::ASTNode *ast_root = instance->ast_root();

            // Helper lambda to build a Location from an AST node
            auto buildLocationFromNode = [&](Cryo::ASTNode *node, const std::string &name) -> std::optional<Location>
            {
                if (!node)
                    return std::nullopt;

                Location loc;
                loc.uri = path_to_uri(file_path);

                // Prefer name_location for declarations that have it
                Cryo::SourceLocation srcLoc = node->location();
                if (auto *func = dynamic_cast<Cryo::FunctionDeclarationNode *>(node))
                {
                    if (func->has_name_location())
                        srcLoc = func->name_location();
                }
                else if (auto *strct = dynamic_cast<Cryo::StructDeclarationNode *>(node))
                {
                    if (strct->has_name_location())
                        srcLoc = strct->name_location();
                }
                else if (auto *cls = dynamic_cast<Cryo::ClassDeclarationNode *>(node))
                {
                    if (cls->has_name_location())
                        srcLoc = cls->name_location();
                }
                else if (auto *enm = dynamic_cast<Cryo::EnumDeclarationNode *>(node))
                {
                    if (enm->has_name_location())
                        srcLoc = enm->name_location();
                }
                else if (auto *trait = dynamic_cast<Cryo::TraitDeclarationNode *>(node))
                {
                    if (trait->has_name_location())
                        srcLoc = trait->name_location();
                }
                else if (auto *alias = dynamic_cast<Cryo::TypeAliasDeclarationNode *>(node))
                {
                    if (alias->has_name_location())
                        srcLoc = alias->name_location();
                }
                else if (auto *varDecl = dynamic_cast<Cryo::VariableDeclarationNode *>(node))
                {
                    if (varDecl->has_name_location())
                        srcLoc = varDecl->name_location();
                }

                loc.range.start.line = static_cast<int>(srcLoc.line() > 0 ? srcLoc.line() - 1 : 0);
                loc.range.start.character = static_cast<int>(srcLoc.column() > 0 ? srcLoc.column() - 1 : 0);
                loc.range.end.line = loc.range.start.line;
                loc.range.end.character = loc.range.start.character + static_cast<int>(name.size());
                return loc;
            };

            // 1. Search top-level declarations (functions, structs, classes, enums, traits)
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

            DeclarationFinder declFinder(found.identifier_name);
            Cryo::ASTNode *declNode = declFinder.find(ast_root);
            if (declNode)
            {
                Transport::log("[Definition] Found declaration in AST");
                auto loc = buildLocationFromNode(declNode, found.identifier_name);
                if (loc.has_value())
                    return loc;
            }

            // 2. Deep search for variable/parameter declarations
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
                void visit(Cryo::VariableDeclarationNode &node) override
                {
                    if (node.name() == _target)
                        _result = &node;
                }
                void visit(Cryo::FunctionDeclarationNode &node) override
                {
                    for (const auto &param : node.parameters())
                        if (param && !_result && param->name() == _target)
                            _result = param.get();
                    if (node.body() && !_result)
                        node.body()->accept(*this);
                }
                void visit(Cryo::StructMethodNode &node) override
                {
                    for (const auto &param : node.parameters())
                        if (param && !_result && param->name() == _target)
                            _result = param.get();
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

            VariableRefResolver varResolver(found.identifier_name);
            Cryo::ASTNode *varNode = varResolver.find(ast_root);
            if (varNode)
            {
                Transport::log("[Definition] Found variable/parameter declaration in AST");
                auto loc = buildLocationFromNode(varNode, found.identifier_name);
                if (loc.has_value())
                    return loc;
            }

            // 3. Search methods/fields in structs, classes, impl blocks
            class MemberFinder : public Cryo::BaseASTVisitor
            {
            public:
                MemberFinder(const std::string &name) : _target(name) {}
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
                void visit(Cryo::StructDeclarationNode &node) override
                {
                    if (_result)
                        return;
                    for (const auto &method : node.methods())
                        if (method && method->name() == _target)
                        {
                            _result = method.get();
                            return;
                        }
                    for (const auto &field : node.fields())
                        if (field && field->name() == _target)
                        {
                            _result = field.get();
                            return;
                        }
                }
                void visit(Cryo::ClassDeclarationNode &node) override
                {
                    if (_result)
                        return;
                    for (const auto &method : node.methods())
                        if (method && method->name() == _target)
                        {
                            _result = method.get();
                            return;
                        }
                    for (const auto &field : node.fields())
                        if (field && field->name() == _target)
                        {
                            _result = field.get();
                            return;
                        }
                }
                void visit(Cryo::ImplementationBlockNode &node) override
                {
                    if (_result)
                        return;
                    for (const auto &method : node.method_implementations())
                        if (method && method->name() == _target)
                        {
                            _result = method.get();
                            return;
                        }
                    for (const auto &field : node.field_implementations())
                        if (field && field->name() == _target)
                        {
                            _result = field.get();
                            return;
                        }
                }

            private:
                std::string _target;
                Cryo::ASTNode *_result = nullptr;
            };

            // For qualified names like "Point::new", search for the member part
            std::string memberName = found.identifier_name;
            auto colonPos = memberName.rfind("::");
            if (colonPos != std::string::npos)
                memberName = memberName.substr(colonPos + 2);

            MemberFinder memberFinder(memberName);
            Cryo::ASTNode *memberNode = memberFinder.find(ast_root);
            if (memberNode)
            {
                Transport::log("[Definition] Found member declaration in AST");
                auto loc = buildLocationFromNode(memberNode, memberName);
                if (loc.has_value())
                    return loc;
            }
        }

        // Fallback: search intrinsics file
        auto *intrinsics = _engine.getIntrinsicsInstance();

        if (intrinsics && intrinsics->ast_root())
        {
            // Direct iteration over ProgramNode children (avoids visitor dispatch)
            Cryo::ASTNode *declNode = nullptr;
            for (const auto &child : intrinsics->ast_root()->statements())
            {
                if (!child)
                    continue;
                if (auto *decl = dynamic_cast<Cryo::IntrinsicDeclarationNode *>(child.get()))
                {
                    if (decl->name() == found.identifier_name)
                    {
                        declNode = decl;
                        break;
                    }
                }
                else if (auto *func = dynamic_cast<Cryo::FunctionDeclarationNode *>(child.get()))
                {
                    if (func->name() == found.identifier_name)
                    {
                        declNode = func;
                        break;
                    }
                }
            }

            if (declNode)
            {
                Location loc;
                loc.uri = path_to_uri(_engine.getIntrinsicsFilePath());

                // Use name_location for FunctionDeclarationNode, or location for IntrinsicDeclarationNode
                auto *funcDecl = dynamic_cast<Cryo::FunctionDeclarationNode *>(declNode);
                auto *intrinsicDecl = dynamic_cast<Cryo::IntrinsicDeclarationNode *>(declNode);

                Cryo::SourceLocation srcLoc = declNode->location();
                if (funcDecl && funcDecl->has_name_location())
                    srcLoc = funcDecl->name_location();
                else if (intrinsicDecl)
                    srcLoc = intrinsicDecl->location();

                loc.range.start.line = static_cast<int>(srcLoc.line() > 0 ? srcLoc.line() - 1 : 0);
                loc.range.start.character = static_cast<int>(srcLoc.column() > 0 ? srcLoc.column() - 1 : 0);
                loc.range.end.line = loc.range.start.line;
                loc.range.end.character = loc.range.start.character + static_cast<int>(found.identifier_name.size());
                return loc;
            }
        }

        Transport::log("[Definition] No definition found");
        return std::nullopt;
    }

} // namespace CryoLSP
