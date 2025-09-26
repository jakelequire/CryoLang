#pragma once
#include "AST/Type.hpp"
#include "AST/ASTNode.hpp"
#include "AST/ASTVisitor.hpp"
#include "AST/ASTContext.hpp"
#include "Lexer/lexer.hpp"
#include <vector>
#include <string>
#include <unordered_map>

namespace Cryo
{
    // Forward declarations
    class TypeContext;
    class SymbolTable;

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

    // Symbol information for type checking
    // Forward declarations for AST nodes
    class StructDeclarationNode;
    class ClassDeclarationNode;

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

        TypedSymbol() = default; // Default constructor for containers
        TypedSymbol(const std::string &n, Type *t, SourceLocation loc)
            : name(n), type(t), declaration_location(loc) {}

        TypedSymbol(const std::string &n, Type *t, SourceLocation loc, StructDeclarationNode *snode)
            : name(n), type(t), declaration_location(loc), struct_node(snode) {}

        TypedSymbol(const std::string &n, Type *t, SourceLocation loc, ClassDeclarationNode *cnode)
            : name(n), type(t), declaration_location(loc), class_node(cnode) {}
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
        TypedSymbol *lookup_symbol(const std::string &name);
        bool is_symbol_defined(const std::string &name);

        // Scope management
        std::unique_ptr<TypedSymbolTable> enter_scope();
        std::unique_ptr<TypedSymbolTable> exit_scope();

        // Utilities
        size_t symbol_count() const { return _symbols.size(); }
        bool has_parent() const { return _parent_scope != nullptr; }

        // Access symbols for detailed inspection
        const std::unordered_map<std::string, TypedSymbol> &get_symbols() const { return _symbols; }

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
        std::vector<TypeError> _errors;

        // Current function return type (for return statement checking)
        Type *_current_function_return_type = nullptr;

        // Current struct type (for 'this' keyword resolution)
        Type *_current_struct_type = nullptr;

        // Current struct name (for field tracking)
        std::string _current_struct_name;

        // Type checking state
        bool _in_function = false;
        bool _in_loop = false;

        // Struct field tracking - maps struct name -> field name -> field type
        std::unordered_map<std::string, std::unordered_map<std::string, Type *>> _struct_fields;

        // Struct method tracking - maps struct name -> method name -> return type
        std::unordered_map<std::string, std::unordered_map<std::string, Type *>> _struct_methods;

        // Current function's generic parameters and their trait bounds
        // Maps generic parameter name -> set of trait names (e.g., "T" -> {"Default", "Clone"})
        std::unordered_map<std::string, std::vector<std::string>> _current_generic_trait_bounds;

        // Reference to main symbol table (for scope resolution lookups)
        const SymbolTable *_main_symbol_table = nullptr;

    public:
        TypeChecker(TypeContext &type_ctx);
        ~TypeChecker() = default;

        // Load built-in functions from main SymbolTable
        void load_builtin_symbols(const SymbolTable &main_symbol_table);
        void load_intrinsic_symbols(const SymbolTable &main_symbol_table);

        // Main entry point
        void check_program(ProgramNode &program);

        // Error handling
        const std::vector<TypeError> &errors() const { return _errors; }
        bool has_errors() const { return !_errors.empty(); }
        size_t error_count() const { return _errors.size(); }

        // Type table access
        void print_type_table(std::ostream &os = std::cout) const { _symbol_table->print_type_table(os); }

        // Visitor methods - Program
        void visit(ProgramNode &node) override;

        // Visitor methods - Declarations
        void visit(VariableDeclarationNode &node) override;
        void visit(FunctionDeclarationNode &node) override;
        void visit(StructDeclarationNode &node) override;
        void visit(ClassDeclarationNode &node) override;
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
        void visit(SizeofExpressionNode &node) override;
        void visit(MemberAccessNode &node) override;
        void visit(ScopeResolutionNode &node) override;

        // Visitor methods - Match Statements
        void visit(MatchStatementNode &node) override;
        void visit(MatchArmNode &node) override;
        void visit(PatternNode &node) override;
        void visit(EnumPatternNode &node) override;

    private:
        // Type inference and deduction
        Type *infer_literal_type(const LiteralNode &node);
        Type *infer_binary_expression_type(const BinaryExpressionNode &node);
        Type *resolve_type_from_annotation(const std::string &type_annotation);

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
        void report_error(TypeError::ErrorKind kind, SourceLocation loc, const std::string &message);
        void report_type_mismatch(SourceLocation loc, Type *expected, Type *actual, const std::string &context);
        void report_undefined_symbol(SourceLocation loc, const std::string &symbol_name);
        void report_redefined_symbol(SourceLocation loc, const std::string &symbol_name);

        // Symbol table helpers
        void enter_scope();
        void exit_scope();
        bool declare_variable(const std::string &name, Type *type, SourceLocation loc, bool is_mutable);
        Type *lookup_variable_type(const std::string &name);

        // Utility methods
        bool is_numeric_type(Type *type);
        bool is_comparable_type(Type *type);
        bool is_integral_type(Type *type);
        bool is_boolean_context_valid(Type *type);

        // Enum validation helpers
        void validate_enum_variant(EnumVariantNode &node, const std::vector<std::string> &generic_param_names);

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