#pragma once

#include "Compiler/PassManager.hpp"
#include "Types/TypeID.hpp"
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace Cryo
{
    // Forward declarations
    class CompilerInstance;
    class Lexer;
    class Parser;
    class FunctionDeclarationNode;
    class StatementNode;
    class ExpressionNode;
    class MemberAccessNode;
    class StructDeclarationNode;
    class ClassDeclarationNode;

    // ============================================================================
    // Stage 1: Frontend Passes
    // ============================================================================

    /**
     * @brief Pass 1.1: Lexical Analysis
     *
     * Tokenizes the source file into a stream of tokens.
     */
    class LexingPass : public CompilerPass
    {
    public:
        explicit LexingPass(CompilerInstance &compiler);

        std::string name() const override { return "Lexing"; }
        PassStage stage() const override { return PassStage::Frontend; }
        int order() const override { return 1; }
        PassScope scope() const override { return PassScope::PerFile; }

        std::vector<std::string> provides() const override
        {
            return {PassProvides::TOKENS};
        }

        std::string description() const override
        {
            return "Tokenize source file";
        }

        PassResult run(PassContext &ctx) override;

    private:
        CompilerInstance &_compiler;
    };

    /**
     * @brief Pass 1.2: Parsing
     *
     * Parses tokens into an Abstract Syntax Tree.
     */
    class ParsingPass : public CompilerPass
    {
    public:
        explicit ParsingPass(CompilerInstance &compiler);

        std::string name() const override { return "Parsing"; }
        PassStage stage() const override { return PassStage::Frontend; }
        int order() const override { return 2; }
        PassScope scope() const override { return PassScope::PerFile; }

        std::vector<PassDependency> dependencies() const override
        {
            return {PassDependency::required(PassProvides::TOKENS)};
        }

        std::vector<std::string> provides() const override
        {
            return {PassProvides::AST};
        }

        std::string description() const override
        {
            return "Parse tokens into AST";
        }

        PassResult run(PassContext &ctx) override;

    private:
        CompilerInstance &_compiler;
    };

    /**
     * @brief Pass 1.3: AST Validation
     *
     * Validates basic AST structure and sanity.
     */
    class ASTValidationPass : public CompilerPass
    {
    public:
        explicit ASTValidationPass(CompilerInstance &compiler);

        std::string name() const override { return "ASTValidation"; }
        PassStage stage() const override { return PassStage::Frontend; }
        int order() const override { return 3; }
        PassScope scope() const override { return PassScope::PerFile; }

        std::vector<PassDependency> dependencies() const override
        {
            return {PassDependency::required(PassProvides::AST)};
        }

        std::vector<std::string> provides() const override
        {
            return {PassProvides::AST_VALIDATED};
        }

        std::string description() const override
        {
            return "Validate AST structure";
        }

        PassResult run(PassContext &ctx) override;

    private:
        CompilerInstance &_compiler;
    };

    /**
     * @brief Pass 1.4: C Header Import
     *
     * Processes C import blocks (name := extern "C" { #include ... }) by
     * running clang-20 -E on referenced headers, parsing the preprocessed
     * C with a custom minimal C parser, and injecting the resulting
     * function declarations into the ExternBlockNode.
     */
    class CHeaderImportPass : public CompilerPass
    {
    public:
        explicit CHeaderImportPass(CompilerInstance &compiler);

        std::string name() const override { return "CHeaderImport"; }
        PassStage stage() const override { return PassStage::Frontend; }
        int order() const override { return 4; }
        PassScope scope() const override { return PassScope::PerFile; }

        std::vector<PassDependency> dependencies() const override
        {
            return {PassDependency::required(PassProvides::AST_VALIDATED)};
        }

        std::vector<std::string> provides() const override
        {
            return {PassProvides::C_HEADERS_IMPORTED};
        }

        std::string description() const override
        {
            return "Import C header declarations via extern C import blocks";
        }

        PassResult run(PassContext &ctx) override;

    private:
        CompilerInstance &_compiler;
    };

    // ============================================================================
    // Stage 2: Module Resolution Passes
    // ============================================================================

    /**
     * @brief Pass 2.1: Auto Import Injection
     *
     * Injects prelude and core type imports.
     */
    class AutoImportPass : public CompilerPass
    {
    public:
        explicit AutoImportPass(CompilerInstance &compiler);

        std::string name() const override { return "AutoImport"; }
        PassStage stage() const override { return PassStage::ModuleResolution; }
        int order() const override { return 1; }
        PassScope scope() const override { return PassScope::PerFile; }

        std::vector<PassDependency> dependencies() const override
        {
            return {PassDependency::required(PassProvides::C_HEADERS_IMPORTED)};
        }

        std::vector<std::string> provides() const override
        {
            return {PassProvides::IMPORTS_DISCOVERED};
        }

        std::string description() const override
        {
            return "Inject auto-imports (prelude, core types)";
        }

        PassResult run(PassContext &ctx) override;

    private:
        CompilerInstance &_compiler;
    };

    /**
     * @brief Pass 2.2: Import Resolution
     *
     * Loads all imported modules.
     */
    class ImportResolutionPass : public CompilerPass
    {
    public:
        explicit ImportResolutionPass(CompilerInstance &compiler);

        std::string name() const override { return "ImportResolution"; }
        PassStage stage() const override { return PassStage::ModuleResolution; }
        int order() const override { return 2; }
        PassScope scope() const override { return PassScope::PerModule; }

        std::vector<PassDependency> dependencies() const override
        {
            return {PassDependency::required(PassProvides::IMPORTS_DISCOVERED)};
        }

        std::vector<std::string> provides() const override
        {
            return {PassProvides::MODULES_LOADED, PassProvides::MODULE_ORDER};
        }

        std::string description() const override
        {
            return "Resolve and load imported modules";
        }

        PassResult run(PassContext &ctx) override;

    private:
        CompilerInstance &_compiler;
    };

    // ============================================================================
    // Stage 3: Declaration Collection Passes
    // ============================================================================

    /**
     * @brief Pass 3.1: Type Declaration Collection
     *
     * Collects all type declarations (structs, enums, traits, type aliases).
     */
    class TypeDeclarationPass : public CompilerPass
    {
    public:
        explicit TypeDeclarationPass(CompilerInstance &compiler);

        std::string name() const override { return "TypeDeclarations"; }
        PassStage stage() const override { return PassStage::DeclarationCollection; }
        int order() const override { return 1; }
        PassScope scope() const override { return PassScope::PerModule; }

        std::vector<PassDependency> dependencies() const override
        {
            return {PassDependency::required(PassProvides::MODULES_LOADED)};
        }

        std::vector<std::string> provides() const override
        {
            return {PassProvides::TYPE_DECLARATIONS};
        }

        std::string description() const override
        {
            return "Collect type declarations (structs, enums, traits)";
        }

        PassResult run(PassContext &ctx) override;

    private:
        CompilerInstance &_compiler;
    };

    /**
     * @brief Pass 3.2: Function Signature Collection
     *
     * Collects all function/method signatures (NOT bodies).
     */
    class FunctionSignaturePass : public CompilerPass
    {
    public:
        explicit FunctionSignaturePass(CompilerInstance &compiler);

        std::string name() const override { return "FunctionSignatures"; }
        PassStage stage() const override { return PassStage::DeclarationCollection; }
        int order() const override { return 2; }
        PassScope scope() const override { return PassScope::PerModule; }

        std::vector<PassDependency> dependencies() const override
        {
            return {PassDependency::required(PassProvides::TYPE_DECLARATIONS)};
        }

        std::vector<std::string> provides() const override
        {
            return {PassProvides::FUNCTION_SIGNATURES};
        }

        std::string description() const override
        {
            return "Collect function signatures";
        }

        PassResult run(PassContext &ctx) override;

    private:
        CompilerInstance &_compiler;
    };

    /**
     * @brief Pass 3.3: Template Registration
     *
     * Registers generic templates for later instantiation.
     */
    class TemplateRegistrationPass : public CompilerPass
    {
    public:
        explicit TemplateRegistrationPass(CompilerInstance &compiler);

        std::string name() const override { return "TemplateRegistration"; }
        PassStage stage() const override { return PassStage::DeclarationCollection; }
        int order() const override { return 3; }
        PassScope scope() const override { return PassScope::PerModule; }

        std::vector<PassDependency> dependencies() const override
        {
            return {PassDependency::required(PassProvides::FUNCTION_SIGNATURES)};
        }

        std::vector<std::string> provides() const override
        {
            return {PassProvides::TEMPLATES_REGISTERED};
        }

        std::string description() const override
        {
            return "Register generic templates";
        }

        PassResult run(PassContext &ctx) override;

    private:
        CompilerInstance &_compiler;
    };

    // ============================================================================
    // Stage 4: Type Resolution Passes
    // ============================================================================

    /**
     * @brief Pass 4.1: Type Resolution
     *
     * Resolves TypeAnnotations on AST nodes to TypeRefs.
     * This is the critical pass that converts generic type syntax like
     * "Result<Duration,SystemTimeError>" from error placeholders to
     * properly instantiated types.
     */
    class TypeResolutionPass : public CompilerPass
    {
    public:
        explicit TypeResolutionPass(CompilerInstance &compiler);

        std::string name() const override { return "TypeResolution"; }
        PassStage stage() const override { return PassStage::TypeResolution; }
        int order() const override { return 1; }
        PassScope scope() const override { return PassScope::PerModule; }

        std::vector<PassDependency> dependencies() const override
        {
            return {PassDependency::required(PassProvides::TEMPLATES_REGISTERED)};
        }

        std::vector<std::string> provides() const override
        {
            return {PassProvides::TYPES_RESOLVED};
        }

        std::string description() const override
        {
            return "Resolve type annotations to TypeRefs";
        }

        PassResult run(PassContext &ctx) override;

    private:
        CompilerInstance &_compiler;
    };

    /**
     * @brief Pass 4.2: Struct Field Type Sync
     *
     * Synchronizes resolved field types from AST nodes back to StructType/ClassType.
     * This pass runs AFTER TypeResolutionPass which resolves type annotations on AST nodes,
     * but the StructType fields may have been populated earlier (in populate_type_fields_pass)
     * with incomplete types. This pass ensures the StructType has the fully resolved field types.
     *
     * This is critical for generic types like Option<String> which can only be resolved
     * after templates are registered but before codegen needs the field types.
     */
    class StructFieldTypeSyncPass : public CompilerPass
    {
    public:
        explicit StructFieldTypeSyncPass(CompilerInstance &compiler);

        std::string name() const override { return "StructFieldTypeSync"; }
        PassStage stage() const override { return PassStage::TypeResolution; }
        int order() const override { return 2; }
        PassScope scope() const override { return PassScope::PerModule; }

        std::vector<PassDependency> dependencies() const override
        {
            return {PassDependency::required(PassProvides::TYPES_RESOLVED)};
        }

        std::vector<std::string> provides() const override
        {
            return {"struct_fields_synced"};
        }

        std::string description() const override
        {
            return "Sync resolved field types to StructTypes";
        }

        PassResult run(PassContext &ctx) override;

    private:
        CompilerInstance &_compiler;

        // Sync a single struct's fields from AST to StructType
        void sync_struct_fields(StructDeclarationNode *struct_decl, PassContext &ctx);

        // Sync a single class's fields from AST to ClassType
        void sync_class_fields(ClassDeclarationNode *class_decl, PassContext &ctx);
    };

    // ============================================================================
    // Stage 5: Semantic Analysis Passes
    // ============================================================================

    /**
     * @brief Pass 5.1: Directive Processing
     *
     * Processes compiler directives before type checking.
     */
    class DirectiveProcessingPass : public CompilerPass
    {
    public:
        explicit DirectiveProcessingPass(CompilerInstance &compiler);

        std::string name() const override { return "DirectiveProcessing"; }
        PassStage stage() const override { return PassStage::SemanticAnalysis; }
        int order() const override { return 1; }
        PassScope scope() const override { return PassScope::PerFile; }

        std::vector<PassDependency> dependencies() const override
        {
            return {PassDependency::required("struct_fields_synced")};
        }

        std::vector<std::string> provides() const override
        {
            return {PassProvides::DIRECTIVES_PROCESSED};
        }

        std::string description() const override
        {
            return "Process compiler directives";
        }

        PassResult run(PassContext &ctx) override;

    private:
        CompilerInstance &_compiler;
    };

    /**
     * @brief Pass 5.2: Function Body Type Checking
     *
     * Type checks all function bodies.
     */
    class FunctionBodyPass : public CompilerPass
    {
    public:
        explicit FunctionBodyPass(CompilerInstance &compiler);

        std::string name() const override { return "FunctionBodies"; }
        PassStage stage() const override { return PassStage::SemanticAnalysis; }
        int order() const override { return 2; }
        PassScope scope() const override { return PassScope::PerModule; }

        std::vector<PassDependency> dependencies() const override
        {
            return {
                PassDependency::required(PassProvides::DIRECTIVES_PROCESSED),
                PassDependency::required(PassProvides::TEMPLATES_REGISTERED)
            };
        }

        std::vector<std::string> provides() const override
        {
            return {PassProvides::BODIES_TYPE_CHECKED};
        }

        std::string description() const override
        {
            return "Type check function bodies";
        }

        PassResult run(PassContext &ctx) override;

    private:
        CompilerInstance &_compiler;
    };

    // ============================================================================
    // Stage 6: Specialization Passes
    // ============================================================================

    /**
     * @brief Pass 6.1: Monomorphization
     *
     * Generates specialized versions of generic types.
     */
    class MonomorphizationPass : public CompilerPass
    {
    public:
        explicit MonomorphizationPass(CompilerInstance &compiler);

        std::string name() const override { return "Monomorphization"; }
        PassStage stage() const override { return PassStage::Specialization; }
        int order() const override { return 1; }
        PassScope scope() const override { return PassScope::WholeProgram; }

        std::vector<PassDependency> dependencies() const override
        {
            return {PassDependency::required(PassProvides::BODIES_TYPE_CHECKED)};
        }

        std::vector<std::string> provides() const override
        {
            return {PassProvides::MONOMORPHIZATION_COMPLETE};
        }

        // Monomorphization failures are non-fatal
        bool is_fatal_on_failure() const override { return false; }

        std::string description() const override
        {
            return "Generate specialized generic instantiations";
        }

        PassResult run(PassContext &ctx) override;

    private:
        CompilerInstance &_compiler;
    };

    /**
     * @brief Pass 6.2: Generic Expression Resolution
     *
     * Resolves generic enum variants (like Option::None, Result::Ok) in expressions
     * by inferring the concrete type from context (return type, assignment target).
     *
     * This pass runs after monomorphization to ensure all concrete enum types exist,
     * and before codegen so that expressions have their types resolved.
     *
     * Example: In a function returning Option<Duration>, `return Option::None;`
     * will have the ScopeResolutionNode's resolved_type set to the concrete
     * Option<Duration> enum type.
     */
    class GenericExpressionResolutionPass : public CompilerPass
    {
    public:
        explicit GenericExpressionResolutionPass(CompilerInstance &compiler);

        std::string name() const override { return "GenericExpressionResolution"; }
        PassStage stage() const override { return PassStage::Specialization; }
        int order() const override { return 2; }
        PassScope scope() const override { return PassScope::PerModule; }

        std::vector<PassDependency> dependencies() const override
        {
            return {PassDependency::required(PassProvides::MONOMORPHIZATION_COMPLETE)};
        }

        std::vector<std::string> provides() const override
        {
            return {PassProvides::GENERIC_EXPRESSIONS_RESOLVED};
        }

        std::string description() const override
        {
            return "Resolve generic enum variants in expressions";
        }

        PassResult run(PassContext &ctx) override;

    private:
        CompilerInstance &_compiler;
        TypeRef _current_struct_type; // Track the current struct/class type for 'this' resolution
        std::unordered_map<std::string, TypeRef> _local_variable_types; // Track local variable types
        std::unordered_set<std::string> _immutable_locals;               // Track const/immutable local variables

        // Resolve generic enum variants in a function body
        void resolve_function_body(FunctionDeclarationNode *func, TypeRef struct_type, PassContext &ctx);

        // Resolve expressions within a statement, given an expected type context
        void resolve_statement(StatementNode *stmt, TypeRef expected_type, PassContext &ctx);

        // Resolve a single expression node
        void resolve_expression(ExpressionNode *expr, TypeRef expected_type, PassContext &ctx);

        // Check if scope_name is a generic enum and resolve against expected_type
        TypeRef resolve_generic_enum_variant(const std::string &scope_name,
                                              const std::string &member_name,
                                              TypeRef expected_type,
                                              PassContext &ctx);

        // Resolve generic static method calls like Array<String>::new()
        TypeRef resolve_generic_static_method(const std::string &scope_name,
                                               const std::vector<std::string> &generic_args,
                                               PassContext &ctx);

        // Look up the concrete type for an InstantiatedType from monomorphized types
        TypeRef lookup_concrete_type(TypeRef inst_type, PassContext &ctx);

        // Resolve the type of a member access expression (e.g., this.field, var.field)
        TypeRef resolve_member_access_type(MemberAccessNode *member_access, PassContext &ctx);
    };

    // ============================================================================
    // Stage 7: Codegen Preparation Passes
    // ============================================================================

    /**
     * @brief Pass 7.1: Type Lowering
     *
     * Lowers Cryo types to LLVM types.
     */
    class TypeLoweringPass : public CompilerPass
    {
    public:
        explicit TypeLoweringPass(CompilerInstance &compiler);

        std::string name() const override { return "TypeLowering"; }
        PassStage stage() const override { return PassStage::CodegenPreparation; }
        int order() const override { return 1; }
        PassScope scope() const override { return PassScope::PerModule; }

        std::vector<PassDependency> dependencies() const override
        {
            return {PassDependency::required(PassProvides::GENERIC_EXPRESSIONS_RESOLVED)};
        }

        std::vector<std::string> provides() const override
        {
            return {PassProvides::TYPES_LOWERED};
        }

        std::string description() const override
        {
            return "Lower Cryo types to LLVM types";
        }

        PassResult run(PassContext &ctx) override;

    private:
        CompilerInstance &_compiler;
    };

    /**
     * @brief Pass 7.2: Function Declaration
     *
     * Declares all functions in LLVM module (stubs without bodies).
     */
    class FunctionDeclarationPass : public CompilerPass
    {
    public:
        explicit FunctionDeclarationPass(CompilerInstance &compiler);

        std::string name() const override { return "FunctionDeclaration"; }
        PassStage stage() const override { return PassStage::CodegenPreparation; }
        int order() const override { return 2; }
        PassScope scope() const override { return PassScope::PerModule; }

        std::vector<PassDependency> dependencies() const override
        {
            return {PassDependency::required(PassProvides::TYPES_LOWERED)};
        }

        std::vector<std::string> provides() const override
        {
            return {PassProvides::FUNCTIONS_DECLARED};
        }

        std::string description() const override
        {
            return "Declare functions in LLVM module";
        }

        PassResult run(PassContext &ctx) override;

    private:
        CompilerInstance &_compiler;
    };

    // ============================================================================
    // Stage 8: IR Generation Passes
    // ============================================================================

    /**
     * @brief Pass 8.1: IR Generation
     *
     * Generates LLVM IR for all function bodies.
     */
    class IRGenerationPass : public CompilerPass
    {
    public:
        explicit IRGenerationPass(CompilerInstance &compiler);

        std::string name() const override { return "IRGeneration"; }
        PassStage stage() const override { return PassStage::IRGeneration; }
        int order() const override { return 1; }
        PassScope scope() const override { return PassScope::PerModule; }

        std::vector<PassDependency> dependencies() const override
        {
            return {PassDependency::required(PassProvides::FUNCTIONS_DECLARED)};
        }

        std::vector<std::string> provides() const override
        {
            return {PassProvides::IR_GENERATED};
        }

        std::string description() const override
        {
            return "Generate LLVM IR";
        }

        PassResult run(PassContext &ctx) override;

    private:
        CompilerInstance &_compiler;
    };

    // ============================================================================
    // Pass Factory
    // ============================================================================

    /**
     * @brief Creates a PassManager with all standard passes registered
     */
    class StandardPassFactory
    {
    public:
        /**
         * @brief Create a PassManager with all standard compilation passes
         */
        static std::unique_ptr<PassManager> create_standard_pipeline(CompilerInstance &compiler);

        /**
         * @brief Create a PassManager for frontend-only compilation (LSP mode)
         */
        static std::unique_ptr<PassManager> create_frontend_pipeline(CompilerInstance &compiler);

        /**
         * @brief Create a PassManager for stdlib compilation
         */
        static std::unique_ptr<PassManager> create_stdlib_pipeline(CompilerInstance &compiler);
    };

} // namespace Cryo
