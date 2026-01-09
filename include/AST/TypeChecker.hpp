#pragma once
#include "AST/Type.hpp"
#include "AST/ASTNode.hpp"
#include "AST/ASTVisitor.hpp"
#include "AST/ASTContext.hpp"
#include "AST/GenericInstantiation.hpp"
#include "AST/SymbolTable.hpp"
#include "GDM/DiagnosticBuilders.hpp"
#include "Utils/SymbolResolutionManager.hpp"
#include "Lexer/lexer.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Cryo
{
    // Forward declarations
    class TypeContext;
    class SymbolTable;
    class DiagnosticManager;

    // Type checking error representation
    struct TypeError
    {
        enum class ErrorKind
        {
            TypeMismatch,
            UndefinedVariable,
            UndefinedFunction,
            RedefinedSymbol,
            InvalidOperation,
            InvalidAssignment,
            InvalidCast,
            IncompatibleTypes,
            TooManyArguments,
            TooFewArguments,
            NonCallableType,
            VoidValueUsage
        };

        ErrorKind kind;
        SourceLocation location;
        std::string message;
        Type *expected_type = nullptr;
        Type *actual_type = nullptr;

        TypeError(ErrorKind k, SourceLocation loc, const std::string &msg)
            : kind(k), location(loc), message(msg) {}

        TypeError(ErrorKind k, SourceLocation loc, const std::string &msg,
                  Type *expected, Type *actual)
            : kind(k), location(loc), message(msg),
              expected_type(expected), actual_type(actual) {}

        std::string to_string() const;
    };

    // Type checking warning representation
    struct TypeWarning
    {
        enum class WarningKind
        {
            PotentialDataLoss, // Narrowing conversion (i64 → i32)
            SignConversion,    // Sign change (i32 → u32)
            UnsafeConversion,  // Mixed sign/size conversion
            UnusedVariable,
            ImplicitConversion
        };

        WarningKind kind;
        SourceLocation location;
        std::string message;
        Type *from_type = nullptr;
        Type *to_type = nullptr;

        TypeWarning(WarningKind k, SourceLocation loc, const std::string &msg)
            : kind(k), location(loc), message(msg) {}

        TypeWarning(WarningKind k, SourceLocation loc, const std::string &msg,
                    Type *from, Type *to)
            : kind(k), location(loc), message(msg),
              from_type(from), to_type(to) {}

        std::string to_string() const;
    };

    // Symbol information for type checking
    // Forward declarations for AST nodes
    class StructDeclarationNode;
    class ClassDeclarationNode;
    class FunctionDeclarationNode;

    struct TypedSymbol
    {
        std::string name;
        Type *type;
        SourceLocation declaration_location;
        bool is_mutable = false;
        bool is_initialized = false;
        // Store AST node pointers for complex types to access their structure
        StructDeclarationNode *struct_node = nullptr;
        ClassDeclarationNode *class_node = nullptr;
        FunctionDeclarationNode *function_node = nullptr;

        TypedSymbol() = default; // Default constructor for containers
        TypedSymbol(const std::string &n, Type *t, SourceLocation loc)
            : name(n), type(t), declaration_location(loc) {}

        TypedSymbol(const std::string &n, Type *t, SourceLocation loc, StructDeclarationNode *snode)
            : name(n), type(t), declaration_location(loc), struct_node(snode) {}

        TypedSymbol(const std::string &n, Type *t, SourceLocation loc, ClassDeclarationNode *cnode)
            : name(n), type(t), declaration_location(loc), class_node(cnode) {}

        TypedSymbol(const std::string &n, Type *t, SourceLocation loc, FunctionDeclarationNode *fnode)
            : name(n), type(t), declaration_location(loc), function_node(fnode) {}
    };

    // Scoped symbol table for type checking
    class TypedSymbolTable
    {
    private:
        std::unordered_map<std::string, TypedSymbol> _symbols;
        std::unique_ptr<TypedSymbolTable> _parent_scope;

    public:
        TypedSymbolTable(std::unique_ptr<TypedSymbolTable> parent = nullptr)
            : _parent_scope(std::move(parent)) {}

        // Symbol management
        bool declare_symbol(const std::string &name, Type *type,
                            SourceLocation loc, bool is_mutable = false);
        bool declare_symbol(const std::string &name, Type *type,
                            SourceLocation loc, StructDeclarationNode *struct_node);
        bool declare_symbol(const std::string &name, Type *type,
                            SourceLocation loc, ClassDeclarationNode *class_node);
        bool declare_symbol(const std::string &name, Type *type,
                            SourceLocation loc, FunctionDeclarationNode *function_node);
        TypedSymbol *lookup_symbol(const std::string &name);
        TypedSymbol *lookup_symbol_in_any_namespace(const std::string &symbol_name);
        bool is_symbol_defined(const std::string &name);

        // Scope management
        std::unique_ptr<TypedSymbolTable> enter_scope();
        std::unique_ptr<TypedSymbolTable> exit_scope();

        // Utilities
        size_t symbol_count() const { return _symbols.size(); }
        bool has_parent() const { return _parent_scope != nullptr; }

        // Access symbols for detailed inspection
        const std::unordered_map<std::string, TypedSymbol> &get_symbols() const { return _symbols; }

        // Clear all symbols (useful for testing)
        void clear() { _symbols.clear(); }

        // Type table display
        void print_type_table(std::ostream &os = std::cout) const;

    private:
        void print_type_symbols(std::ostream &os, int scope_level) const;
        std::string determine_type_category(Type *type) const;
        std::string determine_flags(const TypedSymbol &symbol) const;
        std::string format_type_size(Type *type) const;
        void print_type_details(std::ostream &os, const TypedSymbol &symbol) const;
        std::string format_field(const std::string &text, int width) const;
        std::string format_field_colored(const std::string &colored_text, const std::string &plain_text, int width) const;
    };

    // Type checker visitor
    class TypeChecker : public BaseASTVisitor
    {
    private:
        TypeContext &_type_context;
        std::unique_ptr<TypedSymbolTable> _symbol_table;
        std::unique_ptr<TypeRegistry> _type_registry;
        std::vector<TypeError> _errors;
        std::vector<TypeWarning> _warnings;

        // Current function return type (for return statement checking)
        Type *_current_function_return_type = nullptr;

        // Current namespace context (for relative namespace resolution)
        std::string _current_namespace;

        // Current struct type (for 'this' keyword resolution)
        Type *_current_struct_type = nullptr;

        // Current struct name (for field tracking)
        std::string _current_struct_name;

        // Three-pass compilation state
        enum class ProcessingPass {
            STRUCT_TYPE_REGISTRATION,  // Pass 1a: Register struct types only
            METHOD_SIGNATURE_REGISTRATION, // Pass 1b: Register method signatures only
            BODY_PROCESSING           // Pass 2: Process method bodies only
        };
        ProcessingPass _current_pass = ProcessingPass::BODY_PROCESSING; // Default to normal processing

        // Type checking state
        bool _in_function = false;
        bool _in_loop = false;
        bool _in_call_expression = false;
        bool _in_unsafe_context = false;

        // Current match expression type (for pattern variable type inference)
        Type *_current_match_expr_type = nullptr;

        // Struct field tracking - maps struct name -> field name -> field type
        std::unordered_map<std::string, std::unordered_map<std::string, Type *>> _struct_fields;

        // Struct method tracking - maps struct name -> method name -> return type
        std::unordered_map<std::string, std::unordered_map<std::string, Type *>> _struct_methods;

        // Private method tracking - maps struct name -> private method name -> return type
        std::unordered_map<std::string, std::unordered_map<std::string, Type *>> _private_struct_methods;

        // Template parameter tracking - maps struct/class name -> template parameter names (e.g., "Pair" -> {"K", "V"})
        std::unordered_map<std::string, std::vector<std::string>> _template_parameters;

        // Current function's generic parameters and their trait bounds
        // Maps generic parameter name -> set of trait names (e.g., "T" -> {"Default", "Clone"})
        std::unordered_map<std::string, std::vector<std::string>> _current_generic_trait_bounds;

        // Generic context management
        struct GenericContext
        {
            std::string type_name;                      // Name of the generic type (e.g., "Array", "Option")
            std::unordered_set<std::string> parameters; // Set of generic parameters (e.g., {"T"})
            bool is_template_definition = true;         // true for template definitions, false for instantiations
            SourceLocation location;                    // Location where this generic context started

            GenericContext(const std::string &name, const std::vector<std::string> &params, SourceLocation loc)
                : type_name(name), parameters(params.begin(), params.end()), location(loc) {}
        };

        std::vector<GenericContext> _generic_context_stack;         // Stack of nested generic contexts
        std::vector<GenericInstantiation> _required_instantiations; // Track all generic instantiations needed

        // Generic context management methods
        void enter_generic_context(const std::string &type_name,
                                   const std::vector<std::string> &parameters,
                                   SourceLocation location);
        void exit_generic_context();
        bool is_in_generic_context() const;
        bool is_generic_parameter(const std::string &name) const;
        std::string get_current_generic_type() const;
        const std::vector<std::string> get_current_generic_parameters() const;

        // Helper method to parse comma-separated template arguments
        std::vector<std::string> parse_template_arguments(const std::string &args_str);

        // NEW: Token-based type resolution (preferred)
        Type *resolve_type_from_tokens(Lexer &lexer);
        Type *resolve_type_from_token_stream(const std::vector<Token> &tokens, size_t &index);

        // Generic instantiation tracking methods
        void track_instantiation(const std::string &base_name,
                                 const std::vector<std::string> &concrete_types,
                                 const std::string &instantiated_name,
                                 SourceLocation location);

        // Reference to main symbol table (for scope resolution lookups)
        const SymbolTable *_main_symbol_table = nullptr;

        // Symbol loading state tracking to prevent duplicate loading
        bool _builtin_symbols_loaded = false;
        bool _intrinsic_symbols_loaded = false;
        bool _user_symbols_loaded = false;
        bool _runtime_symbols_loaded = false;

        // Standard library compilation mode flag
        bool _stdlib_compilation_mode = false;

        // Diagnostic manager for error reporting (optional)
        DiagnosticManager *_diagnostic_manager = nullptr;
        std::string _source_file; // Current source file being checked
        bool _lsp_mode = false;   // LSP compilation mode

        // Diagnostic builder for enhanced error reporting
        std::unique_ptr<TypeCheckerDiagnosticBuilder> _diagnostic_builder;

        // Symbol Resolution Manager (SRM) for standardized naming
        std::unique_ptr<Cryo::SRM::SymbolResolutionContext> _srm_context;
        std::unique_ptr<Cryo::SRM::SymbolResolutionManager> _srm_manager;

        // Contextual typing support - used for array literal type inference
        Type *_current_expected_type = nullptr;

    public:
        TypeChecker(TypeContext &type_ctx);
        TypeChecker(TypeContext &type_ctx, DiagnosticManager *diagnostic_manager, const std::string &source_file = "");
        ~TypeChecker() = default;

        // Load built-in functions from main SymbolTable
        void load_builtin_symbols(const SymbolTable &main_symbol_table);
        void load_intrinsic_symbols(const SymbolTable &main_symbol_table);
        void load_runtime_symbols(const SymbolTable &main_symbol_table);
        void load_user_symbols(const SymbolTable &main_symbol_table);

        // Standard library compilation mode control
        void set_stdlib_compilation_mode(bool enable) { _stdlib_compilation_mode = enable; }

        // Generic type management
        void register_generic_type(const std::string &base_name, const std::vector<std::string> &param_names);
        void register_generic_enum_type(const std::string &base_name, const std::vector<std::string> &param_names);
        void register_builtin_generic_types();
        ParameterizedType *resolve_generic_type(const std::string &type_string);

        // Dynamic generic type discovery from AST nodes
        void discover_generic_types_from_ast(ProgramNode &program);
        void discover_generic_type_from_struct(StructDeclarationNode &struct_node);
        void discover_generic_type_from_class(ClassDeclarationNode &class_node);
        void discover_generic_type_from_enum(EnumDeclarationNode &enum_node);

        // Enhanced type resolution that considers generic context
        Type *resolve_type_with_generic_context(const std::string &type_string);

        // Generic instantiation tracking - public access for monomorphization
        const std::vector<GenericInstantiation> &get_required_instantiations() const;

        // Main entry point
        void check_program(ProgramNode &program);

        // Additional method to visit imported modules for complete symbol information
        void check_imported_modules(const std::unordered_map<std::string, std::unique_ptr<ProgramNode>> &imported_asts);

        // Set current namespace context (called by compiler)
        void set_current_namespace(const std::string &namespace_name) { _current_namespace = namespace_name; }

        // Set source file for error reporting
        void set_source_file(const std::string &source_file);

        // Access to TypeContext for monomorphization
        TypeContext &get_type_context() { return _type_context; }

        // Access to SRM context for import resolution
        Cryo::SRM::SymbolResolutionContext *get_srm_context() { return _srm_context.get(); }
        const Cryo::SRM::SymbolResolutionContext *get_srm_context() const { return _srm_context.get(); }

        // Error and warning handling
        const std::vector<TypeError> &errors() const { return _errors; }
        bool has_errors() const { return !_errors.empty(); }
        size_t error_count() const { return _errors.size(); }

        const std::vector<TypeWarning> &warnings() const { return _warnings; }
        bool has_warnings() const { return !_warnings.empty(); }
        size_t warning_count() const { return _warnings.size(); }

        // Diagnostic manager status
        bool has_diagnostic_manager() const { return _diagnostic_manager != nullptr; }

        // Type table access
        void print_type_table(std::ostream &os = std::cout) const { _symbol_table->print_type_table(os); }

        // Clear symbol table (useful for testing)
        void clear_symbols()
        {
            _symbol_table->clear();
            // Reset symbol loading flags to allow fresh loading
            _builtin_symbols_loaded = false;
            _intrinsic_symbols_loaded = false;
            _user_symbols_loaded = false;
            _runtime_symbols_loaded = false;
        }

        // Reset all TypeChecker state (for multi-file compilation)
        void reset_state()
        {
            // Clear symbol table and reset loading flags
            clear_symbols();
            
            // Clear all tracking data structures
            _struct_fields.clear();
            _struct_methods.clear();
            _private_struct_methods.clear();
            _template_parameters.clear();
            _current_generic_trait_bounds.clear();
            _generic_context_stack.clear();
            _required_instantiations.clear();
            
            // Reset current context state
            _current_struct_type = nullptr;
            _current_struct_name = "";
            _current_function_return_type = nullptr;
            _current_namespace = "Global";
            _current_match_expr_type = nullptr;
            _current_expected_type = nullptr;
            
            // Reset state flags
            _in_function = false;
            _in_loop = false;
            _in_call_expression = false;
            _in_unsafe_context = false;
            
            // Clear errors and warnings
            _errors.clear();
            _warnings.clear();
        }

        // Symbol table access for LSP
        TypedSymbol *lookup_symbol(const std::string &name) { return _symbol_table->lookup_symbol(name); }
        TypedSymbol *lookup_symbol_in_any_namespace(const std::string &symbol_name) { return _symbol_table->lookup_symbol_in_any_namespace(symbol_name); }

        // Symbol table access for LSP
        TypedSymbol *lookup_symbol(const std::string &name) const { return _symbol_table->lookup_symbol(name); }

        // SRM Helper Methods for standardized naming
        std::string generate_function_name(const std::string &function_name, const std::vector<Cryo::Type *> &parameter_types);
        std::string generate_method_name(const std::string &type_name, const std::string &method_name, const std::vector<Cryo::Type *> &parameter_types);
        std::string generate_qualified_name(const std::string &base_name, Cryo::SymbolKind symbol_kind);
        std::vector<std::string> get_current_namespace_parts() const;
        std::string resolve_module_path_to_namespace(const std::string &module_path) const;

        // Visitor methods - Program
        void visit(ProgramNode &node) override;

        // Visitor methods - Declarations
        void visit(VariableDeclarationNode &node) override;
        void visit(FunctionDeclarationNode &node) override;
        void visit(IntrinsicDeclarationNode &node) override;
        void visit(IntrinsicConstDeclarationNode &node) override;
        void visit(ImportDeclarationNode &node) override;
        void visit(ModuleDeclarationNode &node) override;
        void visit(StructDeclarationNode &node) override;
        void visit(ClassDeclarationNode &node) override;

        // Helper methods for three-pass struct/class processing
        void register_struct_declaration_signatures(StructDeclarationNode &node);
        void register_struct_type_only(StructDeclarationNode &node);
        void process_struct_method_bodies(StructDeclarationNode &node);
        void process_struct_method_body(StructMethodNode &node);
        void register_class_declaration_signatures(ClassDeclarationNode &node);
        void register_class_type_only(ClassDeclarationNode &node);
        void process_class_method_bodies(ClassDeclarationNode &node);
        void register_implementation_block_signatures(ImplementationBlockNode &node);
        void process_implementation_block_bodies(ImplementationBlockNode &node);

        void visit(TraitDeclarationNode &node) override;
        void visit(EnumDeclarationNode &node) override;
        void visit(EnumVariantNode &node) override;
        void visit(TypeAliasDeclarationNode &node) override;
        void visit(ImplementationBlockNode &node) override;
        void visit(ExternBlockNode &node) override;
        void visit(GenericParameterNode &node) override;
        void visit(StructFieldNode &node) override;
        void visit(StructMethodNode &node) override;

        // Visitor methods - Statements
        void visit(BlockStatementNode &node) override;
        void visit(UnsafeBlockStatementNode &node) override;
        void visit(ReturnStatementNode &node) override;
        void visit(IfStatementNode &node) override;
        void visit(WhileStatementNode &node) override;
        void visit(ForStatementNode &node) override;
        void visit(ExpressionStatementNode &node) override;
        void visit(DeclarationStatementNode &node) override;

        // Visitor methods - Expressions
        void visit(LiteralNode &node) override;
        void visit(IdentifierNode &node) override;
        void visit(BinaryExpressionNode &node) override;
        void visit(UnaryExpressionNode &node) override;
        void visit(CallExpressionNode &node) override;
        void visit(NewExpressionNode &node) override;
        void visit(StructLiteralNode &node) override;
        void visit(SizeofExpressionNode &node) override;
        void visit(AlignofExpressionNode &node) override;
        void visit(CastExpressionNode &node) override;
        void visit(ArrayLiteralNode &node) override;
        void visit(ArrayAccessNode &node) override;
        void visit(MemberAccessNode &node) override;
        void visit(ScopeResolutionNode &node) override;

        // Visitor methods - Match Statements
        void visit(MatchStatementNode &node) override;
        void visit(MatchArmNode &node) override;
        void visit(PatternNode &node) override;
        void visit(EnumPatternNode &node) override;

        // Method specialization support for MonomorphizationPass
        const std::unordered_map<std::string, Type *> *get_struct_methods(const std::string &struct_name) const;
        void register_specialized_method(const std::string &struct_name, const std::string &method_name, Type *method_type);
        const std::unordered_map<std::string, std::unordered_map<std::string, Type *>> &get_all_struct_methods() const;

    private:
        // Type inference and deduction
        Type *infer_literal_type(const LiteralNode &node);
        Type *infer_binary_expression_type(const BinaryExpressionNode &node);
        Type *resolve_type_from_annotation(const std::string &type_annotation);

        // Type lookup helpers to replace parse_type_from_string
        Type *lookup_type_by_name(const std::string &type_name);

        // Type utility helpers
        bool is_integer_type(Type *type);

        // Method signature registration for forward references
        void register_method_signature(StructMethodNode &method);

        // Type compatibility checking
        bool check_assignment_compatibility(Type *lhs_type, Type *rhs_type, SourceLocation loc);
        bool check_binary_operation_compatibility(TokenKind op, Type *lhs_type, Type *rhs_type, SourceLocation loc);
        bool check_function_call_compatibility(FunctionType *func_type,
                                               const std::vector<Type *> &arg_types,
                                               SourceLocation loc);

        // Type conversion and promotion
        Type *get_common_type_for_operation(Type *lhs, Type *rhs, TokenKind op);
        bool can_implicitly_convert(Type *from, Type *to);
        bool requires_explicit_cast(Type *from, Type *to);

        // Error reporting
        [[deprecated("Use _diagnostic_builder->create_*_error() methods instead")]]
        void report_error(TypeError::ErrorKind kind, SourceLocation loc, const std::string &message);
        void report_error(TypeError::ErrorKind kind, SourceLocation loc, const std::string &message, ASTNode *node);
        void report_warning(TypeWarning::WarningKind kind, SourceLocation loc, const std::string &message);
        void report_conversion_warning(TypeWarning::WarningKind kind, SourceLocation loc,
                                       const std::string &message, Type *from, Type *to);
        void report_type_mismatch(SourceLocation loc, Type *expected, Type *actual, const std::string &context);
        void report_undefined_symbol(SourceLocation loc, const std::string &symbol_name);
        void report_redefined_symbol(SourceLocation loc, const std::string &symbol_name);
        // Overloads with node for proper source context
        void report_undefined_symbol(ASTNode *node, const std::string &symbol_name);
        void report_redefined_symbol(ASTNode *node, const std::string &symbol_name);

        // Symbol table helpers
        void enter_scope();
        void exit_scope();
        bool declare_variable(const std::string &name, Type *type, SourceLocation loc, bool is_mutable);
        bool declare_function(const std::string &name, Type *type, SourceLocation loc, FunctionDeclarationNode *function_node);
        Type *lookup_variable_type(const std::string &name);

        // Utility methods
        bool is_numeric_type(Type *type);
        bool is_comparable_type(Type *type);
        bool is_integral_type(Type *type);
        bool is_boolean_context_valid(Type *type);
        bool is_in_unsafe_context() const { return _in_unsafe_context; }
        bool is_primitive_integer_type(const std::string &type_name);
        bool is_method_declared_in_type(const std::string &type_name, const std::string &method_name);

        // Type casting helpers
        bool is_valid_type(const std::string &type_name);
        bool is_cast_valid(const std::string &from_type, const std::string &to_type);
        bool requires_explicit_cast(const std::string &from_type, const std::string &to_type);

        // Template parameter management
        std::vector<std::string> get_template_parameters(const std::string &type_name) const;

        // Enum validation helpers
        void validate_enum_variant(EnumVariantNode &node, const std::vector<std::string> &generic_param_names);

        // LSP-specific API
        struct LSPTypeInfo
        {
            std::string type_name;
            std::string kind; // "primitive", "struct", "class", "enum", "function", etc.
            std::string documentation;
            std::vector<std::string> members;         // For structs/classes
            std::vector<std::string> methods;         // For classes
            std::string return_type;                  // For functions
            std::vector<std::string> parameter_types; // For functions
            bool is_generic;
            std::vector<std::string> generic_parameters;
        };

        std::optional<LSPTypeInfo> get_type_info_at_position(const std::string &filename,
                                                             size_t line, size_t column) const;
        std::optional<LSPTypeInfo> get_type_info_by_name(const std::string &type_name) const;
        std::vector<std::string> get_available_completions(const std::string &context) const;
        std::string get_hover_info(const std::string &symbol_name) const;

        // Generic type substitution helpers
        std::vector<std::string> extract_template_parameter_names(const std::string &template_type_string);
        std::shared_ptr<Type> substitute_type_with_map(const std::shared_ptr<Type> &type,
                                                       const std::unordered_map<std::string, std::shared_ptr<Type>> &substitution_map);
        Type *substitute_generic_parameters(Type *base_type, const std::vector<std::shared_ptr<Type>> &concrete_types);

        std::string format_type_error(const std::string &context, Type *expected, Type *actual);
    };

    // Type inference engine (separate from type checker for modularity)
    class TypeInference
    {
    private:
        TypeContext &_type_context;

        // Constraint-based inference state
        struct TypeConstraint
        {
            Type *lhs;
            Type *rhs;
            enum
            {
                Equal,
                Subtype,
                Convertible
            } relationship;
            SourceLocation location;
        };

        std::vector<TypeConstraint> _constraints;

    public:
        TypeInference(TypeContext &type_ctx) : _type_context(type_ctx) {}

        // Type inference for auto variables
        Type *infer_auto_variable_type(ExpressionNode *initializer);

        // Expression type inference
        Type *infer_expression_type(ExpressionNode *expr);
        Type *infer_literal_type(LiteralNode *literal);
        Type *infer_binary_expression_type(BinaryExpressionNode *binary_expr,
                                           Type *lhs_type, Type *rhs_type);

        // Function signature inference
        Type *infer_function_return_type(FunctionDeclarationNode *func_decl);

        // Constraint-based inference (for more complex cases)
        void add_constraint(Type *lhs, Type *rhs, SourceLocation loc);
        bool solve_constraints();
        void clear_constraints();

    private:
        // Constraint resolution helpers
        bool unify_types(Type *t1, Type *t2);
        Type *generalize_type(Type *type);
        Type *instantiate_type(Type *type);
    };

    // Type system utilities
    class TypeUtils
    {
    public:
        // Type comparison utilities
        static bool are_identical_types(Type *t1, Type *t2);
        static bool are_compatible_types(Type *t1, Type *t2);
        static bool is_subtype_of(Type *subtype, Type *supertype);

        // Type conversion utilities
        static bool can_convert_implicitly(Type *from, Type *to);
        static bool can_convert_explicitly(Type *from, Type *to);
        static Type *get_conversion_result_type(Type *from, Type *to);

        // Numeric type utilities
        static bool is_numeric_promotion_valid(Type *from, Type *to);
        static Type *get_arithmetic_result_type(Type *lhs, Type *rhs, TokenKind op);
        static Type *get_comparison_result_type(Type *lhs, Type *rhs, TokenKind op);

        // String formatting for error messages
        static std::string type_to_string(Type *type);
        static std::string format_function_signature(FunctionType *func_type);
        static std::string format_type_list(const std::vector<Type *> &types);
    };

    // Built-in function type signatures (for standard library functions)
    class BuiltinTypes
    {
    private:
        TypeContext &_type_context;
        std::unordered_map<std::string, FunctionType *> _builtin_functions;

    public:
        BuiltinTypes(TypeContext &type_ctx);

        // Register built-in functions
        void register_builtin_functions();

        // Lookup built-in function type
        FunctionType *get_builtin_function_type(const std::string &name);
        bool is_builtin_function(const std::string &name);

    private:
        void register_io_functions();     // print, println, read, etc.
        void register_math_functions();   // abs, min, max, etc.
        void register_string_functions(); // len, substr, etc.
        void register_array_functions();  // push, pop, size, etc.
    };
}