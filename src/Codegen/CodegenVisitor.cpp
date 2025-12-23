#include "Codegen/CodegenVisitor.hpp"
#include "Codegen/CodegenContext.hpp"

// Include all component headers
#include "Codegen/Memory/MemoryCodegen.hpp"
#include "Codegen/Memory/ScopeManager.hpp"
#include "Codegen/Expressions/OperatorCodegen.hpp"
#include "Codegen/Expressions/CallCodegen.hpp"
#include "Codegen/Expressions/ExpressionCodegen.hpp"
#include "Codegen/Expressions/CastCodegen.hpp"
#include "Codegen/Statements/ControlFlowCodegen.hpp"
#include "Codegen/Statements/StatementCodegen.hpp"
#include "Codegen/Declarations/DeclarationCodegen.hpp"
#include "Codegen/Declarations/TypeCodegen.hpp"
#include "Codegen/Declarations/GenericCodegen.hpp"

#include "Utils/Logger.hpp"

namespace Cryo::Codegen
{
    //===================================================================
    // Construction
    //===================================================================

    CodegenVisitor::CodegenVisitor(
        LLVMContextManager &context_manager,
        Cryo::SymbolTable &symbol_table,
        Cryo::DiagnosticManager *gdm)
        : _ctx(std::make_unique<CodegenContext>(context_manager, symbol_table, gdm))
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Initializing new dispatcher-based visitor");

        // Set visitor reference in context
        _ctx->set_visitor(this);

        // Initialize all components
        initialize_components();

        // Wire up component dependencies
        wire_component_dependencies();

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Initialization complete");
    }

    CodegenVisitor::~CodegenVisitor() = default;

    void CodegenVisitor::initialize_components()
    {
        // Create all component instances
        _memory = std::make_unique<MemoryCodegen>(*_ctx);
        _scope = std::make_unique<ScopeManager>(*_ctx);
        _operators = std::make_unique<OperatorCodegen>(*_ctx);
        _calls = std::make_unique<CallCodegen>(*_ctx);
        _control_flow = std::make_unique<ControlFlowCodegen>(*_ctx);
        _statements = std::make_unique<StatementCodegen>(*_ctx);
        _declarations = std::make_unique<DeclarationCodegen>(*_ctx);
        _types = std::make_unique<TypeCodegen>(*_ctx);
        _generics = std::make_unique<GenericCodegen>(*_ctx);
        _expressions = std::make_unique<ExpressionCodegen>(*_ctx);
        _casts = std::make_unique<CastCodegen>(*_ctx);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: All components initialized");
    }

    void CodegenVisitor::wire_component_dependencies()
    {
        // Wire up inter-component dependencies
        // Statements needs ControlFlow for if/while/for/switch
        _statements->set_control_flow(_control_flow.get());

        // ControlFlow needs Statements for block generation
        _control_flow->set_statement_codegen(_statements.get());

        // Operators needs Calls for method calls in operator overloading
        _operators->set_call_codegen(_calls.get());

        // Expressions needs Operators for binary/unary operations
        _expressions->set_operator_codegen(_operators.get());

        // Expressions needs Calls for call expressions
        _expressions->set_call_codegen(_calls.get());

        // Expressions needs Casts for cast expressions
        _expressions->set_cast_codegen(_casts.get());

        // Declarations needs TypeCodegen for struct/class types
        _declarations->set_type_codegen(_types.get());

        // Declarations needs GenericCodegen for generic instantiation
        _declarations->set_generic_codegen(_generics.get());

        // GenericCodegen needs DeclarationCodegen for function body generation
        _generics->set_declaration_codegen(_declarations.get());

        // GenericCodegen needs TypeCodegen for type operations
        _generics->set_type_codegen(_types.get());

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Component dependencies wired");
    }

    //===================================================================
    // Main Generation Interface
    //===================================================================

    bool CodegenVisitor::generate_program(Cryo::ProgramNode *program)
    {
        if (!program)
        {
            report_error("Cannot generate program: null program node");
            return false;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Generating program");

        try
        {
            // Visit the program node
            program->accept(*this);

            if (_has_errors)
            {
                LOG_ERROR(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Program generation completed with errors");
                return false;
            }

            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Program generation successful");
            return true;
        }
        catch (const std::exception &e)
        {
            report_error(std::string("Program generation failed: ") + e.what());
            return false;
        }
    }

    llvm::Value *CodegenVisitor::get_generated_value(Cryo::ASTNode *node)
    {
        return _ctx->get_value(node);
    }

    void CodegenVisitor::set_source_info(const std::string &source_file, const std::string &namespace_context)
    {
        _ctx->set_source_info(source_file, namespace_context);
    }

    void CodegenVisitor::set_stdlib_compilation_mode(bool enable)
    {
        _stdlib_compilation_mode = enable;
    }

    void CodegenVisitor::set_imported_asts(const std::unordered_map<std::string, std::unique_ptr<Cryo::ProgramNode>> *imported_asts)
    {
        // Store for cross-module enum variant extraction
        // Components that need this can access via context
    }

    //===================================================================
    // AST Visitor Implementation - Declarations
    //===================================================================

    void CodegenVisitor::visit(Cryo::ProgramNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting ProgramNode");

        // Process all top-level declarations
        for (const auto &decl : node.declarations())
        {
            if (decl)
            {
                decl->accept(*this);
            }
        }
    }

    void CodegenVisitor::visit(Cryo::DeclarationNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting DeclarationNode");
        // Base class - dispatch to specific type
    }

    void CodegenVisitor::visit(Cryo::FunctionDeclarationNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting FunctionDeclarationNode: {}", node.name());

        _declarations->generate_function(&node);
    }

    void CodegenVisitor::visit(Cryo::IntrinsicDeclarationNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting IntrinsicDeclarationNode");
        // Intrinsics are handled by the Intrinsics class
    }

    void CodegenVisitor::visit(Cryo::IntrinsicConstDeclarationNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting IntrinsicConstDeclarationNode");
        // Intrinsic constants are handled at usage site
    }

    void CodegenVisitor::visit(Cryo::ImportDeclarationNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting ImportDeclarationNode");
        // Imports are resolved during linking
    }

    void CodegenVisitor::visit(Cryo::VariableDeclarationNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting VariableDeclarationNode: {}", node.name());

        _declarations->generate_variable(&node);
    }

    void CodegenVisitor::visit(Cryo::StructDeclarationNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting StructDeclarationNode: {}", node.name());

        _types->generate_struct(&node);
    }

    void CodegenVisitor::visit(Cryo::ClassDeclarationNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting ClassDeclarationNode: {}", node.name());

        _types->generate_class(&node);
    }

    void CodegenVisitor::visit(Cryo::EnumDeclarationNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting EnumDeclarationNode: {}", node.name());

        _types->generate_enum(&node);
    }

    void CodegenVisitor::visit(Cryo::EnumVariantNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting EnumVariantNode");
        // Enum variants are handled during enum generation
    }

    void CodegenVisitor::visit(Cryo::TypeAliasDeclarationNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting TypeAliasDeclarationNode");

        _types->generate_type_alias(&node);
    }

    void CodegenVisitor::visit(Cryo::TraitDeclarationNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting TraitDeclarationNode");
        // Traits are compile-time only
    }

    void CodegenVisitor::visit(Cryo::ImplementationBlockNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting ImplementationBlockNode");

        _declarations->generate_impl_block(&node);
    }

    void CodegenVisitor::visit(Cryo::ExternBlockNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting ExternBlockNode");

        _declarations->generate_extern_block(&node);
    }

    void CodegenVisitor::visit(Cryo::GenericParameterNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting GenericParameterNode");
        // Generic parameters are handled during instantiation
    }

    void CodegenVisitor::visit(Cryo::StructFieldNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting StructFieldNode");
        // Fields are handled during struct generation
    }

    void CodegenVisitor::visit(Cryo::StructMethodNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting StructMethodNode: {}", node.name());

        _declarations->generate_method(&node);
    }

    void CodegenVisitor::visit(Cryo::DirectiveNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting DirectiveNode");
        // Directives are compile-time only
    }

    //===================================================================
    // AST Visitor Implementation - Statements
    //===================================================================

    void CodegenVisitor::visit(Cryo::StatementNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting StatementNode");
        // Base class - dispatch to specific type
    }

    void CodegenVisitor::visit(Cryo::BlockStatementNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting BlockStatementNode");

        _statements->generate_block(&node);
    }

    void CodegenVisitor::visit(Cryo::UnsafeBlockStatementNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting UnsafeBlockStatementNode");

        // Unsafe blocks generate code like regular blocks
        // (safety checks are disabled at compile-time)
        for (const auto &stmt : node.body())
        {
            if (stmt)
            {
                stmt->accept(*this);
            }
        }
    }

    void CodegenVisitor::visit(Cryo::ReturnStatementNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting ReturnStatementNode");

        _control_flow->generate_return(&node);
    }

    void CodegenVisitor::visit(Cryo::IfStatementNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting IfStatementNode");

        _control_flow->generate_if(&node);
    }

    void CodegenVisitor::visit(Cryo::WhileStatementNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting WhileStatementNode");

        _control_flow->generate_while(&node);
    }

    void CodegenVisitor::visit(Cryo::ForStatementNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting ForStatementNode");

        _control_flow->generate_for(&node);
    }

    void CodegenVisitor::visit(Cryo::MatchStatementNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting MatchStatementNode");

        _control_flow->generate_match(&node);
    }

    void CodegenVisitor::visit(Cryo::SwitchStatementNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting SwitchStatementNode");

        _control_flow->generate_switch(&node);
    }

    void CodegenVisitor::visit(Cryo::CaseStatementNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting CaseStatementNode");
        // Cases are handled during switch generation
    }

    void CodegenVisitor::visit(Cryo::MatchArmNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting MatchArmNode");
        // Match arms are handled during match generation
    }

    void CodegenVisitor::visit(Cryo::PatternNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting PatternNode");
        // Patterns are handled during match generation
    }

    void CodegenVisitor::visit(Cryo::EnumPatternNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting EnumPatternNode");
        // Enum patterns are handled during match generation
    }

    void CodegenVisitor::visit(Cryo::ExpressionStatementNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting ExpressionStatementNode");

        // Generate expression for side effects
        if (node.expression())
        {
            node.expression()->accept(*this);
        }
    }

    void CodegenVisitor::visit(Cryo::DeclarationStatementNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting DeclarationStatementNode");

        // Visit the contained declaration
        if (node.declaration())
        {
            node.declaration()->accept(*this);
        }
    }

    void CodegenVisitor::visit(Cryo::BreakStatementNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting BreakStatementNode");

        _control_flow->generate_break(&node);
    }

    void CodegenVisitor::visit(Cryo::ContinueStatementNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting ContinueStatementNode");

        _control_flow->generate_continue(&node);
    }

    //===================================================================
    // AST Visitor Implementation - Expressions
    //===================================================================

    void CodegenVisitor::visit(Cryo::ExpressionNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting ExpressionNode");
        // Base class - dispatch to specific type
    }

    void CodegenVisitor::visit(Cryo::LiteralNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting LiteralNode");

        llvm::Value *result = _expressions->generate_literal(&node);
        if (result)
        {
            _ctx->set_result(result);
            _ctx->register_value(&node, result);
        }
    }

    void CodegenVisitor::visit(Cryo::IdentifierNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting IdentifierNode: {}", node.name());

        llvm::Value *result = _expressions->generate_identifier(&node);
        if (result)
        {
            _ctx->set_result(result);
            _ctx->register_value(&node, result);
        }
    }

    void CodegenVisitor::visit(Cryo::BinaryExpressionNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting BinaryExpressionNode");

        llvm::Value *result = _operators->generate_binary(&node);
        if (result)
        {
            _ctx->set_result(result);
            _ctx->register_value(&node, result);
        }
    }

    void CodegenVisitor::visit(Cryo::UnaryExpressionNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting UnaryExpressionNode");

        llvm::Value *result = _operators->generate_unary(&node);
        if (result)
        {
            _ctx->set_result(result);
            _ctx->register_value(&node, result);
        }
    }

    void CodegenVisitor::visit(Cryo::TernaryExpressionNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting TernaryExpressionNode");

        llvm::Value *result = _expressions->generate_ternary(&node);
        if (result)
        {
            _ctx->set_result(result);
            _ctx->register_value(&node, result);
        }
    }

    void CodegenVisitor::visit(Cryo::CallExpressionNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting CallExpressionNode");

        llvm::Value *result = _calls->generate(&node);
        if (result)
        {
            _ctx->set_result(result);
            _ctx->register_value(&node, result);
        }
    }

    void CodegenVisitor::visit(Cryo::NewExpressionNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting NewExpressionNode");

        llvm::Value *result = _expressions->generate_new(&node);
        if (result)
        {
            _ctx->set_result(result);
            _ctx->register_value(&node, result);
        }
    }

    void CodegenVisitor::visit(Cryo::SizeofExpressionNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting SizeofExpressionNode");

        llvm::Value *result = _expressions->generate_sizeof(&node);
        if (result)
        {
            _ctx->set_result(result);
            _ctx->register_value(&node, result);
        }
    }

    void CodegenVisitor::visit(Cryo::CastExpressionNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting CastExpressionNode");

        llvm::Value *result = _casts->generate_cast_expression(&node);
        if (result)
        {
            _ctx->set_result(result);
            _ctx->register_value(&node, result);
        }
    }

    void CodegenVisitor::visit(Cryo::StructLiteralNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting StructLiteralNode");

        llvm::Value *result = _expressions->generate_struct_literal(&node);
        if (result)
        {
            _ctx->set_result(result);
            _ctx->register_value(&node, result);
        }
    }

    void CodegenVisitor::visit(Cryo::ArrayLiteralNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting ArrayLiteralNode");

        llvm::Value *result = _expressions->generate_array_literal(&node);
        if (result)
        {
            _ctx->set_result(result);
            _ctx->register_value(&node, result);
        }
    }

    void CodegenVisitor::visit(Cryo::ArrayAccessNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting ArrayAccessNode");

        llvm::Value *result = _expressions->generate_array_access(&node);
        if (result)
        {
            _ctx->set_result(result);
            _ctx->register_value(&node, result);
        }
    }

    void CodegenVisitor::visit(Cryo::MemberAccessNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting MemberAccessNode");

        llvm::Value *result = _expressions->generate_member_access(&node);
        if (result)
        {
            _ctx->set_result(result);
            _ctx->register_value(&node, result);
        }
    }

    void CodegenVisitor::visit(Cryo::ScopeResolutionNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting ScopeResolutionNode");

        llvm::Value *result = _expressions->generate_scope_resolution(&node);
        if (result)
        {
            _ctx->set_result(result);
            _ctx->register_value(&node, result);
        }
    }

    //===================================================================
    // Error Handling
    //===================================================================

    void CodegenVisitor::clear_errors()
    {
        _has_errors = false;
        _last_error.clear();
        _errors.clear();
        _ctx->clear_errors();
    }

    void CodegenVisitor::report_error(const std::string &message)
    {
        _has_errors = true;
        _last_error = message;
        _errors.push_back(message);
        LOG_ERROR(Cryo::LogComponent::CODEGEN, "CodegenVisitor error: {}", message);
    }

    void CodegenVisitor::report_error(const std::string &message, Cryo::ASTNode *node)
    {
        _has_errors = true;
        _last_error = message;
        _errors.push_back(message);
        _ctx->report_error(ErrorCode::E0600_CODEGEN_ERROR, node, message);
    }

    //===================================================================
    // Public Accessors (for compatibility)
    //===================================================================

    void CodegenVisitor::pre_register_functions_from_symbol_table()
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Pre-registering functions from symbol table");
        _declarations->pre_register_functions();
    }

    void CodegenVisitor::import_specialized_methods(const Cryo::TypeChecker &type_checker)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Importing specialized methods");
        _declarations->import_specialized_methods(type_checker);
    }

    void CodegenVisitor::import_namespace_aliases(const Cryo::TypeChecker &type_checker)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Importing namespace aliases");
        // Namespace aliases are handled by SRM context
    }

    void CodegenVisitor::process_global_variables_recursively(ASTNode *node)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Processing global variables");
        _declarations->process_global_variables(node);
    }

    void CodegenVisitor::generate_global_constructors()
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Generating global constructors");
        _declarations->generate_global_constructors();
    }

    void CodegenVisitor::set_pre_registration_mode(bool enabled)
    {
        _pre_registration_mode = enabled;
        _declarations->set_pre_registration_mode(enabled);
    }

    TypeMapper *CodegenVisitor::get_type_mapper() const
    {
        return &_ctx->types();
    }

    Cryo::ASTNode *CodegenVisitor::get_current_node() const
    {
        return _ctx->current_node();
    }

    Cryo::SRM::SymbolResolutionManager *CodegenVisitor::get_srm_manager() const
    {
        return &_ctx->srm();
    }

    Cryo::SRM::SymbolResolutionContext *CodegenVisitor::get_srm_context() const
    {
        return &_ctx->srm_context();
    }

} // namespace Cryo::Codegen
