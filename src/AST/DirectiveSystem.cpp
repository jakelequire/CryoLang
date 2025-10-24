#include "AST/DirectiveSystem.hpp"
#include "AST/ASTNode.hpp"
#include "Utils/Logger.hpp"
#include <iostream>
#include <algorithm>

namespace Cryo
{

    //===----------------------------------------------------------------------===//
    // CompilationContext Implementation
    //===----------------------------------------------------------------------===//

    void CompilationContext::add_pending_effect(std::unique_ptr<DirectiveEffect> effect)
    {
        if (effect)
        {
            _pending_effects.push_back(std::move(effect));
        }
    }

    void CompilationContext::process_pending_effects(ASTNode *target_node)
    {
        if (!target_node || _pending_effects.empty())
        {
            return;
        }

        for (auto &effect : _pending_effects)
        {
            if (effect->apply(target_node))
            {
                _active_effects.push_back(std::move(effect));
            }
            else
            {
                LOG_WARN(LogComponent::GENERAL, "Failed to apply directive effect: {}",
                         effect->get_description());
            }
        }

        _pending_effects.clear();
    }

    bool CompilationContext::validate_all_effects() const
    {
        bool all_valid = true;

        for (const auto &effect : _active_effects)
        {
            if (!effect->validate())
            {
                LOG_ERROR(LogComponent::GENERAL, "Directive effect validation failed: {}",
                          effect->get_description());
                all_valid = false;
            }
        }

        return all_valid;
    }

    bool CompilationContext::has_context(const std::string &key) const
    {
        return _context_data.find(key) != _context_data.end();
    }

    void CompilationContext::clear_context()
    {
        _context_data.clear();
        _pending_effects.clear();
        _active_effects.clear();
    }

    //===----------------------------------------------------------------------===//
    // DirectiveRegistry Implementation
    //===----------------------------------------------------------------------===//

    void DirectiveRegistry::register_processor(std::unique_ptr<DirectiveProcessor> processor)
    {
        if (!processor)
        {
            LOG_ERROR(LogComponent::GENERAL, "Attempted to register null directive processor");
            return;
        }

        std::string name = processor->get_directive_name();
        if (name.empty())
        {
            LOG_ERROR(LogComponent::GENERAL, "Directive processor has empty name");
            return;
        }

        if (_processors.find(name) != _processors.end())
        {
            LOG_WARN(LogComponent::GENERAL, "Overwriting existing directive processor: {}", name);
        }

        _processors[name] = std::move(processor);
        LOG_DEBUG(LogComponent::GENERAL, "Registered directive processor: {}", name);
    }

    DirectiveProcessor *DirectiveRegistry::get_processor(const std::string &name) const
    {
        auto it = _processors.find(name);
        return (it != _processors.end()) ? it->second.get() : nullptr;
    }

    bool DirectiveRegistry::has_processor(const std::string &name) const
    {
        return _processors.find(name) != _processors.end();
    }

    std::vector<std::string> DirectiveRegistry::get_available_directives() const
    {
        std::vector<std::string> directives;
        directives.reserve(_processors.size());

        for (const auto &[name, processor] : _processors)
        {
            directives.push_back(name);
        }

        std::sort(directives.begin(), directives.end());
        return directives;
    }

    std::vector<std::string> DirectiveRegistry::get_statement_level_directives() const
    {
        std::vector<std::string> directives;

        for (const auto &[name, processor] : _processors)
        {
            if (processor->is_statement_level())
            {
                directives.push_back(name);
            }
        }

        std::sort(directives.begin(), directives.end());
        return directives;
    }

    std::vector<std::string> DirectiveRegistry::get_file_level_directives() const
    {
        std::vector<std::string> directives;

        for (const auto &[name, processor] : _processors)
        {
            if (!processor->is_statement_level())
            {
                directives.push_back(name);
            }
        }

        std::sort(directives.begin(), directives.end());
        return directives;
    }

    std::string DirectiveRegistry::get_directive_help(const std::string &name) const
    {
        auto processor = get_processor(name);
        if (!processor)
        {
            return "Unknown directive: " + name;
        }

        return processor->get_help_text();
    }

    void DirectiveRegistry::print_all_directives_help() const
    {
        std::cout << "Available Directives:\n";
        std::cout << "====================\n\n";

        auto file_level = get_file_level_directives();
        auto stmt_level = get_statement_level_directives();

        if (!file_level.empty())
        {
            std::cout << "File-level directives:\n";
            for (const auto &name : file_level)
            {
                auto processor = get_processor(name);
                std::cout << "  #[" << name << "] - " << processor->get_help_text() << "\n";
            }
            std::cout << "\n";
        }

        if (!stmt_level.empty())
        {
            std::cout << "Statement-level directives:\n";
            for (const auto &name : stmt_level)
            {
                auto processor = get_processor(name);
                std::cout << "  #[" << name << "] - " << processor->get_help_text() << "\n";
            }
            std::cout << "\n";
        }
    }

    //===----------------------------------------------------------------------===//
    // ErrorExpectationEffect Implementation
    //===----------------------------------------------------------------------===//

    ErrorExpectationEffect::ErrorExpectationEffect(const std::vector<std::string> &expected_errors, SourceRange range)
        : _expected_error_codes(expected_errors), _target_range(range)
    {
    }

    bool ErrorExpectationEffect::apply(ASTNode *target_node)
    {
        if (!target_node)
        {
            return false;
        }

        // Store the target node's source range for error matching
        // This will be used during error reporting to check if errors occur in the expected location
        LOG_DEBUG(LogComponent::GENERAL, "Applied error expectation effect for {} errors at line {}",
                  _expected_error_codes.size(), _target_range.start.line());

        return true;
    }

    bool ErrorExpectationEffect::validate() const
    {
        _validated = true;

        // Check if all expected errors were found
        for (const auto &expected : _expected_error_codes)
        {
            if (std::find(_found_errors.begin(), _found_errors.end(), expected) == _found_errors.end())
            {
                LOG_ERROR(LogComponent::GENERAL, "Expected error '{}' was not found", expected);
                return false;
            }
        }

        // Check if any unexpected errors were found
        for (const auto &found : _found_errors)
        {
            if (std::find(_expected_error_codes.begin(), _expected_error_codes.end(), found) == _expected_error_codes.end())
            {
                LOG_WARN(LogComponent::GENERAL, "Unexpected error '{}' was found", found);
                // This is a warning, not a failure
            }
        }

        return true;
    }

    std::string ErrorExpectationEffect::get_description() const
    {
        std::string desc = "Expect errors: [";
        for (size_t i = 0; i < _expected_error_codes.size(); ++i)
        {
            if (i > 0)
                desc += ", ";
            desc += _expected_error_codes[i];
        }
        desc += "] at line " + std::to_string(_target_range.start.line());
        return desc;
    }

    void ErrorExpectationEffect::record_found_error(const std::string &error_code)
    {
        _found_errors.push_back(error_code);
        LOG_DEBUG(LogComponent::GENERAL, "Recorded found error: {}", error_code);
    }

    //===----------------------------------------------------------------------===//
    // TestMetadataEffect Implementation
    //===----------------------------------------------------------------------===//

    TestMetadataEffect::TestMetadataEffect(const std::string &name, const std::string &category)
        : _test_name(name), _test_category(category)
    {
    }

    bool TestMetadataEffect::apply(ASTNode *target_node)
    {
        if (!target_node)
        {
            return false;
        }

        LOG_DEBUG(LogComponent::GENERAL, "Applied test metadata effect: name='{}', category='{}'",
                  _test_name, _test_category);

        return true;
    }

    bool TestMetadataEffect::validate() const
    {
        // Test metadata is always valid if it was successfully applied
        return !_test_name.empty();
    }

    std::string TestMetadataEffect::get_description() const
    {
        return "Test metadata: name='" + _test_name + "', category='" + _test_category + "'";
    }

    void TestMetadataEffect::add_metadata(const std::string &key, const std::string &value)
    {
        _metadata[key] = value;
    }

    std::optional<std::string> TestMetadataEffect::get_metadata(const std::string &key) const
    {
        auto it = _metadata.find(key);
        return (it != _metadata.end()) ? std::make_optional(it->second) : std::nullopt;
    }

} // namespace Cryo