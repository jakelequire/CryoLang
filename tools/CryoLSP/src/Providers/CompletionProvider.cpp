#include "LSP/Providers/CompletionProvider.hpp"
#include "LSP/Transport.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "AST/ASTVisitor.hpp"
#include "Types/SymbolTable.hpp"
#include "Types/TypeAnnotation.hpp"

#include <sstream>
#include <algorithm>

namespace CryoLSP
{

    // ============================================================================
    // AST Walkers (local to this TU)
    // ============================================================================

    // Walks top-level AST to find a type declaration by name
    class TypeDeclFinder : public Cryo::BaseASTVisitor
    {
    public:
        TypeDeclFinder(const std::string &name) : _target(name) {}

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

        void visit(Cryo::ImplementationBlockNode &node) override
        {
            if (node.target_type() == _target)
                _result = &node;
        }

    private:
        std::string _target;
        Cryo::ASTNode *_result = nullptr;
    };

    // Collects ALL impl block methods/fields for a given target type name
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

        void collect(Cryo::ASTNode *root)
        {
            methods.clear();
            fields.clear();
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

    // Deep AST walker to find a variable/parameter declaration and extract its type name.
    // This is needed because in LSP mode (parse-only), the symbol table doesn't contain
    // local variables — only top-level declarations are registered.
    class VariableTypeFinder : public Cryo::BaseASTVisitor
    {
    public:
        VariableTypeFinder(const std::string &name) : _target(name) {}

        // Returns the type annotation string for the variable, or "" if not found.
        std::string find(Cryo::ASTNode *root)
        {
            _type_name.clear();
            _found = false;
            if (root)
                root->accept(*this);
            return _type_name;
        }

        void visit(Cryo::ProgramNode &node) override
        {
            for (const auto &child : node.statements())
                if (child && !_found)
                    child->accept(*this);
        }

        void visit(Cryo::DeclarationStatementNode &node) override
        {
            if (node.declaration() && !_found)
                node.declaration()->accept(*this);
        }

        void visit(Cryo::BlockStatementNode &node) override
        {
            for (const auto &stmt : node.statements())
                if (stmt && !_found)
                    stmt->accept(*this);
        }

        void visit(Cryo::ExpressionStatementNode &) override {}

        void visit(Cryo::VariableDeclarationNode &node) override
        {
            if (_found)
                return;
            if (node.name() == _target)
            {
                if (node.has_type_annotation())
                    _type_name = node.type_annotation()->to_string();
                _found = true;
            }
        }

        void visit(Cryo::FunctionDeclarationNode &node) override
        {
            if (_found)
                return;
            // Check parameters
            for (const auto &param : node.parameters())
            {
                if (param && !_found && param->name() == _target)
                {
                    if (param->has_type_annotation())
                        _type_name = param->type_annotation()->to_string();
                    _found = true;
                    return;
                }
            }
            // Recurse into body
            if (node.body() && !_found)
                node.body()->accept(*this);
        }

        void visit(Cryo::StructMethodNode &node) override
        {
            if (_found)
                return;
            for (const auto &param : node.parameters())
            {
                if (param && !_found && param->name() == _target)
                {
                    if (param->has_type_annotation())
                        _type_name = param->type_annotation()->to_string();
                    _found = true;
                    return;
                }
            }
            if (node.body() && !_found)
                node.body()->accept(*this);
        }

        void visit(Cryo::StructDeclarationNode &node) override
        {
            if (_found)
                return;
            for (const auto &method : node.methods())
                if (method && !_found)
                    method->accept(*this);
        }

        void visit(Cryo::ClassDeclarationNode &node) override
        {
            if (_found)
                return;
            for (const auto &method : node.methods())
                if (method && !_found)
                    method->accept(*this);
        }

        void visit(Cryo::ImplementationBlockNode &node) override
        {
            if (_found)
                return;
            for (const auto &method : node.method_implementations())
                if (method && !_found)
                    method->accept(*this);
        }

        void visit(Cryo::IfStatementNode &node) override
        {
            if (_found)
                return;
            if (node.then_statement())
                node.then_statement()->accept(*this);
            if (!_found && node.else_statement())
                node.else_statement()->accept(*this);
        }

        void visit(Cryo::ForStatementNode &node) override
        {
            if (_found)
                return;
            if (node.init())
                node.init()->accept(*this);
            if (!_found && node.body())
                node.body()->accept(*this);
        }

        void visit(Cryo::WhileStatementNode &node) override
        {
            if (_found)
                return;
            if (node.body())
                node.body()->accept(*this);
        }

    private:
        std::string _target;
        std::string _type_name;
        bool _found = false;
    };

    // ============================================================================
    // Helpers
    // ============================================================================

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

    // Extract the line text up to the cursor column from document content
    static std::string getLineUpToCursor(const std::string &content, int line, int column)
    {
        std::istringstream stream(content);
        std::string currentLine;
        int currentLineNum = 0;

        while (std::getline(stream, currentLine))
        {
            if (currentLineNum == line)
            {
                if (column >= 0 && column <= static_cast<int>(currentLine.size()))
                    return currentLine.substr(0, column);
                return currentLine;
            }
            currentLineNum++;
        }
        return "";
    }

    // Scan backwards from end of linePrefix to find the identifier before the trigger
    static std::string extractIdentifierBefore(const std::string &linePrefix, size_t pos)
    {
        if (pos == 0)
            return "";

        // Skip backwards past whitespace (shouldn't be any for `.` but just in case)
        size_t end = pos;

        // Scan backwards over identifier characters
        size_t start = end;
        while (start > 0)
        {
            char c = linePrefix[start - 1];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
                --start;
            else
                break;
        }

        if (start == end)
            return "";

        return linePrefix.substr(start, end - start);
    }

    // ============================================================================
    // CompletionProvider Implementation
    // ============================================================================

    CompletionProvider::CompletionProvider(AnalysisEngine &engine, DocumentStore &documents)
        : _engine(engine), _documents(documents) {}

    CompletionList CompletionProvider::getCompletions(const std::string &uri, const Position &position,
                                                      const std::string &triggerCharacter)
    {
        CompletionList result;
        result.isIncomplete = false;

        std::string contextIdentifier;
        CompletionContext ctx = detectContext(uri, position, triggerCharacter, contextIdentifier);

        Transport::log("Completion context: " +
                       std::string(ctx == CompletionContext::MemberAccess    ? "MemberAccess"
                                   : ctx == CompletionContext::ScopeResolution ? "ScopeResolution"
                                                                              : "General") +
                       " identifier: '" + contextIdentifier + "'");

        switch (ctx)
        {
        case CompletionContext::MemberAccess:
            getMemberCompletions(contextIdentifier, uri, result.items);
            break;

        case CompletionContext::ScopeResolution:
            getScopeCompletions(contextIdentifier, uri, result.items);
            break;

        case CompletionContext::General:
            getGeneralCompletions(uri, position, result.items);
            break;
        }

        return result;
    }

    // ============================================================================
    // Context Detection
    // ============================================================================

    CompletionContext CompletionProvider::detectContext(const std::string &uri, const Position &position,
                                                       const std::string &triggerCharacter,
                                                       std::string &contextIdentifier)
    {
        contextIdentifier.clear();

        // Fast path: check triggerCharacter first
        if (triggerCharacter == ".")
        {
            // Get line text to extract identifier before the dot
            auto content = _documents.getContent(uri);
            if (content.has_value())
            {
                std::string linePrefix = getLineUpToCursor(content.value(), position.line, position.character);
                // The trigger char '.' is at position.character - 1 (already typed)
                // We need the identifier before it
                if (!linePrefix.empty() && linePrefix.back() == '.')
                {
                    contextIdentifier = extractIdentifierBefore(linePrefix, linePrefix.size() - 1);
                }
                else
                {
                    // Cursor is right after '.', linePrefix might not include it
                    contextIdentifier = extractIdentifierBefore(linePrefix, linePrefix.size());
                }
            }
            if (!contextIdentifier.empty())
                return CompletionContext::MemberAccess;
        }

        if (triggerCharacter == ":")
        {
            auto content = _documents.getContent(uri);
            if (content.has_value())
            {
                std::string linePrefix = getLineUpToCursor(content.value(), position.line, position.character);
                // Check for '::' pattern — the ':' trigger fires on the second ':'
                if (linePrefix.size() >= 2 && linePrefix.substr(linePrefix.size() - 2) == "::")
                {
                    contextIdentifier = extractIdentifierBefore(linePrefix, linePrefix.size() - 2);
                    if (!contextIdentifier.empty())
                        return CompletionContext::ScopeResolution;
                }
            }
        }

        // No explicit trigger — analyze the line text
        auto content = _documents.getContent(uri);
        if (content.has_value())
        {
            std::string linePrefix = getLineUpToCursor(content.value(), position.line, position.character);

            // Scan backwards for '.' or '::'
            for (int i = static_cast<int>(linePrefix.size()) - 1; i >= 0; --i)
            {
                char c = linePrefix[i];

                // Skip identifier chars (the user might be partially typing after . or ::)
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
                    continue;

                if (c == '.')
                {
                    contextIdentifier = extractIdentifierBefore(linePrefix, i);
                    if (!contextIdentifier.empty())
                        return CompletionContext::MemberAccess;
                    break;
                }

                if (c == ':' && i > 0 && linePrefix[i - 1] == ':')
                {
                    contextIdentifier = extractIdentifierBefore(linePrefix, i - 1);
                    if (!contextIdentifier.empty())
                        return CompletionContext::ScopeResolution;
                    break;
                }

                // Any other non-identifier character — stop scanning
                break;
            }
        }

        return CompletionContext::General;
    }

    // ============================================================================
    // Member Access Completions (dot-completion)
    // ============================================================================

    void CompletionProvider::getMemberCompletions(const std::string &varName, const std::string &uri,
                                                  std::vector<CompletionItem> &items)
    {
        std::string file_path = uri_to_path(uri);
        auto *instance = _engine.getCompilerInstance(file_path);
        if (!instance)
            return;

        // Resolve the variable name to a type name
        std::string typeName;

        // 1) Try symbol table lookup (works when semantic analysis has run)
        if (instance->symbol_table())
        {
            auto *sym = instance->symbol_table()->lookup(varName);
            if (!sym)
                sym = instance->symbol_table()->lookup_with_imports(varName);

            if (sym && sym->type.is_valid())
            {
                const auto *type = sym->type.get();
                if (type)
                {
                    if (sym->kind == Cryo::SymbolKind::Type)
                        typeName = sym->name;
                    else
                        typeName = type->display_name();
                }
            }
        }

        // 2) Fallback: walk the AST for variable/parameter declarations (needed in
        //    LSP parse-only mode where the symbol table lacks local variables)
        if (typeName.empty() && instance->ast_root())
        {
            VariableTypeFinder varFinder(varName);
            typeName = varFinder.find(instance->ast_root());
        }

        if (typeName.empty())
        {
            Transport::log("Could not resolve type for variable: " + varName);
            return;
        }

        Transport::log("Resolved '" + varName + "' to type: " + typeName);

        // Strip any reference/pointer wrappers from the type name for lookup
        // e.g., "&Point" -> "Point", "*Point" -> "Point"
        std::string baseTypeName = typeName;
        while (!baseTypeName.empty() && (baseTypeName[0] == '&' || baseTypeName[0] == '*'))
            baseTypeName = baseTypeName.substr(1);
        // Also handle "mut " prefix
        if (baseTypeName.substr(0, 4) == "mut ")
            baseTypeName = baseTypeName.substr(4);

        // Try to find the declaration in the AST
        if (!instance->ast_root())
            return;

        TypeDeclFinder finder(baseTypeName);
        Cryo::ASTNode *declNode = finder.find(instance->ast_root());

        // Helper lambda to add a method completion item
        auto addMethodItem = [&](Cryo::StructMethodNode *method, bool skipStatic)
        {
            if (!method)
                return;
            if (skipStatic && method->is_static())
                return;
            if (method->is_destructor())
                return;

            CompletionItem item;
            item.label = method->name();
            item.kind = CompletionItemKind::Method;
            item.sortText = "0_" + method->name();

            // Build detail string
            std::string detail;
            if (method->is_static())
                detail += "static ";
            detail += "fn " + method->name() + "(";
            std::vector<std::string> paramNames;
            const auto &params = method->parameters();
            bool first = true;
            for (const auto &param : params)
            {
                if (!param)
                    continue;
                if (param->name() == "this")
                    continue;
                if (!first)
                    detail += ", ";
                first = false;
                detail += param->name() + ": " + getParamTypeStr(param.get());
                paramNames.push_back(param->name());
            }
            detail += ")";
            std::string ret = getReturnTypeStr(method);
            if (!ret.empty())
                detail += " -> " + ret;
            item.detail = detail;

            // Snippet
            if (!paramNames.empty())
            {
                item.insertText = buildFunctionSnippet(method->name(), paramNames);
                item.insertTextFormat = 2; // Snippet
            }
            else
            {
                item.insertText = method->name() + "()";
            }

            item.filterText = method->name();
            items.push_back(std::move(item));
        };

        // Helper lambda to add a field completion item
        auto addFieldItem = [&](Cryo::StructFieldNode *field)
        {
            if (!field)
                return;

            CompletionItem item;
            item.label = field->name();
            item.kind = CompletionItemKind::Field;
            item.sortText = "0_" + field->name();
            item.detail = getFieldTypeStr(field);
            items.push_back(std::move(item));
        };

        if (auto *structDecl = dynamic_cast<Cryo::StructDeclarationNode *>(declNode))
        {
            for (const auto &field : structDecl->fields())
                addFieldItem(field.get());
            for (const auto &method : structDecl->methods())
                addMethodItem(method.get(), true); // skip static
        }
        else if (auto *classDecl = dynamic_cast<Cryo::ClassDeclarationNode *>(declNode))
        {
            for (const auto &field : classDecl->fields())
                addFieldItem(field.get());
            for (const auto &method : classDecl->methods())
                addMethodItem(method.get(), true); // skip static
        }

        // Also collect from impl blocks for this type
        ImplBlockCollector collector(baseTypeName);
        collector.collect(instance->ast_root());
        for (const auto &entry : collector.fields)
            addFieldItem(entry.field);
        for (const auto &entry : collector.methods)
            addMethodItem(entry.method, true); // skip static for dot-access
    }

    // ============================================================================
    // Scope Resolution Completions (Type::)
    // ============================================================================

    void CompletionProvider::getScopeCompletions(const std::string &typeName, const std::string &uri,
                                                 std::vector<CompletionItem> &items)
    {
        std::string file_path = uri_to_path(uri);
        auto *instance = _engine.getCompilerInstance(file_path);
        if (!instance || !instance->ast_root())
            return;

        TypeDeclFinder finder(typeName);
        Cryo::ASTNode *declNode = finder.find(instance->ast_root());

        // Helper for adding static methods
        auto addStaticMethodItem = [&](Cryo::StructMethodNode *method)
        {
            if (!method)
                return;
            if (!method->is_static() && !method->is_constructor())
                return;

            CompletionItem item;
            item.label = method->name();
            item.kind = method->is_constructor() ? CompletionItemKind::Constructor : CompletionItemKind::Method;
            item.sortText = "0_" + method->name();

            std::string detail;
            if (method->is_static())
                detail += "static ";
            detail += "fn " + method->name() + "(";
            std::vector<std::string> paramNames;
            const auto &params = method->parameters();
            bool first = true;
            for (const auto &param : params)
            {
                if (!param)
                    continue;
                if (param->name() == "this")
                    continue;
                if (!first)
                    detail += ", ";
                first = false;
                detail += param->name() + ": " + getParamTypeStr(param.get());
                paramNames.push_back(param->name());
            }
            detail += ")";
            std::string ret = getReturnTypeStr(method);
            if (!ret.empty())
                detail += " -> " + ret;
            item.detail = detail;

            if (!paramNames.empty())
            {
                item.insertText = buildFunctionSnippet(method->name(), paramNames);
                item.insertTextFormat = 2;
            }
            else
            {
                item.insertText = method->name() + "()";
            }

            item.filterText = method->name();
            items.push_back(std::move(item));
        };

        if (auto *structDecl = dynamic_cast<Cryo::StructDeclarationNode *>(declNode))
        {
            for (const auto &method : structDecl->methods())
                addStaticMethodItem(method.get());
        }
        else if (auto *classDecl = dynamic_cast<Cryo::ClassDeclarationNode *>(declNode))
        {
            for (const auto &method : classDecl->methods())
                addStaticMethodItem(method.get());
        }
        else if (auto *enumDecl = dynamic_cast<Cryo::EnumDeclarationNode *>(declNode))
        {
            // Add all enum variants
            for (const auto &variant : enumDecl->variants())
            {
                if (!variant)
                    continue;

                CompletionItem item;
                item.label = variant->name();
                item.kind = CompletionItemKind::EnumMember;
                item.sortText = "0_" + variant->name();

                if (!variant->is_simple_variant())
                {
                    std::string detail = variant->name() + "(";
                    std::vector<std::string> paramNames;
                    const auto &types = variant->associated_types();
                    for (size_t i = 0; i < types.size(); ++i)
                    {
                        if (i > 0)
                            detail += ", ";
                        detail += types[i];
                        paramNames.push_back("value" + std::to_string(i + 1));
                    }
                    detail += ")";
                    item.detail = detail;

                    // Snippet for variants with payloads
                    item.insertText = buildFunctionSnippet(variant->name(), paramNames);
                    item.insertTextFormat = 2;
                }
                else if (variant->has_explicit_value())
                {
                    item.detail = variant->name() + " = " + std::to_string(variant->explicit_value());
                }
                else
                {
                    item.detail = "variant";
                }

                items.push_back(std::move(item));
            }
        }

        // Also collect static methods from impl blocks
        ImplBlockCollector collector(typeName);
        collector.collect(instance->ast_root());
        for (const auto &entry : collector.methods)
            addStaticMethodItem(entry.method);
    }

    // ============================================================================
    // General Completions
    // ============================================================================

    void CompletionProvider::getGeneralCompletions(const std::string &uri, const Position &position,
                                                   std::vector<CompletionItem> &items)
    {
        std::string file_path = uri_to_path(uri);
        auto *instance = _engine.getCompilerInstance(file_path);

        if (instance && instance->symbol_table())
        {
            auto symbols = instance->symbol_table()->get_all_symbols_for_lsp();

            for (const auto &sym : symbols)
            {
                CompletionItem item;
                item.label = sym.name;

                std::string sortPrefix;

                switch (sym.kind)
                {
                case Cryo::SymbolKind::Variable:
                case Cryo::SymbolKind::Parameter:
                    item.kind = CompletionItemKind::Variable;
                    sortPrefix = "0_";
                    break;
                case Cryo::SymbolKind::Constant:
                    item.kind = CompletionItemKind::Constant;
                    sortPrefix = "0_";
                    break;
                case Cryo::SymbolKind::Function:
                    item.kind = CompletionItemKind::Function;
                    sortPrefix = "1_";
                    break;
                case Cryo::SymbolKind::Method:
                    item.kind = CompletionItemKind::Method;
                    sortPrefix = "1_";
                    break;
                case Cryo::SymbolKind::Type:
                    item.kind = CompletionItemKind::Class;
                    sortPrefix = "2_";
                    break;
                case Cryo::SymbolKind::TypeAlias:
                    item.kind = CompletionItemKind::Class;
                    sortPrefix = "2_";
                    break;
                case Cryo::SymbolKind::Field:
                    item.kind = CompletionItemKind::Field;
                    sortPrefix = "0_";
                    break;
                case Cryo::SymbolKind::EnumVariant:
                    item.kind = CompletionItemKind::EnumMember;
                    sortPrefix = "2_";
                    break;
                case Cryo::SymbolKind::GenericParam:
                    item.kind = CompletionItemKind::TypeParameter;
                    sortPrefix = "2_";
                    break;
                case Cryo::SymbolKind::Namespace:
                case Cryo::SymbolKind::Import:
                    item.kind = CompletionItemKind::Module;
                    sortPrefix = "2_";
                    break;
                case Cryo::SymbolKind::Intrinsic:
                    item.kind = CompletionItemKind::Function;
                    sortPrefix = "1_";
                    break;
                default:
                    item.kind = CompletionItemKind::Text;
                    sortPrefix = "3_";
                    break;
                }

                item.sortText = sortPrefix + sym.name;

                if (sym.type.is_valid())
                    item.detail = sym.type->display_name();

                if (!sym.documentation.empty())
                    item.documentation = sym.documentation;

                // Generate snippets for function-like symbols
                if (sym.kind == Cryo::SymbolKind::Function || sym.kind == Cryo::SymbolKind::Intrinsic)
                {
                    // Try to get parameter info from the AST
                    if (instance->ast_root())
                    {
                        TypeDeclFinder declFinder(sym.name);
                        Cryo::ASTNode *declNode = declFinder.find(instance->ast_root());
                        // TypeDeclFinder only finds types, not functions — so check directly
                        // We'll use the function type info instead
                    }
                    // Use insertText = name() for functions (without snippet, as we don't
                    // know params reliably from just the symbol table)
                }

                items.push_back(std::move(item));
            }
        }

        addKeywords(items);
        addBuiltinTypes(items);
    }

    // ============================================================================
    // Snippet Builder
    // ============================================================================

    std::string CompletionProvider::buildFunctionSnippet(const std::string &name,
                                                         const std::vector<std::string> &paramNames)
    {
        std::string snippet = name + "(";
        for (size_t i = 0; i < paramNames.size(); ++i)
        {
            if (i > 0)
                snippet += ", ";
            snippet += "${" + std::to_string(i + 1) + ":" + paramNames[i] + "}";
        }
        snippet += ")";
        return snippet;
    }

    // ============================================================================
    // Keywords + Builtin Types (with sortText)
    // ============================================================================

    void CompletionProvider::addKeywords(std::vector<CompletionItem> &items)
    {
        static const std::vector<std::string> keywords = {
            "fn", "function", "let", "const", "mut", "if", "else", "while",
            "for", "return", "struct", "class", "enum", "impl", "trait",
            "match", "import", "from", "pub", "public", "private",
            "true", "false", "null", "new", "self", "type", "as",
            "break", "continue", "do", "switch", "case", "default",
            "unsafe", "extern", "static", "inline", "where",
        };

        for (const auto &kw : keywords)
        {
            CompletionItem item;
            item.label = kw;
            item.kind = CompletionItemKind::Keyword;
            item.sortText = "3_" + kw;
            items.push_back(std::move(item));
        }
    }

    void CompletionProvider::addBuiltinTypes(std::vector<CompletionItem> &items)
    {
        static const std::vector<std::string> types = {
            "int", "i8", "i16", "i32", "i64", "i128",
            "uint", "u8", "u16", "u32", "u64", "u128",
            "float", "f32", "f64",
            "bool", "char", "string", "void",
        };

        for (const auto &t : types)
        {
            CompletionItem item;
            item.label = t;
            item.kind = CompletionItemKind::Class;
            item.detail = "built-in type";
            item.sortText = "4_" + t;
            items.push_back(std::move(item));
        }
    }

} // namespace CryoLSP
