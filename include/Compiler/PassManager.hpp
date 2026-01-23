#pragma once

#include "Diagnostics/Diag.hpp"
#include <memory>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <optional>

namespace Cryo
{
    // Forward declarations
    class CompilerInstance;
    class ASTContext;
    class SymbolTable;
    class ProgramNode;
    class ASTNode;
    class DiagEmitter;
    class ModuleLoader;
    class TemplateRegistry;
    class TypeChecker;
    class TypeResolver;
    class GenericRegistry;
    class Monomorphizer;

    namespace Codegen
    {
        class CodeGenerator;
    }

    namespace SRM
    {
        class SymbolResolutionManager;
    }

    /**
     * @brief Defines the scope at which a pass operates
     */
    enum class PassScope
    {
        PerFile,     // Runs once per source file (lexing, parsing)
        PerModule,   // Runs once per module in topological order
        WholeProgram // Runs once for entire compilation unit
    };

    /**
     * @brief Defines the compilation stage a pass belongs to
     *
     * Stages are processed in order. Within a stage, passes are ordered
     * by their order() value.
     */
    enum class PassStage
    {
        Frontend = 1,            // Stage 1: Lexing, Parsing, AST Validation
        ModuleResolution = 2,    // Stage 2: Import discovery, module loading
        DeclarationCollection = 3, // Stage 3: Collect all declarations
        TypeResolution = 4,      // Stage 4: Resolve type annotations
        SemanticAnalysis = 5,    // Stage 5: Type checking, flow analysis
        Specialization = 6,      // Stage 6: Generic instantiation, monomorphization
        CodegenPreparation = 7,  // Stage 7: Type lowering, function stubs
        IRGeneration = 8,        // Stage 8: LLVM IR generation
        Optimization = 9         // Stage 9: Optimization and linking
    };

    /**
     * @brief Converts PassStage to string for debugging/logging
     */
    inline const char *pass_stage_to_string(PassStage stage)
    {
        switch (stage)
        {
        case PassStage::Frontend:
            return "Frontend";
        case PassStage::ModuleResolution:
            return "ModuleResolution";
        case PassStage::DeclarationCollection:
            return "DeclarationCollection";
        case PassStage::TypeResolution:
            return "TypeResolution";
        case PassStage::SemanticAnalysis:
            return "SemanticAnalysis";
        case PassStage::Specialization:
            return "Specialization";
        case PassStage::CodegenPreparation:
            return "CodegenPreparation";
        case PassStage::IRGeneration:
            return "IRGeneration";
        case PassStage::Optimization:
            return "Optimization";
        default:
            return "Unknown";
        }
    }

    /**
     * @brief Represents a dependency that a pass requires from earlier passes
     *
     * Wraps the PassProvides identifier with metadata about whether the
     * dependency is required or optional.
     */
    struct PassDependency
    {
        /// The identifier of what's required (from PassProvides namespace)
        std::string id;

        /// Whether this dependency is required (true) or optional (false)
        bool is_required;

        /// Create a required dependency
        static PassDependency required(const std::string &id)
        {
            return PassDependency{id, true};
        }

        /// Create an optional/soft dependency
        static PassDependency soft(const std::string &id)
        {
            return PassDependency{id, false};
        }
    };

    /**
     * @brief Result of running a compiler pass
     *
     * Note: Errors and warnings should be emitted directly to the DiagEmitter
     * via PassContext::diagnostics(). The PassResult only tracks:
     * - Whether the pass succeeded (based on DiagEmitter::has_errors() or explicit failure)
     * - What the pass provided for dependency tracking
     *
     * This ensures the Diagnostics system remains the single point of truth
     * for all error management.
     */
    struct PassResult
    {
        bool success = true;

        // What this pass produced (for dependency tracking)
        std::unordered_set<std::string> provided;

        // Number of errors emitted during this pass (for logging)
        size_t errors_emitted = 0;
        size_t warnings_emitted = 0;

        static PassResult ok(std::unordered_set<std::string> provided = {})
        {
            PassResult result;
            result.success = true;
            result.provided = std::move(provided);
            return result;
        }

        static PassResult failure()
        {
            PassResult result;
            result.success = false;
            return result;
        }

        // Create a result that checks the DiagEmitter for errors
        static PassResult from_diagnostics(DiagEmitter *diag,
                                           size_t errors_before,
                                           size_t warnings_before,
                                           std::unordered_set<std::string> provided = {})
        {
            PassResult result;
            result.provided = std::move(provided);

            if (diag)
            {
                result.errors_emitted = diag->error_count() - errors_before;
                result.warnings_emitted = diag->warning_count() - warnings_before;
                // Check if THIS pass emitted errors, not total accumulated errors
                // This is critical for stdlib compilation where errors accumulate across modules
                result.success = (result.errors_emitted == 0);
            }
            else
            {
                result.success = true;
            }

            return result;
        }
    };

    /**
     * @brief Context passed to each compiler pass
     *
     * Provides access to all compiler components and shared state.
     * Passes should only access what they need.
     */
    class PassContext
    {
    public:
        explicit PassContext(CompilerInstance &compiler);

        // Source information
        const std::string &source_file() const { return _source_file; }
        void set_source_file(const std::string &file) { _source_file = file; }

        // AST access
        ProgramNode *ast_root() const { return _ast_root; }
        void set_ast_root(ProgramNode *root) { _ast_root = root; }

        // Component access - semantic components
        ASTContext *ast_context() const { return _ast_context; }
        SymbolTable *symbol_table() const { return _symbol_table; }
        TypeChecker *type_checker() const { return _type_checker; }
        TypeResolver *type_resolver() const { return _type_resolver; }
        GenericRegistry *generic_registry() const { return _generic_registry; }
        Monomorphizer *monomorphizer() const { return _monomorphizer; }
        TemplateRegistry *template_registry() const { return _template_registry; }
        ModuleLoader *module_loader() const { return _module_loader; }
        DiagEmitter *diagnostics() const { return _diagnostics; }
        SRM::SymbolResolutionManager *symbol_resolution_manager() const { return _srm; }

        // Component access - codegen components (only for Stage 7+)
        Codegen::CodeGenerator *codegen() const { return _codegen; }

        // Diagnostic helpers for pass execution
        // Call before running a pass to snapshot current counts
        size_t current_error_count() const
        {
            return _diagnostics ? _diagnostics->error_count() : 0;
        }

        size_t current_warning_count() const
        {
            return _diagnostics ? _diagnostics->warning_count() : 0;
        }

        bool has_errors() const
        {
            return _diagnostics && _diagnostics->has_errors();
        }

        // Emit diagnostics with proper error codes
        // Passes should use appropriate ErrorCodes from ErrorCodes.hpp
        void emit_error(ErrorCode code, const std::string &message);
        void emit_warning(ErrorCode code, const std::string &message);

        // Emit diagnostics with source location
        void emit_error(ErrorCode code, const std::string &message, const Span &span);
        void emit_warning(ErrorCode code, const std::string &message, const Span &span);

        // Compilation flags
        bool is_stdlib_mode() const { return _stdlib_mode; }
        void set_stdlib_mode(bool mode) { _stdlib_mode = mode; }

        bool is_debug_mode() const { return _debug_mode; }
        void set_debug_mode(bool mode) { _debug_mode = mode; }

        bool auto_imports_enabled() const { return _auto_imports_enabled; }
        void set_auto_imports_enabled(bool enabled) { _auto_imports_enabled = enabled; }

        // Namespace context
        const std::string &current_namespace() const { return _current_namespace; }
        void set_current_namespace(const std::string &ns) { _current_namespace = ns; }

        // Track what has been provided by previous passes
        bool has_provided(const std::string &item) const
        {
            return _provided_items.count(item) > 0;
        }

        void mark_provided(const std::string &item)
        {
            _provided_items.insert(item);
        }

        void mark_provided(const std::unordered_set<std::string> &items)
        {
            _provided_items.insert(items.begin(), items.end());
        }

        // Module graph for multi-module compilation
        struct ModuleInfo
        {
            std::string name;
            std::string file_path;
            ProgramNode *ast = nullptr;
            std::vector<std::string> dependencies;
            bool processed = false;
        };

        void add_module(const std::string &name, const ModuleInfo &info)
        {
            _modules[name] = info;
        }

        ModuleInfo *get_module(const std::string &name)
        {
            auto it = _modules.find(name);
            return it != _modules.end() ? &it->second : nullptr;
        }

        const std::unordered_map<std::string, ModuleInfo> &modules() const
        {
            return _modules;
        }

        // Topologically sorted module order (set by ModuleResolution stage)
        const std::vector<std::string> &module_order() const { return _module_order; }
        void set_module_order(std::vector<std::string> order) { _module_order = std::move(order); }

    private:
        // Source info
        std::string _source_file;
        ProgramNode *_ast_root = nullptr;

        // Component pointers (non-owning, owned by CompilerInstance)
        ASTContext *_ast_context = nullptr;
        SymbolTable *_symbol_table = nullptr;
        TypeChecker *_type_checker = nullptr;
        TypeResolver *_type_resolver = nullptr;
        GenericRegistry *_generic_registry = nullptr;
        Monomorphizer *_monomorphizer = nullptr;
        TemplateRegistry *_template_registry = nullptr;
        ModuleLoader *_module_loader = nullptr;
        DiagEmitter *_diagnostics = nullptr;
        SRM::SymbolResolutionManager *_srm = nullptr;
        Codegen::CodeGenerator *_codegen = nullptr;

        // Compilation flags
        bool _stdlib_mode = false;
        bool _debug_mode = false;
        bool _auto_imports_enabled = true;
        std::string _current_namespace;

        // Dependency tracking
        std::unordered_set<std::string> _provided_items;

        // Module graph
        std::unordered_map<std::string, ModuleInfo> _modules;
        std::vector<std::string> _module_order;
    };

    /**
     * @brief Abstract base class for all compiler passes
     *
     * Each pass represents a discrete step in the compilation pipeline.
     * Passes declare their dependencies and what they provide, allowing
     * the PassManager to validate and order them correctly.
     */
    class CompilerPass
    {
    public:
        virtual ~CompilerPass() = default;

        // Pass identification
        virtual std::string name() const = 0;
        virtual PassStage stage() const = 0;

        /**
         * @brief Order within the stage (e.g., 1, 2, 3)
         *
         * Passes within the same stage are sorted by this value.
         * Use integers for clarity (1, 2, 3) rather than floats.
         */
        virtual int order() const = 0;

        /**
         * @brief Scope at which this pass operates
         */
        virtual PassScope scope() const = 0;

        /**
         * @brief Items this pass requires from previous passes
         *
         * The PassManager will ensure all required dependencies are provided
         * before running this pass. Optional dependencies are checked but
         * do not prevent the pass from running.
         */
        virtual std::vector<PassDependency> dependencies() const { return {}; }

        /**
         * @brief Items this pass provides for later passes
         *
         * These are added to the PassContext after successful execution.
         */
        virtual std::vector<std::string> provides() const { return {}; }

        /**
         * @brief Whether compilation should halt if this pass fails
         *
         * Some passes (like monomorphization) may have non-fatal failures
         * where unused code fails to compile but the program is still valid.
         */
        virtual bool is_fatal_on_failure() const { return true; }

        /**
         * @brief Execute this pass
         *
         * @param ctx The compilation context with all available components
         * @return PassResult indicating success/failure and what was produced
         */
        virtual PassResult run(PassContext &ctx) = 0;

        /**
         * @brief Get a description of what this pass does
         */
        virtual std::string description() const { return ""; }
    };

    /**
     * @brief Manages registration and execution of compiler passes
     *
     * The PassManager is responsible for:
     * - Registering passes
     * - Validating the dependency graph
     * - Executing passes in the correct order
     * - Tracking what has been provided
     * - Reporting pass execution status
     */
    class PassManager
    {
    public:
        PassManager();
        ~PassManager() = default;

        /**
         * @brief Register a pass with the manager
         *
         * Passes should be registered before calling validate() or run().
         */
        void register_pass(std::unique_ptr<CompilerPass> pass);

        /**
         * @brief Register multiple passes at once
         */
        template <typename... Passes>
        void register_passes(std::unique_ptr<Passes>... passes)
        {
            (register_pass(std::move(passes)), ...);
        }

        /**
         * @brief Validate the dependency graph
         *
         * Checks that:
         * - All required items are provided by some pass
         * - No circular dependencies exist
         * - Pass ordering is consistent
         *
         * @return true if valid, false otherwise (errors logged)
         */
        bool validate();

        /**
         * @brief Run all registered passes
         *
         * Passes are executed in stage order, then by order() within each stage.
         * Execution stops on the first fatal error.
         *
         * @param ctx The compilation context
         * @return true if all passes succeeded (or had non-fatal failures)
         */
        bool run_all(PassContext &ctx);

        /**
         * @brief Run passes up to and including a specific stage
         *
         * Useful for partial compilation (e.g., frontend-only for LSP).
         */
        bool run_until(PassContext &ctx, PassStage until_stage);

        /**
         * @brief Run a specific stage only
         *
         * Assumes previous stages have been run.
         */
        bool run_stage(PassContext &ctx, PassStage stage);

        /**
         * @brief Get all registered passes
         */
        const std::vector<std::unique_ptr<CompilerPass>> &passes() const { return _passes; }

        /**
         * @brief Get passes in execution order
         */
        std::vector<CompilerPass *> execution_order() const;

        /**
         * @brief Print the pass execution order for debugging
         */
        void dump_pass_order(std::ostream &os) const;

        /**
         * @brief Enable/disable verbose logging of pass execution
         */
        void set_verbose(bool verbose) { _verbose = verbose; }

        /**
         * @brief Get execution statistics
         */
        struct PassStats
        {
            std::string pass_name;
            PassStage stage;
            bool executed = false;
            bool success = false;
            double duration_ms = 0.0;

            // Diagnostic counts from this pass
            size_t errors_emitted = 0;
            size_t warnings_emitted = 0;
        };

        const std::vector<PassStats> &stats() const { return _stats; }

    private:
        std::vector<std::unique_ptr<CompilerPass>> _passes;
        std::vector<PassStats> _stats;
        bool _validated = false;
        bool _verbose = false;

        // Compute execution order based on stage and order()
        void compute_execution_order();
        std::vector<CompilerPass *> _execution_order;

        // Check if all requirements for a pass are satisfied
        bool check_requirements(CompilerPass *pass, const PassContext &ctx) const;
    };

    // ============================================================================
    // Standard Pass Identifiers (for dependency tracking)
    // ============================================================================

    namespace PassProvides
    {
        // Stage 1: Frontend
        constexpr const char *TOKENS = "tokens";
        constexpr const char *AST = "ast";
        constexpr const char *AST_VALIDATED = "ast_validated";

        // Stage 2: Module Resolution
        constexpr const char *IMPORTS_DISCOVERED = "imports_discovered";
        constexpr const char *MODULES_LOADED = "modules_loaded";
        constexpr const char *MODULE_GRAPH = "module_graph";
        constexpr const char *MODULE_ORDER = "module_order";

        // Stage 3: Declaration Collection
        constexpr const char *TYPE_DECLARATIONS = "type_declarations";
        constexpr const char *FUNCTION_SIGNATURES = "function_signatures";
        constexpr const char *CONSTANT_DECLARATIONS = "constant_declarations";
        constexpr const char *TEMPLATES_REGISTERED = "templates_registered";

        // Stage 4: Type Resolution
        constexpr const char *TYPES_RESOLVED = "types_resolved";
        constexpr const char *FIELD_TYPES_RESOLVED = "field_types_resolved";
        constexpr const char *PARAM_TYPES_RESOLVED = "param_types_resolved";
        constexpr const char *CONSTRAINTS_VALIDATED = "constraints_validated";

        // Stage 5: Semantic Analysis
        constexpr const char *BODIES_TYPE_CHECKED = "bodies_type_checked";
        constexpr const char *EXPRESSIONS_TYPED = "expressions_typed";
        constexpr const char *CONTROL_FLOW_ANALYZED = "control_flow_analyzed";
        constexpr const char *DIRECTIVES_PROCESSED = "directives_processed";

        // Stage 6: Specialization
        constexpr const char *INSTANTIATIONS_COLLECTED = "instantiations_collected";
        constexpr const char *MONOMORPHIZATION_COMPLETE = "monomorphization_complete";
        constexpr const char *GENERIC_EXPRESSIONS_RESOLVED = "generic_expressions_resolved";

        // Stage 7: Codegen Preparation
        constexpr const char *TYPES_LOWERED = "types_lowered";
        constexpr const char *FUNCTIONS_DECLARED = "functions_declared";
        constexpr const char *GLOBALS_DECLARED = "globals_declared";

        // Stage 8: IR Generation
        constexpr const char *IR_GENERATED = "ir_generated";

        // Stage 9: Optimization
        constexpr const char *OPTIMIZED = "optimized";
        constexpr const char *OBJECTS_EMITTED = "objects_emitted";
        constexpr const char *LINKED = "linked";
    } // namespace PassProvides

} // namespace Cryo
