#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <any>
#include <optional>
#include "AST/ASTNode.hpp"
#include "Lexer/lexer.hpp"
#include "Diagnostics/ErrorCodes.hpp"
#include "Diagnostics/Diag.hpp"

// Forward declarations
namespace Cryo
{
    class Parser;
    class DirectiveNode;
    class ASTNode;
    class CompilerInstance;
}

namespace Cryo
{

    //===----------------------------------------------------------------------===//
    // DirectiveEffect - Represents an effect that a directive has on compilation
    //===----------------------------------------------------------------------===//

    class DirectiveEffect
    {
    public:
        virtual ~DirectiveEffect() = default;
        virtual bool apply(ASTNode *target_node) = 0;
        virtual bool validate() const = 0;
        virtual std::string get_description() const = 0;
    };

    //===----------------------------------------------------------------------===//
    // CompilationContext - Holds directive-related compilation state
    //===----------------------------------------------------------------------===//

    class CompilationContext
    {
    private:
        std::unordered_map<std::string, std::any> _context_data;
        std::vector<std::unique_ptr<DirectiveEffect>> _pending_effects;
        std::vector<std::unique_ptr<DirectiveEffect>> _active_effects;

    public:
        // Context data management
        template <typename T>
        void set_context(const std::string &key, T &&value)
        {
            _context_data[key] = std::forward<T>(value);
        }

        template <typename T>
        std::optional<T> get_context(const std::string &key) const
        {
            auto it = _context_data.find(key);
            if (it != _context_data.end())
            {
                try
                {
                    return std::any_cast<T>(it->second);
                }
                catch (const std::bad_any_cast &)
                {
                    return std::nullopt;
                }
            }
            return std::nullopt;
        }

        // Effect management
        void add_pending_effect(std::unique_ptr<DirectiveEffect> effect);
        void process_pending_effects(ASTNode *target_node);
        bool validate_all_effects() const;

        // Query methods
        bool has_context(const std::string &key) const;
        void clear_context();
        size_t pending_effects_count() const { return _pending_effects.size(); }
        size_t active_effects_count() const { return _active_effects.size(); }
    };

    //===----------------------------------------------------------------------===//
    // DirectiveProcessor - Base class for processing specific directive types
    //===----------------------------------------------------------------------===//

    class DirectiveProcessor
    {
    public:
        virtual ~DirectiveProcessor() = default;

        // Core interface
        virtual bool process(const DirectiveNode *directive, CompilationContext &context) = 0;
        virtual std::string get_directive_name() const = 0;
        virtual bool is_statement_level() const = 0; // true for statement-level, false for file-level

        // Parser interface - implemented by concrete processors
        virtual std::unique_ptr<DirectiveNode> parse_directive_arguments(Parser &parser) = 0;

        // Validation
        virtual bool validate_directive(const DirectiveNode *directive) const { return true; }

        // Documentation/Help
        virtual std::string get_help_text() const { return "No help available"; }
        virtual std::vector<std::string> get_argument_names() const { return {}; }
    };

    //===----------------------------------------------------------------------===//
    // DirectiveRegistry - Registry for all directive processors
    //===----------------------------------------------------------------------===//

    class DirectiveRegistry
    {
    private:
        std::unordered_map<std::string, std::unique_ptr<DirectiveProcessor>> _processors;

    public:
        DirectiveRegistry() = default;
        ~DirectiveRegistry() = default;

        // Processor management
        void register_processor(std::unique_ptr<DirectiveProcessor> processor);
        DirectiveProcessor *get_processor(const std::string &name) const;
        bool has_processor(const std::string &name) const;

        // Query methods
        std::vector<std::string> get_available_directives() const;
        std::vector<std::string> get_statement_level_directives() const;
        std::vector<std::string> get_file_level_directives() const;

        // Help and documentation
        std::string get_directive_help(const std::string &name) const;
        void print_all_directives_help() const;
    };

    //===----------------------------------------------------------------------===//
    // Specific DirectiveEffect implementations
    //===----------------------------------------------------------------------===//

    class ErrorExpectationEffect : public DirectiveEffect
    {
    private:
        std::vector<std::string> _expected_error_codes;
        SourceRange _target_range;
        mutable std::vector<std::string> _found_errors;
        mutable bool _validated = false;

    public:
        ErrorExpectationEffect(const std::vector<std::string> &expected_errors, SourceRange range);

        bool apply(ASTNode *target_node) override;
        bool validate() const override;
        std::string get_description() const override;

        // Error tracking
        void record_found_error(const std::string &error_code);
        const std::vector<std::string> &expected_errors() const { return _expected_error_codes; }
        const std::vector<std::string> &found_errors() const { return _found_errors; }
        SourceRange target_range() const { return _target_range; }
    };

    class TestMetadataEffect : public DirectiveEffect
    {
    private:
        std::string _test_name;
        std::string _test_category;
        std::unordered_map<std::string, std::string> _metadata;

    public:
        TestMetadataEffect(const std::string &name, const std::string &category);

        bool apply(ASTNode *target_node) override;
        bool validate() const override;
        std::string get_description() const override;

        // Accessors
        const std::string &test_name() const { return _test_name; }
        const std::string &test_category() const { return _test_category; }
        void add_metadata(const std::string &key, const std::string &value);
        std::optional<std::string> get_metadata(const std::string &key) const;
    };

} // namespace Cryo