#include "Compiler/PassManager.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "AST/ASTContext.hpp"
#include "AST/ASTNode.hpp"
#include "AST/TemplateRegistry.hpp"
#include "Types/SymbolTable.hpp"
#include "Types/TypeChecker.hpp"
#include "Types/TypeResolver.hpp"
#include "Types/GenericRegistry.hpp"
#include "Types/Monomorphizer.hpp"
#include "Codegen/CodeGenerator.hpp"
#include "Diagnostics/Diag.hpp"
#include "Utils/SymbolResolutionManager.hpp"
#include "Utils/Logger.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>

namespace Cryo
{
    // ============================================================================
    // PassContext Implementation
    // ============================================================================

    PassContext::PassContext(CompilerInstance &compiler)
    {
        // Get all component pointers from the compiler instance
        _ast_context = compiler.ast_context();
        _symbol_table = compiler.symbol_table();
        _type_checker = compiler.type_checker();
        _type_resolver = compiler.type_resolver();
        _generic_registry = compiler.generic_registry();
        _monomorphizer = compiler.monomorphizer();
        _template_registry = compiler.template_registry();
        _diagnostics = compiler.diagnostics();
        _codegen = compiler.codegen();
        _module_loader = compiler.module_loader();
        _srm = compiler.symbol_resolution_manager();

        // Get compilation flags
        _stdlib_mode = compiler.stdlib_compilation_mode();
        _debug_mode = false; // Will be set by caller if needed
        _auto_imports_enabled = compiler.auto_imports_enabled();
        _current_namespace = compiler.get_namespace_context();

        // AST root will be set after parsing
        _ast_root = compiler.ast_root();
    }

    void PassContext::emit_error(ErrorCode code, const std::string &message)
    {
        if (_diagnostics)
        {
            _diagnostics->emit(Diag::error(code, message));
        }
    }

    void PassContext::emit_warning(ErrorCode code, const std::string &message)
    {
        if (_diagnostics)
        {
            _diagnostics->emit(Diag::warning(code, message));
        }
    }

    void PassContext::emit_error(ErrorCode code, const std::string &message, const Span &span)
    {
        if (_diagnostics)
        {
            _diagnostics->emit(Diag::error(code, message).at(span));
        }
    }

    void PassContext::emit_warning(ErrorCode code, const std::string &message, const Span &span)
    {
        if (_diagnostics)
        {
            _diagnostics->emit(Diag::warning(code, message).at(span));
        }
    }

    // ============================================================================
    // PassManager Implementation
    // ============================================================================

    PassManager::PassManager()
    {
    }

    void PassManager::register_pass(std::unique_ptr<CompilerPass> pass)
    {
        if (!pass)
        {
            LOG_ERROR(LogComponent::GENERAL, "Attempted to register null pass");
            return;
        }

        LOG_DEBUG(LogComponent::GENERAL, "Registering pass: {} (Stage {}, Order {})",
                  pass->name(), pass_stage_to_string(pass->stage()), pass->order());

        _passes.push_back(std::move(pass));
        _validated = false; // Need to re-validate after adding passes
    }

    bool PassManager::validate()
    {
        if (_passes.empty())
        {
            LOG_WARN(LogComponent::GENERAL, "PassManager: No passes registered");
            return true;
        }

        // Compute execution order
        compute_execution_order();

        // Build a map of what each pass provides
        std::unordered_set<std::string> all_provided;

        // Check each pass's dependencies
        for (auto *pass : _execution_order)
        {
            // Check that all dependencies are satisfied
            for (const auto &dep : pass->dependencies())
            {
                if (all_provided.find(dep) == all_provided.end())
                {
                    LOG_ERROR(LogComponent::GENERAL,
                              "PassManager validation failed: Pass '{}' requires '{}' but no earlier pass provides it",
                              pass->name(), dep);
                    return false;
                }
            }

            // Add what this pass provides
            for (const auto &item : pass->provides())
            {
                all_provided.insert(item);
            }
        }

        _validated = true;
        LOG_DEBUG(LogComponent::GENERAL, "PassManager: Validated {} passes", _passes.size());
        return true;
    }

    void PassManager::compute_execution_order()
    {
        _execution_order.clear();

        // Collect all passes
        for (auto &pass : _passes)
        {
            _execution_order.push_back(pass.get());
        }

        // Sort by stage first, then by order within stage
        std::sort(_execution_order.begin(), _execution_order.end(),
                  [](CompilerPass *a, CompilerPass *b)
                  {
                      if (a->stage() != b->stage())
                      {
                          return static_cast<int>(a->stage()) < static_cast<int>(b->stage());
                      }
                      return a->order() < b->order();
                  });
    }

    std::vector<CompilerPass *> PassManager::execution_order() const
    {
        return _execution_order;
    }

    bool PassManager::check_requirements(CompilerPass *pass, const PassContext &ctx) const
    {
        for (const auto &dep : pass->dependencies())
        {
            if (!ctx.has_provided(dep))
            {
                LOG_ERROR(LogComponent::GENERAL,
                          "Pass '{}' requires '{}' which has not been provided",
                          pass->name(), dep);
                return false;
            }
        }
        return true;
    }

    bool PassManager::run_all(PassContext &ctx)
    {
        if (!_validated)
        {
            if (!validate())
            {
                return false;
            }
        }

        _stats.clear();

        for (auto *pass : _execution_order)
        {
            PassStats stats;
            stats.pass_name = pass->name();
            stats.stage = pass->stage();
            stats.executed = true;

            if (_verbose)
            {
                LOG_INFO(LogComponent::GENERAL, "Running pass: {} [Stage {}.{}]",
                         pass->name(), static_cast<int>(pass->stage()), pass->order());
            }

            // Check requirements
            if (!check_requirements(pass, ctx))
            {
                stats.success = false;
                _stats.push_back(stats);

                // Emit a diagnostic for the requirement failure
                ctx.emit_error(ErrorCode::E0901_UNEXPECTED_COMPILER_STATE,
                               "Pass '" + pass->name() + "': Required dependencies not satisfied");

                if (pass->is_fatal_on_failure())
                {
                    LOG_ERROR(LogComponent::GENERAL, "Pass '{}' failed requirement check (fatal)", pass->name());
                    return false;
                }
                else
                {
                    LOG_WARN(LogComponent::GENERAL, "Pass '{}' failed requirement check (non-fatal)", pass->name());
                    continue;
                }
            }

            // Snapshot diagnostic counts before running the pass
            size_t errors_before = ctx.current_error_count();
            size_t warnings_before = ctx.current_warning_count();

            // Run the pass with timing
            auto start = std::chrono::high_resolution_clock::now();
            PassResult result = pass->run(ctx);
            auto end = std::chrono::high_resolution_clock::now();

            stats.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
            stats.success = result.success;
            stats.errors_emitted = result.errors_emitted;
            stats.warnings_emitted = result.warnings_emitted;

            // If the pass didn't track its own counts, compute from diagnostics
            if (stats.errors_emitted == 0 && stats.warnings_emitted == 0)
            {
                stats.errors_emitted = ctx.current_error_count() - errors_before;
                stats.warnings_emitted = ctx.current_warning_count() - warnings_before;
            }

            _stats.push_back(stats);

            if (result.success)
            {
                // Mark provided items
                ctx.mark_provided(result.provided);

                // Also mark the declared provides
                for (const auto &item : pass->provides())
                {
                    ctx.mark_provided(item);
                }

                if (_verbose)
                {
                    if (stats.errors_emitted > 0 || stats.warnings_emitted > 0)
                    {
                        LOG_INFO(LogComponent::GENERAL,
                                 "Pass '{}' completed in {:.2f}ms ({} errors, {} warnings)",
                                 pass->name(), stats.duration_ms,
                                 stats.errors_emitted, stats.warnings_emitted);
                    }
                    else
                    {
                        LOG_INFO(LogComponent::GENERAL, "Pass '{}' completed in {:.2f}ms",
                                 pass->name(), stats.duration_ms);
                    }
                }
            }
            else
            {
                if (pass->is_fatal_on_failure())
                {
                    LOG_ERROR(LogComponent::GENERAL,
                              "Pass '{}' failed with {} errors (fatal)",
                              pass->name(), stats.errors_emitted);
                    return false;
                }
                else
                {
                    LOG_WARN(LogComponent::GENERAL,
                             "Pass '{}' failed with {} errors (non-fatal, continuing)",
                             pass->name(), stats.errors_emitted);
                }
            }
        }

        // Final check: return false if any errors were emitted
        return !ctx.has_errors();
    }

    bool PassManager::run_until(PassContext &ctx, PassStage until_stage)
    {
        if (!_validated)
        {
            if (!validate())
            {
                return false;
            }
        }

        _stats.clear();

        for (auto *pass : _execution_order)
        {
            // Stop if we've passed the target stage
            if (static_cast<int>(pass->stage()) > static_cast<int>(until_stage))
            {
                break;
            }

            PassStats stats;
            stats.pass_name = pass->name();
            stats.stage = pass->stage();
            stats.executed = true;

            if (_verbose)
            {
                LOG_INFO(LogComponent::GENERAL, "Running pass: {} [Stage {}.{}]",
                         pass->name(), static_cast<int>(pass->stage()), pass->order());
            }

            // Check requirements
            if (!check_requirements(pass, ctx))
            {
                stats.success = false;
                _stats.push_back(stats);

                ctx.emit_error(ErrorCode::E0901_UNEXPECTED_COMPILER_STATE,
                               "Pass '" + pass->name() + "': Required dependencies not satisfied");

                if (pass->is_fatal_on_failure())
                {
                    return false;
                }
                continue;
            }

            // Snapshot diagnostic counts
            size_t errors_before = ctx.current_error_count();
            size_t warnings_before = ctx.current_warning_count();

            // Run the pass
            auto start = std::chrono::high_resolution_clock::now();
            PassResult result = pass->run(ctx);
            auto end = std::chrono::high_resolution_clock::now();

            stats.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
            stats.success = result.success;
            stats.errors_emitted = ctx.current_error_count() - errors_before;
            stats.warnings_emitted = ctx.current_warning_count() - warnings_before;
            _stats.push_back(stats);

            if (result.success)
            {
                ctx.mark_provided(result.provided);
                for (const auto &item : pass->provides())
                {
                    ctx.mark_provided(item);
                }
            }
            else if (pass->is_fatal_on_failure())
            {
                return false;
            }
        }

        return !ctx.has_errors();
    }

    bool PassManager::run_stage(PassContext &ctx, PassStage stage)
    {
        if (!_validated)
        {
            if (!validate())
            {
                return false;
            }
        }

        for (auto *pass : _execution_order)
        {
            if (pass->stage() != stage)
            {
                continue;
            }

            if (_verbose)
            {
                LOG_INFO(LogComponent::GENERAL, "Running pass: {} [Stage {}.{}]",
                         pass->name(), static_cast<int>(pass->stage()), pass->order());
            }

            if (!check_requirements(pass, ctx))
            {
                ctx.emit_error(ErrorCode::E0901_UNEXPECTED_COMPILER_STATE,
                               "Pass '" + pass->name() + "': Required dependencies not satisfied");

                if (pass->is_fatal_on_failure())
                {
                    return false;
                }
                continue;
            }

            PassResult result = pass->run(ctx);

            if (result.success)
            {
                ctx.mark_provided(result.provided);
                for (const auto &item : pass->provides())
                {
                    ctx.mark_provided(item);
                }
            }
            else if (pass->is_fatal_on_failure())
            {
                return false;
            }
        }

        return !ctx.has_errors();
    }

    void PassManager::dump_pass_order(std::ostream &os) const
    {
        os << "=== Pass Execution Order ===\n";
        os << "Total passes: " << _execution_order.size() << "\n\n";

        PassStage current_stage = PassStage::Frontend;
        bool first_in_stage = true;

        for (const auto *pass : _execution_order)
        {
            if (pass->stage() != current_stage)
            {
                current_stage = pass->stage();
                first_in_stage = true;
                os << "\n";
            }

            if (first_in_stage)
            {
                os << "Stage " << static_cast<int>(current_stage)
                   << " (" << pass_stage_to_string(current_stage) << "):\n";
                first_in_stage = false;
            }

            os << "  " << static_cast<int>(current_stage) << "." << pass->order()
               << " " << pass->name();

            if (!pass->description().empty())
            {
                os << " - " << pass->description();
            }

            // Show dependencies
            auto deps = pass->dependencies();
            if (!deps.empty())
            {
                os << "\n      requires: ";
                for (size_t i = 0; i < deps.size(); ++i)
                {
                    if (i > 0)
                        os << ", ";
                    os << deps[i];
                }
            }

            // Show provides
            auto provs = pass->provides();
            if (!provs.empty())
            {
                os << "\n      provides: ";
                for (size_t i = 0; i < provs.size(); ++i)
                {
                    if (i > 0)
                        os << ", ";
                    os << provs[i];
                }
            }

            os << "\n";
        }

        os << "\n=== End Pass Order ===\n";
    }

} // namespace Cryo
