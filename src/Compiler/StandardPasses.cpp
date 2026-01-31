#include "Compiler/StandardPasses.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "Compiler/ModuleLoader.hpp"
#include "AST/ASTContext.hpp"
#include "AST/ASTNode.hpp"
#include "AST/TemplateRegistry.hpp"
#include "AST/DirectiveSystem.hpp"
#include "Types/SymbolTable.hpp"
#include "Types/TypeChecker.hpp"
#include "Types/TypeResolver.hpp"
#include "Types/TypeAnnotation.hpp"
#include "Types/GenericRegistry.hpp"
#include "Types/GenericTypes.hpp"
#include "Types/CompoundTypes.hpp"
#include "Types/UserDefinedTypes.hpp"
#include "Types/ModuleTypeRegistry.hpp"
#include "Types/Monomorphizer.hpp"
#include "Codegen/CodeGenerator.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "Diagnostics/Diag.hpp"
#include "Utils/Logger.hpp"

#include <algorithm>
#include <sstream>

namespace Cryo
{
    // ============================================================================
    // Stage 1: Frontend Passes
    // ============================================================================

    LexingPass::LexingPass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult LexingPass::run(PassContext &ctx)
    {
        // Run the lexing phase on the compiler instance
        if (!_compiler.run_lexing_phase())
        {
            ctx.emit_error(ErrorCode::E0011_LEXING_EXCEPTION,
                           "Lexing phase failed");
            return PassResult::failure();
        }

        LOG_DEBUG(LogComponent::GENERAL, "LexingPass: Lexer created successfully");
        return PassResult::ok({PassProvides::TOKENS});
    }

    ParsingPass::ParsingPass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult ParsingPass::run(PassContext &ctx)
    {
        // Run the parsing phase on the compiler instance
        if (!_compiler.run_parsing_phase())
        {
            ctx.emit_error(ErrorCode::E0116_PARSE_EXCEPTION,
                           "Parsing phase failed");
            return PassResult::failure();
        }

        // Update the PassContext with the newly created AST root
        ctx.set_ast_root(_compiler.ast_root());

        // Note: We don't check ctx.has_errors() here because in stdlib compilation mode,
        // errors accumulate across modules. If THIS module's parsing failed,
        // run_parsing_phase() already returned false above. Checking has_errors()
        // would cause all modules after the first failure to fail immediately.

        LOG_DEBUG(LogComponent::GENERAL, "ParsingPass: AST successfully created");
        return PassResult::ok({PassProvides::AST});
    }

    ASTValidationPass::ASTValidationPass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult ASTValidationPass::run(PassContext &ctx)
    {
        auto *program = _compiler.ast_root();
        if (!program)
        {
            ctx.emit_error(ErrorCode::E0900_INTERNAL_COMPILER_ERROR,
                           "Cannot validate null AST");
            return PassResult::failure();
        }

        // Basic validation - check that we have a ProgramNode with statements
        if (program->statements().empty())
        {
            ctx.emit_warning(ErrorCode::W0009_DEAD_CODE, "Program has no statements");
            // Empty programs are valid, just warn
        }

        LOG_DEBUG(LogComponent::GENERAL, "ASTValidationPass: AST validated with {} top-level statements",
                  program->statements().size());

        return PassResult::ok({PassProvides::AST_VALIDATED});
    }

    // ============================================================================
    // Stage 2: Module Resolution Passes
    // ============================================================================

    AutoImportPass::AutoImportPass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult AutoImportPass::run(PassContext &ctx)
    {
        // Run the auto-import phase on the compiler instance
        _compiler.run_auto_import_phase();

        LOG_DEBUG(LogComponent::GENERAL, "AutoImportPass: Auto-imports processed");
        return PassResult::ok({PassProvides::IMPORTS_DISCOVERED});
    }

    ImportResolutionPass::ImportResolutionPass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult ImportResolutionPass::run(PassContext &ctx)
    {
        // Import resolution is currently integrated into collect_declarations_pass
        // This pass marks that module loading is complete

        LOG_DEBUG(LogComponent::GENERAL, "ImportResolutionPass: Modules loaded and resolved");
        return PassResult::ok({PassProvides::MODULES_LOADED, PassProvides::MODULE_ORDER});
    }

    // ============================================================================
    // Stage 3: Declaration Collection Passes
    // ============================================================================

    TypeDeclarationPass::TypeDeclarationPass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult TypeDeclarationPass::run(PassContext &ctx)
    {
        // Run the declaration collection phase on the compiler instance
        // This collects all declarations: types, functions, and templates
        _compiler.run_declaration_collection_phase();

        LOG_DEBUG(LogComponent::GENERAL, "TypeDeclarationPass: Type declarations collected");
        return PassResult::ok({PassProvides::TYPE_DECLARATIONS});
    }

    FunctionSignaturePass::FunctionSignaturePass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult FunctionSignaturePass::run(PassContext &ctx)
    {
        // Function signatures were collected in TypeDeclarationPass (collect_declarations_pass)
        // This pass marks the function signature portion as complete
        LOG_DEBUG(LogComponent::GENERAL, "FunctionSignaturePass: Function signatures collected");
        return PassResult::ok({PassProvides::FUNCTION_SIGNATURES});
    }

    TemplateRegistrationPass::TemplateRegistrationPass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult TemplateRegistrationPass::run(PassContext &ctx)
    {
        // Templates were registered in TypeDeclarationPass (collect_declarations_pass)
        // This pass marks the template registration portion as complete
        LOG_DEBUG(LogComponent::GENERAL, "TemplateRegistrationPass: Templates registered");
        return PassResult::ok({PassProvides::TEMPLATES_REGISTERED});
    }

    // ============================================================================
    // Stage 4: Type Resolution Passes
    // ============================================================================

    // ========================================================================
    // Helper: Propagate expected type to expression for type inference
    // ========================================================================

    static void propagate_type_to_expression(
        ExpressionNode *expr,
        TypeRef expected_type,
        size_t &resolved_count)
    {
        if (!expr || !expected_type.is_valid())
            return;

        // If the expression already has a resolved type, don't override
        if (expr->has_resolved_type())
            return;

        // Skip types that contain unresolved generic parameters (like Option<T>)
        // These will be handled during generic instantiation when we know the concrete type
        if (contains_generic_params(expected_type))
        {
            LOG_DEBUG(LogComponent::GENERAL,
                "TypeResolutionPass: Skipping propagation of generic type '{}' (contains unresolved params)",
                expected_type->display_name());
            return;
        }

        // Set the resolved type on the expression
        expr->set_resolved_type(expected_type);
        resolved_count++;

        LOG_DEBUG(LogComponent::GENERAL,
            "TypeResolutionPass: Propagated type '{}' to expression (kind={})",
            expected_type->display_name(), static_cast<int>(expr->kind()));
    }

    // ========================================================================
    // Helper: Check if a type needs resolution
    // A type needs resolution if:
    // 1. It's not valid
    // 2. It's an error type (unresolved generic, undefined type, etc.)
    // 3. It's a placeholder struct that was created for a type alias but hasn't been
    //    resolved to the actual underlying type yet
    // ========================================================================

    static bool type_needs_resolution(TypeRef current_type, const TypeAnnotation *ann)
    {
        // No type at all - definitely needs resolution
        if (!current_type.is_valid())
            return true;

        // Error types always need resolution
        if (current_type.is_error())
            return true;

        // If we have an annotation, check if the current type is a placeholder
        // that matches the annotation name (meaning it's a type alias that
        // hasn't been resolved to its target type yet)
        if (ann && current_type->kind() == TypeKind::Struct)
        {
            std::string current_name = current_type->display_name();
            std::string ann_name = ann->name;

            // Check for generic annotation names too (e.g., "Option<Layout>")
            if (ann->kind == TypeAnnotationKind::Generic && ann->inner)
            {
                ann_name = ann->inner->name;
            }

            // If the annotation is a generic type (e.g., "IoResult<PathBuf>")
            // extract the base name
            size_t angle_pos = ann_name.find('<');
            if (angle_pos != std::string::npos)
            {
                ann_name = ann_name.substr(0, angle_pos);
            }

            // If the current type name matches the annotation name exactly,
            // it's likely a placeholder struct that needs resolution
            // (A properly resolved type alias would have the target type, not the alias name)
            if (current_name == ann->name || current_name == ann_name)
            {
                LOG_DEBUG(LogComponent::GENERAL,
                    "TypeResolutionPass: Type '{}' appears to be a placeholder for annotation '{}', needs resolution",
                    current_name, ann->to_string());
                return true;
            }
        }

        return false;
    }

    // ========================================================================
    // Helper: Resolve variable types in function/method bodies
    // ========================================================================

    static void resolve_variable_types_in_statement(
        StatementNode *stmt,
        TypeResolver &resolver,
        ResolutionContext &ctx,
        TypeRef expected_return_type,
        size_t &resolved_count,
        size_t &error_count)
    {
        if (!stmt)
            return;

        // Handle return statements - propagate function's return type to expression
        if (auto *return_stmt = dynamic_cast<ReturnStatementNode *>(stmt))
        {
            if (return_stmt->expression() && expected_return_type.is_valid())
            {
                propagate_type_to_expression(return_stmt->expression(), expected_return_type, resolved_count);
            }
            return;
        }

        // Check if this is a variable declaration
        if (auto *var_decl = dynamic_cast<VariableDeclarationNode *>(stmt))
        {
            const auto *ann = var_decl->type_annotation();
            if (ann && (!var_decl->get_resolved_type().is_valid() || var_decl->get_resolved_type().is_error()))
            {
                TypeRef resolved = resolver.resolve(*ann, ctx);
                if (!resolved.is_error())
                {
                    var_decl->set_resolved_type(resolved);
                    resolved_count++;
                    LOG_DEBUG(LogComponent::GENERAL,
                        "TypeResolutionPass: Resolved local variable '{}' type to '{}'",
                        var_decl->name(), resolved->display_name());
                }
                else
                {
                    error_count++;
                    LOG_DEBUG(LogComponent::GENERAL,
                        "TypeResolutionPass: Failed to resolve local variable '{}' type: {}",
                        var_decl->name(), resolved->display_name());
                }
            }
        }
        // Handle declaration statements (variable declarations wrapped in statement nodes)
        else if (auto *decl_stmt = dynamic_cast<DeclarationStatementNode *>(stmt))
        {
            // Extract the inner declaration and process if it's a variable declaration
            if (auto *var_decl = dynamic_cast<VariableDeclarationNode *>(decl_stmt->declaration()))
            {
                const auto *ann = var_decl->type_annotation();
                if (ann && (!var_decl->get_resolved_type().is_valid() || var_decl->get_resolved_type().is_error()))
                {
                    TypeRef resolved = resolver.resolve(*ann, ctx);
                    if (!resolved.is_error())
                    {
                        var_decl->set_resolved_type(resolved);
                        resolved_count++;
                        LOG_DEBUG(LogComponent::GENERAL,
                            "TypeResolutionPass: Resolved local variable '{}' type to '{}'",
                            var_decl->name(), resolved->display_name());
                    }
                    else
                    {
                        error_count++;
                        LOG_DEBUG(LogComponent::GENERAL,
                            "TypeResolutionPass: Failed to resolve local variable '{}' type: {}",
                            var_decl->name(), resolved->display_name());
                    }
                }
            }
        }
        // Recursively process block statements
        else if (auto *block = dynamic_cast<BlockStatementNode *>(stmt))
        {
            for (const auto &child : block->statements())
            {
                resolve_variable_types_in_statement(child.get(), resolver, ctx, expected_return_type, resolved_count, error_count);
            }
        }
        // Process if statement branches
        else if (auto *if_stmt = dynamic_cast<IfStatementNode *>(stmt))
        {
            if (if_stmt->then_statement())
                resolve_variable_types_in_statement(if_stmt->then_statement(), resolver, ctx, expected_return_type, resolved_count, error_count);
            if (if_stmt->else_statement())
                resolve_variable_types_in_statement(if_stmt->else_statement(), resolver, ctx, expected_return_type, resolved_count, error_count);
        }
        // Process while loop body
        else if (auto *while_stmt = dynamic_cast<WhileStatementNode *>(stmt))
        {
            if (while_stmt->body())
                resolve_variable_types_in_statement(while_stmt->body(), resolver, ctx, expected_return_type, resolved_count, error_count);
        }
        // Process for loop body
        else if (auto *for_stmt = dynamic_cast<ForStatementNode *>(stmt))
        {
            if (for_stmt->body())
                resolve_variable_types_in_statement(for_stmt->body(), resolver, ctx, expected_return_type, resolved_count, error_count);
        }
        // Process match statement arms
        else if (auto *match_stmt = dynamic_cast<MatchStatementNode *>(stmt))
        {
            for (const auto &arm : match_stmt->arms())
            {
                if (arm->body())
                    resolve_variable_types_in_statement(arm->body(), resolver, ctx, expected_return_type, resolved_count, error_count);
            }
        }
    }

    TypeResolutionPass::TypeResolutionPass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult TypeResolutionPass::run(PassContext &ctx)
    {
        auto *program = _compiler.ast_root();
        if (!program)
        {
            LOG_DEBUG(LogComponent::GENERAL, "TypeResolutionPass: No AST to process");
            return PassResult::ok({PassProvides::TYPES_RESOLVED});
        }

        // Get the components we need
        auto *ast_ctx = _compiler.ast_context();
        if (!ast_ctx)
        {
            LOG_DEBUG(LogComponent::GENERAL, "TypeResolutionPass: No AST context");
            return PassResult::ok({PassProvides::TYPES_RESOLVED});
        }

        TypeArena &arena = ast_ctx->types();
        ModuleTypeRegistry &module_registry = ast_ctx->modules();

        auto *gen_reg = _compiler.generic_registry();
        if (!gen_reg)
        {
            LOG_DEBUG(LogComponent::GENERAL, "TypeResolutionPass: No generic registry");
            return PassResult::ok({PassProvides::TYPES_RESOLVED});
        }
        GenericRegistry &generic_registry = *gen_reg;

        // Create the TypeResolver
        TypeResolver resolver(arena, module_registry, generic_registry, ctx.diagnostics());

        LOG_DEBUG(LogComponent::GENERAL, "TypeResolutionPass: GenericRegistry has {} templates",
            generic_registry.template_count());

        // Set up resolution context with the actual module identity
        ResolutionContext res_ctx;
        auto *symbols = _compiler.symbol_table();
        if (symbols)
        {
            res_ctx.current_module = symbols->current_module();
            LOG_DEBUG(LogComponent::GENERAL,
                "TypeResolutionPass: Using module ID {} from SymbolTable",
                res_ctx.current_module.id);
        }

        // Register all imported type symbols from the SymbolTable into the
        // ModuleTypeRegistry under the current module. This makes imported
        // non-generic types (e.g., IoError, PathBuf) discoverable by
        // resolve_with_imports which checks the current module first.
        // Generic templates (e.g., IoResult<T>, Option<T>) are intentionally
        // skipped — they are resolved via GenericRegistry::get_template_by_name.
        if (symbols && res_ctx.current_module.is_valid())
        {
            size_t registered_imports = 0;
            symbols->for_each_symbol([&](const Symbol &sym) {
                if (sym.kind == SymbolKind::Type && sym.type.is_valid() && !sym.type.is_error())
                {
                    // Skip types that are generic templates — those are handled
                    // by the GenericRegistry and must use its TypeRef to avoid
                    // TypeID mismatches with is_template().
                    if (generic_registry.get_template_by_name(sym.name))
                        return;

                    module_registry.register_type(res_ctx.current_module, sym.name, sym.type);
                    registered_imports++;
                }
            });
            LOG_DEBUG(LogComponent::GENERAL,
                "TypeResolutionPass: Registered {} imported type symbols in module {}",
                registered_imports, res_ctx.current_module.id);
        }

        size_t resolved_count = 0;
        size_t error_count = 0;

        // === PHASE 1: Pre-register ALL types in ModuleTypeRegistry ===
        // This ensures forward references work (e.g., Layout using LayoutExtend before it's defined)
        LOG_DEBUG(LogComponent::GENERAL, "TypeResolutionPass: Phase 1 - Pre-registering all types for forward reference support");
        for (auto &stmt : program->statements())
        {
            // Pre-register struct types
            if (auto *struct_decl = dynamic_cast<StructDeclarationNode *>(stmt.get()))
            {
                TypeRef struct_type = symbols->lookup_struct_type(struct_decl->name());
                if (struct_type.is_valid())
                {
                    module_registry.register_type(res_ctx.current_module, struct_decl->name(), struct_type);
                    LOG_DEBUG(LogComponent::GENERAL,
                        "TypeResolutionPass: Pre-registered struct '{}' in module registry",
                        struct_decl->name());
                }
            }
            // Pre-register class types
            else if (auto *class_decl = dynamic_cast<ClassDeclarationNode *>(stmt.get()))
            {
                TypeRef class_type = symbols->lookup_class_type(class_decl->name());
                if (class_type.is_valid())
                {
                    module_registry.register_type(res_ctx.current_module, class_decl->name(), class_type);
                    LOG_DEBUG(LogComponent::GENERAL,
                        "TypeResolutionPass: Pre-registered class '{}' in module registry",
                        class_decl->name());
                }
            }
            // Pre-register enum types
            else if (auto *enum_decl = dynamic_cast<EnumDeclarationNode *>(stmt.get()))
            {
                TypeRef enum_type = symbols->lookup_enum_type(enum_decl->name());
                if (enum_type.is_valid())
                {
                    module_registry.register_type(res_ctx.current_module, enum_decl->name(), enum_type);
                    LOG_DEBUG(LogComponent::GENERAL,
                        "TypeResolutionPass: Pre-registered enum '{}' in module registry",
                        enum_decl->name());
                }
            }
            // Pre-register type aliases
            else if (auto *alias_decl = dynamic_cast<TypeAliasDeclarationNode *>(stmt.get()))
            {
                // Type aliases need to be resolved first, but we can register the base type name
                // for enum variant resolution (e.g., IoResult -> Result so IoResult::Ok works)
                std::string alias_name = alias_decl->alias_name();

                // Extract base type name from target annotation or resolved type
                std::string base_type_name;
                if (alias_decl->has_target_type_annotation())
                {
                    std::string target_str = alias_decl->target_type_annotation()->to_string();
                    // Handle qualified names (e.g., "std::Result<T, E>" -> "Result")
                    size_t last_sep = target_str.rfind("::");
                    if (last_sep != std::string::npos)
                        base_type_name = target_str.substr(last_sep + 2);
                    else
                        base_type_name = target_str;

                    // Remove generic parameters (e.g., "Result<T, IoError>" -> "Result")
                    size_t angle_pos = base_type_name.find('<');
                    if (angle_pos != std::string::npos)
                        base_type_name = base_type_name.substr(0, angle_pos);
                }
                else if (alias_decl->has_resolved_target_type())
                {
                    TypeRef resolved = alias_decl->get_resolved_target_type();
                    base_type_name = resolved->display_name();
                    // Remove generic parameters
                    size_t angle_pos = base_type_name.find('<');
                    if (angle_pos != std::string::npos)
                        base_type_name = base_type_name.substr(0, angle_pos);
                }

                // Register the base type name if we extracted it
                if (!base_type_name.empty() && base_type_name != alias_name)
                {
                    module_registry.register_type_alias_base(alias_name, base_type_name);
                    LOG_DEBUG(LogComponent::GENERAL,
                        "TypeResolutionPass: Registered type alias base '{}' -> '{}'",
                        alias_name, base_type_name);
                }

                LOG_DEBUG(LogComponent::GENERAL,
                    "TypeResolutionPass: Found type alias '{}' (will resolve in phase 2)",
                    alias_decl->alias_name());
            }
        }
        LOG_DEBUG(LogComponent::GENERAL, "TypeResolutionPass: Phase 1 complete - all types pre-registered");

        // === PHASE 2a: Resolve type aliases FIRST ===
        // This ensures that when we resolve function return types later, the type aliases
        // are already available in the module registry. This is critical for forward references
        // where a function uses a type alias defined later in the file.
        LOG_DEBUG(LogComponent::GENERAL, "TypeResolutionPass: Phase 2a - Resolving type aliases first");
        for (auto &stmt : program->statements())
        {
            if (auto *alias_decl = dynamic_cast<TypeAliasDeclarationNode *>(stmt.get()))
            {
                // If the alias has a deferred target type annotation, resolve it now
                if (alias_decl->has_target_type_annotation() && !alias_decl->has_resolved_target_type())
                {
                    const TypeAnnotation *target_ann = alias_decl->target_type_annotation();

                    // Create a resolution context, binding generic params if this is a generic alias
                    ResolutionContext alias_ctx = res_ctx.child();
                    const auto &generic_params = alias_decl->generic_params();
                    if (!generic_params.empty())
                    {
                        for (size_t i = 0; i < generic_params.size(); ++i)
                        {
                            TypeRef param_type = arena.create_generic_param(generic_params[i], i);
                            alias_ctx.bind_generic(generic_params[i], param_type);
                            LOG_DEBUG(LogComponent::GENERAL,
                                "TypeResolutionPass: (2a) Bound generic param '{}' (index {}) for type alias '{}'",
                                generic_params[i], i, alias_decl->alias_name());
                        }
                    }

                    TypeRef resolved = resolver.resolve(*target_ann, alias_ctx);
                    if (!resolved.is_error())
                    {
                        alias_decl->set_resolved_target_type(resolved);
                        resolved_count++;
                        LOG_DEBUG(LogComponent::GENERAL,
                            "TypeResolutionPass: (2a) Resolved type alias '{}' target to '{}'",
                            alias_decl->alias_name(), resolved->display_name());

                        // Only register non-generic type aliases in the module registry
                        // Generic type aliases are handled by the GenericRegistry
                        if (!alias_decl->is_generic())
                        {
                            module_registry.register_type(res_ctx.current_module, alias_decl->alias_name(), resolved);
                        }
                    }
                    else
                    {
                        error_count++;
                        LOG_DEBUG(LogComponent::GENERAL,
                            "TypeResolutionPass: (2a) Failed to resolve type alias '{}' target: {}",
                            alias_decl->alias_name(), resolved->display_name());
                    }
                }
                // Handle already-resolved type aliases (register in module registry)
                else if (alias_decl->has_resolved_target_type())
                {
                    if (!alias_decl->is_generic())
                    {
                        TypeRef target_type = alias_decl->get_resolved_target_type();
                        module_registry.register_type(res_ctx.current_module, alias_decl->alias_name(), target_type);
                        LOG_DEBUG(LogComponent::GENERAL,
                            "TypeResolutionPass: (2a) Registered pre-resolved type alias '{}' -> '{}'",
                            alias_decl->alias_name(), target_type->display_name());
                    }
                }
            }
        }
        LOG_DEBUG(LogComponent::GENERAL, "TypeResolutionPass: Phase 2a complete - type aliases resolved");

        // === PHASE 2b: Resolve all other type annotations ===
        LOG_DEBUG(LogComponent::GENERAL, "TypeResolutionPass: Phase 2b - Resolving other type annotations");
        for (auto &stmt : program->statements())
        {
            // Handle implement blocks
            if (auto *impl = dynamic_cast<ImplementationBlockNode *>(stmt.get()))
            {
                // Create a child context for this implement block
                ResolutionContext impl_ctx = res_ctx.child();

                // Parse target type to extract generic parameters (e.g., "Option<T>" -> ["T"])
                const std::string &target = impl->target_type();
                size_t angle_pos = target.find('<');
                if (angle_pos != std::string::npos && target.back() == '>')
                {
                    std::string params_str = target.substr(angle_pos + 1, target.size() - angle_pos - 2);
                    // Split by comma
                    std::vector<std::string> param_names;
                    size_t start = 0;
                    for (size_t i = 0; i <= params_str.size(); ++i)
                    {
                        if (i == params_str.size() || params_str[i] == ',')
                        {
                            std::string param = params_str.substr(start, i - start);
                            // Trim whitespace
                            while (!param.empty() && (param.front() == ' ' || param.front() == '\t'))
                                param.erase(0, 1);
                            while (!param.empty() && (param.back() == ' ' || param.back() == '\t'))
                                param.pop_back();
                            if (!param.empty())
                                param_names.push_back(param);
                            start = i + 1;
                        }
                    }

                    // Bind each generic parameter
                    for (size_t i = 0; i < param_names.size(); ++i)
                    {
                        TypeRef param_type = arena.create_generic_param(param_names[i], i);
                        impl_ctx.bind_generic(param_names[i], param_type);
                        LOG_DEBUG(LogComponent::GENERAL,
                            "TypeResolutionPass: Bound generic param '{}' (index {}) for impl block '{}'",
                            param_names[i], i, target);
                    }
                }

                for (auto &method : impl->method_implementations())
                {
                    // Create a method-level context that inherits impl's generic params
                    // and adds the method's own generic params (if any)
                    ResolutionContext method_ctx = impl_ctx.child();

                    // Bind method-level generic parameters
                    const auto &method_generic_params = method->generic_parameters();
                    size_t impl_param_count = 0; // Count how many params impl_ctx has
                    for (const auto &[name, _] : impl_ctx.generic_bindings)
                        impl_param_count++;

                    if (!method_generic_params.empty())
                    {
                        for (size_t i = 0; i < method_generic_params.size(); ++i)
                        {
                            const std::string &param_name = method_generic_params[i]->name();
                            TypeRef param_type = arena.create_generic_param(param_name, impl_param_count + i);
                            method_ctx.bind_generic(param_name, param_type);
                            LOG_DEBUG(LogComponent::GENERAL,
                                "TypeResolutionPass: Bound impl method generic param '{}' (index {}) for '{}::{}'",
                                param_name, impl_param_count + i, target, method->name());
                        }
                    }

                    // Resolve return type
                    auto *ann = method->return_type_annotation();
                    if (ann && type_needs_resolution(method->get_resolved_return_type(), ann))
                    {
                        TypeRef resolved = resolver.resolve(*ann, method_ctx);
                        if (!resolved.is_error())
                        {
                            method->set_resolved_return_type(resolved);
                            resolved_count++;
                            LOG_DEBUG(LogComponent::GENERAL,
                                "TypeResolutionPass: Resolved method '{}' return type to '{}'",
                                method->name(), resolved->display_name());
                        }
                        else
                        {
                            error_count++;
                            LOG_DEBUG(LogComponent::GENERAL,
                                "TypeResolutionPass: Could not resolve method '{}' return type: {}",
                                method->name(), resolved->display_name());
                        }
                    }

                    // Resolve parameter types
                    for (auto &param : method->parameters())
                    {
                        // Special handling for 'this' parameter with unresolved type
                        if (param->name() == "this" && param->has_resolved_type())
                        {
                            TypeRef current_type = param->get_resolved_type();
                            if (current_type.is_error())
                            {
                                // The parser created an error type - try to resolve using impl target
                                const std::string &target = impl->target_type();
                                std::string base_name = target;
                                std::vector<std::string> generic_arg_names;
                                size_t angle_pos = target.find('<');
                                if (angle_pos != std::string::npos && target.back() == '>')
                                {
                                    base_name = target.substr(0, angle_pos);
                                    // Extract generic argument names (e.g., "T", "E" from "Result<T, E>")
                                    std::string params_str = target.substr(angle_pos + 1, target.size() - angle_pos - 2);
                                    size_t start = 0;
                                    for (size_t i = 0; i <= params_str.size(); ++i)
                                    {
                                        if (i == params_str.size() || params_str[i] == ',')
                                        {
                                            std::string arg = params_str.substr(start, i - start);
                                            // Trim whitespace
                                            while (!arg.empty() && (arg.front() == ' ' || arg.front() == '\t'))
                                                arg.erase(0, 1);
                                            while (!arg.empty() && (arg.back() == ' ' || arg.back() == '\t'))
                                                arg.pop_back();
                                            if (!arg.empty())
                                                generic_arg_names.push_back(arg);
                                            start = i + 1;
                                        }
                                    }
                                }

                                // For generic impl blocks, construct a TypeAnnotation and resolve it
                                // This ensures we get Option<T> (with T as GenericParamType), not just Option
                                if (!generic_arg_names.empty())
                                {
                                    // Construct TypeAnnotation for the generic type
                                    TypeAnnotation base_ann = TypeAnnotation::named(base_name, param->location());
                                    std::vector<TypeAnnotation> arg_annotations;
                                    for (const auto &arg_name : generic_arg_names)
                                    {
                                        arg_annotations.push_back(TypeAnnotation::named(arg_name, param->location()));
                                    }
                                    TypeAnnotation generic_ann = TypeAnnotation::generic(
                                        std::move(base_ann), std::move(arg_annotations), param->location());

                                    // Resolve the generic type using method_ctx (where T, E, etc. are bound)
                                    TypeRef resolved_type = resolver.resolve(generic_ann, method_ctx);
                                    if (resolved_type.is_valid() && !resolved_type.is_error())
                                    {
                                        // Create a reference type for &this
                                        TypeRef this_type = arena.get_reference_to(resolved_type);
                                        param->set_resolved_type(this_type);
                                        resolved_count++;
                                        LOG_DEBUG(LogComponent::GENERAL,
                                            "TypeResolutionPass: Resolved generic 'this' param for impl '{}::{}' to '{}'",
                                            target, method->name(), this_type->display_name());
                                    }
                                    else
                                    {
                                        LOG_DEBUG(LogComponent::GENERAL,
                                            "TypeResolutionPass: Could not resolve generic 'this' type for impl '{}::{}' (resolved to error: {})",
                                            target, method->name(), resolved_type.is_valid() ? resolved_type->display_name() : "<invalid>");
                                    }
                                }
                                else
                                {
                                    // Non-generic impl block - use the existing lookup strategies
                                    TypeRef base_type;

                                    // Strategy 1: Try arena lookup by name
                                    base_type = arena.lookup_type_by_name(base_name);

                                    // Strategy 2: If arena lookup failed, try symbol table enum lookup
                                    if ((!base_type.is_valid() || base_type.is_error()) && symbols)
                                    {
                                        base_type = symbols->lookup_enum_type(base_name);
                                        if (base_type.is_valid() && !base_type.is_error())
                                        {
                                            LOG_DEBUG(LogComponent::GENERAL,
                                                "TypeResolutionPass: Found '{}' via symbol table enum lookup", base_name);
                                        }
                                    }

                                    // Strategy 3: Try symbol table struct lookup
                                    if ((!base_type.is_valid() || base_type.is_error()) && symbols)
                                    {
                                        base_type = symbols->lookup_struct_type(base_name);
                                        if (base_type.is_valid() && !base_type.is_error())
                                        {
                                            LOG_DEBUG(LogComponent::GENERAL,
                                                "TypeResolutionPass: Found '{}' via symbol table struct lookup", base_name);
                                        }
                                    }

                                    // Strategy 4: Try symbol table class lookup
                                    if ((!base_type.is_valid() || base_type.is_error()) && symbols)
                                    {
                                        base_type = symbols->lookup_class_type(base_name);
                                        if (base_type.is_valid() && !base_type.is_error())
                                        {
                                            LOG_DEBUG(LogComponent::GENERAL,
                                                "TypeResolutionPass: Found '{}' via symbol table class lookup", base_name);
                                        }
                                    }

                                    // Strategy 5: Try direct Symbol lookup and extract type
                                    if ((!base_type.is_valid() || base_type.is_error()) && symbols)
                                    {
                                        const Symbol *sym = symbols->lookup(base_name);
                                        if (sym && sym->type.is_valid() && !sym->type.is_error())
                                        {
                                            base_type = sym->type;
                                            LOG_DEBUG(LogComponent::GENERAL,
                                                "TypeResolutionPass: Found '{}' via direct symbol lookup", base_name);
                                        }
                                    }

                                    if (base_type.is_valid() && !base_type.is_error())
                                    {
                                        // Create a reference type for &this
                                        TypeRef this_type = arena.get_reference_to(base_type);
                                        param->set_resolved_type(this_type);
                                        resolved_count++;
                                        LOG_DEBUG(LogComponent::GENERAL,
                                            "TypeResolutionPass: Resolved 'this' param for impl '{}::{}' to '{}'",
                                            target, method->name(), this_type->display_name());
                                    }
                                    else
                                    {
                                        LOG_DEBUG(LogComponent::GENERAL,
                                            "TypeResolutionPass: Could not resolve 'this' type for impl '{}::{}' (base '{}' not found in arena or symbol table)",
                                            target, method->name(), base_name);
                                    }
                                }
                            }
                        }
                        else
                        {
                            const auto *param_ann = param->type_annotation();
                            if (param_ann && (!param->has_resolved_type() || param->get_resolved_type().is_error()))
                            {
                                TypeRef resolved = resolver.resolve(*param_ann, method_ctx);
                                if (!resolved.is_error())
                                {
                                    param->set_resolved_type(resolved);
                                    resolved_count++;
                                    LOG_DEBUG(LogComponent::GENERAL,
                                        "TypeResolutionPass: Resolved impl param '{}::{}' type to '{}'",
                                        method->name(), param->name(), resolved->display_name());
                                }
                            }
                        }
                    }
                }
            }
            // Handle struct declarations (with methods)
            else if (auto *struct_decl = dynamic_cast<StructDeclarationNode *>(stmt.get()))
            {
                // Create a child context for this struct
                ResolutionContext struct_ctx = res_ctx.child();

                // If the struct is generic, bind its type parameters
                const auto &struct_generic_params = struct_decl->generic_parameters();
                if (!struct_generic_params.empty())
                {
                    for (size_t i = 0; i < struct_generic_params.size(); ++i)
                    {
                        const std::string &param_name = struct_generic_params[i]->name();
                        // Create a GenericParamType for this parameter
                        TypeRef param_type = arena.create_generic_param(param_name, i);
                        struct_ctx.bind_generic(param_name, param_type);
                        LOG_DEBUG(LogComponent::GENERAL,
                            "TypeResolutionPass: Bound generic param '{}' (index {}) for struct '{}'",
                            param_name, i, struct_decl->name());
                    }
                }

                // Try to get the struct's own type for self-referential resolution
                // The struct should already be registered in the symbol table from the declaration pass
                auto *symbols = _compiler.symbol_table();
                if (symbols)
                {
                    TypeRef struct_type = symbols->lookup_struct_type(struct_decl->name());
                    if (struct_type.is_valid())
                    {
                        // Temporarily register the struct type in the module registry so it can be found
                        // during method return type resolution (for self-referential types)
                        module_registry.register_type(struct_ctx.current_module, struct_decl->name(), struct_type);
                        LOG_DEBUG(LogComponent::GENERAL,
                            "TypeResolutionPass: Registered struct '{}' in module registry for self-reference",
                            struct_decl->name());
                    }
                }

                for (auto &method : struct_decl->methods())
                {
                    // Create a method-level context that inherits struct's generic params
                    // and adds the method's own generic params (if any)
                    ResolutionContext method_ctx = struct_ctx.child();

                    // Bind method-level generic parameters (e.g., alloc<T>())
                    const auto &method_generic_params = method->generic_parameters();
                    if (!method_generic_params.empty())
                    {
                        // Start index after struct's generic params to avoid collision
                        size_t base_index = struct_generic_params.size();
                        for (size_t i = 0; i < method_generic_params.size(); ++i)
                        {
                            const std::string &param_name = method_generic_params[i]->name();
                            TypeRef param_type = arena.create_generic_param(param_name, base_index + i);
                            method_ctx.bind_generic(param_name, param_type);
                            LOG_DEBUG(LogComponent::GENERAL,
                                "TypeResolutionPass: Bound method generic param '{}' (index {}) for '{}::{}'",
                                param_name, base_index + i, struct_decl->name(), method->name());
                        }
                    }

                    // Resolve return type
                    auto *ann = method->return_type_annotation();
                    if (ann && type_needs_resolution(method->get_resolved_return_type(), ann))
                    {
                        LOG_DEBUG(LogComponent::GENERAL,
                            "TypeResolutionPass: Attempting to resolve '{}::{}' annotation='{}' kind={}",
                            struct_decl->name(), method->name(), ann->to_string(), static_cast<int>(ann->kind));
                        TypeRef resolved = resolver.resolve(*ann, method_ctx);
                        if (!resolved.is_error())
                        {
                            method->set_resolved_return_type(resolved);
                            resolved_count++;
                            LOG_DEBUG(LogComponent::GENERAL,
                                "TypeResolutionPass: Resolved struct method '{}::{}' return type to '{}'",
                                struct_decl->name(), method->name(), resolved->display_name());
                        }
                        else
                        {
                            error_count++;
                            LOG_DEBUG(LogComponent::GENERAL,
                                "TypeResolutionPass: Failed to resolve '{}::{}' annotation='{}': {}",
                                struct_decl->name(), method->name(), ann->to_string(), resolved->display_name());
                        }
                    }

                    // Resolve parameter types
                    for (auto &param : method->parameters())
                    {
                        const auto *param_ann = param->type_annotation();
                        if (param_ann && (!param->has_resolved_type() || param->get_resolved_type().is_error()))
                        {
                            TypeRef resolved = resolver.resolve(*param_ann, method_ctx);
                            if (!resolved.is_error())
                            {
                                param->set_resolved_type(resolved);
                                resolved_count++;
                                LOG_DEBUG(LogComponent::GENERAL,
                                    "TypeResolutionPass: Resolved param '{}::{}::{}' type to '{}'",
                                    struct_decl->name(), method->name(), param->name(), resolved->display_name());
                            }
                            else
                            {
                                error_count++;
                                LOG_DEBUG(LogComponent::GENERAL,
                                    "TypeResolutionPass: Failed to resolve param '{}::{}::{}' type",
                                    struct_decl->name(), method->name(), param->name());
                            }
                        }
                    }
                }

                // Resolve struct field types
                for (auto &field : struct_decl->fields())
                {
                    const auto *ann = field->type_annotation();
                    if (ann && (!field->has_resolved_type() || field->get_resolved_type().is_error()))
                    {
                        LOG_DEBUG(LogComponent::GENERAL,
                            "TypeResolutionPass: Attempting to resolve field '{}::{}' annotation='{}' kind={}",
                            struct_decl->name(), field->name(), ann->to_string(), static_cast<int>(ann->kind));
                        TypeRef resolved = resolver.resolve(*ann, struct_ctx);
                        if (!resolved.is_error())
                        {
                            field->set_resolved_type(resolved);
                            resolved_count++;
                            LOG_DEBUG(LogComponent::GENERAL,
                                "TypeResolutionPass: Resolved struct field '{}::{}' type to '{}'",
                                struct_decl->name(), field->name(), resolved->display_name());
                        }
                        else
                        {
                            error_count++;
                            LOG_DEBUG(LogComponent::GENERAL,
                                "TypeResolutionPass: Failed to resolve field '{}::{}' annotation='{}': {}",
                                struct_decl->name(), field->name(), ann->to_string(), resolved->display_name());
                        }
                    }
                }
            }
            // Handle function declarations
            else if (auto *func = dynamic_cast<FunctionDeclarationNode *>(stmt.get()))
            {
                // Create a child context for this function
                ResolutionContext func_ctx = res_ctx.child();

                // If the function is generic, bind its type parameters
                const auto &generic_params = func->generic_parameters();
                if (!generic_params.empty())
                {
                    for (size_t i = 0; i < generic_params.size(); ++i)
                    {
                        const std::string &param_name = generic_params[i]->name();
                        // Create a GenericParamType for this parameter
                        TypeRef param_type = arena.create_generic_param(param_name, i);
                        func_ctx.bind_generic(param_name, param_type);
                        LOG_DEBUG(LogComponent::GENERAL,
                            "TypeResolutionPass: Bound generic param '{}' (index {}) for function '{}'",
                            param_name, i, func->name());
                    }
                }

                // Resolve return type
                auto *ann = func->return_type_annotation();
                if (ann && type_needs_resolution(func->get_resolved_return_type(), ann))
                {
                    TypeRef resolved = resolver.resolve(*ann, func_ctx);
                    if (!resolved.is_error())
                    {
                        func->set_resolved_return_type(resolved);
                        resolved_count++;
                        LOG_DEBUG(LogComponent::GENERAL,
                            "TypeResolutionPass: Resolved function '{}' return type to '{}'",
                            func->name(), resolved->display_name());
                    }
                    else
                    {
                        error_count++;
                    }
                }

                // Resolve parameter types
                for (auto &param : func->parameters())
                {
                    const auto *param_ann = param->type_annotation();
                    if (param_ann && (!param->has_resolved_type() || param->get_resolved_type().is_error()))
                    {
                        TypeRef resolved = resolver.resolve(*param_ann, func_ctx);
                        if (!resolved.is_error())
                        {
                            param->set_resolved_type(resolved);
                            resolved_count++;
                            LOG_DEBUG(LogComponent::GENERAL,
                                "TypeResolutionPass: Resolved func param '{}::{}' type to '{}'",
                                func->name(), param->name(), resolved->display_name());
                        }
                    }
                }
            }
            // Type alias declarations are handled in Phase 2a - skip them here
            else if (dynamic_cast<TypeAliasDeclarationNode *>(stmt.get()))
            {
                // Already handled in Phase 2a
            }
        }

        // === PHASE 3: Resolve local variable types in function/method bodies ===
        LOG_DEBUG(LogComponent::GENERAL, "TypeResolutionPass: Phase 3 - Resolving local variable types in function bodies");
        for (auto &stmt : program->statements())
        {
            // Handle implement blocks
            if (auto *impl = dynamic_cast<ImplementationBlockNode *>(stmt.get()))
            {
                // Create a child context for this implement block
                ResolutionContext impl_ctx = res_ctx.child();

                // Parse target type to extract generic parameters
                const std::string &target = impl->target_type();
                size_t angle_pos = target.find('<');
                if (angle_pos != std::string::npos && target.back() == '>')
                {
                    std::string params_str = target.substr(angle_pos + 1, target.size() - angle_pos - 2);
                    std::vector<std::string> param_names;
                    size_t start = 0;
                    for (size_t i = 0; i <= params_str.size(); ++i)
                    {
                        if (i == params_str.size() || params_str[i] == ',')
                        {
                            std::string param = params_str.substr(start, i - start);
                            while (!param.empty() && (param.front() == ' ' || param.front() == '\t'))
                                param.erase(0, 1);
                            while (!param.empty() && (param.back() == ' ' || param.back() == '\t'))
                                param.pop_back();
                            if (!param.empty())
                                param_names.push_back(param);
                            start = i + 1;
                        }
                    }
                    for (size_t i = 0; i < param_names.size(); ++i)
                    {
                        TypeRef param_type = arena.create_generic_param(param_names[i], i);
                        impl_ctx.bind_generic(param_names[i], param_type);
                    }
                }

                for (auto &method : impl->method_implementations())
                {
                    ResolutionContext method_ctx = impl_ctx.child();
                    const auto &method_generic_params = method->generic_parameters();
                    size_t impl_param_count = 0;
                    for (const auto &[name, _] : impl_ctx.generic_bindings)
                        impl_param_count++;

                    for (size_t i = 0; i < method_generic_params.size(); ++i)
                    {
                        const std::string &param_name = method_generic_params[i]->name();
                        TypeRef param_type = arena.create_generic_param(param_name, impl_param_count + i);
                        method_ctx.bind_generic(param_name, param_type);
                    }

                    // Resolve variable types in method body and propagate return type to return expressions
                    if (method->body())
                    {
                        TypeRef return_type = method->get_resolved_return_type();
                        resolve_variable_types_in_statement(method->body(), resolver, method_ctx, return_type, resolved_count, error_count);
                    }
                }
            }
            // Handle struct declarations
            else if (auto *struct_decl = dynamic_cast<StructDeclarationNode *>(stmt.get()))
            {
                ResolutionContext struct_ctx = res_ctx.child();
                const auto &struct_generic_params = struct_decl->generic_parameters();
                for (size_t i = 0; i < struct_generic_params.size(); ++i)
                {
                    TypeRef param_type = arena.create_generic_param(struct_generic_params[i]->name(), i);
                    struct_ctx.bind_generic(struct_generic_params[i]->name(), param_type);
                }

                for (auto &method : struct_decl->methods())
                {
                    ResolutionContext method_ctx = struct_ctx.child();
                    const auto &method_generic_params = method->generic_parameters();
                    size_t base_index = struct_generic_params.size();
                    for (size_t i = 0; i < method_generic_params.size(); ++i)
                    {
                        TypeRef param_type = arena.create_generic_param(method_generic_params[i]->name(), base_index + i);
                        method_ctx.bind_generic(method_generic_params[i]->name(), param_type);
                    }

                    // Resolve variable types in method body and propagate return type to return expressions
                    if (method->body())
                    {
                        TypeRef return_type = method->get_resolved_return_type();
                        resolve_variable_types_in_statement(method->body(), resolver, method_ctx, return_type, resolved_count, error_count);
                    }
                }
            }
            // Handle function declarations
            else if (auto *func = dynamic_cast<FunctionDeclarationNode *>(stmt.get()))
            {
                ResolutionContext func_ctx = res_ctx.child();
                const auto &generic_params = func->generic_parameters();
                for (size_t i = 0; i < generic_params.size(); ++i)
                {
                    TypeRef param_type = arena.create_generic_param(generic_params[i]->name(), i);
                    func_ctx.bind_generic(generic_params[i]->name(), param_type);
                }

                // Resolve variable types in function body and propagate return type to return expressions
                if (func->body())
                {
                    TypeRef return_type = func->get_resolved_return_type();
                    resolve_variable_types_in_statement(func->body(), resolver, func_ctx, return_type, resolved_count, error_count);
                }
            }
        }

        LOG_DEBUG(LogComponent::GENERAL,
            "TypeResolutionPass: Resolved {} types, {} errors", resolved_count, error_count);

        return PassResult::ok({PassProvides::TYPES_RESOLVED});
    }

    // ============================================================================
    // Stage 4.2: Struct Field Type Sync Pass
    // ============================================================================

    StructFieldTypeSyncPass::StructFieldTypeSyncPass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult StructFieldTypeSyncPass::run(PassContext &ctx)
    {
        auto *program = _compiler.ast_root();
        if (!program)
        {
            LOG_DEBUG(LogComponent::GENERAL, "StructFieldTypeSyncPass: No AST to process");
            return PassResult::ok({"struct_fields_synced"});
        }

        LOG_DEBUG(LogComponent::GENERAL, "StructFieldTypeSyncPass: Syncing resolved field types to StructTypes/ClassTypes");

        int synced_count = 0;

        // Process all declarations in the AST
        for (auto &decl : program->statements())
        {
            if (auto *struct_decl = dynamic_cast<StructDeclarationNode *>(decl.get()))
            {
                sync_struct_fields(struct_decl, ctx);
                synced_count++;
            }
            else if (auto *class_decl = dynamic_cast<ClassDeclarationNode *>(decl.get()))
            {
                sync_class_fields(class_decl, ctx);
                synced_count++;
            }
        }

        LOG_DEBUG(LogComponent::GENERAL, "StructFieldTypeSyncPass: Synced {} struct/class types", synced_count);
        return PassResult::ok({"struct_fields_synced"});
    }

    void StructFieldTypeSyncPass::sync_struct_fields(StructDeclarationNode *struct_decl, PassContext &ctx)
    {
        if (!struct_decl || struct_decl->fields().empty())
            return;

        // Don't sync generic structs - their fields contain type parameters
        if (!struct_decl->generic_parameters().empty())
        {
            LOG_DEBUG(LogComponent::GENERAL, "StructFieldTypeSyncPass: Skipping generic struct '{}'", struct_decl->name());
            return;
        }

        // Look up the StructType
        TypeRef struct_type = _compiler.symbol_table()->lookup_struct_type(struct_decl->name());
        if (!struct_type.is_valid())
        {
            struct_type = _compiler.ast_context()->types().lookup_type_by_name(struct_decl->name());
        }

        if (!struct_type.is_valid())
        {
            LOG_DEBUG(LogComponent::GENERAL, "StructFieldTypeSyncPass: Could not find StructType for '{}'", struct_decl->name());
            return;
        }

        auto *struct_ptr = const_cast<StructType *>(dynamic_cast<const StructType *>(struct_type.get()));
        if (!struct_ptr)
        {
            LOG_DEBUG(LogComponent::GENERAL, "StructFieldTypeSyncPass: '{}' is not a StructType", struct_decl->name());
            return;
        }

        // Build new field list from AST nodes with resolved types
        std::vector<FieldInfo> new_fields;
        bool has_changes = false;

        for (const auto &field : struct_decl->fields())
        {
            if (!field)
                continue;

            TypeRef resolved_type = field->get_resolved_type();
            if (resolved_type.is_valid() && !resolved_type.is_error())
            {
                new_fields.emplace_back(field->name(), resolved_type, 0, true, field->is_mutable());

                // Check if this is a change from the current StructType
                auto existing_field = struct_ptr->get_field(field->name());
                if (!existing_field || !existing_field->type.is_valid() ||
                    existing_field->type.id() != resolved_type.id())
                {
                    has_changes = true;
                    LOG_DEBUG(LogComponent::GENERAL,
                        "StructFieldTypeSyncPass: Updated field '{}::{}' type to '{}' (TypeID={})",
                        struct_decl->name(), field->name(), resolved_type->display_name(),
                        resolved_type.id().id);
                }
            }
            else
            {
                // Keep the existing field type if resolution failed
                auto existing_field = struct_ptr->get_field(field->name());
                if (existing_field && existing_field->type.is_valid())
                {
                    new_fields.emplace_back(existing_field->name, existing_field->type,
                                           existing_field->offset, existing_field->is_public,
                                           existing_field->is_mutable);
                }
                else
                {
                    LOG_WARN(LogComponent::GENERAL,
                        "StructFieldTypeSyncPass: Field '{}::{}' still unresolved after TypeResolutionPass",
                        struct_decl->name(), field->name());
                }
            }
        }

        // Only update if we have changes and valid fields
        if (has_changes && !new_fields.empty())
        {
            struct_ptr->set_fields(std::move(new_fields));
            LOG_DEBUG(LogComponent::GENERAL,
                "StructFieldTypeSyncPass: Updated StructType '{}' with {} fields",
                struct_decl->name(), struct_decl->fields().size());
        }
    }

    void StructFieldTypeSyncPass::sync_class_fields(ClassDeclarationNode *class_decl, PassContext &ctx)
    {
        if (!class_decl || class_decl->fields().empty())
            return;

        // Don't sync generic classes - their fields contain type parameters
        if (!class_decl->generic_parameters().empty())
        {
            LOG_DEBUG(LogComponent::GENERAL, "StructFieldTypeSyncPass: Skipping generic class '{}'", class_decl->name());
            return;
        }

        // Look up the ClassType
        TypeRef class_type = _compiler.symbol_table()->lookup_class_type(class_decl->name());
        if (!class_type.is_valid())
        {
            class_type = _compiler.ast_context()->types().lookup_type_by_name(class_decl->name());
        }

        if (!class_type.is_valid())
        {
            LOG_DEBUG(LogComponent::GENERAL, "StructFieldTypeSyncPass: Could not find ClassType for '{}'", class_decl->name());
            return;
        }

        auto *class_ptr = const_cast<ClassType *>(dynamic_cast<const ClassType *>(class_type.get()));
        if (!class_ptr)
        {
            LOG_DEBUG(LogComponent::GENERAL, "StructFieldTypeSyncPass: '{}' is not a ClassType", class_decl->name());
            return;
        }

        // Build new field list from AST nodes with resolved types
        std::vector<FieldInfo> new_fields;
        bool has_changes = false;

        for (const auto &field : class_decl->fields())
        {
            if (!field)
                continue;

            TypeRef resolved_type = field->get_resolved_type();
            if (resolved_type.is_valid() && !resolved_type.is_error())
            {
                new_fields.emplace_back(field->name(), resolved_type, 0, true, field->is_mutable());

                // Check if this is a change from the current ClassType
                auto existing_field = class_ptr->get_field(field->name());
                if (!existing_field || !existing_field->type.is_valid() ||
                    existing_field->type.id() != resolved_type.id())
                {
                    has_changes = true;
                    LOG_DEBUG(LogComponent::GENERAL,
                        "StructFieldTypeSyncPass: Updated field '{}::{}' type to '{}' (TypeID={})",
                        class_decl->name(), field->name(), resolved_type->display_name(),
                        resolved_type.id().id);
                }
            }
            else
            {
                // Keep the existing field type if resolution failed
                auto existing_field = class_ptr->get_field(field->name());
                if (existing_field && existing_field->type.is_valid())
                {
                    new_fields.emplace_back(existing_field->name, existing_field->type,
                                           existing_field->offset, existing_field->is_public,
                                           existing_field->is_mutable);
                }
                else
                {
                    LOG_WARN(LogComponent::GENERAL,
                        "StructFieldTypeSyncPass: Field '{}::{}' still unresolved after TypeResolutionPass",
                        class_decl->name(), field->name());
                }
            }
        }

        // Only update if we have changes and valid fields
        if (has_changes && !new_fields.empty())
        {
            class_ptr->set_fields(std::move(new_fields));
            LOG_DEBUG(LogComponent::GENERAL,
                "StructFieldTypeSyncPass: Updated ClassType '{}' with {} fields",
                class_decl->name(), class_decl->fields().size());
        }
    }

    // ============================================================================
    // Stage 5: Semantic Analysis Passes
    // ============================================================================

    DirectiveProcessingPass::DirectiveProcessingPass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult DirectiveProcessingPass::run(PassContext &ctx)
    {
        // Run the directive processing phase on the compiler instance
        if (!_compiler.run_directive_processing_phase())
        {
            ctx.emit_error(ErrorCode::E0900_INTERNAL_COMPILER_ERROR,
                           "Directive processing failed");
            return PassResult::failure();
        }

        LOG_DEBUG(LogComponent::GENERAL, "DirectiveProcessingPass: Directives processed");
        return PassResult::ok({PassProvides::DIRECTIVES_PROCESSED});
    }

    FunctionBodyPass::FunctionBodyPass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult FunctionBodyPass::run(PassContext &ctx)
    {
        // Run the function body processing phase on the compiler instance
        if (!_compiler.run_function_body_phase())
        {
            // Errors already emitted to DiagEmitter
            return PassResult::failure();
        }

        LOG_DEBUG(LogComponent::GENERAL, "FunctionBodyPass: Function bodies type-checked");
        return PassResult::ok({PassProvides::BODIES_TYPE_CHECKED});
    }

    // ============================================================================
    // Stage 6: Specialization Passes
    // ============================================================================

    MonomorphizationPass::MonomorphizationPass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult MonomorphizationPass::run(PassContext &ctx)
    {
        auto *mono = ctx.monomorphizer();
        if (!mono)
        {
            LOG_WARN(LogComponent::GENERAL, "MonomorphizationPass: No monomorphizer available");
            return PassResult::ok({PassProvides::MONOMORPHIZATION_COMPLETE});
        }

        // Import cached instantiations
        mono->import_cached_instantiations();

        if (!mono->has_pending())
        {
            LOG_DEBUG(LogComponent::GENERAL, "MonomorphizationPass: No instantiations to process");
            return PassResult::ok({PassProvides::MONOMORPHIZATION_COMPLETE});
        }

        LOG_DEBUG(LogComponent::GENERAL, "MonomorphizationPass: Processing {} pending instantiations",
                  mono->pending_count());

        if (!mono->process_all())
        {
            // Non-fatal - some instantiations may not be used
            LOG_WARN(LogComponent::GENERAL, "MonomorphizationPass: Some instantiations failed (non-fatal)");
        }

        LOG_DEBUG(LogComponent::GENERAL, "MonomorphizationPass: Created {} specializations",
                  mono->specialization_count());

        return PassResult::ok({PassProvides::MONOMORPHIZATION_COMPLETE});
    }

    // ============================================================================
    // Pass 6.2: Generic Expression Resolution
    // ============================================================================

    GenericExpressionResolutionPass::GenericExpressionResolutionPass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult GenericExpressionResolutionPass::run(PassContext &ctx)
    {
        auto *ast_root = _compiler.ast_root();
        if (!ast_root)
        {
            LOG_WARN(LogComponent::GENERAL, "GenericExpressionResolutionPass: No AST root available");
            return PassResult::ok({PassProvides::GENERIC_EXPRESSIONS_RESOLVED});
        }

        LOG_DEBUG(LogComponent::GENERAL, "GenericExpressionResolutionPass: Resolving generic enum variants in expressions");
        LOG_DEBUG(LogComponent::GENERAL, "GenericExpressionResolutionPass: AST has {} statements", ast_root->statements().size());

        // Walk all declarations in the program
        for (auto &decl : ast_root->statements())
        {
            LOG_DEBUG(LogComponent::GENERAL, "GenericExpressionResolutionPass: Processing declaration kind {}", static_cast<int>(decl->kind()));

            if (auto *func = dynamic_cast<FunctionDeclarationNode *>(decl.get()))
            {
                LOG_DEBUG(LogComponent::GENERAL, "GenericExpressionResolutionPass: Found function '{}'", func->name());
                resolve_function_body(func, TypeRef{}, ctx);
            }
            else if (auto *struct_decl = dynamic_cast<StructDeclarationNode *>(decl.get()))
            {
                LOG_DEBUG(LogComponent::GENERAL, "GenericExpressionResolutionPass: Found struct '{}' with {} methods",
                          struct_decl->name(), struct_decl->methods().size());
                // Look up the struct type for 'this' resolution in methods
                TypeRef struct_type = _compiler.symbol_table()->lookup_struct_type(struct_decl->name());
                LOG_DEBUG(LogComponent::GENERAL, "GenericExpressionResolutionPass: Struct type lookup for '{}': {}",
                          struct_decl->name(), struct_type.is_valid() ? "found" : "NOT FOUND");
                // Process struct methods
                for (auto &method : struct_decl->methods())
                {
                    LOG_DEBUG(LogComponent::GENERAL, "GenericExpressionResolutionPass: Processing struct method '{}'", method->name());
                    resolve_function_body(method.get(), struct_type, ctx);
                }
            }
            else if (auto *class_decl = dynamic_cast<ClassDeclarationNode *>(decl.get()))
            {
                // Look up the class type for 'this' resolution in methods
                TypeRef class_type = _compiler.symbol_table()->lookup_class_type(class_decl->name());
                // Process class methods
                for (auto &method : class_decl->methods())
                {
                    resolve_function_body(method.get(), class_type, ctx);
                }
            }
            else if (auto *impl_block = dynamic_cast<ImplementationBlockNode *>(decl.get()))
            {
                LOG_DEBUG(LogComponent::GENERAL, "GenericExpressionResolutionPass: Found impl block for '{}' with {} methods",
                          impl_block->target_type(), impl_block->method_implementations().size());
                // Look up the type for 'this' resolution in impl block methods
                std::string type_name = impl_block->target_type();
                TypeRef impl_type = _compiler.symbol_table()->lookup_struct_type(type_name);
                if (!impl_type.is_valid())
                {
                    impl_type = _compiler.symbol_table()->lookup_class_type(type_name);
                }
                LOG_DEBUG(LogComponent::GENERAL, "GenericExpressionResolutionPass: Impl type lookup for '{}': {}",
                          type_name, impl_type.is_valid() ? "found" : "NOT FOUND");
                // Process implementation block methods
                for (auto &method : impl_block->method_implementations())
                {
                    LOG_DEBUG(LogComponent::GENERAL, "GenericExpressionResolutionPass: Processing impl method '{}'", method->name());
                    resolve_function_body(method.get(), impl_type, ctx);
                }
            }
            // Note: EnumDeclarationNode doesn't have methods in this AST design
        }

        LOG_DEBUG(LogComponent::GENERAL, "GenericExpressionResolutionPass: Complete");
        return PassResult::ok({PassProvides::GENERIC_EXPRESSIONS_RESOLVED});
    }

    void GenericExpressionResolutionPass::resolve_function_body(FunctionDeclarationNode *func, TypeRef struct_type, PassContext &ctx)
    {
        if (!func || !func->body())
            return;

        // Store the struct type for 'this' resolution in member assignments
        TypeRef previous_struct_type = _current_struct_type;
        _current_struct_type = struct_type;

        // Clear local variable tracking for this function scope
        _local_variable_types.clear();

        // Track function parameters as local variables
        for (const auto &param : func->parameters())
        {
            if (param && !param->name().empty())
            {
                TypeRef param_type = param->get_resolved_type();
                if (param_type.is_valid())
                {
                    _local_variable_types[param->name()] = param_type;
                }
            }
        }

        // Get the function's return type - this is the context for return statements
        TypeRef return_type = func->get_resolved_return_type();

        LOG_DEBUG(LogComponent::GENERAL,
                  "GenericExpressionResolutionPass: Processing function '{}' with return type '{}', struct_type='{}'",
                  func->name(),
                  return_type.is_valid() ? return_type->display_name() : "void",
                  struct_type.is_valid() ? struct_type->display_name() : "none");

        // Resolve expressions in the function body
        resolve_statement(func->body(), return_type, ctx);

        // Restore previous struct type
        _current_struct_type = previous_struct_type;
    }

    /// Helper function to unwrap TypeAlias chain to get the underlying type
    static TypeRef unwrap_type_alias(TypeRef type)
    {
        while (type.is_valid() && type->kind() == TypeKind::TypeAlias)
        {
            auto *alias = static_cast<const TypeAliasType *>(type.get());
            type = alias->target();
        }
        return type;
    }

    void GenericExpressionResolutionPass::resolve_statement(StatementNode *stmt, TypeRef expected_type, PassContext &ctx)
    {
        if (!stmt)
            return;

        switch (stmt->kind())
        {
        case NodeKind::ReturnStatement:
        {
            auto *ret = static_cast<ReturnStatementNode *>(stmt);
            if (ret->expression())
            {
                // Unwrap TypeAlias for return type context (e.g., AllocResult -> Result<void*, AllocError>)
                TypeRef unwrapped_expected = unwrap_type_alias(expected_type);
                resolve_expression(ret->expression(), unwrapped_expected, ctx);
            }
            break;
        }
        case NodeKind::BlockStatement:
        {
            auto *block = static_cast<BlockStatementNode *>(stmt);
            for (auto &child_stmt : block->statements())
            {
                resolve_statement(child_stmt.get(), expected_type, ctx);
            }
            break;
        }
        case NodeKind::IfStatement:
        {
            auto *if_stmt = static_cast<IfStatementNode *>(stmt);
            resolve_expression(if_stmt->condition(), TypeRef{}, ctx); // Conditions have no expected type
            resolve_statement(if_stmt->then_statement(), expected_type, ctx);
            if (if_stmt->else_statement())
            {
                resolve_statement(if_stmt->else_statement(), expected_type, ctx);
            }
            break;
        }
        case NodeKind::WhileStatement:
        {
            auto *while_stmt = static_cast<WhileStatementNode *>(stmt);
            resolve_expression(while_stmt->condition(), TypeRef{}, ctx);
            resolve_statement(while_stmt->body(), expected_type, ctx);
            break;
        }
        case NodeKind::ForStatement:
        {
            auto *for_stmt = static_cast<ForStatementNode *>(stmt);
            // For loop init is a VariableDeclarationNode - resolve its initializer
            if (for_stmt->init() && for_stmt->init()->initializer())
            {
                TypeRef init_type = for_stmt->init()->get_resolved_type();
                resolve_expression(for_stmt->init()->initializer(), init_type, ctx);
            }
            if (for_stmt->condition())
                resolve_expression(for_stmt->condition(), TypeRef{}, ctx);
            if (for_stmt->update())
                resolve_expression(for_stmt->update(), TypeRef{}, ctx);
            resolve_statement(for_stmt->body(), expected_type, ctx);
            break;
        }
        case NodeKind::DeclarationStatement:
        {
            auto *decl_stmt = static_cast<DeclarationStatementNode *>(stmt);
            if (auto *var_decl = dynamic_cast<VariableDeclarationNode *>(decl_stmt->declaration()))
            {
                // Variable declarations: the expected type is the variable's declared type
                TypeRef var_type = var_decl->get_resolved_type();

                // Track variable type for later member access resolution
                if (var_type.is_valid() && !var_decl->name().empty())
                {
                    _local_variable_types[var_decl->name()] = var_type;
                }

                if (var_decl->initializer())
                {
                    resolve_expression(var_decl->initializer(), var_type, ctx);
                }
            }
            break;
        }
        case NodeKind::ExpressionStatement:
        {
            auto *expr_stmt = static_cast<ExpressionStatementNode *>(stmt);
            resolve_expression(expr_stmt->expression(), TypeRef{}, ctx);
            break;
        }
        case NodeKind::MatchStatement:
        {
            auto *match_stmt = static_cast<MatchStatementNode *>(stmt);
            resolve_expression(match_stmt->expr(), TypeRef{}, ctx);
            for (auto &arm : match_stmt->arms())
            {
                resolve_statement(arm->body(), expected_type, ctx);
            }
            break;
        }
        default:
            // Other statement types - continue walking
            break;
        }
    }

    void GenericExpressionResolutionPass::resolve_expression(ExpressionNode *expr, TypeRef expected_type, PassContext &ctx)
    {
        if (!expr)
            return;

        switch (expr->kind())
        {
        case NodeKind::ScopeResolution:
        {
            auto *scope_res = static_cast<ScopeResolutionNode *>(expr);

            // Skip if already resolved
            if (scope_res->has_resolved_type())
                return;

            // Try to resolve as a generic enum variant first
            TypeRef resolved = resolve_generic_enum_variant(
                scope_res->scope_name(),
                scope_res->member_name(),
                expected_type,
                ctx);

            // If that didn't work and we have generic args, try as generic static method
            if (!resolved.is_valid() && scope_res->has_generic_args())
            {
                resolved = resolve_generic_static_method(
                    scope_res->scope_name(),
                    scope_res->generic_args(),
                    ctx);
            }

            if (resolved.is_valid())
            {
                scope_res->set_resolved_type(resolved);
                LOG_DEBUG(LogComponent::GENERAL,
                          "GenericExpressionResolutionPass: Resolved {}::{} to type '{}'",
                          scope_res->scope_name(),
                          scope_res->member_name(),
                          resolved->display_name());
            }
            break;
        }
        case NodeKind::CallExpression:
        {
            auto *call = static_cast<CallExpressionNode *>(expr);

            // Check if this is an enum variant constructor like Option::Some(value)
            // or a generic static method call like Array<String>::new()
            if (auto *scope_res = dynamic_cast<ScopeResolutionNode *>(call->callee()))
            {
                if (!scope_res->has_resolved_type())
                {
                    // First try to resolve as generic enum variant
                    TypeRef resolved = resolve_generic_enum_variant(
                        scope_res->scope_name(),
                        scope_res->member_name(),
                        expected_type,
                        ctx);

                    // If that didn't work and we have generic args, try as generic static method
                    if (!resolved.is_valid() && scope_res->has_generic_args())
                    {
                        resolved = resolve_generic_static_method(
                            scope_res->scope_name(),
                            scope_res->generic_args(),
                            ctx);
                    }

                    if (resolved.is_valid())
                    {
                        scope_res->set_resolved_type(resolved);
                        // Also set the resolved type on the CallExpressionNode itself
                        // This is crucial because codegen reads from the call node, not the callee
                        call->set_resolved_type(resolved);
                        LOG_DEBUG(LogComponent::GENERAL,
                                  "GenericExpressionResolutionPass: Resolved {}::{} call to type '{}'",
                                  scope_res->scope_name(),
                                  scope_res->member_name(),
                                  resolved->display_name());
                    }
                }
            }

            // Look up function signature to get parameter types for argument resolution
            std::vector<TypeRef> param_types;

            // Try to get function type from callee
            if (auto *ident = dynamic_cast<IdentifierNode *>(call->callee()))
            {
                // Simple function call: foo(arg)
                const Symbol *func_sym = _compiler.symbol_table()->lookup(ident->name());
                if (func_sym && func_sym->type.is_valid() && func_sym->type->kind() == TypeKind::Function)
                {
                    auto *func_type = static_cast<const FunctionType *>(func_sym->type.get());
                    param_types = func_type->param_types();
                    LOG_DEBUG(LogComponent::GENERAL,
                              "GenericExpressionResolutionPass: Found function '{}' with {} params",
                              ident->name(), param_types.size());
                }
            }
            else if (auto *member = dynamic_cast<MemberAccessNode *>(call->callee()))
            {
                // Method call: obj.method(arg) - try to get method signature from receiver type
                TypeRef receiver_type = member->object()->get_resolved_type();
                if (receiver_type.is_valid())
                {
                    std::string method_name = member->member();
                    // Try to find method in struct/class type
                    if (receiver_type->kind() == TypeKind::Struct)
                    {
                        auto *struct_type = static_cast<const StructType *>(receiver_type.get());
                        const MethodInfo *method_info = struct_type->get_method(method_name);
                        if (method_info && method_info->function_type.is_valid() &&
                            method_info->function_type->kind() == TypeKind::Function)
                        {
                            auto *func_type = static_cast<const FunctionType *>(method_info->function_type.get());
                            param_types = func_type->param_types();
                            // Skip 'self' parameter if present
                            if (!param_types.empty())
                            {
                                param_types.erase(param_types.begin());
                            }
                            LOG_DEBUG(LogComponent::GENERAL,
                                      "GenericExpressionResolutionPass: Found method '{}.{}' with {} params",
                                      receiver_type->display_name(), method_name, param_types.size());
                        }
                    }
                    else if (receiver_type->kind() == TypeKind::Class)
                    {
                        auto *class_type = static_cast<const ClassType *>(receiver_type.get());
                        const MethodInfo *method_info = class_type->get_method(method_name);
                        if (method_info && method_info->function_type.is_valid() &&
                            method_info->function_type->kind() == TypeKind::Function)
                        {
                            auto *func_type = static_cast<const FunctionType *>(method_info->function_type.get());
                            param_types = func_type->param_types();
                            // Skip 'self' parameter if present
                            if (!param_types.empty())
                            {
                                param_types.erase(param_types.begin());
                            }
                            LOG_DEBUG(LogComponent::GENERAL,
                                      "GenericExpressionResolutionPass: Found method '{}.{}' with {} params",
                                      receiver_type->display_name(), method_name, param_types.size());
                        }
                    }
                }
            }
            else if (auto *scope_res_callee = dynamic_cast<ScopeResolutionNode *>(call->callee()))
            {
                // Static method call: Type::method(arg) - look up in symbol table
                std::string qualified_name = scope_res_callee->scope_name() + "::" + scope_res_callee->member_name();
                const Symbol *func_sym = _compiler.symbol_table()->lookup(qualified_name);
                if (!func_sym)
                {
                    // Try just the member name for imported functions
                    func_sym = _compiler.symbol_table()->lookup(scope_res_callee->member_name());
                }
                if (func_sym && func_sym->type.is_valid() && func_sym->type->kind() == TypeKind::Function)
                {
                    auto *func_type = static_cast<const FunctionType *>(func_sym->type.get());
                    param_types = func_type->param_types();
                    LOG_DEBUG(LogComponent::GENERAL,
                              "GenericExpressionResolutionPass: Found static method '{}' with {} params",
                              qualified_name, param_types.size());
                }
            }

            // Resolve arguments with parameter types as expected types
            auto &args = call->arguments();
            for (size_t i = 0; i < args.size(); ++i)
            {
                TypeRef expected_arg_type = (i < param_types.size()) ? param_types[i] : TypeRef{};
                if (expected_arg_type.is_valid())
                {
                    LOG_DEBUG(LogComponent::GENERAL,
                              "GenericExpressionResolutionPass: Resolving arg {} with expected type '{}'",
                              i, expected_arg_type->display_name());
                }
                resolve_expression(args[i].get(), expected_arg_type, ctx);
            }
            break;
        }
        case NodeKind::BinaryExpression:
        {
            auto *binary = static_cast<BinaryExpressionNode *>(expr);
            // For assignments, RHS inherits expected type from LHS
            if (binary->operator_token().kind() == TokenKind::TK_EQUAL)
            {
                TypeRef lhs_type = binary->left()->get_resolved_type();

                // If LHS is a member access (e.g., this.name), use our helper to resolve the type
                if (!lhs_type.is_valid())
                {
                    if (auto *member_access = dynamic_cast<MemberAccessNode *>(binary->left()))
                    {
                        lhs_type = resolve_member_access_type(member_access, ctx);

                        if (lhs_type.is_valid())
                        {
                            LOG_DEBUG(LogComponent::GENERAL,
                                      "GenericExpressionResolutionPass: Resolved member access '{}' to type '{}' (kind={}, generic_params={})",
                                      member_access->member(),
                                      lhs_type->display_name(),
                                      static_cast<int>(lhs_type->kind()),
                                      contains_generic_params(lhs_type) ? "yes" : "no");
                        }
                        else
                        {
                            LOG_DEBUG(LogComponent::GENERAL,
                                      "GenericExpressionResolutionPass: Could not resolve member access '{}'",
                                      member_access->member());
                        }
                    }
                }

                resolve_expression(binary->left(), TypeRef{}, ctx);
                resolve_expression(binary->right(), lhs_type.is_valid() ? lhs_type : expected_type, ctx);
            }
            else
            {
                resolve_expression(binary->left(), TypeRef{}, ctx);
                resolve_expression(binary->right(), TypeRef{}, ctx);
            }
            break;
        }
        case NodeKind::StructLiteral:
        {
            auto *struct_lit = static_cast<StructLiteralNode *>(expr);
            std::string struct_name = struct_lit->struct_type();

            LOG_DEBUG(LogComponent::GENERAL,
                      "GenericExpressionResolutionPass: Processing struct literal for '{}'", struct_name);

            // Try to look up the struct type to get field types
            TypeRef struct_type_ref = _compiler.symbol_table()->lookup_struct_type(struct_name);
            const StructType *struct_type = nullptr;

            if (struct_type_ref.is_valid() && struct_type_ref->kind() == TypeKind::Struct)
            {
                struct_type = static_cast<const StructType *>(struct_type_ref.get());
                LOG_DEBUG(LogComponent::GENERAL,
                          "GenericExpressionResolutionPass: Found struct type '{}' with {} fields",
                          struct_name, struct_type->field_count());
            }
            else
            {
                LOG_DEBUG(LogComponent::GENERAL,
                          "GenericExpressionResolutionPass: Could not find struct type '{}'", struct_name);
            }

            for (auto &field_init : struct_lit->field_initializers())
            {
                TypeRef field_type;
                const std::string &field_name = field_init->field_name();

                if (struct_type)
                {
                    auto field_type_opt = struct_type->field_type(field_name);
                    if (field_type_opt)
                    {
                        field_type = *field_type_opt;
                        LOG_DEBUG(LogComponent::GENERAL,
                                  "GenericExpressionResolutionPass: Field '{}' has type '{}' (kind={}, generic_params={})",
                                  field_name,
                                  field_type->display_name(),
                                  static_cast<int>(field_type->kind()),
                                  contains_generic_params(field_type) ? "yes" : "no");

                        // If the field type is an InstantiatedType, try to get the concrete type
                        if (field_type->kind() == TypeKind::InstantiatedType)
                        {
                            TypeRef concrete = lookup_concrete_type(field_type, ctx);
                            if (concrete.is_valid())
                            {
                                LOG_DEBUG(LogComponent::GENERAL,
                                          "GenericExpressionResolutionPass: Using concrete type '{}' for field '{}'",
                                          concrete->display_name(), field_name);
                                field_type = concrete;
                            }
                        }
                    }
                    else
                    {
                        LOG_DEBUG(LogComponent::GENERAL,
                                  "GenericExpressionResolutionPass: Field '{}' type not found in struct '{}', trying AST fallback",
                                  field_name, struct_name);

                        // Try to find the field type from AST annotation
                        auto *program = _compiler.ast_root();
                        if (program)
                        {
                            for (const auto &decl : program->statements())
                            {
                                if (auto *struct_decl = dynamic_cast<StructDeclarationNode *>(decl.get()))
                                {
                                    if (struct_decl->name() == struct_name)
                                    {
                                        for (const auto &field : struct_decl->fields())
                                        {
                                            if (field->name() == field_name && field->has_type_annotation())
                                            {
                                                ResolutionContext res_ctx(_compiler.symbol_table()->current_module());
                                                TypeRef resolved = _compiler.type_resolver()->resolve(*field->type_annotation(), res_ctx);
                                                if (resolved.is_valid() && !resolved.is_error())
                                                {
                                                    LOG_DEBUG(LogComponent::GENERAL,
                                                              "GenericExpressionResolutionPass: Resolved field '{}' from AST to '{}'",
                                                              field_name, resolved->display_name());

                                                    field_type = resolved;
                                                    if (field_type->kind() == TypeKind::InstantiatedType)
                                                    {
                                                        TypeRef concrete = lookup_concrete_type(field_type, ctx);
                                                        if (concrete.is_valid())
                                                        {
                                                            LOG_DEBUG(LogComponent::GENERAL,
                                                                      "GenericExpressionResolutionPass: Using concrete type '{}' for field '{}'",
                                                                      concrete->display_name(), field_name);
                                                            field_type = concrete;
                                                        }
                                                    }
                                                }
                                                break;
                                            }
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                resolve_expression(field_init->value(), field_type, ctx);
            }
            break;
        }
        case NodeKind::MatchExpression:
        {
            // Handle match expressions - propagate expected type to arm bodies
            // This is crucial for generic enum variants inside match arms like:
            //   match (x) { Pattern => Option::None, ... }
            // where Option::None needs the expected type to be resolved
            auto *match_expr = static_cast<MatchExpressionNode *>(expr);

            // Resolve the match expression itself (no expected type)
            resolve_expression(match_expr->expression(), TypeRef{}, ctx);

            // Propagate expected type to each arm's body
            for (const auto &arm : match_expr->arms())
            {
                // Handle the arm body - it's a StatementNode (usually BlockStatementNode)
                auto *body = arm->body();
                if (body)
                {
                    // If the body is a block, process each statement
                    if (body->kind() == NodeKind::BlockStatement)
                    {
                        auto *block = static_cast<BlockStatementNode *>(body);
                        for (auto &stmt : block->statements())
                        {
                            // For expression statements, pass the expected type
                            // This handles cases like: { Option::None } or { Option::Some(x) }
                            if (stmt->kind() == NodeKind::ExpressionStatement)
                            {
                                auto *expr_stmt = static_cast<ExpressionStatementNode *>(stmt.get());
                                resolve_expression(expr_stmt->expression(), expected_type, ctx);
                            }
                            else
                            {
                                resolve_statement(stmt.get(), expected_type, ctx);
                            }
                        }
                    }
                    else
                    {
                        // Handle non-block body
                        resolve_statement(body, expected_type, ctx);
                    }
                }
            }
            break;
        }
        default:
            // For other expressions, continue walking child expressions
            break;
        }
    }

    TypeRef GenericExpressionResolutionPass::resolve_generic_enum_variant(
        const std::string &scope_name,
        const std::string &member_name,
        TypeRef expected_type,
        PassContext &ctx)
    {
        if (!expected_type.is_valid())
            return TypeRef{};

        // Unwrap TypeAlias to get the underlying type (e.g., AllocResult -> Result<void*, AllocError>)
        TypeRef unwrapped_expected = unwrap_type_alias(expected_type);
        if (!unwrapped_expected.is_valid())
            return TypeRef{};

        auto *generics = _compiler.generic_registry();
        if (!generics)
            return TypeRef{};

        // Resolve type alias if applicable (e.g., IoResult -> Result)
        std::string resolved_scope_name = scope_name;
        auto *ast_ctx = _compiler.ast_context();
        if (ast_ctx)
        {
            ModuleTypeRegistry &module_registry = ast_ctx->modules();
            resolved_scope_name = module_registry.resolve_type_alias(scope_name);
            if (resolved_scope_name != scope_name)
            {
                LOG_DEBUG(LogComponent::GENERAL,
                          "resolve_generic_enum_variant: Resolved type alias '{}' -> '{}'",
                          scope_name, resolved_scope_name);
            }
        }

        // Check if unwrapped expected_type is an instantiated generic type
        if (unwrapped_expected->kind() != TypeKind::InstantiatedType)
        {
            // Could also be a monomorphized enum directly - check if scope_name matches
            if (unwrapped_expected->kind() == TypeKind::Enum)
            {
                auto *enum_type = static_cast<const EnumType *>(unwrapped_expected.get());
                std::string enum_name = enum_type->name();

                // Direct match
                if (enum_name == resolved_scope_name)
                {
                    return unwrapped_expected;
                }

                // Monomorphized name match (e.g., "Option_String" starts with "Option")
                // Check if the enum name starts with the scope name followed by "_"
                if (enum_name.size() > resolved_scope_name.size() &&
                    enum_name.substr(0, resolved_scope_name.size()) == resolved_scope_name &&
                    enum_name[resolved_scope_name.size()] == '_')
                {
                    LOG_DEBUG(LogComponent::GENERAL,
                              "resolve_generic_enum_variant: Matched monomorphized enum '{}' to generic '{}'",
                              enum_name, resolved_scope_name);
                    return unwrapped_expected;
                }

                // Also check if the enum is a generic enum that needs resolution
                // This handles cases where the EnumType is the base generic type
            }
            LOG_DEBUG(LogComponent::GENERAL,
                      "resolve_generic_enum_variant: unwrapped_expected is not InstantiatedType (kind={}, name='{}'), scope='{}', member='{}'",
                      static_cast<int>(unwrapped_expected->kind()),
                      unwrapped_expected->display_name(),
                      resolved_scope_name,
                      member_name);
            return TypeRef{};
        }

        auto *inst_type = static_cast<const InstantiatedType *>(unwrapped_expected.get());
        TypeRef base_type = inst_type->generic_base();

        if (!base_type.is_valid())
            return TypeRef{};

        // Check if the base type name matches the scope_name
        std::string base_name = base_type->display_name();
        // Handle qualified names like "Option<T>" - extract just "Option"
        size_t angle_pos = base_name.find('<');
        if (angle_pos != std::string::npos)
        {
            base_name = base_name.substr(0, angle_pos);
        }
        // Handle namespace prefixes
        size_t last_colon = base_name.rfind("::");
        if (last_colon != std::string::npos)
        {
            base_name = base_name.substr(last_colon + 2);
        }

        if (base_name != resolved_scope_name)
        {
            LOG_DEBUG(LogComponent::GENERAL,
                      "resolve_generic_enum_variant: base_name mismatch: '{}' != '{}' (scope='{}')",
                      base_name, resolved_scope_name, scope_name);
            return TypeRef{};
        }

        // Verify the member_name is a valid variant of this enum
        // First check if base_type is an enum
        if (base_type->kind() == TypeKind::Enum)
        {
            auto *base_enum = static_cast<const EnumType *>(base_type.get());
            if (!base_enum->get_variant(member_name))
            {
                // Not a valid variant
                return TypeRef{};
            }
        }

        // The unwrapped_expected (the InstantiatedType) is the correct resolved type
        // If it has a resolved_type (the monomorphized concrete enum), use that
        if (inst_type->has_resolved_type())
        {
            return inst_type->resolved_type();
        }

        // Try to look up the concrete monomorphized type
        TypeRef concrete = lookup_concrete_type(unwrapped_expected, ctx);
        if (concrete.is_valid())
        {
            LOG_DEBUG(LogComponent::GENERAL,
                      "resolve_generic_enum_variant: Found concrete type '{}' for InstantiatedType '{}'",
                      concrete->display_name(), unwrapped_expected->display_name());
            return concrete;
        }

        // Otherwise, use the InstantiatedType itself - it will be resolved during codegen
        return unwrapped_expected;
    }

    TypeRef GenericExpressionResolutionPass::resolve_generic_static_method(
        const std::string &scope_name,
        const std::vector<std::string> &generic_args,
        PassContext &ctx)
    {
        if (generic_args.empty())
            return TypeRef{};

        auto *generics = _compiler.generic_registry();
        auto *type_resolver = _compiler.type_resolver();
        auto *ast_ctx = _compiler.ast_context();

        if (!generics || !type_resolver || !ast_ctx)
            return TypeRef{};

        // 1. Look up generic template by name
        auto tmpl = generics->get_template_by_name(scope_name);
        if (!tmpl)
        {
            LOG_DEBUG(LogComponent::GENERAL,
                      "resolve_generic_static_method: No template found for '{}'",
                      scope_name);
            return TypeRef{};
        }

        // 2. Resolve string args to TypeRefs
        TypeArena &arena = ast_ctx->types();
        ResolutionContext res_ctx(_compiler.symbol_table()->current_module());

        std::vector<TypeRef> type_args;
        for (const std::string &arg_str : generic_args)
        {
            TypeRef arg_type = type_resolver->resolve_string(arg_str, res_ctx);
            if (!arg_type.is_valid() || arg_type.is_error())
            {
                LOG_DEBUG(LogComponent::GENERAL,
                          "resolve_generic_static_method: Failed to resolve type arg '{}'",
                          arg_str);
                return TypeRef{};
            }
            type_args.push_back(arg_type);
        }

        // 3. Create InstantiatedType
        TypeRef result = generics->instantiate(tmpl->generic_type, type_args, arena);
        if (result.is_valid())
        {
            LOG_DEBUG(LogComponent::GENERAL,
                      "resolve_generic_static_method: Resolved {} to '{}'",
                      scope_name, result->display_name());
        }
        return result;
    }

    TypeRef GenericExpressionResolutionPass::lookup_concrete_type(TypeRef inst_type, PassContext &ctx)
    {
        if (!inst_type.is_valid() || inst_type->kind() != TypeKind::InstantiatedType)
            return TypeRef{};

        auto *inst = static_cast<const InstantiatedType *>(inst_type.get());

        // If already has resolved type, return it
        if (inst->has_resolved_type())
            return inst->resolved_type();

        TypeRef base = inst->generic_base();
        if (!base.is_valid())
            return TypeRef{};

        // Try with generic registry's instantiation cache first - most reliable
        auto *generics = _compiler.generic_registry();
        if (generics)
        {
            auto cached = generics->get_cached_instantiation(base, inst->type_args());
            if (cached && cached->is_valid())
            {
                if (cached->get()->kind() == TypeKind::InstantiatedType)
                {
                    auto *cached_inst = static_cast<const InstantiatedType *>(cached->get());
                    if (cached_inst->has_resolved_type())
                    {
                        LOG_DEBUG(LogComponent::GENERAL,
                                  "lookup_concrete_type: Found in generic registry cache: '{}'",
                                  cached_inst->resolved_type()->display_name());
                        return cached_inst->resolved_type();
                    }
                }
            }

            // If not in cache with resolved_type, try to get the template and use monomorphizer
            auto *mono = ctx.monomorphizer();
            if (mono && generics->is_template(base))
            {
                LOG_DEBUG(LogComponent::GENERAL,
                          "lookup_concrete_type: Attempting direct monomorphization for '{}'",
                          inst_type->display_name());

                // Specialize the type using monomorphizer
                auto result = mono->specialize(base, inst->type_args());
                if (result.is_ok())
                {
                    TypeRef specialized = result.specialized_type;
                    if (specialized.is_valid() && specialized->kind() == TypeKind::InstantiatedType)
                    {
                        auto *spec_inst = static_cast<const InstantiatedType *>(specialized.get());
                        if (spec_inst->has_resolved_type())
                        {
                            LOG_DEBUG(LogComponent::GENERAL,
                                      "lookup_concrete_type: Monomorphization succeeded: '{}'",
                                      spec_inst->resolved_type()->display_name());
                            return spec_inst->resolved_type();
                        }
                    }
                }
                else
                {
                    LOG_DEBUG(LogComponent::GENERAL,
                              "lookup_concrete_type: Monomorphization failed for '{}': {}",
                              inst_type->display_name(), result.error_message);
                }
            }
        }

        // Build the mangled name matching Monomorphizer's convention
        // Monomorphizer uses: base.display_name() + "_" + arg1_name + "_" + arg2_name...
        std::string base_display = base->display_name();

        std::ostringstream oss;
        oss << base_display;

        if (!inst->type_args().empty())
        {
            oss << "_";
            for (size_t i = 0; i < inst->type_args().size(); ++i)
            {
                if (i > 0)
                    oss << "_";
                const auto &arg = inst->type_args()[i];
                if (arg.is_valid())
                {
                    // Use the same cleanup as Monomorphizer
                    std::string arg_name = arg->display_name();
                    std::replace(arg_name.begin(), arg_name.end(), '<', '_');
                    std::replace(arg_name.begin(), arg_name.end(), '>', '_');
                    std::replace(arg_name.begin(), arg_name.end(), ',', '_');
                    std::replace(arg_name.begin(), arg_name.end(), ' ', '_');
                    std::replace(arg_name.begin(), arg_name.end(), ':', '_');
                    oss << arg_name;
                }
                else
                {
                    oss << "unknown";
                }
            }
        }

        std::string mangled_name = oss.str();
        LOG_DEBUG(LogComponent::GENERAL,
                  "lookup_concrete_type: Looking for mangled name '{}' (base='{}')",
                  mangled_name, base_display);

        // Look up in symbol table (try enum first since we're typically dealing with generic enums)
        TypeRef concrete = _compiler.symbol_table()->lookup_enum_type(mangled_name);
        if (concrete.is_valid())
        {
            LOG_DEBUG(LogComponent::GENERAL,
                      "lookup_concrete_type: Found enum in symbol table: '{}'", concrete->display_name());
            return concrete;
        }

        // Also try struct lookup in case it's a generic struct
        concrete = _compiler.symbol_table()->lookup_struct_type(mangled_name);
        if (concrete.is_valid())
        {
            LOG_DEBUG(LogComponent::GENERAL,
                      "lookup_concrete_type: Found struct in symbol table: '{}'", concrete->display_name());
            return concrete;
        }

        // Try looking up in the AST context's type registry
        concrete = _compiler.ast_context()->types().lookup_type_by_name(mangled_name);
        if (concrete.is_valid())
        {
            LOG_DEBUG(LogComponent::GENERAL,
                      "lookup_concrete_type: Found in type registry: '{}'", concrete->display_name());
            return concrete;
        }

        // Try with simplified base name (no angle brackets)
        std::string simple_base = base_display;
        size_t angle_pos = simple_base.find('<');
        if (angle_pos != std::string::npos)
            simple_base = simple_base.substr(0, angle_pos);

        if (simple_base != base_display)
        {
            // Rebuild mangled name with simplified base
            std::ostringstream simple_oss;
            simple_oss << simple_base;
            if (!inst->type_args().empty())
            {
                simple_oss << "_";
                for (size_t i = 0; i < inst->type_args().size(); ++i)
                {
                    if (i > 0)
                        simple_oss << "_";
                    const auto &arg = inst->type_args()[i];
                    if (arg.is_valid())
                    {
                        std::string arg_name = arg->display_name();
                        std::replace(arg_name.begin(), arg_name.end(), '<', '_');
                        std::replace(arg_name.begin(), arg_name.end(), '>', '_');
                        std::replace(arg_name.begin(), arg_name.end(), ',', '_');
                        std::replace(arg_name.begin(), arg_name.end(), ' ', '_');
                        std::replace(arg_name.begin(), arg_name.end(), ':', '_');
                        oss << arg_name;
                    }
                    else
                    {
                        simple_oss << "unknown";
                    }
                }
            }
            std::string simple_mangled = simple_oss.str();

            LOG_DEBUG(LogComponent::GENERAL,
                      "lookup_concrete_type: Trying simplified name '{}'", simple_mangled);

            concrete = _compiler.symbol_table()->lookup_enum_type(simple_mangled);
            if (concrete.is_valid())
            {
                LOG_DEBUG(LogComponent::GENERAL,
                          "lookup_concrete_type: Found with simplified name: '{}'", concrete->display_name());
                return concrete;
            }

            concrete = _compiler.symbol_table()->lookup_struct_type(simple_mangled);
            if (concrete.is_valid())
            {
                LOG_DEBUG(LogComponent::GENERAL,
                          "lookup_concrete_type: Found struct with simplified name: '{}'", concrete->display_name());
                return concrete;
            }

            concrete = _compiler.ast_context()->types().lookup_type_by_name(simple_mangled);
            if (concrete.is_valid())
            {
                LOG_DEBUG(LogComponent::GENERAL,
                          "lookup_concrete_type: Found in type registry with simplified name: '{}'",
                          concrete->display_name());
                return concrete;
            }
        }

        LOG_DEBUG(LogComponent::GENERAL,
                  "lookup_concrete_type: Could not find concrete type for '{}' or '{}'",
                  mangled_name, simple_base + "_...");
        return TypeRef{};
    }

    TypeRef GenericExpressionResolutionPass::resolve_member_access_type(MemberAccessNode *member_access, PassContext &ctx)
    {
        if (!member_access)
            return TypeRef{};

        // Get the object type
        TypeRef obj_type = member_access->object()->get_resolved_type();

        // Special case: if the object is 'this' without resolved type, use tracked struct type
        if (!obj_type.is_valid())
        {
            if (auto *obj_id = dynamic_cast<IdentifierNode *>(member_access->object()))
            {
                if (obj_id->name() == "this" && _current_struct_type.is_valid())
                {
                    obj_type = _current_struct_type;
                }
                else
                {
                    // Try looking up in local variable types
                    auto it = _local_variable_types.find(obj_id->name());
                    if (it != _local_variable_types.end())
                    {
                        obj_type = it->second;
                    }
                }
            }
            // Handle nested member access (e.g., a.b.c)
            else if (auto *nested_access = dynamic_cast<MemberAccessNode *>(member_access->object()))
            {
                obj_type = resolve_member_access_type(nested_access, ctx);
            }
        }

        if (!obj_type.is_valid())
            return TypeRef{};

        const std::string &field_name = member_access->member();

        // Look up the member type from the object type
        if (obj_type->kind() == TypeKind::Struct)
        {
            auto *struct_type = static_cast<const StructType *>(obj_type.get());
            auto field_type_opt = struct_type->field_type(field_name);
            if (field_type_opt)
            {
                TypeRef field_type = *field_type_opt;
                // If the field type is an InstantiatedType, try to get the concrete type
                if (field_type->kind() == TypeKind::InstantiatedType)
                {
                    TypeRef concrete = lookup_concrete_type(field_type, ctx);
                    if (concrete.is_valid())
                        return concrete;
                }
                return field_type;
            }

            // Field type not found in StructType - try to resolve from AST annotation
            // This handles cases where field type population failed during earlier passes
            LOG_DEBUG(LogComponent::GENERAL,
                      "resolve_member_access_type: Field '{}' not found in StructType '{}', trying AST fallback",
                      field_name, struct_type->name());

            // Find the struct declaration AST to get the field annotation
            auto *program = _compiler.ast_root();
            if (program)
            {
                for (const auto &decl : program->statements())
                {
                    if (auto *struct_decl = dynamic_cast<StructDeclarationNode *>(decl.get()))
                    {
                        if (struct_decl->name() == struct_type->name())
                        {
                            // Found the struct declaration, look for the field
                            for (const auto &field : struct_decl->fields())
                            {
                                if (field->name() == field_name && field->has_type_annotation())
                                {
                                    // Try to resolve the type annotation
                                    ResolutionContext res_ctx(_compiler.symbol_table()->current_module());
                                    TypeRef resolved = _compiler.type_resolver()->resolve(*field->type_annotation(), res_ctx);
                                    if (resolved.is_valid() && !resolved.is_error())
                                    {
                                        LOG_DEBUG(LogComponent::GENERAL,
                                                  "resolve_member_access_type: Resolved field '{}' from AST to '{}'",
                                                  field_name, resolved->display_name());

                                        // If it's an InstantiatedType, try to get the concrete type
                                        if (resolved->kind() == TypeKind::InstantiatedType)
                                        {
                                            TypeRef concrete = lookup_concrete_type(resolved, ctx);
                                            if (concrete.is_valid())
                                                return concrete;
                                        }
                                        return resolved;
                                    }
                                }
                            }
                            break;
                        }
                    }
                }
            }
        }
        else if (obj_type->kind() == TypeKind::Class)
        {
            auto *class_type = static_cast<const ClassType *>(obj_type.get());
            auto field_type_opt = class_type->field_type(field_name);
            if (field_type_opt)
            {
                TypeRef field_type = *field_type_opt;
                // If the field type is an InstantiatedType, try to get the concrete type
                if (field_type->kind() == TypeKind::InstantiatedType)
                {
                    TypeRef concrete = lookup_concrete_type(field_type, ctx);
                    if (concrete.is_valid())
                        return concrete;
                }
                return field_type;
            }
        }

        return TypeRef{};
    }

    // ============================================================================
    // Stage 7: Codegen Preparation Passes
    // ============================================================================

    TypeLoweringPass::TypeLoweringPass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult TypeLoweringPass::run(PassContext &ctx)
    {
        auto *codegen = _compiler.codegen();
        if (!codegen)
        {
            ctx.emit_error(ErrorCode::E0600_CODEGEN_FAILED,
                           "CodeGenerator not available for type lowering");
            return PassResult::failure();
        }

        // Run the type lowering phase on the compiler instance
        _compiler.run_type_lowering_phase();

        LOG_DEBUG(LogComponent::GENERAL, "TypeLoweringPass: Types lowered to LLVM");
        return PassResult::ok({PassProvides::TYPES_LOWERED});
    }

    FunctionDeclarationPass::FunctionDeclarationPass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult FunctionDeclarationPass::run(PassContext &ctx)
    {
        auto *codegen = _compiler.codegen();
        if (!codegen)
        {
            ctx.emit_error(ErrorCode::E0600_CODEGEN_FAILED,
                           "CodeGenerator not available for function declaration");
            return PassResult::failure();
        }

        // Run the function declaration phase on the compiler instance
        _compiler.run_function_declaration_phase();

        LOG_DEBUG(LogComponent::GENERAL, "FunctionDeclarationPass: Functions declared in LLVM module");
        return PassResult::ok({PassProvides::FUNCTIONS_DECLARED});
    }

    // ============================================================================
    // Stage 8: IR Generation Passes
    // ============================================================================

    IRGenerationPass::IRGenerationPass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult IRGenerationPass::run(PassContext &ctx)
    {
        // Run the IR generation phase on the compiler instance
        if (!_compiler.run_ir_generation_phase())
        {
            // Errors already emitted to DiagEmitter
            return PassResult::failure();
        }

        LOG_DEBUG(LogComponent::GENERAL, "IRGenerationPass: LLVM IR generated");
        return PassResult::ok({PassProvides::IR_GENERATED});
    }

    // ============================================================================
    // Pass Factory Implementation
    // ============================================================================

    std::unique_ptr<PassManager> StandardPassFactory::create_standard_pipeline(CompilerInstance &compiler)
    {
        auto manager = std::make_unique<PassManager>();

        // Stage 1: Frontend
        manager->register_pass(std::make_unique<LexingPass>(compiler));
        manager->register_pass(std::make_unique<ParsingPass>(compiler));
        manager->register_pass(std::make_unique<ASTValidationPass>(compiler));

        // Stage 2: Module Resolution
        manager->register_pass(std::make_unique<AutoImportPass>(compiler));
        manager->register_pass(std::make_unique<ImportResolutionPass>(compiler));

        // Stage 3: Declaration Collection
        manager->register_pass(std::make_unique<TypeDeclarationPass>(compiler));
        manager->register_pass(std::make_unique<FunctionSignaturePass>(compiler));
        manager->register_pass(std::make_unique<TemplateRegistrationPass>(compiler));

        // Stage 4: Type Resolution
        manager->register_pass(std::make_unique<TypeResolutionPass>(compiler));
        manager->register_pass(std::make_unique<StructFieldTypeSyncPass>(compiler));

        // Stage 5: Semantic Analysis
        manager->register_pass(std::make_unique<DirectiveProcessingPass>(compiler));
        manager->register_pass(std::make_unique<FunctionBodyPass>(compiler));

        // Stage 6: Specialization
        manager->register_pass(std::make_unique<MonomorphizationPass>(compiler));
        manager->register_pass(std::make_unique<GenericExpressionResolutionPass>(compiler));

        // Stage 7: Codegen Preparation
        manager->register_pass(std::make_unique<TypeLoweringPass>(compiler));
        manager->register_pass(std::make_unique<FunctionDeclarationPass>(compiler));

        // Stage 8: IR Generation
        manager->register_pass(std::make_unique<IRGenerationPass>(compiler));

        return manager;
    }

    std::unique_ptr<PassManager> StandardPassFactory::create_frontend_pipeline(CompilerInstance &compiler)
    {
        auto manager = std::make_unique<PassManager>();

        // Stage 1: Frontend
        manager->register_pass(std::make_unique<LexingPass>(compiler));
        manager->register_pass(std::make_unique<ParsingPass>(compiler));
        manager->register_pass(std::make_unique<ASTValidationPass>(compiler));

        // Stage 2: Module Resolution
        manager->register_pass(std::make_unique<AutoImportPass>(compiler));
        manager->register_pass(std::make_unique<ImportResolutionPass>(compiler));

        // Stage 3: Declaration Collection
        manager->register_pass(std::make_unique<TypeDeclarationPass>(compiler));
        manager->register_pass(std::make_unique<FunctionSignaturePass>(compiler));
        manager->register_pass(std::make_unique<TemplateRegistrationPass>(compiler));

        // Stage 4: Type Resolution
        manager->register_pass(std::make_unique<TypeResolutionPass>(compiler));
        manager->register_pass(std::make_unique<StructFieldTypeSyncPass>(compiler));

        // Stage 5: Semantic Analysis (partial - for LSP)
        manager->register_pass(std::make_unique<DirectiveProcessingPass>(compiler));
        manager->register_pass(std::make_unique<FunctionBodyPass>(compiler));

        // No codegen passes for frontend-only mode

        return manager;
    }

    std::unique_ptr<PassManager> StandardPassFactory::create_stdlib_pipeline(CompilerInstance &compiler)
    {
        // Stdlib uses the same pipeline as standard, but with stdlib mode enabled
        return create_standard_pipeline(compiler);
    }

} // namespace Cryo
