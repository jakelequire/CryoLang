#include "Compiler/StandardPasses.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "Compiler/ModuleLoader.hpp"
#include "AST/ASTContext.hpp"
#include "AST/ASTNode.hpp"
#include "AST/TemplateRegistry.hpp"
#include "AST/DirectiveSystem.hpp"
#include "Types/SymbolTable.hpp"
#include "Types/TypeChecker.hpp"
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
        // Lexing is handled by the parser in the current implementation
        // This pass is a placeholder for future separation
        LOG_DEBUG(LogComponent::GENERAL, "LexingPass: Lexer initialization handled by parser");
        return PassResult::ok({PassProvides::TOKENS});
    }

    ParsingPass::ParsingPass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult ParsingPass::run(PassContext &ctx)
    {
        // The actual parsing is done by CompilerInstance::parse()
        // This pass validates that parsing was successful
        if (!ctx.ast_root())
        {
            ctx.emit_error(ErrorCode::E0900_INTERNAL_COMPILER_ERROR,
                           "AST root is null - parsing may have failed");
            return PassResult::failure();
        }

        // Check if parsing produced any errors
        if (ctx.has_errors())
        {
            return PassResult::failure();
        }

        LOG_DEBUG(LogComponent::GENERAL, "ParsingPass: AST successfully created");
        return PassResult::ok({PassProvides::AST});
    }

    ASTValidationPass::ASTValidationPass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult ASTValidationPass::run(PassContext &ctx)
    {
        if (!ctx.ast_root())
        {
            ctx.emit_error(ErrorCode::E0900_INTERNAL_COMPILER_ERROR,
                           "Cannot validate null AST");
            return PassResult::failure();
        }

        // Basic validation - check that we have a ProgramNode with statements
        auto *program = ctx.ast_root();
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
        // Auto imports are injected based on compilation mode
        // This is currently handled by inject_auto_imports() in CompilerInstance

        if (ctx.is_stdlib_mode())
        {
            LOG_DEBUG(LogComponent::GENERAL, "AutoImportPass: Skipping auto-imports in stdlib mode");
            return PassResult::ok({PassProvides::IMPORTS_DISCOVERED});
        }

        if (!ctx.auto_imports_enabled())
        {
            LOG_DEBUG(LogComponent::GENERAL, "AutoImportPass: Auto-imports disabled");
            return PassResult::ok({PassProvides::IMPORTS_DISCOVERED});
        }

        // The actual auto-import injection happens in CompilerInstance
        // This pass marks that the discovery phase is complete
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
        // Type declarations are collected during collect_declarations_pass
        // This pass represents the type declaration portion

        LOG_DEBUG(LogComponent::GENERAL, "TypeDeclarationPass: Type declarations collected");
        return PassResult::ok({PassProvides::TYPE_DECLARATIONS});
    }

    FunctionSignaturePass::FunctionSignaturePass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult FunctionSignaturePass::run(PassContext &ctx)
    {
        // Function signatures are collected during collect_declarations_pass
        // This pass represents the function signature portion

        LOG_DEBUG(LogComponent::GENERAL, "FunctionSignaturePass: Function signatures collected");
        return PassResult::ok({PassProvides::FUNCTION_SIGNATURES});
    }

    TemplateRegistrationPass::TemplateRegistrationPass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult TemplateRegistrationPass::run(PassContext &ctx)
    {
        // Template registration happens during collect_declarations_pass
        // This pass represents the template registration portion

        LOG_DEBUG(LogComponent::GENERAL, "TemplateRegistrationPass: Templates registered");
        return PassResult::ok({PassProvides::TEMPLATES_REGISTERED});
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
        // Directive processing is handled by CompilerInstance::process_directives()
        // This pass represents that phase

        LOG_DEBUG(LogComponent::GENERAL, "DirectiveProcessingPass: Directives processed");
        return PassResult::ok({PassProvides::DIRECTIVES_PROCESSED});
    }

    FunctionBodyPass::FunctionBodyPass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult FunctionBodyPass::run(PassContext &ctx)
    {
        // Function body processing is handled by populate_symbol_table_with_scope
        // This pass represents that phase

        // Check for type errors via the diagnostic system
        if (ctx.has_errors())
        {
            // Errors already emitted to DiagEmitter, just return failure
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
        auto *codegen = ctx.codegen();
        if (!codegen)
        {
            ctx.emit_error(ErrorCode::E0600_CODEGEN_FAILED,
                           "CodeGenerator not available for type lowering");
            return PassResult::failure();
        }

        // Type lowering is handled by process_struct_declarations_for_preregistration
        // in the current implementation

        LOG_DEBUG(LogComponent::GENERAL, "TypeLoweringPass: Types lowered to LLVM");
        return PassResult::ok({PassProvides::TYPES_LOWERED});
    }

    FunctionDeclarationPass::FunctionDeclarationPass(CompilerInstance &compiler)
        : _compiler(compiler)
    {
    }

    PassResult FunctionDeclarationPass::run(PassContext &ctx)
    {
        auto *codegen = ctx.codegen();
        if (!codegen)
        {
            ctx.emit_error(ErrorCode::E0600_CODEGEN_FAILED,
                           "CodeGenerator not available for function declaration");
            return PassResult::failure();
        }

        // Function pre-registration is handled by pre_register_functions_from_symbol_table
        // in the current implementation

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
        // IR generation is handled by CompilerInstance::generate_ir()
        // This pass represents that phase

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
