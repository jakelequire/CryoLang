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
#include "Types/TypeChecker.hpp"

#include "Utils/Logger.hpp"

namespace Cryo::Codegen
{
    //===================================================================
    // Construction
    //===================================================================

    CodegenVisitor::CodegenVisitor(
        LLVMContextManager &context_manager,
        Cryo::SymbolTable &symbol_table,
        Cryo::DiagEmitter *diagnostics)
        : _ctx(std::make_unique<CodegenContext>(context_manager, symbol_table, diagnostics))
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

        // Expressions needs ControlFlow for match expressions
        _expressions->set_control_flow_codegen(_control_flow.get());

        // Declarations needs TypeCodegen for struct/class types
        _declarations->set_type_codegen(_types.get());

        // Declarations needs GenericCodegen for generic instantiation
        _declarations->set_generic_codegen(_generics.get());

        // GenericCodegen needs DeclarationCodegen for function body generation
        _generics->set_declaration_codegen(_declarations.get());

        // GenericCodegen needs TypeCodegen for type operations
        _generics->set_type_codegen(_types.get());

        // Wire TypeMapper to GenericCodegen for parameterized type instantiation
        // This allows TypeMapper to delegate generic type instantiation to GenericCodegen
        // We use get_instantiated_type which properly dispatches to either instantiate_struct
        // or instantiate_class based on whether the generic definition is a struct or class
        GenericCodegen *generics_ptr = _generics.get();
        _ctx->types().set_generic_instantiator(
            [generics_ptr](const std::string &generic_name,
                           const std::vector<TypeRef> &type_args) -> llvm::StructType *
            {
                llvm::Type *type = generics_ptr->get_instantiated_type(generic_name, type_args);
                return llvm::dyn_cast_or_null<llvm::StructType>(type);
            });

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
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting ProgramNode with {} statements",
                  node.statements().size());

        // Multi-pass processing to ensure proper dependency order
        // Order is critical:
        //   Pass 0: Enums (so enum variants can be used in types and code)
        //   Pass 1: Struct/Class TYPES only (so types can be used for globals and parameters)
        //   Pass 2: Global variables (so they can be used in struct methods and functions)
        //   Pass 3: Struct/Class METHOD BODIES (now globals are available)
        //   Pass 4: Functions, impl blocks, etc.

        // Pass 0: Process all enum declarations first (for enum variant resolution)
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Pass 0: Processing enum declarations");
        int enum_count = 0;
        for (const auto &stmt : node.statements())
        {
            if (!stmt)
                continue;
            // Check for direct EnumDeclarationNode
            if (auto *enum_decl = dynamic_cast<Cryo::EnumDeclarationNode *>(stmt.get()))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Pass 0: Found direct enum declaration: {}", enum_decl->name());
                stmt->accept(*this);
                enum_count++;
            }
            // Also check for DeclarationStatementNode wrapping an EnumDeclarationNode
            else if (auto *decl_stmt = dynamic_cast<Cryo::DeclarationStatementNode *>(stmt.get()))
            {
                if (decl_stmt->declaration())
                {
                    if (auto *inner_enum = dynamic_cast<Cryo::EnumDeclarationNode *>(decl_stmt->declaration()))
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Pass 0: Found wrapped enum declaration: {}", inner_enum->name());
                        stmt->accept(*this);
                        enum_count++;
                    }
                }
            }
        }
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Pass 0: Processed {} enum declarations", enum_count);

        // Pass 1: Generate struct/class TYPES only (NOT method bodies yet)
        // This allows globals to reference struct types, and struct methods to reference globals
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Pass 1: Processing struct/class TYPE declarations (types only, no method bodies)");
        _defer_method_generation = true; // Flag to skip method body generation
        for (const auto &stmt : node.statements())
        {
            if (!stmt)
                continue;
            // Check for direct StructDeclarationNode or ClassDeclarationNode
            if (dynamic_cast<Cryo::StructDeclarationNode *>(stmt.get()) ||
                dynamic_cast<Cryo::ClassDeclarationNode *>(stmt.get()))
            {
                stmt->accept(*this);
            }
            // Also check for DeclarationStatementNode wrapping a struct/class
            else if (auto *decl_stmt = dynamic_cast<Cryo::DeclarationStatementNode *>(stmt.get()))
            {
                if (decl_stmt->declaration() &&
                    (dynamic_cast<Cryo::StructDeclarationNode *>(decl_stmt->declaration()) ||
                     dynamic_cast<Cryo::ClassDeclarationNode *>(decl_stmt->declaration())))
                {
                    stmt->accept(*this);
                }
            }
        }
        _defer_method_generation = false;

        // Pass 2: Process all global variable/constant declarations (for identifier resolution)
        // IMPORTANT: Clear any insert point to ensure globals are detected as global, not local
        llvm::BasicBlock *prev_block = _ctx->builder().GetInsertBlock();
        if (prev_block)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Pass 2: Clearing stale insert block: {}",
                      prev_block->getName().str());
            _ctx->builder().ClearInsertionPoint();
        }
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Pass 2: Processing global variable/constant declarations");
        int var_count = 0;
        for (const auto &stmt : node.statements())
        {
            if (!stmt)
                continue;
            // Check for direct VariableDeclarationNode
            if (auto *var_decl = dynamic_cast<Cryo::VariableDeclarationNode *>(stmt.get()))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Pass 2: Found direct variable declaration: {}", var_decl->name());
                stmt->accept(*this);
                var_count++;
            }
            // Also check for DeclarationStatementNode wrapping a VariableDeclarationNode
            else if (auto *decl_stmt = dynamic_cast<Cryo::DeclarationStatementNode *>(stmt.get()))
            {
                if (decl_stmt->declaration())
                {
                    if (auto *inner_var = dynamic_cast<Cryo::VariableDeclarationNode *>(decl_stmt->declaration()))
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Pass 2: Found wrapped variable declaration: {}", inner_var->name());
                        stmt->accept(*this);
                        var_count++;
                    }
                }
            }
        }
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Pass 2: Processed {} global variable declarations", var_count);

        // Pass 2.5: Pre-register impl block method return types BEFORE generating struct method bodies
        // This ensures that when struct methods call impl block methods, the return types are already registered.
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Pass 2.5: Pre-registering impl block method return types (namespace_context='{}')",
                  _ctx->namespace_context());
        int impl_method_count = 0;
        int impl_block_count = 0;
        Cryo::TemplateRegistry *template_registry = _ctx->template_registry();
        for (const auto &stmt : node.statements())
        {
            if (!stmt)
                continue;
            Cryo::ImplementationBlockNode *impl_block = nullptr;
            if (auto *impl = dynamic_cast<Cryo::ImplementationBlockNode *>(stmt.get()))
            {
                impl_block = impl;
            }
            else if (auto *decl_stmt = dynamic_cast<Cryo::DeclarationStatementNode *>(stmt.get()))
            {
                if (decl_stmt->declaration())
                {
                    impl_block = dynamic_cast<Cryo::ImplementationBlockNode *>(decl_stmt->declaration());
                }
            }
            if (impl_block && template_registry)
            {
                impl_block_count++;
                std::string type_name = impl_block->target_type();
                std::string ns_context = _ctx->namespace_context();
                std::string qualified_type = ns_context.empty() ? type_name : ns_context + "::" + type_name;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Pass 2.5: Found impl block for '{}', qualified_type='{}', methods={}",
                          type_name, qualified_type, impl_block->method_implementations().size());

                // For generic types like "Option<T>", extract base name
                std::string base_type_name = type_name;
                size_t angle_pos = type_name.find('<');
                if (angle_pos != std::string::npos)
                {
                    base_type_name = type_name.substr(0, angle_pos);
                    qualified_type = ns_context.empty() ? base_type_name : ns_context + "::" + base_type_name;

                    // Register enum impl blocks for generic enums
                    // This allows the monomorphizer to generate methods during instantiation
                    const Cryo::TemplateRegistry::TemplateInfo *tmpl_info = template_registry->find_template(base_type_name);
                    if (tmpl_info && tmpl_info->enum_template)
                    {
                        template_registry->register_enum_impl_block(base_type_name, impl_block, ns_context);
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Pass 2.5: Registered enum impl block for '{}' (base: {})",
                                  type_name, base_type_name);
                    }
                }

                for (const auto &method : impl_block->method_implementations())
                {
                    Cryo::StructMethodNode *fn_node = method.get();
                    if (!fn_node)
                        continue;

                    std::string qualified_method_name = qualified_type + "::" + fn_node->name();

                    // Register string-based return type annotation from the parser
                    std::string return_type_str = "void";
                    if (fn_node->return_type_annotation())
                    {
                        return_type_str = fn_node->return_type_annotation()->to_string();
                    }

                    // Only register non-void return types
                    if (!return_type_str.empty() && return_type_str != "void")
                    {
                        template_registry->register_method_return_type_annotation(qualified_method_name, return_type_str);
                        impl_method_count++;
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Pass 2.5: Pre-registered method return type: {} -> {}",
                                  qualified_method_name, return_type_str);
                    }

                    // Also register resolved TypeRef if available
                    TypeRef return_type = fn_node->get_resolved_return_type();
                    if (return_type.is_valid() && !return_type.is_error())
                    {
                        template_registry->register_method_return_type(qualified_method_name, return_type);
                    }
                }
            }
        }
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Pass 2.5: Found {} impl blocks, pre-registered {} method return types", impl_block_count, impl_method_count);

        // Pass 2.6: Pre-register STRUCT-EMBEDDED method return types
        // This is critical for methods that call other methods defined later in the same struct.
        // Example: String::contains() calls String::find(), but find is declared after contains.
        // Without pre-registration, find's return type won't be known when generating contains body.
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Pass 2.6: Pre-registering struct-embedded method return types");
        int struct_method_count = 0;
        for (const auto &stmt : node.statements())
        {
            if (!stmt)
                continue;

            Cryo::StructDeclarationNode *struct_decl = nullptr;
            Cryo::ClassDeclarationNode *class_decl = nullptr;

            if (auto *s = dynamic_cast<Cryo::StructDeclarationNode *>(stmt.get()))
            {
                struct_decl = s;
            }
            else if (auto *c = dynamic_cast<Cryo::ClassDeclarationNode *>(stmt.get()))
            {
                class_decl = c;
            }
            else if (auto *decl_stmt = dynamic_cast<Cryo::DeclarationStatementNode *>(stmt.get()))
            {
                if (decl_stmt->declaration())
                {
                    struct_decl = dynamic_cast<Cryo::StructDeclarationNode *>(decl_stmt->declaration());
                    class_decl = dynamic_cast<Cryo::ClassDeclarationNode *>(decl_stmt->declaration());
                }
            }

            // Process struct-embedded methods
            if (struct_decl && template_registry && !struct_decl->methods().empty())
            {
                std::string type_name = struct_decl->name();
                std::string ns_context = _ctx->namespace_context();
                std::string qualified_type = ns_context.empty() ? type_name : ns_context + "::" + type_name;

                // Skip generic templates - their methods are registered when instantiated
                if (!struct_decl->generic_parameters().empty())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Pass 2.6: Skipping generic template struct '{}'", type_name);
                    continue;
                }

                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Pass 2.6: Processing struct '{}' ({} methods), qualified_type='{}'",
                          type_name, struct_decl->methods().size(), qualified_type);

                for (const auto &method : struct_decl->methods())
                {
                    if (!method)
                        continue;

                    // Skip generic methods
                    if (!method->generic_parameters().empty())
                        continue;

                    std::string qualified_method_name = qualified_type + "::" + method->name();

                    // Register string-based return type annotation from the parser
                    std::string return_type_str = "void";
                    if (method->return_type_annotation())
                    {
                        return_type_str = method->return_type_annotation()->to_string();
                        LOG_ERROR(Cryo::LogComponent::CODEGEN,
                                  "Pass 2.6: Method '{}' has return_type_annotation: '{}'",
                                  qualified_method_name, return_type_str);
                    }
                    else
                    {
                        LOG_ERROR(Cryo::LogComponent::CODEGEN,
                                  "Pass 2.6: Method '{}' has NO return_type_annotation (null)",
                                  qualified_method_name);
                    }

                    // Register non-void return types
                    if (!return_type_str.empty() && return_type_str != "void")
                    {
                        template_registry->register_method_return_type_annotation(qualified_method_name, return_type_str);
                        struct_method_count++;
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Pass 2.6: Pre-registered struct method return type: {} -> {}",
                                  qualified_method_name, return_type_str);
                    }

                    // Also register resolved TypeRef if available
                    TypeRef return_type = method->get_resolved_return_type();
                    if (return_type.is_valid() && !return_type.is_error())
                    {
                        template_registry->register_method_return_type(qualified_method_name, return_type);
                    }
                }
            }

            // Process class-embedded methods
            if (class_decl && template_registry && !class_decl->methods().empty())
            {
                std::string type_name = class_decl->name();
                std::string ns_context = _ctx->namespace_context();
                std::string qualified_type = ns_context.empty() ? type_name : ns_context + "::" + type_name;

                // Skip generic templates
                if (!class_decl->generic_parameters().empty())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Pass 2.6: Skipping generic template class '{}'", type_name);
                    continue;
                }

                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Pass 2.6: Processing class '{}' ({} methods), qualified_type='{}'",
                          type_name, class_decl->methods().size(), qualified_type);

                for (const auto &method : class_decl->methods())
                {
                    if (!method)
                        continue;

                    // Skip generic methods
                    if (!method->generic_parameters().empty())
                        continue;

                    std::string qualified_method_name = qualified_type + "::" + method->name();

                    // Register string-based return type annotation from the parser
                    std::string return_type_str = "void";
                    if (method->return_type_annotation())
                    {
                        return_type_str = method->return_type_annotation()->to_string();
                    }

                    // Register non-void return types
                    if (!return_type_str.empty() && return_type_str != "void")
                    {
                        template_registry->register_method_return_type_annotation(qualified_method_name, return_type_str);
                        struct_method_count++;
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Pass 2.6: Pre-registered class method return type: {} -> {}",
                                  qualified_method_name, return_type_str);
                    }

                    // Also register resolved TypeRef if available
                    TypeRef return_type = method->get_resolved_return_type();
                    if (return_type.is_valid() && !return_type.is_error())
                    {
                        template_registry->register_method_return_type(qualified_method_name, return_type);
                    }
                }
            }
        }
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Pass 2.6: Pre-registered {} struct/class method return types", struct_method_count);

        // Pass 3: Generate struct/class METHOD BODIES (now globals are available)
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Pass 3: Processing struct/class method bodies");
        _generate_method_bodies_only = true; // Flag to only generate method bodies, skip type generation
        for (const auto &stmt : node.statements())
        {
            if (!stmt)
                continue;
            // Check for direct StructDeclarationNode or ClassDeclarationNode
            if (dynamic_cast<Cryo::StructDeclarationNode *>(stmt.get()) ||
                dynamic_cast<Cryo::ClassDeclarationNode *>(stmt.get()))
            {
                stmt->accept(*this);
            }
            // Also check for DeclarationStatementNode wrapping a struct/class
            else if (auto *decl_stmt = dynamic_cast<Cryo::DeclarationStatementNode *>(stmt.get()))
            {
                if (decl_stmt->declaration() &&
                    (dynamic_cast<Cryo::StructDeclarationNode *>(decl_stmt->declaration()) ||
                     dynamic_cast<Cryo::ClassDeclarationNode *>(decl_stmt->declaration())))
                {
                    stmt->accept(*this);
                }
            }
        }
        _generate_method_bodies_only = false;

        // Pass 4: Process remaining declarations (functions, impl blocks, extern blocks, etc.)
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Pass 4: Processing remaining declarations (functions, impl blocks, etc.)");
        for (const auto &stmt : node.statements())
        {
            if (!stmt)
                continue;
            // Skip already processed types (direct declarations)
            if (dynamic_cast<Cryo::EnumDeclarationNode *>(stmt.get()) ||
                dynamic_cast<Cryo::StructDeclarationNode *>(stmt.get()) ||
                dynamic_cast<Cryo::ClassDeclarationNode *>(stmt.get()) ||
                dynamic_cast<Cryo::VariableDeclarationNode *>(stmt.get()))
            {
                continue;
            }
            // Skip StructMethodNodes at top level - they should only be processed within their parent context
            // (inside StructDeclarationNode or ImplementationBlockNode)
            if (dynamic_cast<Cryo::StructMethodNode *>(stmt.get()))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Pass 4: Skipping orphaned StructMethodNode at top level");
                continue;
            }
            // Skip DeclarationStatementNode wrapping already-processed declaration types
            if (auto *decl_stmt = dynamic_cast<Cryo::DeclarationStatementNode *>(stmt.get()))
            {
                auto *inner = decl_stmt->declaration();
                if (inner && (dynamic_cast<Cryo::EnumDeclarationNode *>(inner) ||
                              dynamic_cast<Cryo::StructDeclarationNode *>(inner) ||
                              dynamic_cast<Cryo::ClassDeclarationNode *>(inner) ||
                              dynamic_cast<Cryo::VariableDeclarationNode *>(inner) ||
                              dynamic_cast<Cryo::StructMethodNode *>(inner)))
                {
                    if (dynamic_cast<Cryo::StructMethodNode *>(inner))
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Pass 4: Skipping orphaned StructMethodNode wrapped in DeclarationStatementNode");
                    }
                    continue;
                }
            }
            stmt->accept(*this);
        }

        // Generate global constructors after all declarations are processed
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Generating global constructors...");
        generate_global_constructors();

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Completed ProgramNode processing");
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

    void CodegenVisitor::visit(Cryo::ModuleDeclarationNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting ModuleDeclarationNode");
        // Module declarations are processed during compilation but don't generate code
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
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting StructDeclarationNode: {} (defer_methods={}, methods_only={})",
                  node.name(), _defer_method_generation, _generate_method_bodies_only);

        // Check if this is a generic struct template
        if (!node.generic_parameters().empty())
        {
            // Register with GenericCodegen for later instantiation
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "CodegenVisitor: Registering generic struct template: {} with {} type parameters",
                      node.name(), node.generic_parameters().size());
            _generics->register_generic_type(node.name(), &node);

            // Also register the namespace for this generic type so instantiations
            // can properly qualify methods
            std::string ns_context = _ctx->namespace_context();
            if (!ns_context.empty())
            {
                _ctx->register_type_namespace(node.name(), ns_context);
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "CodegenVisitor: Registered generic struct '{}' namespace: {}",
                          node.name(), ns_context);
            }

            // Don't generate the template directly - it will be instantiated when used
            return;
        }

        // Pass 1: Generate struct TYPE only (when _defer_method_generation is true)
        llvm::StructType *struct_type = nullptr;
        if (!_generate_method_bodies_only)
        {
            // Generate the struct type
            struct_type = _types->generate_struct(&node);

            // If struct type generation failed (e.g., fields have unresolved types), skip method generation
            if (!struct_type)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "CodegenVisitor: Skipping method generation for struct {} - struct type could not be generated",
                          node.name());
                return;
            }
        }
        else
        {
            // In method bodies only mode, check if the struct type exists
            struct_type = llvm::StructType::getTypeByName(_ctx->llvm_context(), node.name());
            if (!struct_type)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "CodegenVisitor: Skipping method body generation for struct {} - struct type not found",
                          node.name());
                return;
            }
        }

        // Generate method declarations even in "type only" mode to enable forward references
        // This allows global variables and other code to call struct methods before their bodies are generated
        if (!node.methods().empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "CodegenVisitor: Generating method declarations for struct {} (defer_bodies={})",
                      node.name(), _defer_method_generation);

            // Set current type context for method generation
            std::string previous_type = _ctx->current_type_name();
            _ctx->set_current_type_name(node.name());

            // Always generate method declarations (function signatures) - needed for forward references
            for (const auto &method : node.methods())
            {
                if (method)
                {
                    _declarations->generate_method_declaration(method.get(), node.name());
                }
            }

            // Only generate method bodies if we're not deferring them
            if (!_defer_method_generation)
            {
                // Generate method bodies
                for (const auto &method : node.methods())
                {
                    if (method)
                    {
                        _declarations->generate_method(method.get());
                    }
                }
            }

            // Restore previous type context
            _ctx->set_current_type_name(previous_type);
        }

        // If we're in "type only" mode, we're done after generating declarations
        if (_defer_method_generation)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "CodegenVisitor: Deferred method body generation for struct {}", node.name());
            return;
        }

        // Generate struct methods if any are defined inline (this section is for legacy compatibility)
        if (!node.methods().empty() && false) // Disabled since we handle this above
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "CodegenVisitor: Generating {} methods for struct {}",
                      node.methods().size(), node.name());

            // Set current type context for method generation
            std::string previous_type = _ctx->current_type_name();
            _ctx->set_current_type_name(node.name());

            // Two-pass approach: First generate all method declarations (forward references)
            // This ensures that methods can call other methods defined later in the struct
            for (const auto &method : node.methods())
            {
                if (method)
                {
                    _declarations->generate_method_declaration(method.get(), node.name());
                }
            }

            // Second pass: Generate method bodies
            for (const auto &method : node.methods())
            {
                if (method)
                {
                    _declarations->generate_method(method.get());
                }
            }

            // Restore previous type context
            _ctx->set_current_type_name(previous_type);
        }
    }

    void CodegenVisitor::visit(Cryo::ClassDeclarationNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting ClassDeclarationNode: {} (defer_methods={}, methods_only={})",
                  node.name(), _defer_method_generation, _generate_method_bodies_only);

        // Check if this is a generic class template
        if (!node.generic_parameters().empty())
        {
            // Register with GenericCodegen for later instantiation
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "CodegenVisitor: Registering generic class template: {} with {} type parameters",
                      node.name(), node.generic_parameters().size());
            _generics->register_generic_type(node.name(), &node);

            // Also register the namespace for this generic type so instantiations
            // can properly qualify methods
            std::string ns_context = _ctx->namespace_context();
            if (!ns_context.empty())
            {
                _ctx->register_type_namespace(node.name(), ns_context);
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "CodegenVisitor: Registered generic class '{}' namespace: {}",
                          node.name(), ns_context);
            }

            // Don't generate the template directly - it will be instantiated when used
            return;
        }

        // Pass 1: Generate class TYPE only (when _defer_method_generation is true)
        llvm::StructType *class_type = nullptr;
        if (!_generate_method_bodies_only)
        {
            // Generate the class type (struct layout)
            class_type = _types->generate_class(&node);

            // If class type generation failed (e.g., fields have unresolved types), skip method generation
            if (!class_type)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "CodegenVisitor: Skipping method generation for class {} - class type could not be generated",
                          node.name());
                return;
            }
        }
        else
        {
            // In method bodies only mode, check if the class type exists
            class_type = llvm::StructType::getTypeByName(_ctx->llvm_context(), node.name());
            if (!class_type)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "CodegenVisitor: Skipping method body generation for class {} - class type not found",
                          node.name());
                return;
            }
        }

        // Generate method declarations even in "type only" mode to enable forward references
        // This allows global variables and other code to call class methods before their bodies are generated
        if (!node.methods().empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "CodegenVisitor: Generating method declarations for class {} (defer_bodies={})",
                      node.name(), _defer_method_generation);

            // Set current type context for method generation
            std::string previous_type = _ctx->current_type_name();
            _ctx->set_current_type_name(node.name());

            // Always generate method declarations (function signatures) - needed for forward references
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Generating method declarations for class {}", node.name());

            for (const auto &method : node.methods())
            {
                if (method)
                {
                    _declarations->generate_method_declaration(method.get(), node.name());
                }
            }

            // Only generate method bodies if we're not deferring them
            if (!_defer_method_generation)
            {
                // Second pass: Generate method bodies
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Generating method bodies for class {}", node.name());

                for (const auto &method : node.methods())
                {
                    if (method)
                    {
                        // Generate the method body (declaration already exists)
                        _declarations->generate_method(method.get());
                    }
                }
            }

            // Restore previous type context
            _ctx->set_current_type_name(previous_type);
        }

        // If we're in "type only" mode, we're done after generating declarations
        if (_defer_method_generation)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "CodegenVisitor: Deferred method body generation for class {}", node.name());
            return;
        }

        // Generate class methods (legacy compatibility section - now handled above)
        if (!node.methods().empty() && false) // Disabled since we handle this above
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "CodegenVisitor: Generating {} methods for class {}",
                      node.methods().size(), node.name());

            // Set current type context for method generation
            std::string previous_type = _ctx->current_type_name();
            _ctx->set_current_type_name(node.name());

            // Two-pass approach: First generate all method declarations (forward references)
            // This ensures that methods can call other methods defined later in the class
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Generating method declarations for class {}", node.name());

            for (const auto &method : node.methods())
            {
                if (method)
                {
                    _declarations->generate_method_declaration(method.get(), node.name());
                }
            }

            // Second pass: Generate method bodies
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Generating method bodies for class {}", node.name());

            for (const auto &method : node.methods())
            {
                if (method)
                {
                    // Generate the method body (declaration already exists)
                    _declarations->generate_method(method.get());
                }
            }

            // Restore previous type context
            _ctx->set_current_type_name(previous_type);
        }
    }

    void CodegenVisitor::visit(Cryo::EnumDeclarationNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting EnumDeclarationNode: {}", node.name());

        // Check if this is a generic enum template
        if (!node.generic_parameters().empty())
        {
            // Register with GenericCodegen for later instantiation
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "CodegenVisitor: Registering generic enum template: {} with {} type parameters",
                      node.name(), node.generic_parameters().size());
            _generics->register_generic_type(node.name(), &node);

            // Also register the namespace for this generic type so instantiations
            // can properly qualify methods and constructors
            std::string ns_context = _ctx->namespace_context();
            if (!ns_context.empty())
            {
                _ctx->register_type_namespace(node.name(), ns_context);
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "CodegenVisitor: Registered generic enum '{}' namespace: {}",
                          node.name(), ns_context);
            }

            // Don't generate the template directly - it will be instantiated when used
            return;
        }

        // Use DeclarationCodegen which properly handles complex enums with payloads
        // (generates tagged union types, constructor functions, and registers field types)
        _declarations->generate_enum_declaration(&node);
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

        // Skip methods of generic templates - they should only be instantiated with concrete types
        std::string current_type = _ctx->current_type_name();
        if (!current_type.empty() && _generics->is_generic_template(current_type))
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "CodegenVisitor: Skipping method '{}' of generic template '{}'",
                      node.name(), current_type);
            return;
        }

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
        for (const auto &stmt : node.block()->statements())
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
        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "=== EXPR STMT DEBUG: Visiting ExpressionStatementNode");
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting ExpressionStatementNode");

        // Generate expression for side effects
        if (node.expression())
        {
            node.expression()->accept(*this);

            // Clear the result - expression statements discard their value
            // This prevents lingering results from affecting subsequent operations
            _ctx->set_result(nullptr);
        }
        else
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "=== EXPR STMT DEBUG: No expression in statement!");
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
        else
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN,
                      "Binary generation returned null! File: {}, Line: {} : {}, Operator: {}",
                      node.source_file(), node.location().line(), node.location().column(),
                      TokenKindToString(node.operator_token().kind()));
            report_error("Binary expression generation failed for operator '" + TokenKindToString(node.operator_token().kind()) + "'", &node);
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

    void CodegenVisitor::visit(Cryo::IfExpressionNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting IfExpressionNode");

        llvm::Value *result = _expressions->generate_if_expression(&node);
        if (result)
        {
            _ctx->set_result(result);
            _ctx->register_value(&node, result);
        }
    }

    void CodegenVisitor::visit(Cryo::MatchExpressionNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting MatchExpressionNode");

        llvm::Value *result = _expressions->generate_match_expression(&node);
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

    void CodegenVisitor::visit(Cryo::AlignofExpressionNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting AlignofExpressionNode");

        llvm::Value *result = _expressions->generate_alignof(&node);
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

    void CodegenVisitor::visit(Cryo::TupleLiteralNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting TupleLiteralNode");

        llvm::Value *result = _expressions->generate_tuple_literal(&node);
        if (result)
        {
            _ctx->set_result(result);
            _ctx->register_value(&node, result);
        }
    }

    void CodegenVisitor::visit(Cryo::LambdaExpressionNode &node)
    {
        NodeTracker tracker(*_ctx, &node);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor: Visiting LambdaExpressionNode");

        llvm::Value *result = _expressions->generate_lambda(&node);
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
        // Emit to central diagnostic system
        _ctx->report_error(ErrorCode::E0600_CODEGEN_FAILED, message);
    }

    void CodegenVisitor::report_error(const std::string &message, Cryo::ASTNode *node)
    {
        _has_errors = true;
        _last_error = message;
        _errors.push_back(message);
        _ctx->report_error(ErrorCode::E0600_CODEGEN_FAILED, node, message);
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
        // TODO: Needs reimplementation for Types - specialized methods tracking not yet in TypeChecker
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor::import_specialized_methods - stub (needs Types reimplementation)");
    }

    void CodegenVisitor::import_namespace_aliases(const Cryo::TypeChecker &type_checker)
    {
        // TODO: Needs reimplementation for Types - SRM context not yet integrated with TypeChecker
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenVisitor::import_namespace_aliases - stub (needs Types reimplementation)");
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
        // When pre-registration mode is enabled, also defer method generation
        // This prevents method bodies from being generated during pre-registration,
        // which would cause them to be skipped during the actual Pass 3 method body generation
        _defer_method_generation = enabled;
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

    GenericCodegen *CodegenVisitor::get_generics() const
    {
        return _generics.get();
    }

} // namespace Cryo::Codegen
