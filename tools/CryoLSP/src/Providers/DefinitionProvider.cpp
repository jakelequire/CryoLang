#include "LSP/Providers/DefinitionProvider.hpp"
#include "LSP/ASTSearchHelpers.hpp"
#include "LSP/PositionFinder.hpp"
#include "LSP/Transport.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "Compiler/ModuleLoader.hpp"
#include "Types/SymbolTable.hpp"
#include "AST/ASTVisitor.hpp"
#include <filesystem>

namespace CryoLSP
{

    DefinitionProvider::DefinitionProvider(AnalysisEngine &engine)
        : _engine(engine) {}

    // ============================================================================
    // buildLocationFromNode - construct an LSP Location from an AST node
    // ============================================================================

    std::optional<Location> DefinitionProvider::buildLocationFromNode(
        Cryo::ASTNode *node, const std::string &name, const std::string &file_path)
    {
        if (!node)
            return std::nullopt;

        Location loc;
        loc.uri = path_to_uri(file_path);

        // Prefer name_location for declaration nodes that have it
        Cryo::SourceLocation srcLoc = node->location();

        if (auto *decl = dynamic_cast<Cryo::DeclarationNode *>(node))
        {
            if (decl->has_name_location())
                srcLoc = decl->name_location();
        }

        loc.range.start.line = static_cast<int>(srcLoc.line() > 0 ? srcLoc.line() - 1 : 0);
        loc.range.start.character = static_cast<int>(srcLoc.column() > 0 ? srcLoc.column() - 1 : 0);
        loc.range.end.line = loc.range.start.line;
        loc.range.end.character = loc.range.start.character + static_cast<int>(name.size());
        return loc;
    }

    // ============================================================================
    // resolveModuleFilePath - resolve a module name to a filesystem path
    // ============================================================================

    std::string DefinitionProvider::resolveModuleFilePath(
        const std::string &module_name, Cryo::CompilerInstance *instance)
    {
        // 1. Try searching open instances by module declaration name
        std::string path = _engine.findModuleFilePath(module_name);
        if (!path.empty())
            return path;

        // 2. Fallback: use the module loader's resolve_import_path
        //    Note: resolve_import_path always returns something (even a non-existent
        //    fallback path), so we must check that the result actually exists.
        if (instance && instance->module_loader())
        {
            std::string resolved = instance->module_loader()->resolve_import_path(module_name);
            if (!resolved.empty() && std::filesystem::exists(resolved))
                return resolved;
        }

        return {};
    }

    // ============================================================================
    // searchDeclaration - search for a declaration across all available sources
    // ============================================================================

    std::optional<Location> DefinitionProvider::searchDeclaration(
        const std::string &name, Cryo::CompilerInstance *instance, const std::string &current_file)
    {
        Cryo::ASTNode *ast_root = instance ? instance->ast_root() : nullptr;

        // 1. Current file: top-level declarations
        if (ast_root)
        {
            DeclarationFinder declFinder(name);
            Cryo::ASTNode *found = declFinder.find(ast_root);
            if (found)
            {
                Transport::log("[Definition] Found declaration in current file AST");
                return buildLocationFromNode(found, name, current_file);
            }
        }

        // 2. Current file: deep variable/parameter search
        if (ast_root)
        {
            VariableRefResolver varResolver(name);
            Cryo::ASTNode *found = varResolver.find(ast_root);
            if (found)
            {
                Transport::log("[Definition] Found variable/parameter in current file");
                return buildLocationFromNode(found, name, current_file);
            }
        }

        // 3. Current file: member search (methods, fields in structs/classes/impl blocks)
        if (ast_root)
        {
            MemberFinder memberFinder(name);
            Cryo::ASTNode *found = memberFinder.find(ast_root);
            if (found)
            {
                Transport::log("[Definition] Found member in current file");
                return buildLocationFromNode(found, name, current_file);
            }
        }

        // 4. Imported module ASTs (from module loader)
        if (instance && instance->module_loader())
        {
            const auto &imported_asts = instance->module_loader()->get_imported_asts();
            for (const auto &[mod_path, mod_ast] : imported_asts)
            {
                if (!mod_ast)
                    continue;

                DeclarationFinder declFinder(name);
                Cryo::ASTNode *found = declFinder.find(mod_ast.get());
                if (found)
                {
                    // Resolve the file path for this module
                    std::string mod_file = resolveModuleFilePath(mod_path, instance);
                    if (mod_file.empty())
                        mod_file = mod_path; // fallback to the key itself
                    Transport::log("[Definition] Found declaration in imported module: " + mod_path);
                    return buildLocationFromNode(found, name, mod_file);
                }

                // Also search members in imported modules
                MemberFinder memberFinder(name);
                found = memberFinder.find(mod_ast.get());
                if (found)
                {
                    std::string mod_file = resolveModuleFilePath(mod_path, instance);
                    if (mod_file.empty())
                        mod_file = mod_path;
                    Transport::log("[Definition] Found member in imported module: " + mod_path);
                    return buildLocationFromNode(found, name, mod_file);
                }
            }
        }

        // 5. Other open instances (for cross-file navigation)
        {
            auto *mod_instance = _engine.findModuleInstance(name);
            if (mod_instance && mod_instance != instance && mod_instance->ast_root())
            {
                // Find the file path for this instance
                std::string mod_file = _engine.findModuleFilePath(name);
                if (!mod_file.empty())
                {
                    DeclarationFinder declFinder(name);
                    Cryo::ASTNode *found = declFinder.find(mod_instance->ast_root());
                    if (found)
                    {
                        Transport::log("[Definition] Found declaration in other open file: " + mod_file);
                        return buildLocationFromNode(found, name, mod_file);
                    }
                }
            }
        }

        // 6. Intrinsics
        auto *intrinsics = _engine.getIntrinsicsInstance();
        if (intrinsics && intrinsics->ast_root())
        {
            DeclarationFinder declFinder(name);
            Cryo::ASTNode *found = declFinder.find(intrinsics->ast_root());
            if (found)
            {
                Transport::log("[Definition] Found declaration in intrinsics");
                return buildLocationFromNode(found, name, _engine.getIntrinsicsFilePath());
            }
        }

        return std::nullopt;
    }

    // ============================================================================
    // getDefinition - main entry point with kind-based dispatch
    // ============================================================================

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
        PositionFinder finder(position.line + 1, position.character + 1);
        auto found = finder.find(instance->ast_root());
        Transport::log("[Definition] PositionFinder: identifier='" + found.identifier_name +
                       "' kind=" + std::to_string(static_cast<int>(found.kind)));

        if (!found.node || found.identifier_name.empty())
            return std::nullopt;

        // Strip generic args for lookup: "Array<Token>" -> "Array"
        // Strip pointer/reference modifiers: "FloatType*" -> "FloatType"
        std::string lookup_name = stripTypeModifiers(stripGenericArgs(found.identifier_name));

        using Kind = FoundNode::Kind;

        switch (found.kind)
        {
        // ---- Cursor is ON a declaration: the found node IS the definition ----
        case Kind::FunctionDecl:
        case Kind::StructDecl:
        case Kind::ClassDecl:
        case Kind::EnumDecl:
        case Kind::VariableDecl:
        case Kind::Parameter:
        case Kind::EnumVariant:
        case Kind::PatternBinding:
            Transport::log("[Definition] Cursor on declaration node");
            return buildLocationFromNode(found.node, lookup_name, file_path);

        // ---- Import / Module declaration: navigate to the imported/declared module file ----
        case Kind::ImportDecl:
        {
            Transport::log("[Definition] Import/module declaration");
            std::string import_path;
            if (auto *import_node = dynamic_cast<Cryo::ImportDeclarationNode *>(found.node))
                import_path = import_node->module_path();
            else if (auto *module_node = dynamic_cast<Cryo::ModuleDeclarationNode *>(found.node))
                import_path = module_node->module_path();
            else
                return std::nullopt;
            Transport::log("[Definition] Import module_path: '" + import_path + "'");

            // Try to resolve the import to a file path
            if (instance->module_loader())
            {
                std::string resolved = instance->module_loader()->resolve_import_path(import_path);
                Transport::log("[Definition] resolve_import_path returned: '" + resolved +
                               "' exists=" + (std::filesystem::exists(resolved) ? "Y" : "N"));
                if (!resolved.empty() && std::filesystem::exists(resolved))
                {
                    Transport::log("[Definition] Import resolved to: " + resolved);
                    Location loc;
                    loc.uri = path_to_uri(resolved);
                    loc.range.start.line = 0;
                    loc.range.start.character = 0;
                    loc.range.end.line = 0;
                    loc.range.end.character = 0;
                    return loc;
                }
            }

            // Fallback: try open instances
            std::string mod_file = _engine.findModuleFilePath(import_path);
            if (!mod_file.empty())
            {
                Location loc;
                loc.uri = path_to_uri(mod_file);
                loc.range.start.line = 0;
                loc.range.start.character = 0;
                loc.range.end.line = 0;
                loc.range.end.character = 0;
                return loc;
            }

            return std::nullopt;
        }

        // ---- Type reference: search for type declaration ----
        case Kind::TypeReference:
        {
            Transport::log("[Definition] Type reference: " + lookup_name);
            return searchDeclaration(lookup_name, instance, file_path);
        }

        // ---- Scope resolution: e.g., Color::RED or Point::new ----
        case Kind::ScopeResolution:
        {
            auto *scope_node = dynamic_cast<Cryo::ScopeResolutionNode *>(found.node);
            if (!scope_node)
            {
                // Fallback to generic search
                return searchDeclaration(lookup_name, instance, file_path);
            }

            std::string scope_name = stripGenericArgs(scope_node->scope_name());
            std::string member_name = scope_node->member_name();
            Transport::log("[Definition] Scope resolution: " + scope_name + "::" + member_name);

            // Search for the member within the scoped type
            // Check current file first
            Cryo::ASTNode *ast_root = instance->ast_root();

            ScopedMemberFinder scopedFinder(scope_name, member_name);
            Cryo::ASTNode *member_node = scopedFinder.find(ast_root);
            if (member_node)
            {
                Transport::log("[Definition] Found scoped member in current file");
                return buildLocationFromNode(member_node, member_name, file_path);
            }

            // Search imported modules
            if (instance->module_loader())
            {
                const auto &imported_asts = instance->module_loader()->get_imported_asts();
                for (const auto &[mod_path, mod_ast] : imported_asts)
                {
                    if (!mod_ast)
                        continue;

                    ScopedMemberFinder importedScopedFinder(scope_name, member_name);
                    Cryo::ASTNode *found_node = importedScopedFinder.find(mod_ast.get());
                    if (found_node)
                    {
                        std::string mod_file = resolveModuleFilePath(mod_path, instance);
                        if (mod_file.empty())
                            mod_file = mod_path;
                        Transport::log("[Definition] Found scoped member in imported module: " + mod_path);
                        return buildLocationFromNode(found_node, member_name, mod_file);
                    }
                }
            }

            // Search intrinsics
            auto *intrinsics = _engine.getIntrinsicsInstance();
            if (intrinsics && intrinsics->ast_root())
            {
                ScopedMemberFinder intrScopedFinder(scope_name, member_name);
                Cryo::ASTNode *found_node = intrScopedFinder.find(intrinsics->ast_root());
                if (found_node)
                {
                    Transport::log("[Definition] Found scoped member in intrinsics");
                    return buildLocationFromNode(found_node, member_name, _engine.getIntrinsicsFilePath());
                }
            }

            // Fallback: try searching just the member name
            return searchDeclaration(member_name, instance, file_path);
        }

        // ---- Field access: e.g., obj.field_name ----
        case Kind::FieldAccess:
        {
            Transport::log("[Definition] Field access: " + lookup_name);

            // Search for the member across all types
            Cryo::ASTNode *ast_root = instance->ast_root();

            MemberFinder memberFinder(lookup_name);
            Cryo::ASTNode *member_node = memberFinder.find(ast_root);
            if (member_node)
            {
                Transport::log("[Definition] Found field/method in current file");
                return buildLocationFromNode(member_node, lookup_name, file_path);
            }

            // Search imported modules
            if (instance->module_loader())
            {
                const auto &imported_asts = instance->module_loader()->get_imported_asts();
                for (const auto &[mod_path, mod_ast] : imported_asts)
                {
                    if (!mod_ast)
                        continue;

                    MemberFinder importedMemberFinder(lookup_name);
                    Cryo::ASTNode *found_node = importedMemberFinder.find(mod_ast.get());
                    if (found_node)
                    {
                        std::string mod_file = resolveModuleFilePath(mod_path, instance);
                        if (mod_file.empty())
                            mod_file = mod_path;
                        Transport::log("[Definition] Found field/method in imported module: " + mod_path);
                        return buildLocationFromNode(found_node, lookup_name, mod_file);
                    }
                }
            }

            // Fallback to general search
            return searchDeclaration(lookup_name, instance, file_path);
        }

        // ---- Field initializer: e.g., field name in `Point { x: 1 }` ----
        case Kind::FieldInitializer:
        {
            auto *struct_lit = dynamic_cast<Cryo::StructLiteralNode *>(found.node);
            if (!struct_lit)
            {
                // Fallback
                return searchDeclaration(lookup_name, instance, file_path);
            }

            std::string struct_type = stripGenericArgs(struct_lit->struct_type());
            std::string field_name = found.identifier_name;
            Transport::log("[Definition] Field initializer: " + struct_type + "." + field_name);

            // Search for the field within the struct declaration
            Cryo::ASTNode *ast_root = instance->ast_root();

            ScopedMemberFinder scopedFinder(struct_type, field_name);
            Cryo::ASTNode *field_node = scopedFinder.find(ast_root);
            if (field_node)
            {
                Transport::log("[Definition] Found field in struct declaration");
                return buildLocationFromNode(field_node, field_name, file_path);
            }

            // Search imported modules
            if (instance->module_loader())
            {
                const auto &imported_asts = instance->module_loader()->get_imported_asts();
                for (const auto &[mod_path, mod_ast] : imported_asts)
                {
                    if (!mod_ast)
                        continue;

                    ScopedMemberFinder importedScopedFinder(struct_type, field_name);
                    Cryo::ASTNode *found_node = importedScopedFinder.find(mod_ast.get());
                    if (found_node)
                    {
                        std::string mod_file = resolveModuleFilePath(mod_path, instance);
                        if (mod_file.empty())
                            mod_file = mod_path;
                        Transport::log("[Definition] Found field in imported module: " + mod_path);
                        return buildLocationFromNode(found_node, field_name, mod_file);
                    }
                }
            }

            return std::nullopt;
        }

        // ---- Identifier (default): symbol table + AST search ----
        case Kind::Identifier:
        default:
        {
            // Skip literals
            if (found.kind == Kind::Literal || found.kind == Kind::Unknown)
                return std::nullopt;

            Transport::log("[Definition] Identifier lookup: " + lookup_name);

            auto *symbol_table = instance->symbol_table();
            if (symbol_table)
            {
                Cryo::Symbol *sym = symbol_table->lookup(lookup_name);
                if (!sym)
                    sym = symbol_table->lookup_with_imports(lookup_name);

                if (sym)
                {
                    // Determine the correct file for this symbol
                    std::string sym_file = file_path;

                    // If the symbol has a scope (module namespace), resolve the correct file
                    if (!sym->scope.empty())
                    {
                        std::string module_file = resolveModuleFilePath(sym->scope, instance);
                        if (!module_file.empty())
                        {
                            sym_file = module_file;
                            Transport::log("[Definition] Resolved imported symbol to file: " + sym_file);
                        }
                    }

                    Location loc;
                    loc.uri = path_to_uri(sym_file);
                    loc.range.start.line = static_cast<int>(sym->location.line() > 0 ? sym->location.line() - 1 : 0);
                    loc.range.start.character = static_cast<int>(sym->location.column() > 0 ? sym->location.column() - 1 : 0);
                    loc.range.end.line = loc.range.start.line;
                    loc.range.end.character = loc.range.start.character + static_cast<int>(sym->name.size());
                    return loc;
                }
            }

            // Fallback to AST-based search
            return searchDeclaration(lookup_name, instance, file_path);
        }
        }
    }

} // namespace CryoLSP
