#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <optional>
#include <string_view>

#include "AST/Type.hpp"
#include "AST/SymbolTable.hpp"
#include "Utils/Logger.hpp"

// Forward declarations
namespace llvm {
    class Function;
    class Type;
}

namespace Cryo {
    class ASTNode;
    class FunctionDeclarationNode;
    class StructDeclarationNode;
    class ClassDeclarationNode;
    class CallExpressionNode;
}

namespace Cryo::SRM {

    // Hash function for SymbolIdentifier variants
    struct SymbolIdentifierHash;
    
    /**
     * @brief Base class for all symbol identifiers in the SRM system
     * 
     * This abstract base provides the fundamental interface for symbol identification
     * and name generation throughout the compiler pipeline. All concrete identifier
     * types inherit from this class.
     */
    class SymbolIdentifier {
    public:
        virtual ~SymbolIdentifier() = default;
        
        // Core naming interface
        virtual std::string to_string() const = 0;
        virtual std::string to_debug_string() const = 0;
        virtual Cryo::SymbolKind get_symbol_kind() const = 0;
        
        // Hash and equality for container usage
        virtual size_t hash() const = 0;
        virtual bool equals(const SymbolIdentifier& other) const = 0;
        
        // Type information
        virtual std::string get_identifier_type() const = 0;
        
        // Operators for container usage
        bool operator==(const SymbolIdentifier& other) const { return equals(other); }
        bool operator!=(const SymbolIdentifier& other) const { return !equals(other); }
    };
    
    /**
     * @brief Represents a qualified symbol name with namespace context
     * 
     * Handles namespace-qualified identifiers like "std::collections::Vector"
     * and provides consistent string representation across the compiler.
     */
    class QualifiedIdentifier : public SymbolIdentifier {
    private:
        std::vector<std::string> namespace_parts_;
        std::string symbol_name_;
        Cryo::SymbolKind symbol_kind_;
        
        // Cached strings for performance
        mutable std::optional<std::string> cached_string_;
        mutable std::optional<std::string> cached_debug_string_;
        mutable std::optional<size_t> cached_hash_;
        
    public:
        QualifiedIdentifier(const std::vector<std::string>& namespaces, 
                          const std::string& name, 
                          Cryo::SymbolKind kind);
        
        QualifiedIdentifier(const std::string& name, Cryo::SymbolKind kind);
        
        // SymbolIdentifier interface
        std::string to_string() const override;
        std::string to_debug_string() const override;
        Cryo::SymbolKind get_symbol_kind() const override { return symbol_kind_; }
        size_t hash() const override;
        bool equals(const SymbolIdentifier& other) const override;
        std::string get_identifier_type() const override { return "QualifiedIdentifier"; }
        
        // Specific accessors
        const std::vector<std::string>& get_namespace_parts() const { return namespace_parts_; }
        const std::string& get_symbol_name() const { return symbol_name_; }
        std::string get_namespace_path() const;
        bool is_namespaced() const { return !namespace_parts_.empty(); }
        
        // Utility methods
        std::string get_simple_name() const { return symbol_name_; }
        std::string get_qualified_name() const { return to_string(); }
        
        // Creation helpers
        static std::unique_ptr<QualifiedIdentifier> create_simple(
            const std::string& name, Cryo::SymbolKind kind);
        static std::unique_ptr<QualifiedIdentifier> create_qualified(
            const std::vector<std::string>& namespaces, 
            const std::string& name, 
            Cryo::SymbolKind kind);
            
    protected:
        void invalidate_cache() const;
        
    private:
        std::string build_string_representation() const;
        std::string build_debug_representation() const;
        size_t calculate_hash() const;
    };
    
    /**
     * @brief Represents a function signature with parameter types and calling convention
     * 
     * Handles function overloading, constructor naming, and LLVM IR name generation.
     * Provides specialized naming for different function types (normal, constructor, 
     * static, member functions).
     */
    class FunctionIdentifier : public QualifiedIdentifier {
    public:
        enum class FunctionType {
            Regular,
            Constructor,
            Destructor,
            StaticMethod,
            MemberMethod,
            Operator
        };
        
    private:
        std::vector<Cryo::Type*> parameter_types_;
        Cryo::Type* return_type_;
        FunctionType function_type_;
        bool is_variadic_;
        
        // Cached specialized strings
        mutable std::optional<std::string> cached_signature_string_;
        mutable std::optional<std::string> cached_llvm_name_;
        mutable std::optional<std::string> cached_overload_key_;
        mutable std::optional<std::string> cached_mangled_name_;
        
    public:
        FunctionIdentifier(const std::vector<std::string>& namespaces,
                         const std::string& name,
                         const std::vector<Cryo::Type*>& params,
                         Cryo::Type* return_type = nullptr,
                         FunctionType func_type = FunctionType::Regular,
                         bool is_variadic = false);
        
        FunctionIdentifier(const std::string& name,
                         const std::vector<Cryo::Type*>& params,
                         Cryo::Type* return_type = nullptr,
                         FunctionType func_type = FunctionType::Regular,
                         bool is_variadic = false);
        
        // Enhanced SymbolIdentifier interface
        std::string to_string() const override;
        std::string to_debug_string() const override;
        size_t hash() const override;
        bool equals(const SymbolIdentifier& other) const override;
        std::string get_identifier_type() const override { return "FunctionIdentifier"; }
        
        // Function-specific naming
        std::string to_signature_string() const;
        std::string to_llvm_name() const;
        std::string to_overload_key() const;
        std::string to_mangled_name() const;
        std::string to_constructor_name() const;
        std::string to_destructor_name() const;
        
        // Accessors
        const std::vector<Cryo::Type*>& get_parameter_types() const { return parameter_types_; }
        Cryo::Type* get_return_type() const { return return_type_; }
        FunctionType get_function_type() const { return function_type_; }
        bool is_variadic() const { return is_variadic_; }
        
        // Type queries
        bool is_constructor() const { return function_type_ == FunctionType::Constructor; }
        bool is_destructor() const { return function_type_ == FunctionType::Destructor; }
        bool is_static_method() const { return function_type_ == FunctionType::StaticMethod; }
        bool is_member_method() const { return function_type_ == FunctionType::MemberMethod; }
        bool is_operator() const { return function_type_ == FunctionType::Operator; }
        
        // Compatibility checking
        bool is_compatible_with_arguments(const std::vector<Cryo::Type*>& arg_types) const;
        int calculate_conversion_cost(const std::vector<Cryo::Type*>& arg_types) const;
        
        // Creation helpers
        static std::unique_ptr<FunctionIdentifier> create_constructor(
            const std::vector<std::string>& namespaces,
            const std::string& type_name,
            const std::vector<Cryo::Type*>& params);
            
        static std::unique_ptr<FunctionIdentifier> create_destructor(
            const std::vector<std::string>& namespaces,
            const std::string& type_name);
            
        static std::unique_ptr<FunctionIdentifier> create_method(
            const std::vector<std::string>& namespaces,
            const std::string& type_name,
            const std::string& method_name,
            const std::vector<Cryo::Type*>& params,
            Cryo::Type* return_type,
            bool is_static = false);
            
    private:
        void invalidate_function_cache() const;
        std::string build_signature_string() const;
        std::string build_llvm_name() const;
        std::string build_overload_key() const;
        std::string build_mangled_name() const;
        std::string get_function_type_string() const;
        std::string normalize_type_for_signature(Cryo::Type* type) const;
    };
    
    /**
     * @brief Represents a type identifier for structs, classes, enums, and templates
     * 
     * Handles type naming including template specializations, nested types,
     * and provides specialized naming for type-related symbols like constructors.
     */
    class TypeIdentifier : public QualifiedIdentifier {
    private:
        std::vector<Cryo::Type*> template_parameters_;
        Cryo::TypeKind type_kind_;
        bool is_template_specialization_;
        
        // Cached specialized strings
        mutable std::optional<std::string> cached_canonical_name_;
        mutable std::optional<std::string> cached_template_name_;
        mutable std::optional<std::string> cached_constructor_name_;
        mutable std::optional<std::string> cached_destructor_name_;
        
    public:
        TypeIdentifier(const std::vector<std::string>& namespaces,
                     const std::string& name,
                     Cryo::TypeKind kind,
                     const std::vector<Cryo::Type*>& template_params = {});
        
        TypeIdentifier(const std::string& name,
                     Cryo::TypeKind kind,
                     const std::vector<Cryo::Type*>& template_params = {});
        
        // Enhanced SymbolIdentifier interface
        std::string to_string() const override;
        std::string to_debug_string() const override;
        size_t hash() const override;
        bool equals(const SymbolIdentifier& other) const override;
        std::string get_identifier_type() const override { return "TypeIdentifier"; }
        
        // Type-specific naming
        std::string to_canonical_name() const;
        std::string to_template_name() const;
        std::string to_constructor_name() const;
        std::string to_destructor_name() const;
        std::string to_llvm_struct_name() const;
        
        // Accessors
        const std::vector<Cryo::Type*>& get_template_parameters() const { return template_parameters_; }
        Cryo::TypeKind get_type_kind() const { return type_kind_; }
        bool is_template_specialization() const { return is_template_specialization_; }
        bool has_template_parameters() const { return !template_parameters_.empty(); }
        
        // Type queries
        bool is_struct() const { return type_kind_ == Cryo::TypeKind::Struct; }
        bool is_class() const { return type_kind_ == Cryo::TypeKind::Class; }
        bool is_enum() const { return type_kind_ == Cryo::TypeKind::Enum; }
        bool is_union() const { return type_kind_ == Cryo::TypeKind::Union; }
        
        // Template operations
        std::unique_ptr<TypeIdentifier> specialize(const std::vector<Cryo::Type*>& concrete_types) const;
        std::unique_ptr<FunctionIdentifier> create_constructor_identifier(
            const std::vector<Cryo::Type*>& param_types) const;
        std::unique_ptr<FunctionIdentifier> create_destructor_identifier() const;
        std::unique_ptr<FunctionIdentifier> create_method_identifier(
            const std::string& method_name,
            const std::vector<Cryo::Type*>& param_types,
            Cryo::Type* return_type,
            bool is_static = false) const;
            
        // Creation helpers
        static std::unique_ptr<TypeIdentifier> create_simple_type(
            const std::string& name, Cryo::TypeKind kind);
        static std::unique_ptr<TypeIdentifier> create_template_type(
            const std::vector<std::string>& namespaces,
            const std::string& name,
            Cryo::TypeKind kind,
            const std::vector<Cryo::Type*>& template_params);
            
    private:
        void invalidate_type_cache() const;
        std::string build_canonical_name() const;
        std::string build_template_name() const;
        std::string format_template_parameters() const;
        std::string get_type_kind_string() const;
    };
    
    // Hash functions for container usage
    struct SymbolIdentifierHash {
        size_t operator()(const SymbolIdentifier& identifier) const {
            return identifier.hash();
        }
        
        size_t operator()(const std::unique_ptr<SymbolIdentifier>& identifier) const {
            return identifier ? identifier->hash() : 0;
        }
    };
    
    struct SymbolIdentifierEqual {
        bool operator()(const SymbolIdentifier& lhs, const SymbolIdentifier& rhs) const {
            return lhs.equals(rhs);
        }
        
        bool operator()(const std::unique_ptr<SymbolIdentifier>& lhs, 
                       const std::unique_ptr<SymbolIdentifier>& rhs) const {
            if (!lhs && !rhs) return true;
            if (!lhs || !rhs) return false;
            return lhs->equals(*rhs);
        }
    };
    
    // Forward declarations for resolution components
    class SymbolResolutionContext;
    class SymbolResolutionManager;
    
    /**
     * @brief Manages namespace context, imports, and symbol resolution state
     * 
     * This class maintains the current compilation context including namespace stack,
     * imported namespaces, namespace aliases, and provides the foundation for
     * symbol resolution throughout the compilation process.
     */
    class SymbolResolutionContext {
    private:
        // Core context state
        std::vector<std::string> namespace_stack_;
        std::unordered_map<std::string, std::string> namespace_aliases_;
        std::unordered_set<std::string> imported_namespaces_;
        std::vector<std::string> import_search_paths_;
        
        // Integration with existing systems
        Cryo::SymbolTable* symbol_table_;
        Cryo::TypeContext* type_context_;
        
        // Caching for performance
        mutable std::unordered_map<std::string, std::vector<std::string>> resolved_namespace_cache_;
        mutable std::unordered_map<std::string, std::string> alias_resolution_cache_;
        
        // Configuration
        bool enable_implicit_std_import_;
        bool enable_namespace_fallback_;
        
    public:
        explicit SymbolResolutionContext(Cryo::SymbolTable* symbol_table, 
                                       Cryo::TypeContext* type_context = nullptr);
        
        ~SymbolResolutionContext() = default;
        
        // Namespace management
        void push_namespace(const std::string& namespace_name);
        void pop_namespace();
        const std::vector<std::string>& get_namespace_stack() const { return namespace_stack_; }
        std::string get_current_namespace_path() const;
        bool is_in_namespace(const std::string& namespace_name) const;
        
        // Alias management
        void register_namespace_alias(const std::string& alias, const std::string& full_namespace);
        void remove_namespace_alias(const std::string& alias);
        std::string resolve_namespace_alias(const std::string& alias) const;
        bool has_namespace_alias(const std::string& alias) const;
        const std::unordered_map<std::string, std::string>& get_namespace_aliases() const { return namespace_aliases_; }
        
        // Import management
        void add_imported_namespace(const std::string& namespace_name);
        void remove_imported_namespace(const std::string& namespace_name);
        bool is_namespace_imported(const std::string& namespace_name) const;
        const std::unordered_set<std::string>& get_imported_namespaces() const { return imported_namespaces_; }
        void add_import_search_path(const std::string& path);
        const std::vector<std::string>& get_import_search_paths() const { return import_search_paths_; }
        
        // Configuration
        void set_enable_implicit_std_import(bool enable) { enable_implicit_std_import_ = enable; }
        bool is_implicit_std_import_enabled() const { return enable_implicit_std_import_; }
        void set_enable_namespace_fallback(bool enable) { enable_namespace_fallback_ = enable; }
        bool is_namespace_fallback_enabled() const { return enable_namespace_fallback_; }
        
        // Symbol identifier creation helpers
        std::unique_ptr<QualifiedIdentifier> create_qualified_identifier(
            const std::string& name, Cryo::SymbolKind kind) const;
        
        std::unique_ptr<FunctionIdentifier> create_function_identifier(
            const std::string& name,
            const std::vector<Cryo::Type*>& params,
            Cryo::Type* return_type = nullptr,
            FunctionIdentifier::FunctionType func_type = FunctionIdentifier::FunctionType::Regular) const;
            
        std::unique_ptr<TypeIdentifier> create_type_identifier(
            const std::string& name,
            Cryo::TypeKind kind,
            const std::vector<Cryo::Type*>& template_params = {}) const;
            
        // Resolution helpers
        std::vector<std::string> resolve_namespace_search_order(const std::string& partial_name) const;
        std::vector<std::unique_ptr<QualifiedIdentifier>> generate_lookup_candidates(
            const std::string& symbol_name, Cryo::SymbolKind kind) const;
            
        // Integration accessors
        Cryo::SymbolTable* get_symbol_table() const { return symbol_table_; }
        Cryo::TypeContext* get_type_context() const { return type_context_; }
        void set_symbol_table(Cryo::SymbolTable* symbol_table) { symbol_table_ = symbol_table; }
        void set_type_context(Cryo::TypeContext* type_context) { type_context_ = type_context; }
        
        // Cache management
        void clear_namespace_cache() const;
        void clear_all_caches() const;
        
        // Debugging and introspection
        std::string to_debug_string() const;
        void dump_context(std::ostream& os = std::cout) const;
        
    private:
        // Internal helpers
        std::vector<std::string> build_namespace_search_order() const;
        void validate_namespace_name(const std::string& namespace_name) const;
        std::string normalize_namespace_path(const std::string& namespace_path) const;
    };
    
    /**
     * @brief Main symbol resolution engine with multiple resolution strategies
     * 
     * This is the core of the SRM system, providing sophisticated symbol resolution
     * with multiple fallback strategies, caching, and integration with the existing
     * compiler infrastructure.
     */
    class SymbolResolutionManager {
    public:
        // Resolution result information
        struct ResolutionResult {
            Cryo::Symbol* symbol;
            std::unique_ptr<SymbolIdentifier> resolved_identifier;
            std::string resolution_strategy;
            int resolution_cost;
            bool is_exact_match;
            
            ResolutionResult() : symbol(nullptr), resolution_cost(-1), is_exact_match(false) {}
            
            // Move constructor
            ResolutionResult(ResolutionResult&& other) noexcept
                : symbol(other.symbol),
                  resolved_identifier(std::move(other.resolved_identifier)),
                  resolution_strategy(std::move(other.resolution_strategy)),
                  resolution_cost(other.resolution_cost),
                  is_exact_match(other.is_exact_match) {
                other.symbol = nullptr;
                other.resolution_cost = -1;
                other.is_exact_match = false;
            }
            
            // Move assignment operator
            ResolutionResult& operator=(ResolutionResult&& other) noexcept {
                if (this != &other) {
                    symbol = other.symbol;
                    resolved_identifier = std::move(other.resolved_identifier);
                    resolution_strategy = std::move(other.resolution_strategy);
                    resolution_cost = other.resolution_cost;
                    is_exact_match = other.is_exact_match;
                    
                    other.symbol = nullptr;
                    other.resolution_cost = -1;
                    other.is_exact_match = false;
                }
                return *this;
            }
            
            // Delete copy constructor and copy assignment operator
            ResolutionResult(const ResolutionResult&) = delete;
            ResolutionResult& operator=(const ResolutionResult&) = delete;
            
            bool is_valid() const { return symbol != nullptr; }
            bool is_ambiguous() const { return resolution_cost > 0; }
        };
        
        // Resolution strategy function type
        using ResolutionStrategy = std::function<ResolutionResult(const SymbolIdentifier&, const SymbolResolutionContext&)>;
        
    private:
        SymbolResolutionContext* context_;
        
        // Resolution strategies in priority order
        std::vector<std::pair<std::string, ResolutionStrategy>> resolution_strategies_;
        
        // Caching for performance
        mutable std::unordered_map<size_t, ResolutionResult> resolution_cache_;
        mutable std::unordered_map<std::string, std::vector<Cryo::Symbol*>> overload_cache_;
        
        // LLVM integration tracking
        std::unordered_map<size_t, llvm::Function*> llvm_function_registry_;
        std::unordered_map<size_t, llvm::Type*> llvm_type_registry_;
        
        // Statistics and debugging
        mutable size_t resolution_attempts_;
        mutable size_t cache_hits_;
        mutable size_t cache_misses_;
        
    public:
        explicit SymbolResolutionManager(SymbolResolutionContext* context);
        ~SymbolResolutionManager() = default;
        
        // Primary resolution interface
        ResolutionResult resolve_symbol(const SymbolIdentifier& identifier) const;
        ResolutionResult resolve_function(const FunctionIdentifier& identifier) const;
        ResolutionResult resolve_type(const TypeIdentifier& identifier) const;
        
        // Overload resolution
        std::vector<ResolutionResult> resolve_function_overloads(
            const std::string& base_name,
            const std::vector<Cryo::Type*>& argument_types) const;
            
        ResolutionResult resolve_best_function_overload(
            const std::string& base_name,
            const std::vector<Cryo::Type*>& argument_types) const;
            
        // Constructor resolution
        ResolutionResult resolve_constructor(
            const TypeIdentifier& type_id,
            const std::vector<Cryo::Type*>& argument_types) const;
            
        ResolutionResult resolve_default_constructor(const TypeIdentifier& type_id) const;
        
        // Batch resolution operations
        std::vector<ResolutionResult> resolve_all_in_namespace(const std::string& namespace_name) const;
        std::vector<ResolutionResult> resolve_imported_symbols() const;
        
        // Query operations
        bool symbol_exists(const SymbolIdentifier& identifier) const;
        std::vector<ResolutionResult> find_similar_symbols(const std::string& partial_name, 
                                                          Cryo::SymbolKind kind = static_cast<Cryo::SymbolKind>(-1)) const;
        std::vector<std::string> suggest_symbol_names(const std::string& typo_name) const;
        
        // LLVM integration
        void register_llvm_function(const FunctionIdentifier& identifier, llvm::Function* function);
        void register_llvm_type(const TypeIdentifier& identifier, llvm::Type* type);
        llvm::Function* get_llvm_function(const FunctionIdentifier& identifier) const;
        llvm::Type* get_llvm_type(const TypeIdentifier& identifier) const;
        
        // Registration for generated symbols
        void register_generated_symbol(std::unique_ptr<SymbolIdentifier> identifier, Cryo::Symbol* symbol);
        void unregister_symbol(const SymbolIdentifier& identifier);
        
        // Strategy management
        void add_resolution_strategy(const std::string& name, ResolutionStrategy strategy);
        void remove_resolution_strategy(const std::string& name);
        void clear_resolution_strategies();
        void reset_to_default_strategies();
        
        // Cache management
        void clear_resolution_cache() const;
        void clear_overload_cache() const;
        void clear_all_caches() const;
        
        // Statistics and debugging
        struct Statistics {
            size_t total_resolution_attempts;
            size_t cache_hits;
            size_t cache_misses;
            double cache_hit_ratio;
            std::unordered_map<std::string, size_t> strategy_usage;
        };
        
        Statistics get_statistics() const;
        void reset_statistics() const;
        std::string get_performance_report() const;
        
        // Debugging and introspection
        std::string to_debug_string() const;
        void dump_symbol_registry(std::ostream& os = std::cout) const;
        void dump_resolution_cache(std::ostream& os = std::cout) const;
        
        // Context accessor
        SymbolResolutionContext* get_context() const { return context_; }
        
    private:
        // Default resolution strategies
        ResolutionResult try_exact_match(const SymbolIdentifier& identifier, const SymbolResolutionContext& context) const;
        ResolutionResult try_namespace_qualified(const SymbolIdentifier& identifier, const SymbolResolutionContext& context) const;
        ResolutionResult try_imported_namespaces(const SymbolIdentifier& identifier, const SymbolResolutionContext& context) const;
        ResolutionResult try_current_namespace(const SymbolIdentifier& identifier, const SymbolResolutionContext& context) const;
        ResolutionResult try_parent_namespaces(const SymbolIdentifier& identifier, const SymbolResolutionContext& context) const;
        ResolutionResult try_implicit_conversions(const SymbolIdentifier& identifier, const SymbolResolutionContext& context) const;
        
        // Function-specific resolution strategies
        ResolutionResult try_function_overload_resolution(const FunctionIdentifier& identifier, const SymbolResolutionContext& context) const;
        ResolutionResult try_constructor_resolution(const FunctionIdentifier& identifier, const SymbolResolutionContext& context) const;
        ResolutionResult try_operator_resolution(const FunctionIdentifier& identifier, const SymbolResolutionContext& context) const;
        
        // Type-specific resolution strategies
        ResolutionResult try_template_instantiation(const TypeIdentifier& identifier, const SymbolResolutionContext& context) const;
        ResolutionResult try_type_alias_resolution(const TypeIdentifier& identifier, const SymbolResolutionContext& context) const;
        
        // Helper methods
        void initialize_default_strategies();
        size_t calculate_identifier_hash(const SymbolIdentifier& identifier) const;
        bool is_compatible_symbol(const Cryo::Symbol* symbol, const SymbolIdentifier& identifier) const;
        int calculate_resolution_cost(const SymbolIdentifier& target, const SymbolIdentifier& candidate) const;
        
        // Cache key generation
        std::string generate_overload_cache_key(const std::string& base_name, const std::vector<Cryo::Type*>& types) const;
        
        // Symbol lookup delegation to existing systems
        Cryo::Symbol* lookup_in_symbol_table(const std::string& name) const;
        Cryo::Symbol* lookup_in_namespace(const std::string& namespace_name, const std::string& symbol_name) const;
    };
    
    /**
     * @brief RAII scope guard for namespace context
     * 
     * Automatically manages namespace push/pop operations to ensure
     * proper cleanup even in exception scenarios.
     */
    class NamespaceScope {
    private:
        SymbolResolutionContext* context_;
        bool should_pop_;
        
    public:
        NamespaceScope(SymbolResolutionContext* context, const std::string& namespace_name)
            : context_(context), should_pop_(true) {
            if (context_) {
                context_->push_namespace(namespace_name);
            } else {
                should_pop_ = false;
            }
        }
        
        ~NamespaceScope() {
            if (should_pop_ && context_) {
                context_->pop_namespace();
            }
        }
        
        // Non-copyable, non-movable
        NamespaceScope(const NamespaceScope&) = delete;
        NamespaceScope& operator=(const NamespaceScope&) = delete;
        NamespaceScope(NamespaceScope&&) = delete;
        NamespaceScope& operator=(NamespaceScope&&) = delete;
    };
    
    // Utility functions for common naming operations
    namespace Utils {
        
        /**
         * @brief Normalize type names for consistent representation
         */
        std::string normalize_type_name(const std::string& type_name);
        
        /**
         * @brief Extract namespace components from qualified name
         */
        std::pair<std::vector<std::string>, std::string> parse_qualified_name(const std::string& qualified_name);
        
        /**
         * @brief Build qualified name from components
         */
        std::string build_qualified_name(const std::vector<std::string>& namespaces, const std::string& name);
        
        /**
         * @brief Check if a name is a valid identifier
         */
        bool is_valid_identifier(const std::string& name);
        
        /**
         * @brief Escape names for LLVM IR usage
         */
        std::string escape_for_llvm(const std::string& name);
        
        /**
         * @brief Create hash from multiple string components
         */
        size_t hash_combine(const std::vector<std::string>& components);
        
        /**
         * @brief Convert SymbolKind to string representation
         */
        std::string symbol_kind_to_string(Cryo::SymbolKind kind);
        
        /**
         * @brief Convert TypeKind to string representation
         */
        std::string type_kind_to_string(Cryo::TypeKind kind);
        
        /**
         * @brief Calculate string similarity for symbol suggestions
         */
        double calculate_string_similarity(const std::string& a, const std::string& b);
        
        /**
         * @brief Generate symbol suggestions based on typos
         */
        std::vector<std::string> generate_symbol_suggestions(
            const std::string& typo, const std::vector<std::string>& candidates, size_t max_suggestions = 5);
    }
    
} // namespace Cryo::SRM