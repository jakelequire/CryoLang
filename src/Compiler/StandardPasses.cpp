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
#include "Types/GenericRegistry.hpp"
#include "Types/GenericTypes.hpp"
#include "Types/ModuleTypeRegistry.hpp"
#include "Types/Monomorphizer.hpp"
#include "Codegen/CodeGenerator.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "Diagnostics/Diag.hpp"
#include "Utils/Logger.hpp"

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

        // Set up resolution context
        ResolutionContext res_ctx;
        res_ctx.current_module = ModuleID::invalid(); // Use default for now

        // Collect imports from the symbol table
        auto *symbols = _compiler.symbol_table();
        if (symbols)
        {
            // The symbol table should have the imports
            // For now, we'll rely on the module registry
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

        // === PHASE 2: Resolve all type annotations ===
        LOG_DEBUG(LogComponent::GENERAL, "TypeResolutionPass: Phase 2 - Resolving type annotations");
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
                    if (ann && method->get_resolved_return_type().is_error())
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
                    if (ann && method->get_resolved_return_type().is_error())
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
                if (ann && func->get_resolved_return_type().is_error())
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
            // Handle type alias declarations - resolve target type annotation
            else if (auto *alias_decl = dynamic_cast<TypeAliasDeclarationNode *>(stmt.get()))
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
                                "TypeResolutionPass: Bound generic param '{}' (index {}) for type alias '{}'",
                                generic_params[i], i, alias_decl->alias_name());
                        }
                    }

                    TypeRef resolved = resolver.resolve(*target_ann, alias_ctx);
                    if (!resolved.is_error())
                    {
                        alias_decl->set_resolved_target_type(resolved);
                        resolved_count++;
                        LOG_DEBUG(LogComponent::GENERAL,
                            "TypeResolutionPass: Resolved type alias '{}' target to '{}'",
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
                            "TypeResolutionPass: Failed to resolve type alias '{}' target: {}",
                            alias_decl->alias_name(), resolved->display_name());
                    }
                }
                else if (alias_decl->has_resolved_target_type())
                {
                    // Already resolved at parse time - register in module registry for cross-module access
                    // But only for non-generic type aliases
                    if (!alias_decl->is_generic())
                    {
                        TypeRef target_type = alias_decl->get_resolved_target_type();
                        module_registry.register_type(res_ctx.current_module, alias_decl->alias_name(), target_type);
                        LOG_DEBUG(LogComponent::GENERAL,
                            "TypeResolutionPass: Registered pre-resolved type alias '{}' -> '{}'",
                            alias_decl->alias_name(), target_type->display_name());
                    }
                }
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

        // Stage 5: Semantic Analysis
        manager->register_pass(std::make_unique<DirectiveProcessingPass>(compiler));
        manager->register_pass(std::make_unique<FunctionBodyPass>(compiler));

        // Stage 6: Specialization
        manager->register_pass(std::make_unique<MonomorphizationPass>(compiler));

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
