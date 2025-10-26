#include "AST/TypeChecker.hpp"
#include "AST/SymbolTable.hpp"
#include "GDM/GDM.hpp"
#include "Lexer/lexer.hpp"
#include "Utils/Logger.hpp"
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <string>
#include <string_view>
#include <fstream>

namespace Cryo
{
    //===----------------------------------------------------------------------===//
    // TypeError Implementation
    //===----------------------------------------------------------------------===//

    std::string TypeError::to_string() const
    {
        std::ostringstream oss;
        oss << "Type error at line " << location.line() << ", column " << location.column() << ": ";
        oss << message;

        if (expected_type && actual_type)
        {
            oss << " (expected '" << expected_type->to_string()
                << "', got '" << actual_type->to_string() << "')";
        }

        return oss.str();
    }

    std::string TypeWarning::to_string() const
    {
        std::ostringstream oss;
        oss << "Type warning at line " << location.line() << ", column " << location.column() << ": ";
        oss << message;

        if (from_type && to_type)
        {
            oss << " (converting from '" << from_type->to_string()
                << "' to '" << to_type->to_string() << "')";
        }

        return oss.str();
    }

    //===----------------------------------------------------------------------===//
    // TypedSymbolTable Implementation
    //===----------------------------------------------------------------------===//

    bool TypedSymbolTable::declare_symbol(const std::string &name, Type *type,
                                          SourceLocation loc, bool is_mutable)
    {
        // Check for redefinition in current scope only
        if (_symbols.find(name) != _symbols.end())
        {
            return false; // Symbol already exists
        }

        TypedSymbol symbol(name, type, loc);
        symbol.is_mutable = is_mutable;
        symbol.is_initialized = true; // Assume initialized for now

        _symbols[name] = std::move(symbol);
        return true;
    }

    bool TypedSymbolTable::declare_symbol(const std::string &name, Type *type,
                                          SourceLocation loc, StructDeclarationNode *struct_node)
    {
        // Check for redefinition in current scope only
        if (_symbols.find(name) != _symbols.end())
        {
            return false; // Symbol already exists
        }

        TypedSymbol symbol(name, type, loc, struct_node);
        symbol.is_mutable = false;
        symbol.is_initialized = true;

        _symbols[name] = std::move(symbol);
        return true;
    }

    bool TypedSymbolTable::declare_symbol(const std::string &name, Type *type,
                                          SourceLocation loc, ClassDeclarationNode *class_node)
    {
        // Check for redefinition in current scope only
        if (_symbols.find(name) != _symbols.end())
        {
            return false; // Symbol already exists
        }

        TypedSymbol symbol(name, type, loc, class_node);
        symbol.is_mutable = false;
        symbol.is_initialized = true;

        _symbols[name] = std::move(symbol);
        return true;
    }

    bool TypedSymbolTable::declare_symbol(const std::string &name, Type *type,
                                          SourceLocation loc, FunctionDeclarationNode *function_node)
    {
        // Check for redefinition in current scope only
        if (_symbols.find(name) != _symbols.end())
        {
            return false; // Symbol already exists
        }

        TypedSymbol symbol(name, type, loc, function_node);
        symbol.is_mutable = false;
        symbol.is_initialized = true;

        _symbols[name] = std::move(symbol);
        return true;
    }

    TypedSymbol *TypedSymbolTable::lookup_symbol(const std::string &name)
    {
        // Search current scope
        auto it = _symbols.find(name);
        if (it != _symbols.end())
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "TypedSymbolTable::lookup_symbol('{}') - FOUND in current scope (has_parent={}) [table={}]", name, _parent_scope ? "true" : "false", static_cast<void *>(this));
            return &it->second;
        }

        // Search parent scopes
        if (_parent_scope)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "TypedSymbolTable::lookup_symbol('{}') - checking parent scope [table={}]", name, static_cast<void *>(this));
            return _parent_scope->lookup_symbol(name);
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "TypedSymbolTable::lookup_symbol('{}') - NOT FOUND (symbol_count={}) [table={}]", name, _symbols.size(), static_cast<void *>(this));
        return nullptr;
    }

    TypedSymbol *TypedSymbolTable::lookup_symbol_in_any_namespace(const std::string &symbol_name)
    {
        // Search current scope
        auto it = _symbols.find(symbol_name);
        if (it != _symbols.end())
        {
            return &it->second;
        }

        // Search parent scopes
        if (_parent_scope)
        {
            return _parent_scope->lookup_symbol_in_any_namespace(symbol_name);
        }

        return nullptr;
    }

    bool TypedSymbolTable::is_symbol_defined(const std::string &name)
    {
        return lookup_symbol(name) != nullptr;
    }

    std::unique_ptr<TypedSymbolTable> TypedSymbolTable::enter_scope()
    {
        // Create a new symbol table with the current table as parent
        auto current_table = std::make_unique<TypedSymbolTable>();
        current_table->_symbols = std::move(this->_symbols);
        current_table->_parent_scope = std::move(this->_parent_scope);

        return std::make_unique<TypedSymbolTable>(std::move(current_table));
    }

    std::unique_ptr<TypedSymbolTable> TypedSymbolTable::exit_scope()
    {
        if (_parent_scope)
        {
            return std::move(_parent_scope);
        }
        else
        {
            // If no parent scope, return a new empty table to avoid null pointer
            return std::make_unique<TypedSymbolTable>();
        }
    }

    void TypedSymbolTable::print_type_table(std::ostream &os) const
    {
        // ANSI color codes for nice formatting
        const std::string RESET = "\033[0m";
        const std::string BOLD = "\033[1m";
        const std::string CYAN = "\033[36m";
        const std::string GREEN = "\033[32m";
        const std::string YELLOW = "\033[33m";
        const std::string BLUE = "\033[34m";
        const std::string MAGENTA = "\033[35m";
        const std::string RED = "\033[31m";

        os << BOLD << CYAN << "\n+=====================================================================================================+" << RESET << std::endl;
        os << BOLD << CYAN << "|                                            Type Table                                               |" << RESET << std::endl;
        os << BOLD << CYAN << "+=====================================================================================================+" << RESET << std::endl;

        if (_symbols.empty() && !_parent_scope)
        {
            os << "|  " << YELLOW << "No types found" << RESET << "                                                                        |" << std::endl;
            os << BOLD << CYAN << "+======================================================================================+" << RESET << std::endl;
            return;
        }

        // Count total symbols across all scopes
        int total_symbols = 0;
        const TypedSymbolTable *current = this;
        while (current)
        {
            total_symbols += current->_symbols.size();
            current = current->_parent_scope.get();
        }

        os << "|  Total symbols: " << BOLD << GREEN << total_symbols << RESET;
        // Pad to align right side of box
        std::string count_str = std::to_string(total_symbols);
        for (int i = count_str.length(); i < 84; i++)
        {
            os << " ";
        }
        os << "|" << std::endl;
        os << BOLD << CYAN << "+---------------+------------------+-------------------------------------+--------+--------+----------+" << RESET << std::endl;
        os << BOLD << CYAN << "|    TypeOf     |      Symbol      |           Type Signature            | Flags  |  Size  | Location |" << RESET << std::endl;
        os << BOLD << CYAN << "+---------------+------------------+-------------------------------------+--------+--------+----------+" << RESET << std::endl;

        // Print symbols from current and parent scopes
        print_type_symbols(os, 0);

        os << BOLD << CYAN << "+=====================================================================================================+" << RESET << std::endl;
    }

    void TypedSymbolTable::print_type_symbols(std::ostream &os, int scope_level) const
    {
        // ANSI color codes
        const std::string RESET = "\033[0m";
        const std::string BOLD = "\033[1m";
        const std::string CYAN = "\033[36m";
        const std::string GREEN = "\033[32m";
        const std::string YELLOW = "\033[33m";
        const std::string BLUE = "\033[34m";
        const std::string MAGENTA = "\033[35m";
        const std::string RED = "\033[31m";

        // Print symbols in current scope
        for (const auto &[name, symbol] : _symbols)
        {
            std::string type_category = determine_type_category(symbol.type);

            // Only include: Struct, Class, Trait, Enum declarations, and Alias types
            // Exclude: Functions, Primitives (enum variants), Arrays, Pointers, etc.
            if (type_category != "Struct" && type_category != "Class" &&
                type_category != "Trait" && type_category != "Enum" && type_category != "Alias")
            {
                continue; // Skip everything else
            }

            std::string type_signature = symbol.type ? symbol.type->to_string() : "unknown";
            std::string flags = determine_flags(symbol);
            std::string size = format_type_size(symbol.type);
            std::string location = std::to_string(symbol.declaration_location.line()) +
                                   ":" + std::to_string(symbol.declaration_location.column());

            // Color code the type category
            std::string colored_category;
            if (type_category == "Struct" || type_category == "Class")
            {
                colored_category = MAGENTA + type_category + RESET;
            }
            else if (type_category == "Trait")
            {
                colored_category = "\033[38;5;208m" + type_category + RESET; // Orange color
            }
            else if (type_category == "Function")
            {
                colored_category = GREEN + type_category + RESET;
            }
            else if (type_category == "Enum")
            {
                colored_category = YELLOW + type_category + RESET;
            }
            else if (type_category == "Alias")
            {
                colored_category = CYAN + type_category + RESET;
            }
            else
            {
                colored_category = BLUE + type_category + RESET;
            }

            // Check if this type will have detailed information
            bool will_have_details = symbol.type && (symbol.type->kind() == TypeKind::Struct || symbol.type->kind() == TypeKind::Class);

            // Print top border if this type will have detailed information
            if (will_have_details)
            {
                os << BOLD << CYAN << "+---------------+------------------+-------------------------------------+--------+--------+----------+" << RESET << std::endl;
            }

            os << "| " << format_field_colored(colored_category, type_category, 13)
               << " | " << format_field(name, 16)
               << " | " << format_field(type_signature, 35)
               << " | " << format_field(flags, 6)
               << " | " << format_field(size, 6)
               << " | " << format_field(location, 8)
               << " |" << std::endl;

            // Print detailed information for complex types
            print_type_details(os, symbol);
        }

        // Print symbols from parent scopes
        if (_parent_scope)
        {
            _parent_scope->print_type_symbols(os, scope_level + 1);
        }
    }

    std::string TypedSymbolTable::determine_type_category(Type *type) const
    {
        if (!type)
            return "Unknown";

        switch (type->kind())
        {
        case TypeKind::Struct:
            return "Struct";
        case TypeKind::Class:
            return "Class";
        case TypeKind::Trait:
            return "Trait";
        case TypeKind::Function:
            return "Function";
        case TypeKind::Enum:
            return "Enum";
        case TypeKind::TypeAlias:
            return "Alias";
        case TypeKind::Integer:
        case TypeKind::Float:
        case TypeKind::Boolean:
        case TypeKind::Char:
        case TypeKind::String:
        case TypeKind::Void:
            return "Primitive";
        case TypeKind::Array:
            return "Array";
        case TypeKind::Pointer:
            return "Pointer";
        case TypeKind::Reference:
            return "Reference";
        case TypeKind::Interface:
            return "Interface";
        case TypeKind::Union:
            return "Union";
        case TypeKind::Generic:
            return "Generic";
        case TypeKind::Optional:
            return "Optional";
        case TypeKind::Tuple:
            return "Tuple";
        case TypeKind::Auto:
            return "Auto";
        default:
            return "Other";
        }
    }

    std::string TypedSymbolTable::determine_flags(const TypedSymbol &symbol) const
    {
        std::string flags;

        // For now, we'll add basic flags based on what we know
        if (!symbol.is_mutable)
        {
            flags += "Im"; // Immutable
        }

        // TODO: Add more flags based on visibility, static nature, etc.
        // This would need to be extended when we have more metadata

        return flags.empty() ? "N/A" : flags;
    }

    std::string TypedSymbolTable::format_type_size(Type *type) const
    {
        if (!type)
            return "N/A";

        size_t size = type->size_bytes();
        if (size == 0)
            return "0";

        // Format size with unit
        if (size < 1024)
            return std::to_string(size) + "B";
        else if (size < 1024 * 1024)
            return std::to_string(size / 1024) + "KB";
        else
            return std::to_string(size / (1024 * 1024)) + "MB";
    }

    void TypedSymbolTable::print_type_details(std::ostream &os, const TypedSymbol &symbol) const
    {
        const std::string RESET = "\033[0m";
        const std::string BOLD = "\033[1m";
        const std::string CYAN = "\033[36m";
        const std::string GREEN = "\033[32m";
        const std::string YELLOW = "\033[33m";

        // For complex types like structs and classes, print their details if available
        if (symbol.type && (symbol.type->kind() == TypeKind::Struct || symbol.type->kind() == TypeKind::Class))
        {
            // Properties section - spans across all columns (no right border)
            os << "|   " << YELLOW << "Properties:" << RESET << std::endl;

            // Helper function to get visibility string
            auto get_visibility_string = [](Visibility vis) -> std::string
            {
                switch (vis)
                {
                case Visibility::Public:
                    return "public";
                case Visibility::Private:
                    return "private";
                case Visibility::Protected:
                    return "protected";
                default:
                    return "public";
                }
            };

            // Print struct fields
            bool has_properties = false;
            if (symbol.struct_node)
            {
                const auto &fields = symbol.struct_node->fields();
                if (!fields.empty())
                {
                    has_properties = true;
                    for (const auto &field : fields)
                    {
                        if (field)
                        {
                            std::string field_name = field->name();
                            std::string field_type = field->type_annotation();
                            std::string visibility = get_visibility_string(field->visibility());

                            // For structs, all properties are public by default
                            std::string property_line = "        " + visibility + " " + field_name + ": " + field_type;
                            os << "|" << property_line << std::endl;
                        }
                    }
                }
            }

            // Print class fields
            if (symbol.class_node)
            {
                const auto &fields = symbol.class_node->fields();
                if (!fields.empty())
                {
                    has_properties = true;
                    for (const auto &field : fields)
                    {
                        if (field)
                        {
                            std::string field_name = field->name();
                            std::string field_type = field->type_annotation();
                            std::string visibility = get_visibility_string(field->visibility());

                            std::string property_line = "        " + visibility + " " + field_name + ": " + field_type;
                            os << "|" << property_line << std::endl;
                        }
                    }
                }
            }

            if (!has_properties)
            {
                os << "|        No properties defined" << std::endl;
            }

            // Methods section - no right border
            os << "|   " << YELLOW << "Methods:" << RESET << std::endl;

            // Print struct methods
            bool has_methods = false;
            if (symbol.struct_node)
            {
                const auto &methods = symbol.struct_node->methods();
                if (!methods.empty())
                {
                    has_methods = true;
                    for (const auto &method : methods)
                    {
                        if (method)
                        {
                            std::string method_name = method->name();
                            std::string method_return_type = method->return_type_annotation();
                            std::string visibility = get_visibility_string(method->visibility());

                            // Build parameter signature
                            std::string params = "(";
                            const auto &parameters = method->parameters();
                            for (size_t i = 0; i < parameters.size(); ++i)
                            {
                                if (i > 0)
                                    params += ", ";
                                if (parameters[i])
                                {
                                    std::string param_name = parameters[i]->name();
                                    std::string param_type = parameters[i]->type_annotation();
                                    params += param_name + ": " + param_type;
                                }
                            }
                            params += ") -> " + method_return_type;

                            // For structs, all methods are public by default
                            std::string method_line = "        " + visibility + " " + method_name + params;
                            os << "|" << method_line << std::endl;
                        }
                    }
                }
            }

            // Print class methods
            if (symbol.class_node)
            {
                const auto &methods = symbol.class_node->methods();
                if (!methods.empty())
                {
                    has_methods = true;
                    for (const auto &method : methods)
                    {
                        if (method)
                        {
                            std::string method_name = method->name();
                            std::string method_return_type = method->return_type_annotation();
                            std::string visibility = get_visibility_string(method->visibility());

                            // Build parameter signature
                            std::string params = "(";
                            const auto &parameters = method->parameters();
                            for (size_t i = 0; i < parameters.size(); ++i)
                            {
                                if (i > 0)
                                    params += ", ";
                                if (parameters[i])
                                {
                                    std::string param_name = parameters[i]->name();
                                    std::string param_type = parameters[i]->type_annotation();
                                    params += param_name + ": " + param_type;
                                }
                            }
                            params += ") -> " + method_return_type;

                            std::string method_line = "        " + visibility + " " + method_name + params;
                            os << "|" << method_line << std::endl;
                        }
                    }
                }
            }

            if (!has_methods)
            {
                os << "|        No methods defined" << std::endl;
            }

            // Bottom border for this type's details with 6 columns
            os << BOLD << CYAN << "+---------------+------------------+-------------------------------------+--------+--------+----------+" << RESET << std::endl;
        }
    }

    std::string TypedSymbolTable::format_field(const std::string &text, int width) const
    {
        if (text.length() >= width)
        {
            return text.substr(0, width);
        }
        else
        {
            std::string result = text;
            result.resize(width, ' ');
            return result;
        }
    }

    std::string TypedSymbolTable::format_field_colored(const std::string &colored_text, const std::string &plain_text, int width) const
    {
        // Calculate padding based on plain text length, but return colored text
        int padding = width - plain_text.length();
        if (padding <= 0)
        {
            return colored_text.substr(0, width);
        }
        else
        {
            return colored_text + std::string(padding, ' ');
        }
    }

    //===----------------------------------------------------------------------===//
    // TypeChecker Implementation
    //===----------------------------------------------------------------------===//

    TypeChecker::TypeChecker(TypeContext &type_ctx)
        : _type_context(type_ctx), _diagnostic_manager(nullptr),
          _builtin_symbols_loaded(false), _intrinsic_symbols_loaded(false),
          _user_symbols_loaded(false), _runtime_symbols_loaded(false),
          _stdlib_compilation_mode(false)
    {
        _symbol_table = std::make_unique<TypedSymbolTable>();
        _type_registry = std::make_unique<TypeRegistry>(&type_ctx);

        // Connect TypeRegistry to TypeContext for generic instantiation
        type_ctx.set_type_registry(_type_registry.get());

        // Generic types will be discovered dynamically from standard library parsing
        // No hardcoded type registrations here
    }

    TypeChecker::TypeChecker(TypeContext &type_ctx, DiagnosticManager *diagnostic_manager, const std::string &source_file)
        : _type_context(type_ctx), _diagnostic_manager(diagnostic_manager), _source_file(source_file),
          _builtin_symbols_loaded(false), _intrinsic_symbols_loaded(false),
          _user_symbols_loaded(false), _runtime_symbols_loaded(false),
          _stdlib_compilation_mode(false)
    {
        _symbol_table = std::make_unique<TypedSymbolTable>();
        _type_registry = std::make_unique<TypeRegistry>(&type_ctx);

        // Connect TypeRegistry to TypeContext for generic instantiation
        type_ctx.set_type_registry(_type_registry.get());

        // Initialize diagnostic builder if we have a diagnostic manager
        if (_diagnostic_manager)
        {
            _diagnostic_builder = std::make_unique<TypeCheckerDiagnosticBuilder>(_diagnostic_manager, _source_file);
        }

        // Generic types will be discovered dynamically from standard library parsing
        // No hardcoded type registrations here
    }

    void TypeChecker::set_source_file(const std::string &source_file)
    {
        _source_file = source_file;

        // Recreate diagnostic builder with new source file
        if (_diagnostic_manager)
        {
            _diagnostic_builder = std::make_unique<TypeCheckerDiagnosticBuilder>(_diagnostic_manager, _source_file);
        }
    }

    //===----------------------------------------------------------------------===//
    // Generic Context Management
    //===----------------------------------------------------------------------===//

    void TypeChecker::enter_generic_context(const std::string &type_name,
                                            const std::vector<std::string> &parameters,
                                            SourceLocation location)
    {
        std::string params_str;
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            params_str += parameters[i];
            if (i < parameters.size() - 1)
                params_str += ", ";
        }
        LOG_DEBUG(Cryo::LogComponent::AST, "Entering generic context for '{}' with parameters: {}", type_name, params_str);

        _generic_context_stack.emplace_back(type_name, parameters, location);
    }

    void TypeChecker::exit_generic_context()
    {
        if (!_generic_context_stack.empty())
        {
            const auto &context = _generic_context_stack.back();
            LOG_DEBUG(Cryo::LogComponent::AST, "Exiting generic context for '{}'", context.type_name);
            _generic_context_stack.pop_back();
        }
    }

    bool TypeChecker::is_in_generic_context() const
    {
        return !_generic_context_stack.empty();
    }

    bool TypeChecker::is_generic_parameter(const std::string &name) const
    {
        for (const auto &context : _generic_context_stack)
        {
            if (context.parameters.find(name) != context.parameters.end())
            {
                return true;
            }
        }
        return false;
    }

    std::string TypeChecker::get_current_generic_type() const
    {
        if (_generic_context_stack.empty())
        {
            return "";
        }
        return _generic_context_stack.back().type_name;
    }

    const std::vector<std::string> TypeChecker::get_current_generic_parameters() const
    {
        if (_generic_context_stack.empty())
        {
            return {};
        }

        const auto &context = _generic_context_stack.back();
        return std::vector<std::string>(context.parameters.begin(), context.parameters.end());
    }

    std::vector<std::string> TypeChecker::parse_template_arguments(const std::string &args_str)
    {
        std::vector<std::string> result;
        if (args_str.empty())
        {
            return result;
        }

        std::string current_arg;
        int bracket_depth = 0;
        int paren_depth = 0; // Track parentheses depth for tuple types

        for (size_t i = 0; i < args_str.length(); ++i)
        {
            char c = args_str[i];

            if (c == '<')
            {
                bracket_depth++;
                current_arg += c;
            }
            else if (c == '>')
            {
                bracket_depth--;
                current_arg += c;
            }
            else if (c == '(')
            {
                paren_depth++;
                current_arg += c;
            }
            else if (c == ')')
            {
                paren_depth--;
                current_arg += c;
            }
            else if (c == ',' && bracket_depth == 0 && paren_depth == 0)
            {
                // Found a top-level comma - this separates arguments
                std::string trimmed_arg = current_arg;
                // Trim whitespace
                size_t start = trimmed_arg.find_first_not_of(" \t");
                size_t end = trimmed_arg.find_last_not_of(" \t");
                if (start != std::string::npos && end != std::string::npos)
                {
                    trimmed_arg = trimmed_arg.substr(start, end - start + 1);
                }
                else if (start != std::string::npos)
                {
                    trimmed_arg = trimmed_arg.substr(start);
                }

                if (!trimmed_arg.empty())
                {
                    result.push_back(trimmed_arg);
                }
                current_arg.clear();
            }
            else
            {
                current_arg += c;
            }
        }

        // Add the last argument
        std::string trimmed_arg = current_arg;
        // Trim whitespace
        size_t start = trimmed_arg.find_first_not_of(" \t");
        size_t end = trimmed_arg.find_last_not_of(" \t");
        if (start != std::string::npos && end != std::string::npos)
        {
            trimmed_arg = trimmed_arg.substr(start, end - start + 1);
        }
        else if (start != std::string::npos)
        {
            trimmed_arg = trimmed_arg.substr(start);
        }

        if (!trimmed_arg.empty())
        {
            result.push_back(trimmed_arg);
        }

        return result;
    }

    Type *TypeChecker::resolve_type_with_generic_context(const std::string &type_string)
    {
        LOG_DEBUG(Cryo::LogComponent::AST, "resolve_type_with_generic_context: '{}'", type_string);

        // First check if this is a generic parameter
        if (is_generic_parameter(type_string))
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "'{}' is a generic parameter, creating generic type", type_string);
            return _type_context.get_generic_type(type_string);
        }

        // Check for tuple types first (before checking for generic syntax)
        if (type_string.front() == '(' && type_string.back() == ')')
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "'{}' is a tuple type, creating proper TupleType", type_string);

            // Parse tuple element types from "(type1, type2, type3)" syntax
            std::string inner = type_string.substr(1, type_string.length() - 2); // Remove parentheses

            std::vector<Type *> element_types;
            if (!inner.empty())
            {
                // Split by comma and recursively resolve each element type
                std::vector<std::string> element_strings;
                size_t start = 0;
                int paren_depth = 0;

                for (size_t i = 0; i < inner.length(); ++i)
                {
                    if (inner[i] == '(')
                        paren_depth++;
                    else if (inner[i] == ')')
                        paren_depth--;
                    else if (inner[i] == ',' && paren_depth == 0)
                    {
                        element_strings.push_back(inner.substr(start, i - start));
                        start = i + 1;
                        // Skip whitespace
                        while (start < inner.length() && std::isspace(inner[start]))
                            start++;
                    }
                }
                // Add the last element
                if (start < inner.length())
                {
                    element_strings.push_back(inner.substr(start));
                }

                // Resolve each element type
                for (const std::string &element_str : element_strings)
                {
                    std::string trimmed = element_str;
                    // Trim whitespace
                    trimmed.erase(0, trimmed.find_first_not_of(" \t"));
                    trimmed.erase(trimmed.find_last_not_of(" \t") + 1);

                    Type *element_type = resolve_type_with_generic_context(trimmed);
                    if (element_type)
                    {
                        element_types.push_back(element_type);
                    }
                    else
                    {
                        LOG_ERROR(Cryo::LogComponent::AST, "Failed to resolve tuple element type: {}", trimmed);
                        return nullptr;
                    }
                }
            }

            return _type_context.create_tuple_type(element_types);
        }

        // Check for reference types (&Type<T>) - strip the & for template lookup
        std::string clean_type_string = type_string;
        bool is_reference = false;
        if (type_string.front() == '&')
        {
            clean_type_string = type_string.substr(1);
            is_reference = true;
            LOG_DEBUG(Cryo::LogComponent::AST, "Stripped reference prefix, checking template for: '{}'", clean_type_string);
        }

        // Check for parameterized types like ptr<T>, Option<T>, Iterator<T>
        if (clean_type_string.find('<') != std::string::npos)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Found generic type syntax in '{}'", clean_type_string);

            // Parse the base type and type arguments
            size_t open_bracket = clean_type_string.find('<');
            size_t close_bracket = clean_type_string.find('>', open_bracket);

            if (close_bracket != std::string::npos)
            {
                std::string base_type = clean_type_string.substr(0, open_bracket);
                std::string type_args_str = clean_type_string.substr(open_bracket + 1,
                                                                     close_bracket - open_bracket - 1);

                // Trim whitespace from base_type and type_args_str
                base_type.erase(0, base_type.find_first_not_of(" \t"));
                base_type.erase(base_type.find_last_not_of(" \t") + 1);
                type_args_str.erase(0, type_args_str.find_first_not_of(" \t"));
                type_args_str.erase(type_args_str.find_last_not_of(" \t") + 1);

                LOG_DEBUG(Cryo::LogComponent::AST, "base_type='{}', type_args='{}'", base_type, type_args_str);

                // Check if base_type is a type alias first
                LOG_DEBUG(Cryo::LogComponent::AST, "Looking for type alias '{}' in symbol table...", base_type);
                TypedSymbol *alias_symbol = _symbol_table->lookup_symbol(base_type);
                LOG_DEBUG(Cryo::LogComponent::AST, "Symbol lookup result: {}", (alias_symbol ? "found" : "not found"));
                if (alias_symbol && alias_symbol->type)
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "Symbol type kind: {}", static_cast<int>(alias_symbol->type->kind()));
                }
                if (alias_symbol && alias_symbol->type && alias_symbol->type->kind() == TypeKind::TypeAlias)
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "Found type alias '{}', expanding...", base_type);
                    // For type aliases like AllocResult<T> = Result<T*, AllocError>
                    // We need to expand AllocResult<void> to Result<void*, AllocError>

                    // Get the target type string from the alias
                    if (auto alias_type = dynamic_cast<TypeAlias *>(alias_symbol->type))
                    {
                        std::string target_type_str = alias_type->target_type()->to_string();
                        LOG_DEBUG(Cryo::LogComponent::AST, "Alias target type: '{}'", target_type_str);

                        // For now, expand manually for AllocResult
                        if (base_type == "AllocResult" && type_args_str == "void")
                        {
                            std::string expanded_type = "Result<void*, AllocError>";
                            LOG_DEBUG(Cryo::LogComponent::AST, "Expanding {} to {}", clean_type_string, expanded_type);
                            return resolve_type_with_generic_context(expanded_type);
                        }
                    }
                }

                // Check if any type arguments are generic parameters
                bool has_generic_args = is_generic_parameter(type_args_str);

                LOG_DEBUG(Cryo::LogComponent::AST, "has_generic_args={}, is_in_generic_context={}", has_generic_args, is_in_generic_context());

                if (has_generic_args)
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "'{}' contains generic parameters, deferring resolution", type_string);
                    // For template definitions with generic parameters, we don't create concrete types yet
                    return _type_context.get_generic_type(type_string);
                }
                else if (!is_in_generic_context())
                {
                    // Only track concrete instantiations when we're NOT defining a generic type
                    // This prevents tracking during template definition parsing
                    std::vector<std::string> concrete_types = parse_template_arguments(type_args_str);

                    LOG_DEBUG(Cryo::LogComponent::AST, "Tracking concrete instantiation: {} (base: {}, args: {})", clean_type_string, base_type, type_args_str);

                    std::string types_str;
                    for (size_t i = 0; i < concrete_types.size(); ++i)
                    {
                        types_str += "'" + concrete_types[i] + "'";
                        if (i < concrete_types.size() - 1)
                            types_str += " ";
                    }
                    LOG_DEBUG(Cryo::LogComponent::AST, "Parsed {} concrete types: {}", concrete_types.size(), types_str);

                    // Track the base template (without &), but record the clean type name for instantiation
                    track_instantiation(base_type, concrete_types, clean_type_string, SourceLocation{});

                    // For valid generic instantiations, return the base type as a placeholder
                    // The monomorphization pass will later create the concrete types
                    if (alias_symbol && alias_symbol->type)
                    {
                        LOG_DEBUG(Cryo::LogComponent::AST, "Returning base generic type '{}' for instantiation '{}'", base_type, clean_type_string);
                        return alias_symbol->type; // Return the base generic type
                    }
                }
            }
        }

        // Check for array types (type[]) - handle this before falling back to TypeContext
        if (type_string.length() > 2 && type_string.substr(type_string.length() - 2) == "[]")
        {
            std::string element_type_str = type_string.substr(0, type_string.length() - 2);
            LOG_DEBUG(Cryo::LogComponent::AST, "Found array type '{}', resolving element type '{}'", type_string, element_type_str);

            // Recursively resolve the element type with generic context
            Type *element_type = resolve_type_with_generic_context(element_type_str);
            if (element_type)
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "Successfully resolved element type '{}' as {}", element_type_str, TypeKindToString(element_type->kind()));
                return _type_context.create_array_type(element_type);
            }
            else
            {
                LOG_ERROR(Cryo::LogComponent::AST, "Failed to resolve array element type '{}'", element_type_str);
                return nullptr;
            }
        }

        // Fall back to direct type lookups without string parsing
        LOG_DEBUG(Cryo::LogComponent::AST, "Attempting direct type lookup for '{}'", type_string);

        // For simple identifiers that are NOT primitive types, check the symbol table first for correct type
        // This ensures that the correct type (struct vs class) is returned based on the actual declaration
        if (type_string.find('*') == std::string::npos &&
            type_string.find('<') == std::string::npos &&
            type_string.find('[') == std::string::npos &&
            type_string.find('&') == std::string::npos &&
            type_string.find('(') == std::string::npos &&
            // Skip primitive types - let token parsing handle them
            type_string != "void" && type_string != "boolean" && type_string != "char" &&
            type_string != "string" && type_string != "auto" && type_string != "..." &&
            type_string != "i8" && type_string != "i16" && type_string != "i32" && type_string != "i64" &&
            type_string != "u8" && type_string != "u16" && type_string != "u32" && type_string != "u64" &&
            type_string != "int" && type_string != "float" && type_string != "double" &&
            type_string != "f32" && type_string != "f64")
        {
            // Check the symbol table FIRST to get the correct declared type (enum, struct, or class)
            TypedSymbol *type_symbol = _symbol_table->lookup_symbol(type_string);
            LOG_DEBUG(Cryo::LogComponent::AST, "lookup_symbol for '{}' returned: {}", type_string, (type_symbol ? "valid pointer" : "nullptr"));
            if (type_symbol && type_symbol->type)
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "Found declared type '{}', kind={}", type_symbol->type->name(), static_cast<int>(type_symbol->type->kind()));
                // Return the correct type as declared (enum, struct, or class)
                return type_symbol->type;
            }

            // Fallback: Check for existing struct types if not found in symbol table
            Type *struct_type = _type_context.lookup_struct_type(type_string);
            if (struct_type)
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "Found existing struct type '{}', kind={}", struct_type->name(), static_cast<int>(struct_type->kind()));
                return struct_type;
            }

            // Fallback: Check for class types if not found elsewhere
            Type *class_type = _type_context.get_class_type(type_string);
            LOG_DEBUG(Cryo::LogComponent::AST, "get_class_type returned: {}", (class_type ? "valid pointer" : "nullptr"));
            if (class_type)
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "class_type name='{}', kind={}", class_type->name(), static_cast<int>(class_type->kind()));
                return class_type;
            }
        }

        // IMPORTANT: Try token-based parsing for complex types (pointers, etc.)
        // This ensures pointer syntax like "HeapBlock*" gets parsed correctly
        LOG_DEBUG(Cryo::LogComponent::AST, "Trying TypeContext token-based parsing...");
        Lexer type_lexer(type_string);
        Type *parsed_type = _type_context.parse_type_from_tokens(type_lexer);
        LOG_DEBUG(Cryo::LogComponent::AST, "Token parsing result for '{}': {} (kind={})",
                  type_string,
                  parsed_type ? parsed_type->name() : "null",
                  parsed_type ? static_cast<int>(parsed_type->kind()) : -1);

        if (parsed_type && parsed_type->kind() != TypeKind::Unknown)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Successfully parsed '{}' via TypeContext (token-based parsing)", type_string);
            return parsed_type;
        }

        // Try primitive types
        if (type_string == "void")
            return _type_context.get_void_type();
        if (type_string == "boolean")
            return _type_context.get_boolean_type();
        if (type_string == "char")
            return _type_context.get_char_type();
        if (type_string == "string")
            return _type_context.get_string_type();
        if (type_string == "auto")
            return _type_context.get_auto_type();

        // Integer types
        if (type_string == "i8")
            return _type_context.get_i8_type();
        if (type_string == "i16")
            return _type_context.get_i16_type();
        if (type_string == "i32")
            return _type_context.get_i32_type();
        if (type_string == "i64")
            return _type_context.get_i64_type();
        if (type_string == "int")
            return _type_context.get_int_type();
        if (type_string == "u8")
            return _type_context.get_u8_type();
        if (type_string == "u16")
            return _type_context.get_u16_type();
        if (type_string == "u32")
            return _type_context.get_u32_type();
        if (type_string == "u64")
            return _type_context.get_u64_type();

        // Float types
        if (type_string == "f32")
            return _type_context.get_f32_type();
        if (type_string == "f64")
            return _type_context.get_f64_type();
        if (type_string == "float")
            return _type_context.get_default_float_type();

        // Try user-defined types ONLY for simple type names (no special syntax)
        if (type_string.find('*') == std::string::npos &&
            type_string.find('<') == std::string::npos &&
            type_string.find('[') == std::string::npos &&
            type_string.find('&') == std::string::npos)
        {
            // Class types were already checked above, so check structs now
            Type *struct_type = _type_context.get_struct_type(type_string);
            if (struct_type)
                return struct_type;

            Type *trait_type = _type_context.get_trait_type(type_string);
            if (trait_type)
                return trait_type;

            Type *enum_type = _type_context.lookup_enum_type(type_string);
            if (enum_type)
                return enum_type;

            Type *generic_type = _type_context.get_generic_type(type_string);
            if (generic_type)
                return generic_type;
        }

        LOG_ERROR(Cryo::LogComponent::AST, "Failed to resolve type '{}' - type not found", type_string);
        return nullptr;
    }

    //===----------------------------------------------------------------------===//
    // Token-based Type Resolution (Preferred)
    //===----------------------------------------------------------------------===//

    Type *TypeChecker::resolve_type_from_tokens(Lexer &lexer)
    {
        LOG_DEBUG(Cryo::LogComponent::AST, "Using token-based type resolution");

        // Delegate to TypeContext's token-based parsing
        Type *parsed_type = _type_context.parse_type_from_tokens(lexer);
        if (parsed_type)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Successfully resolved type via tokens");
            return parsed_type;
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "Token-based parsing failed");
        return nullptr;
    }

    Type *TypeChecker::resolve_type_from_token_stream(const std::vector<Token> &tokens, size_t &index)
    {
        LOG_DEBUG(Cryo::LogComponent::AST, "Using token stream type resolution");

        // Delegate to TypeContext's token stream parsing
        Type *parsed_type = _type_context.parse_type_from_token_stream(tokens, index);
        if (parsed_type)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Successfully resolved type via token stream");
            return parsed_type;
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "Token stream parsing failed");
        return nullptr;
    }

    //===----------------------------------------------------------------------===//
    // Generic Instantiation Tracking
    //===----------------------------------------------------------------------===//

    void TypeChecker::track_instantiation(const std::string &base_name,
                                          const std::vector<std::string> &concrete_types,
                                          const std::string &instantiated_name,
                                          SourceLocation location)
    {
        // Check if we already have this instantiation
        for (const auto &inst : _required_instantiations)
        {
            if (inst.instantiated_name == instantiated_name)
            {
                return; // Already tracked
            }
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "Tracking instantiation '{}' (base: {})", instantiated_name, base_name);

        _required_instantiations.emplace_back(base_name, concrete_types, instantiated_name, location);
    }

    const std::vector<GenericInstantiation> &TypeChecker::get_required_instantiations() const
    {
        return _required_instantiations;
    }

    void TypeChecker::load_builtin_symbols(const SymbolTable &main_symbol_table)
    {
        // Check if already loaded to prevent duplicate loading
        if (_builtin_symbols_loaded)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Builtin symbols already loaded, skipping");
            return;
        }

        // Store reference to main symbol table for scope lookups
        _main_symbol_table = &main_symbol_table;

        LOG_DEBUG(Cryo::LogComponent::AST, "Loading builtin symbols into TypeChecker...");
        LOG_DEBUG(Cryo::LogComponent::AST, "Main symbol table has {} symbols", main_symbol_table.get_symbols().size());

        // Copy all built-in function symbols from main symbol table
        int copied_count = 0;
        int skipped_count = 0;
        for (const auto &[name, symbol] : main_symbol_table.get_symbols())
        {
            if ((symbol.kind == SymbolKind::Function || symbol.kind == SymbolKind::Intrinsic) && symbol.data_type != nullptr)
            {
                // Check if symbol already exists to avoid redefinition errors
                if (_symbol_table->lookup_symbol(name) != nullptr)
                {
                    skipped_count++;
                    continue;
                }

                if (_symbol_table->declare_symbol(name, symbol.data_type, symbol.declaration_location))
                {
                    copied_count++;
                }
            }
        }
        LOG_DEBUG(Cryo::LogComponent::AST, "Copied {} builtin functions, skipped {} existing symbols to TypeChecker symbol table", copied_count, skipped_count);

        _builtin_symbols_loaded = true;
        LOG_DEBUG(Cryo::LogComponent::AST, "Builtin symbols loading completed");
    }

    void TypeChecker::load_intrinsic_symbols(const SymbolTable &main_symbol_table)
    {
        // Check if already loaded to prevent duplicate loading
        if (_intrinsic_symbols_loaded)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Intrinsic symbols already loaded, skipping");
            return;
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "Loading intrinsic symbols into TypeChecker...");

        // Copy only intrinsic symbols from main symbol table
        int copied_count = 0;
        int skipped_count = 0;
        for (const auto &[name, symbol] : main_symbol_table.get_symbols())
        {
            if (symbol.kind == SymbolKind::Intrinsic && symbol.data_type != nullptr)
            {
                // Check if symbol already exists to avoid redefinition errors
                if (_symbol_table->lookup_symbol(name) != nullptr)
                {
                    skipped_count++;
                    continue;
                }

                if (_symbol_table->declare_symbol(name, symbol.data_type, symbol.declaration_location))
                {
                    copied_count++;
                    LOG_DEBUG(Cryo::LogComponent::AST, "Loaded intrinsic: {}", name);
                }
            }
        }
        LOG_DEBUG(Cryo::LogComponent::AST, "Copied {} intrinsic symbols, skipped {} existing symbols to TypeChecker symbol table", copied_count, skipped_count);

        _intrinsic_symbols_loaded = true;
        LOG_DEBUG(Cryo::LogComponent::AST, "Intrinsic symbols loading completed");
    }

    void TypeChecker::load_user_symbols(const SymbolTable &main_symbol_table)
    {
        // Check if already loaded to prevent duplicate loading
        if (_user_symbols_loaded)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "User symbols already loaded, skipping");
            return;
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "Loading user-defined symbols into TypeChecker... [table={}]", static_cast<void *>(_symbol_table.get()));

        // Copy user-defined symbols from main symbol table (functions AND variables)
        int copied_count = 0;
        int skipped_count = 0;
        for (const auto &[name, symbol] : main_symbol_table.get_symbols())
        {
            if ((symbol.kind == SymbolKind::Function || symbol.kind == SymbolKind::Variable) && symbol.data_type != nullptr)
            {
                // Check if symbol already exists to avoid redefinition errors
                if (_symbol_table->lookup_symbol(name) != nullptr)
                {
                    skipped_count++;
                    LOG_DEBUG(Cryo::LogComponent::AST, "Skipped existing symbol: {} [table={}]", name, static_cast<void *>(_symbol_table.get()));
                    continue;
                }

                if (_symbol_table->declare_symbol(name, symbol.data_type, symbol.declaration_location))
                {
                    copied_count++;
                    if (symbol.kind == SymbolKind::Function)
                    {
                        LOG_DEBUG(Cryo::LogComponent::AST, "Loaded user function: {} [table={}]", name, static_cast<void *>(_symbol_table.get()));
                    }
                    else if (symbol.kind == SymbolKind::Variable)
                    {
                        LOG_DEBUG(Cryo::LogComponent::AST, "Loaded user variable: {} [table={}]", name, static_cast<void *>(_symbol_table.get()));
                    }
                }
                else
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "Failed to declare symbol: {} (already exists) [table={}]", name, static_cast<void *>(_symbol_table.get()));
                }
            }
        }
        LOG_DEBUG(Cryo::LogComponent::AST, "Copied {} user symbols, skipped {} existing symbols to TypeChecker symbol table [table={}]", copied_count, skipped_count, static_cast<void *>(_symbol_table.get()));

        _user_symbols_loaded = true;
        LOG_DEBUG(Cryo::LogComponent::AST, "User symbols loading completed");
    }

    void TypeChecker::load_runtime_symbols(const SymbolTable &main_symbol_table)
    {
        // Check if already loaded to prevent duplicate loading
        if (_runtime_symbols_loaded)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Runtime symbols already loaded, skipping");
            return;
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "Loading runtime symbols into TypeChecker...");

        // Debug: Show all symbols in the main symbol table
        LOG_DEBUG(Cryo::LogComponent::AST, "Main symbol table contains {} symbols:", main_symbol_table.get_symbols().size());
        for (const auto &[name, symbol] : main_symbol_table.get_symbols())
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "  - {} (kind: {})", name, static_cast<int>(symbol.kind));
        }

        // Access runtime symbols from the std::Runtime namespace
        int copied_count = 0;
        const auto &namespaces = main_symbol_table.get_namespaces();
        auto runtime_ns_it = namespaces.find("std::Runtime");

        if (runtime_ns_it != namespaces.end())
        {
            const auto &runtime_symbols = runtime_ns_it->second;
            LOG_DEBUG(Cryo::LogComponent::AST, "Found std::Runtime namespace with {} symbols", runtime_symbols.size());

            // Only load symbols that have valid type information to avoid crashes
            for (const auto &[name, symbol] : runtime_symbols)
            {
                if (symbol.kind == SymbolKind::Function && symbol.data_type != nullptr)
                {
                    // Register the function globally without namespace qualification
                    _symbol_table->declare_symbol(name, symbol.data_type, symbol.declaration_location);
                    copied_count++;
                    LOG_DEBUG(Cryo::LogComponent::AST, "Loaded runtime function globally: {}", name);
                }
                else if (symbol.kind == SymbolKind::Function)
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "Skipping runtime function '{}' - no type information (will be resolved via namespace during function calls)", name);
                }
            }
        }
        else
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "std::Runtime namespace not found");
            LOG_DEBUG(Cryo::LogComponent::AST, "Available namespaces:");
            for (const auto &[ns_name, symbols] : namespaces)
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "  - {} ({} symbols)", ns_name, symbols.size());
            }
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "Copied {} runtime symbols to TypeChecker symbol table", copied_count);

        // Note: Runtime functions without type information will be resolved via namespace lookup
        // during function call analysis. This is actually the preferred approach since it allows
        // the type system to resolve the correct function signature at call sites.
        LOG_DEBUG(Cryo::LogComponent::AST, "NOTE: Runtime functions will be resolved via std::Runtime namespace during function calls");

        _runtime_symbols_loaded = true;
        LOG_DEBUG(Cryo::LogComponent::AST, "Runtime symbols loading completed");
    }

    void TypeChecker::register_generic_type(const std::string &base_name, const std::vector<std::string> &param_names)
    {
        _type_registry->register_template(base_name, param_names);
    }

    ParameterizedType *TypeChecker::resolve_generic_type(const std::string &type_string)
    {
        return _type_registry->parse_and_instantiate(type_string);
    }

    void TypeChecker::discover_generic_types_from_ast(ProgramNode &program)
    {
        // Traverse the AST to discover generic type definitions
        for (const auto &stmt : program.statements())
        {
            if (!stmt)
                continue;

            // Check for struct declarations with generic parameters
            if (auto struct_decl = dynamic_cast<StructDeclarationNode *>(stmt.get()))
            {
                discover_generic_type_from_struct(*struct_decl);
            }
            // Check for class declarations with generic parameters
            else if (auto class_decl = dynamic_cast<ClassDeclarationNode *>(stmt.get()))
            {
                discover_generic_type_from_class(*class_decl);
            }
        }
    }

    void TypeChecker::discover_generic_type_from_struct(StructDeclarationNode &struct_node)
    {
        if (!struct_node.generic_parameters().empty())
        {
            std::vector<std::string> param_names;
            for (const auto &param : struct_node.generic_parameters())
            {
                if (param)
                {
                    param_names.push_back(param->name());
                }
            }

            if (!param_names.empty())
            {
                register_generic_type(struct_node.name(), param_names);
                LOG_DEBUG(Cryo::LogComponent::TYPECHECKER,
                          "Discovered generic struct '{}' with {} parameter(s)",
                          struct_node.name(), param_names.size());
            }
        }
    }

    void TypeChecker::discover_generic_type_from_class(ClassDeclarationNode &class_node)
    {
        if (!class_node.generic_parameters().empty())
        {
            std::vector<std::string> param_names;
            for (const auto &param : class_node.generic_parameters())
            {
                if (param)
                {
                    param_names.push_back(param->name());
                }
            }

            if (!param_names.empty())
            {
                register_generic_type(class_node.name(), param_names);
                LOG_DEBUG(Cryo::LogComponent::TYPECHECKER,
                          "Discovered generic class '{}' with {} parameter(s)",
                          class_node.name(), param_names.size());
            }
        }
    }

    void TypeChecker::check_program(ProgramNode &program)
    {
        // Clear any previous state
        _errors.clear();
        _warnings.clear();
        _current_function_return_type = nullptr;
        _in_function = false;
        _in_loop = false;

        // First pass: Discover generic types from the AST
        discover_generic_types_from_ast(program);

        // Visit all top-level declarations and statements
        for (const auto &stmt : program.statements())
        {
            if (stmt)
            {
                stmt->accept(*this);
            }
        }
    }

    void TypeChecker::check_imported_modules(const std::unordered_map<std::string, std::unique_ptr<ProgramNode>> &imported_asts)
    {
        LOG_DEBUG(Cryo::LogComponent::AST, "Processing {} imported modules for AST node updates", imported_asts.size());
        auto all_symbols = _symbol_table->get_symbols();

        // Store the original source file to restore later
        std::string original_source_file = _source_file;

        for (const auto &[module_name, ast] : imported_asts)
        {
            if (!ast)
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "Skipping null AST for module: {}", module_name);
                continue;
            }

            LOG_DEBUG(Cryo::LogComponent::AST, "Processing imported module: {}", module_name);
            LOG_DEBUG(Cryo::LogComponent::AST, "Module has {} statements", ast->statements().size());

            // Set the source file context to this imported module for proper error attribution
            // This is particularly important for stdlib modules to avoid attributing errors to user code
            std::string module_file = module_name + ".cryo"; // Simple mapping for now
            if (module_name.find("/") != std::string::npos)
            {
                // For stdlib paths like "core/types", construct the stdlib path
                module_file = "stdlib/" + module_name + ".cryo";
            }
            _source_file = module_file;
            LOG_DEBUG(Cryo::LogComponent::AST, "Set source context to: {}", _source_file);

            // Visit all top-level declarations in the imported module
            // This will update existing symbols with their AST node references
            int stmt_count = 0;
            for (const auto &stmt : ast->statements())
            {
                if (stmt)
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "Processing statement {} in module {}", (++stmt_count), module_name);

                    // Debug: What kind of statement is this?
                    const char *node_type = "unknown";
                    switch (stmt->kind())
                    {
                    case NodeKind::FunctionDeclaration:
                        node_type = "FunctionDeclaration";
                        break;
                    case NodeKind::ImportDeclaration:
                        node_type = "ImportDeclaration";
                        break;
                    case NodeKind::VariableDeclaration:
                        node_type = "VariableDeclaration";
                        break;
                    case NodeKind::StructDeclaration:
                        node_type = "StructDeclaration";
                        break;
                    case NodeKind::EnumDeclaration:
                        node_type = "EnumDeclaration";
                        break;
                    case NodeKind::IntrinsicDeclaration:
                        node_type = "IntrinsicDeclaration";
                        break;
                    case NodeKind::ExternBlock:
                        node_type = "ExternBlock";
                        break;
                    default:
                        node_type = "other";
                        break;
                    }

                    // For function declarations, we need to check with qualified names
                    if (stmt->kind() == NodeKind::FunctionDeclaration)
                    {
                        if (auto func_decl = dynamic_cast<FunctionDeclarationNode *>(stmt.get()))
                        {
                            std::string func_name = func_decl->name();
                            std::string qualified_name = module_name + "::" + func_name;

                            // Try multiple lookup strategies for imported module functions
                            TypedSymbol *existing_symbol = nullptr;

                            // Strategy 1: Look up unqualified name
                            existing_symbol = _symbol_table->lookup_symbol(func_name);
                            if (!existing_symbol)
                            {
                                // Strategy 2: Look up fully qualified name
                                existing_symbol = _symbol_table->lookup_symbol(qualified_name);
                                if (existing_symbol)
                                {
                                    // Strategy 3: Check namespaces
                                    existing_symbol = _symbol_table->lookup_symbol_in_any_namespace(func_name);
                                }
                            }
                            if (existing_symbol)
                            {
                                existing_symbol->function_node = func_decl;
                            }
                        }
                    }
                    else
                    {
                        // For non-function statements, use normal visitor
                        stmt->accept(*this);
                    }
                }
            }
        }

        // Restore the original source file context
        _source_file = original_source_file;
        LOG_DEBUG(Cryo::LogComponent::AST, "Restored source context to: {}", _source_file);
    }

    //===----------------------------------------------------------------------===//
    // Program Visitors
    //===----------------------------------------------------------------------===//

    void TypeChecker::visit(ProgramNode &node)
    {
        // Visit all top-level declarations and statements
        for (const auto &stmt : node.statements())
        {
            if (stmt)
            {
                stmt->accept(*this);
            }
        }
    }

    //===----------------------------------------------------------------------===//
    // Declaration Visitors
    //===----------------------------------------------------------------------===//

    void TypeChecker::visit(VariableDeclarationNode &node)
    {
        const std::string &var_name = node.name();
        Type *declared_type = nullptr;
        Type *inferred_type = nullptr;

        LOG_DEBUG(Cryo::LogComponent::AST, "TypeChecker::visit(VariableDeclarationNode): Processing variable '{}' is_mutable={}, stdlib_compilation_mode={}", var_name, node.is_mutable(), _stdlib_compilation_mode);

        // Parse type annotation from node
        if (!node.type_annotation().empty() && node.type_annotation() != "auto")
        {
            declared_type = resolve_type_with_generic_context(node.type_annotation());

            // Debug: Track malformed pointer types at declaration time
            if (declared_type && node.type_annotation().find("*") != std::string::npos)
            {
                if (declared_type->kind() == TypeKind::Struct)
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "MALFORMED TYPE DETECTED: Variable '{}' with annotation '{}' resolved to struct type '{}' (kind={}) instead of pointer type",
                              var_name, node.type_annotation(), declared_type->name(), static_cast<int>(declared_type->kind()));
                }
                else
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "CORRECT TYPE: Variable '{}' with annotation '{}' resolved to type '{}' (kind={})",
                              var_name, node.type_annotation(), declared_type->name(), static_cast<int>(declared_type->kind()));
                }
            }
        }

        if (node.initializer())
        {
            // Visit the initializer to determine its type
            node.initializer()->accept(*this);
            inferred_type = node.initializer()->get_resolved_type();
        }

        // Determine final type
        Type *final_type = nullptr;
        if (declared_type && inferred_type)
        {
            // Both declared and inferred - check compatibility
            if (!check_assignment_compatibility(declared_type, inferred_type, node.location()))
            {
                // Use the initializer's location for better error highlighting
                SourceLocation error_loc = node.initializer() ? node.initializer()->location() : node.location();
                report_type_mismatch(error_loc, declared_type, inferred_type,
                                     "variable initialization");
                final_type = declared_type; // Use declared type for error recovery
            }
            else
            {
                final_type = declared_type;
            }
        }
        else if (declared_type)
        {
            final_type = declared_type;
        }
        else if (inferred_type)
        {
            final_type = inferred_type;
        }
        else
        {
            if (_diagnostic_builder)
            {
                _diagnostic_builder->create_undefined_symbol_error(var_name, NodeKind::Declaration, node.location());
            }
            else
            {
                Type *expected = _type_context.get_void_type(); // Placeholder for "inferred type"
                Type *found = _type_context.get_unknown_type();
                _diagnostic_builder->create_assignment_type_error(expected, found, node.location());
            }
            final_type = _type_context.get_unknown_type();
        }

        // Declare the variable in symbol table
        LOG_DEBUG(Cryo::LogComponent::AST, "TypeChecker::visit(VariableDeclarationNode): Attempting to declare variable '{}' with type '{}' (kind={})", var_name, final_type ? final_type->name() : "null", final_type ? static_cast<int>(final_type->kind()) : -1);
        if (!declare_variable(var_name, final_type, node.location(), node.is_mutable()))
        {
            // Check if we're in stdlib compilation mode and if this is a compatible redefinition
            bool should_report_error = true;
            if (_stdlib_compilation_mode)
            {
                // In stdlib compilation mode, check if this is a compatible redefinition
                TypedSymbol *existing_symbol = _symbol_table->lookup_symbol(var_name);
                if (existing_symbol && existing_symbol->type && final_type)
                {
                    // Allow redefinition if the types are the same
                    bool types_match = (existing_symbol->type == final_type) ||
                                       (existing_symbol->type->name() == final_type->name());

                    // In stdlib compilation mode, be more lenient with mutability:
                    // Allow if current declaration is mutable (which is the intended state)
                    // or if both have the same mutability
                    bool mutability_compatible = (existing_symbol->is_mutable == node.is_mutable()) ||
                                                 (node.is_mutable() && !existing_symbol->is_mutable);

                    if (types_match && mutability_compatible)
                    {
                        should_report_error = false;
                        LOG_DEBUG(Cryo::LogComponent::AST, "TypeChecker::visit(VariableDeclarationNode): Allowing compatible redefinition of variable '{}' (type: {}, mutable: {}) in stdlib compilation mode",
                                  var_name, final_type->name(), node.is_mutable());

                        // Update the existing symbol with the correct mutability if needed
                        if (!existing_symbol->is_mutable && node.is_mutable())
                        {
                            existing_symbol->is_mutable = true;
                            LOG_DEBUG(Cryo::LogComponent::AST, "TypeChecker::visit(VariableDeclarationNode): Updated mutability for variable '{}' to mutable in stdlib compilation mode", var_name);
                        }
                    }
                    else
                    {
                        LOG_DEBUG(Cryo::LogComponent::AST, "TypeChecker::visit(VariableDeclarationNode): Incompatible redefinition of variable '{}' - existing: {} ({}), new: {} ({}), existing_mutable: {}, new_mutable: {}",
                                  var_name, existing_symbol->type->name(), static_cast<void *>(existing_symbol->type),
                                  final_type->name(), static_cast<void *>(final_type), existing_symbol->is_mutable, node.is_mutable());
                    }
                }
            }

            if (should_report_error)
            {
                LOG_ERROR(Cryo::LogComponent::AST, "TypeChecker::visit(VariableDeclarationNode): Failed to declare variable '{}' - already exists! stdlib_compilation_mode={}", var_name, _stdlib_compilation_mode);
                _diagnostic_builder->create_redefined_symbol_error(var_name, NodeKind::VariableDeclaration, node.location());
            }
        }
        else
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "TypeChecker::visit(VariableDeclarationNode): Successfully declared variable '{}'", var_name);
        }

        // If we're in a struct/class context and NOT in a function, register this as a field
        // This is the proper place for struct field registration when fields are incorrectly
        // processed as VariableDeclarationNode instead of StructFieldNode
        if (!_current_struct_name.empty() && !_in_function && final_type && final_type->kind() != TypeKind::Unknown)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Registering variable '{}' as struct field in '{}' (type: {})",
                      var_name, _current_struct_name, final_type->name());

            _struct_fields[_current_struct_name][var_name] = final_type;
            LOG_DEBUG(Cryo::LogComponent::AST, "Registered struct field '{}' of type '{}' in struct/class '{}'",
                      var_name, final_type->name(), _current_struct_name);
        }
    }

    void TypeChecker::visit(FunctionDeclarationNode &node)
    {
        const std::string &func_name = node.name();
        LOG_DEBUG(Cryo::LogComponent::AST, "TypeChecker::visit(FunctionDeclarationNode) - Processing function '{}' in struct context '{}'", 
                 func_name, _current_struct_name);

        // Handle generic functions - enter generic context if needed
        bool is_generic_function = !node.generic_parameters().empty();
        if (is_generic_function)
        {
            std::vector<std::string> generic_param_names;
            for (const auto &generic_param : node.generic_parameters())
            {
                generic_param_names.push_back(generic_param->name());
            }
            enter_generic_context(func_name, generic_param_names, node.location());
        }

        // Parse return type from node annotation
        const std::string &return_type_str = node.return_type_annotation();
        Type *return_type = resolve_type_with_generic_context(return_type_str);

        if (!return_type)
        {
            _diagnostic_builder->create_undefined_symbol_error(return_type_str, NodeKind::Declaration, node.location());
            return_type = _type_context.get_unknown_type();
        }

        // Collect parameter types (exclude variadic parameters from param_types)
        std::vector<Type *> param_types;
        for (const auto &param : node.parameters())
        {
            if (param)
            {
                const std::string &param_type_str = param->type_annotation();
                Type *param_type = resolve_type_with_generic_context(param_type_str);
                if (!param_type)
                {
                    _diagnostic_builder->create_undefined_symbol_error(param_type_str, NodeKind::Declaration, param->location());
                    param_type = _type_context.get_unknown_type();
                }

                // Skip variadic parameters - they're handled by the is_variadic flag
                if (param_type && param_type->kind() != TypeKind::Variadic)
                {
                    param_types.push_back(param_type);
                    LOG_DEBUG(Cryo::LogComponent::AST, "Added parameter type '{}' (kind={})", param_type->name(), static_cast<int>(param_type->kind()));
                }
                else
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "Skipped variadic parameter type '{}' (kind={})", param_type ? param_type->name() : "null", param_type ? static_cast<int>(param_type->kind()) : -1);
                }
            }
        }

        // Create function type - check if this is a variadic function
        bool is_variadic = node.is_variadic();
        LOG_DEBUG(Cryo::LogComponent::AST, "FunctionDeclarationNode '{}' - is_variadic from node: {}, param_count: {}", func_name, is_variadic, param_types.size());
        FunctionType *func_type = static_cast<FunctionType *>(
            _type_context.create_function_type(return_type, param_types, is_variadic));

        LOG_DEBUG(Cryo::LogComponent::AST, "Created function type for '{}' - is_variadic: {}, func_type->is_variadic(): {}", func_name, is_variadic, func_type->is_variadic());

        TypedSymbol *existing_symbol = _symbol_table->lookup_symbol(func_name);
        if (!existing_symbol)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Not found in main symbols, checking namespaces...");
            TypedSymbol *ns_symbol = _symbol_table->lookup_symbol_in_any_namespace(func_name);
            if (ns_symbol)
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "Found '{}' in namespaces!", func_name);
                existing_symbol = ns_symbol; // Use the namespaced symbol
            }
        }
        if (existing_symbol)
        {
            // Function already exists (likely from load_user_symbols), verify type compatibility
            LOG_DEBUG(Cryo::LogComponent::AST, "Function '{}' already exists", func_name);
            LOG_DEBUG(Cryo::LogComponent::AST, "Existing type: {}", (existing_symbol->type ? existing_symbol->type->to_string() : "null"));
            LOG_DEBUG(Cryo::LogComponent::AST, "New type: {}", (func_type ? func_type->to_string() : "null"));

            // Check if the new function type is more specific (e.g., variadic vs non-variadic)
            bool should_update_type = false;
            if (existing_symbol->type && func_type)
            { 
                auto existing_func_type = dynamic_cast<FunctionType *>(existing_symbol->type);
                auto new_func_type = dynamic_cast<FunctionType *>(func_type);

                if (existing_func_type && new_func_type)
                {
                    // If new type is variadic but existing is not, prefer the variadic version
                    if (new_func_type->is_variadic() && !existing_func_type->is_variadic())
                    {
                        should_update_type = true;
                        LOG_DEBUG(Cryo::LogComponent::AST, "Updating '{}' type from non-variadic to variadic", func_name);
                    }
                }
            }

            if (existing_symbol->type != func_type)
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "Types don't match - checking type equality more deeply");
                // Types might be functionally equivalent but different objects
                // For now, allow redefinition if it's the same function
                LOG_DEBUG(Cryo::LogComponent::AST, "Allowing redefinition for now");
            }

            // Update the symbol with new type if needed
            if (should_update_type)
            {
                existing_symbol->type = func_type;
                LOG_DEBUG(Cryo::LogComponent::AST, "Updated function '{}' type to: {}", func_name, func_type->to_string());
            }

            // Function exists with compatible type - update with AST node reference for LSP
            LOG_DEBUG(Cryo::LogComponent::AST, "Updating existing function '{}' with AST node reference", func_name);
            existing_symbol->function_node = &node;
        }
        else
        {
            // Function doesn't exist yet, declare it
            if (!declare_function(func_name, func_type, node.location(), &node))
            {
                // Check if this is a class/struct method that was already registered during signature registration
                bool is_method_already_registered = false;
                LOG_DEBUG(Cryo::LogComponent::AST, "declare_function failed for '{}', current_struct_name='{}', checking method registries...", 
                         func_name, _current_struct_name);
                if (!_current_struct_name.empty())
                {
                    // Check if this method was already registered in the method registries
                    auto public_methods_it = _struct_methods.find(_current_struct_name);
                    auto private_methods_it = _private_struct_methods.find(_current_struct_name);
                    
                    bool found_in_public = (public_methods_it != _struct_methods.end() && 
                                          public_methods_it->second.find(func_name) != public_methods_it->second.end());
                    bool found_in_private = (private_methods_it != _private_struct_methods.end() && 
                                           private_methods_it->second.find(func_name) != private_methods_it->second.end());
                    
                    LOG_DEBUG(Cryo::LogComponent::AST, "Method lookup for '{}': found_in_public={}, found_in_private={}", 
                             func_name, found_in_public, found_in_private);
                    
                    if (found_in_public || found_in_private)
                    {
                        is_method_already_registered = true;
                        LOG_DEBUG(Cryo::LogComponent::AST, "Suppressing redefinition error for method '{}' in '{}' - already registered during signature phase", 
                                 func_name, _current_struct_name);
                    }
                }
                else
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "Not in struct context, current_struct_name is empty for function '{}'", func_name);
                }
                
                if (!is_method_already_registered)
                {
                    // Check if this is a false positive - look up the existing symbol to see what it actually is
                    TypedSymbol* existing_symbol = _symbol_table->lookup_symbol(func_name);
                    if (existing_symbol && existing_symbol->type) {
                        // If the existing symbol is not a function type, this is a false positive
                        // Don't report function redefinition errors for variables, etc.
                        if (existing_symbol->type->kind() != TypeKind::Function) {
                            LOG_DEBUG(Cryo::LogComponent::AST, "Suppressing false positive function redefinition error for variable '{}'", func_name);
                        } else {
                            _diagnostic_builder->create_redefined_symbol_error(func_name, NodeKind::FunctionDeclaration, node.location());
                        }
                    } else {
                        // If we can't determine the existing symbol type, report the error as usual
                        _diagnostic_builder->create_redefined_symbol_error(func_name, NodeKind::FunctionDeclaration, node.location());
                    }
                }
            }
        }

        // Enter function scope for parameter and body checking
        enter_scope();
        _in_function = true;
        _current_function_return_type = return_type;

        // Process trait bounds for generic parameters
        _current_generic_trait_bounds.clear();
        for (const auto &trait_bound : node.trait_bounds())
        {
            _current_generic_trait_bounds[trait_bound.type_parameter].push_back(trait_bound.trait_name);
        }

        // Declare parameters in function scope
        for (const auto &param : node.parameters())
        {
            if (param)
            {
                param->accept(*this);
            }
        }

        // Check function body
        if (node.body())
        {
            node.body()->accept(*this);
        }

        // Exit function scope
        _current_function_return_type = nullptr;
        _in_function = false;
        _current_generic_trait_bounds.clear();
        exit_scope();

        // Exit generic context if it was a generic function
        if (is_generic_function)
        {
            exit_generic_context();
        }
    }

    void TypeChecker::visit(IntrinsicDeclarationNode &node)
    {
        const std::string &func_name = node.name();

        LOG_DEBUG(Cryo::LogComponent::AST, "Type checking intrinsic function: {}", func_name);

        // Create function type from intrinsic declaration
        std::vector<Type *> param_types;
        for (const auto &param : node.parameters())
        {
            if (param)
            {
                // For intrinsic parameters, use type annotation if available
                const std::string &param_type_str = param->type_annotation();
                Type *param_type = lookup_type_by_name(param_type_str);
                if (param_type)
                {
                    param_types.push_back(param_type);
                }
                else
                {
                    _diagnostic_builder->create_undefined_symbol_error(param_type_str, NodeKind::Declaration, param->location());
                    param_types.push_back(_type_context.get_unknown_type());
                }
            }
        }

        // Get return type
        Type *return_type = node.get_resolved_return_type();
        if (!return_type)
        {
            const std::string &return_type_str = node.return_type_annotation();
            return_type = lookup_type_by_name(return_type_str);
            if (!return_type)
            {
                _diagnostic_builder->create_undefined_symbol_error(return_type_str, NodeKind::Declaration, node.location());
                return_type = _type_context.get_unknown_type();
            }
            node.set_resolved_return_type(return_type);
        }

        // Create function type for the intrinsic
        FunctionType *func_type = static_cast<FunctionType *>(_type_context.create_function_type(return_type, param_types));

        // Check if intrinsic already exists (likely loaded via load_intrinsic_symbols)
        TypedSymbol *existing_symbol = _symbol_table->lookup_symbol(func_name);
        if (existing_symbol)
        {
            // Intrinsic already loaded - verify type compatibility and update AST reference
            LOG_DEBUG(Cryo::LogComponent::AST, "Intrinsic '{}' already exists, verifying type compatibility", func_name);
            if (existing_symbol->type && func_type)
            {
                // Types should match, but we can be lenient for intrinsics
                LOG_DEBUG(Cryo::LogComponent::AST, "Existing type: {}", existing_symbol->type->to_string());
                LOG_DEBUG(Cryo::LogComponent::AST, "New type: {}", func_type->to_string());
            }
            LOG_DEBUG(Cryo::LogComponent::AST, "Intrinsic '{}' already registered, skipping duplicate declaration", func_name);
        }
        else
        {
            // Intrinsic not loaded yet - declare it now
            if (!_symbol_table->declare_symbol(func_name, func_type, node.location()))
            {
                _diagnostic_builder->create_redefined_symbol_error(func_name, NodeKind::FunctionDeclaration, node.location());
            }
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "Registered intrinsic function: {} with type: {}", func_name, func_type->to_string());
    }

    //===----------------------------------------------------------------------===//
    // Statement Visitors
    //===----------------------------------------------------------------------===//

    void TypeChecker::visit(BlockStatementNode &node)
    {
        // Always create a new scope for block statements to ensure proper variable scoping
        // This is essential for detecting out-of-scope variable references
        enter_scope();

        for (const auto &stmt : node.statements())
        {
            if (stmt)
            {
                stmt->accept(*this);
            }
        }

        exit_scope();
    }

    void TypeChecker::visit(ReturnStatementNode &node)
    {
        if (!_in_function)
        {
            _diagnostic_builder->create_invalid_operation_error("return", nullptr, nullptr, node.location());
            return;
        }

        if (node.expression())
        {
            // Check return expression type
            node.expression()->accept(*this);

            if (node.expression()->has_resolved_type())
            {
                Type *expr_type = node.expression()->get_resolved_type();

                if (_current_function_return_type &&
                    !check_assignment_compatibility(_current_function_return_type, expr_type, node.location()))
                {
                    report_type_mismatch(node.location(), _current_function_return_type, expr_type,
                                         "return statement");
                }
            }
        }
        else
        {
            // Void return - check if function expects void
            if (_current_function_return_type && !_current_function_return_type->is_void())
            {
                Type *void_type = _type_context.get_void_type();
                _diagnostic_builder->create_assignment_type_error(_current_function_return_type, void_type, node.location());
            }
        }
    }

    void TypeChecker::visit(IfStatementNode &node)
    {
        // Check condition type
        if (node.condition())
        {
            node.condition()->accept(*this);

            if (node.condition()->has_resolved_type())
            {
                Type *cond_type = node.condition()->get_resolved_type();

                if (!is_boolean_context_valid(cond_type) && cond_type->kind() != TypeKind::Unknown)
                {
                    report_type_mismatch(node.location(), _type_context.get_boolean_type(),
                                         cond_type, "if condition");
                }
            }
        }

        // Check then and else branches
        if (node.then_statement())
        {
            node.then_statement()->accept(*this);
        }

        if (node.else_statement())
        {
            node.else_statement()->accept(*this);
        }
    }

    void TypeChecker::visit(WhileStatementNode &node)
    {
        // Check condition type
        if (node.condition())
        {
            node.condition()->accept(*this);

            if (node.condition()->has_resolved_type())
            {
                Type *cond_type = node.condition()->get_resolved_type();

                if (!is_boolean_context_valid(cond_type) && cond_type->kind() != TypeKind::Unknown)
                {
                    report_type_mismatch(node.location(), _type_context.get_boolean_type(),
                                         cond_type, "while condition");
                }
            }
        }

        // Check loop body
        bool was_in_loop = _in_loop;
        _in_loop = true;

        if (node.body())
        {
            node.body()->accept(*this);
        }

        _in_loop = was_in_loop;
    }

    void TypeChecker::visit(ForStatementNode &node)
    {
        enter_scope(); // For loop creates its own scope

        // Check initialization
        if (node.init())
        {
            node.init()->accept(*this);
        }

        // Check condition
        if (node.condition())
        {
            node.condition()->accept(*this);

            if (node.condition()->has_resolved_type())
            {
                Type *cond_type = node.condition()->get_resolved_type();

                if (!is_boolean_context_valid(cond_type))
                {
                    report_type_mismatch(node.location(), _type_context.get_boolean_type(),
                                         cond_type, "for condition");
                }
            }
        }

        // Check update expression
        if (node.update())
        {
            node.update()->accept(*this);
        }

        // Check loop body
        bool was_in_loop = _in_loop;
        _in_loop = true;

        if (node.body())
        {
            node.body()->accept(*this);
        }

        _in_loop = was_in_loop;
        exit_scope();
    }

    void TypeChecker::visit(ExpressionStatementNode &node)
    {
        // Visit the expression
        if (node.expression())
        {
            node.expression()->accept(*this);
        }
    }

    void TypeChecker::visit(DeclarationStatementNode &node)
    {
        // Forward to the wrapped declaration
        if (node.declaration())
        {
            node.declaration()->accept(*this);
        }
    }

    //===----------------------------------------------------------------------===//
    // Expression Visitors
    //===----------------------------------------------------------------------===//

    void TypeChecker::visit(LiteralNode &node)
    {
        Type *literal_type = infer_literal_type(node);
        if (literal_type)
        {
            node.set_resolved_type(literal_type);
        }
    }

    void TypeChecker::visit(IdentifierNode &node)
    {
        const std::string &name = node.name();

        // Special handling for 'this' keyword
        if (name == "this")
        {
            // For now, treat 'this' as having the current struct type
            // TODO: Add proper implementation block context checking
            if (_current_struct_type)
            {
                // 'this' should be a pointer to the current struct type, not the struct type itself
                Type *this_ptr_type = _type_context.create_pointer_type(_current_struct_type);
                node.set_resolved_type(this_ptr_type);
                LOG_DEBUG(Cryo::LogComponent::AST, "Set 'this' type to: {}", this_ptr_type->to_string());
            }
            else
            {
                _diagnostic_builder->create_undefined_symbol_error(name, NodeKind::VariableDeclaration, node.location());
                node.set_resolved_type(_type_context.get_unknown_type());
            }
            return;
        }

        // Special handling for primitive type constructors
        if (is_primitive_integer_type(name))
        {
            // Primitive type constructors are functions that take one argument and return the target type
            // For now, we'll set this to unknown type since we need a proper function type
            // TODO: Create proper function types for primitive constructors
            node.set_resolved_type(_type_context.get_unknown_type());
            return;
        }

        TypedSymbol *symbol = _symbol_table->lookup_symbol(name);
        LOG_DEBUG(Cryo::LogComponent::AST, "IdentifierNode '{}': symbol lookup result = {}", name, symbol ? "found" : "not found");
        if (!symbol)
        {
            // Try to find the symbol in the std::Runtime namespace as a fallback
            // This allows runtime functions like cryo_alloc to be used without qualification
            if (_main_symbol_table)
            {
                Symbol *runtime_symbol = _main_symbol_table->lookup_namespaced_symbol_with_context("std::Runtime", name, _current_namespace);
                if (runtime_symbol)
                {
                    // Found runtime function - for now set to unknown type
                    // TODO: Create proper function types for runtime functions
                    node.set_resolved_type(_type_context.get_unknown_type());
                    return;
                }

                // Try to find the symbol in the std::Intrinsics namespace as well
                // This allows intrinsic functions like __printf__ to be used without qualification
                Symbol *intrinsic_symbol = _main_symbol_table->lookup_namespaced_symbol_with_context("std::Intrinsics", name, _current_namespace);
                if (intrinsic_symbol)
                {
                    // Found intrinsic function - use the actual function type
                    if (intrinsic_symbol->data_type)
                    {
                        node.set_resolved_type(intrinsic_symbol->data_type);
                    }
                    else
                    {
                        node.set_resolved_type(_type_context.get_unknown_type());
                    }
                    return;
                }

                // Try to find the symbol in the main symbol table as a final fallback
                // This handles variables that were declared but not properly loaded into current scope
                Symbol *main_symbol = _main_symbol_table->lookup_symbol(name);
                if (main_symbol && main_symbol->data_type)
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "IdentifierNode '{}': found in main symbol table fallback (kind={})", name, static_cast<int>(main_symbol->kind));
                    node.set_resolved_type(main_symbol->data_type);
                    return;
                }
            }

            // Check if we're in call context before reporting error
            if (_in_call_expression)
            {
                // Don't report error here if in call context - let CallExpressionNode handle it
                node.set_resolved_type(_type_context.get_unknown_type());
            }
            else
            {
                // Only report the error if none of the fallback mechanisms worked
                _diagnostic_builder->create_undefined_symbol_error(name, NodeKind::VariableDeclaration, node.location());
                report_undefined_symbol(node.location(), name);
                node.set_resolved_type(_type_context.get_unknown_type());
            }
        }
        else
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "IdentifierNode '{}': setting resolved type to {} (kind={})", name,
                      symbol->type ? symbol->type->name() : "null",
                      symbol->type ? static_cast<int>(symbol->type->kind()) : -1);
            node.set_resolved_type(symbol->type);
        }
    }

    void TypeChecker::visit(BinaryExpressionNode &node)
    {
        // Visit the left and right operands
        if (node.left())
        {
            node.left()->accept(*this);
        }
        if (node.right())
        {
            node.right()->accept(*this);
        }

        // Type check the binary operation
        if (node.left() && node.right())
        {
            Type *left_type = nullptr;
            Type *right_type = nullptr;

            if (node.left()->has_resolved_type())
            {
                left_type = node.left()->get_resolved_type();
            }

            if (node.right()->has_resolved_type())
            {
                right_type = node.right()->get_resolved_type();
            }

            if (left_type && right_type)
            {
                TokenKind op = node.operator_token().kind();

                // Check for assignment operations
                if (op == TokenKind::TK_EQUAL || op == TokenKind::TK_PLUSEQUAL || op == TokenKind::TK_MINUSEQUAL ||
                    op == TokenKind::TK_STAREQUAL || op == TokenKind::TK_SLASHEQUAL)
                {
                    // For assignment, check if right is assignable to left using enhanced compatibility check
                    if (!check_assignment_compatibility(left_type, right_type, node.location()))
                    {
                        _diagnostic_builder->create_invalid_assignment_error(left_type, right_type, node.location());
                    }
                    // Assignment result has the type of the left operand
                    node.set_resolved_type(left_type);
                }
                else
                {
                    // For other operations, types should be compatible
                    Type *result_type = nullptr;

                    // Arithmetic operations
                    if (op == TokenKind::TK_PLUS || op == TokenKind::TK_MINUS || op == TokenKind::TK_STAR || op == TokenKind::TK_SLASH)
                    {
                        // Handle string concatenation for + operator
                        if (op == TokenKind::TK_PLUS && left_type->name() == "string" && right_type->name() == "string")
                        {
                            result_type = left_type; // string + string = string
                        }
                        // Handle string + char concatenation for + operator
                        else if (op == TokenKind::TK_PLUS && left_type->name() == "string" && right_type->name() == "char")
                        {
                            result_type = left_type; // string + char = string
                        }
                        // Handle char + string concatenation for + operator
                        else if (op == TokenKind::TK_PLUS && left_type->name() == "char" && right_type->name() == "string")
                        {
                            result_type = right_type; // char + string = string
                        }
                        // Handle pointer arithmetic: pointer + integer = pointer, pointer - integer = pointer
                        else if ((op == TokenKind::TK_PLUS || op == TokenKind::TK_MINUS) && 
                                 left_type->kind() == TypeKind::Pointer && right_type->is_integral())
                        {
                            LOG_DEBUG(Cryo::LogComponent::AST, "Pointer arithmetic: {} {} {} = {}", 
                                     left_type->to_string(), 
                                     (op == TokenKind::TK_PLUS ? "+" : "-"), 
                                     right_type->to_string(), 
                                     left_type->to_string());
                            result_type = left_type; // pointer +/- int = pointer
                        }
                        // Handle integer + pointer = pointer (commutative for addition only)
                        else if (op == TokenKind::TK_PLUS && 
                                 left_type->is_integral() && right_type->kind() == TypeKind::Pointer)
                        {
                            LOG_DEBUG(Cryo::LogComponent::AST, "Pointer arithmetic (commutative): {} + {} = {}", 
                                     left_type->to_string(), 
                                     right_type->to_string(), 
                                     right_type->to_string());
                            result_type = right_type; // int + pointer = pointer
                        }
                        // Handle pointer - pointer = integer (pointer difference)
                        else if (op == TokenKind::TK_MINUS && 
                                 left_type->kind() == TypeKind::Pointer && right_type->kind() == TypeKind::Pointer)
                        {
                            LOG_DEBUG(Cryo::LogComponent::AST, "Pointer difference: {} - {} = ptrdiff_t", 
                                     left_type->to_string(), right_type->to_string());
                            // Return a signed integer type for pointer difference
                            result_type = _type_context.get_integer_type(IntegerKind::I64, true);
                        }
                        // Handle numeric arithmetic
                        else if (left_type->is_numeric() && right_type->is_numeric())
                        {
                            // Mixed float/int arithmetic
                            if (left_type->kind() == TypeKind::Float && right_type->is_integral())
                            {
                                result_type = left_type; // float + int = float
                            }
                            else if (left_type->is_integral() && right_type->kind() == TypeKind::Float)
                            {
                                result_type = right_type; // int + float = float
                            }
                            // Same type numeric operations
                            else if (left_type == right_type &&
                                     (left_type->name() == "int" || left_type->name() == "double" || left_type->name() == "float" ||
                                      left_type->name() == "i8" || left_type->name() == "i16" || left_type->name() == "i32" || left_type->name() == "i64" ||
                                      left_type->name() == "u8" || left_type->name() == "u16" || left_type->name() == "u32" || left_type->name() == "u64"))
                            {
                                result_type = left_type;
                            }
                            else
                            {
                                report_type_mismatch(node.location(), left_type, right_type,
                                                     "arithmetic operation");
                            }
                        }
                        else
                        {
                            std::string left_name = left_type ? left_type->to_string() : "unknown";
                            std::string right_name = right_type ? right_type->to_string() : "unknown";
                            report_error(TypeError::ErrorKind::InvalidOperation, node.location(),
                                         "Cannot apply arithmetic operation to types '" + left_name + "' and '" + right_name + "'");
                            node.set_resolved_type(_type_context.get_unknown_type());
                        }
                    }
                    // Bit shift operations
                    else if (op == TokenKind::TK_LESSLESS || op == TokenKind::TK_GREATERGREATER)
                    {
                        // Bit shift operations require integral types
                        if (left_type->is_integral() && right_type->is_integral())
                        {
                            // Result type is the type of the left operand (the value being shifted)
                            result_type = left_type;
                        }
                        else
                        {
                            std::string left_name = left_type ? left_type->to_string() : "unknown";
                            std::string right_name = right_type ? right_type->to_string() : "unknown";
                            std::string op_str = (op == TokenKind::TK_LESSLESS) ? "<<" : ">>";
                            report_error(TypeError::ErrorKind::InvalidOperation, node.location(),
                                         "Cannot apply bit shift operation '" + op_str + "' to types '" + left_name + "' and '" + right_name + "' (both operands must be integral types)");
                            node.set_resolved_type(_type_context.get_unknown_type());
                        }
                    }
                    // Comparison operations
                    else if (op == TokenKind::TK_EQUALEQUAL || op == TokenKind::TK_EXCLAIMEQUAL ||
                             op == TokenKind::TK_L_ANGLE || op == TokenKind::TK_R_ANGLE ||
                             op == TokenKind::TK_LESSEQUAL || op == TokenKind::TK_GREATEREQUAL)
                    {
                        // Special handling for null comparisons with nullable types (pointers, optionals, etc.)
                        bool is_null_comparison = false;
                        if ((left_type->kind() == TypeKind::Null && right_type->is_nullable()) ||
                            (right_type->kind() == TypeKind::Null && left_type->is_nullable()))
                        {
                            is_null_comparison = true;
                        }

                        // Fallback: string-based null comparison check for migration issues
                        std::string left_str = left_type->to_string();
                        std::string right_str = right_type->to_string();

                        if ((left_str == "null" && right_str.find("*") != std::string::npos) ||
                            (right_str == "null" && left_str.find("*") != std::string::npos))
                        {
                            is_null_comparison = true;
                        }

                        // Special handling for mixed integer type comparisons
                        bool is_mixed_integer_comparison = false;
                        if (left_type->is_integral() && right_type->is_integral())
                        {
                            is_mixed_integer_comparison = true;
                        }

                        if (is_null_comparison ||
                            is_mixed_integer_comparison ||
                            left_type->is_assignable_from(*right_type) ||
                            right_type->is_assignable_from(*left_type))
                        {
                            result_type = _type_context.get_boolean_type();
                        }
                        else
                        {
                            _diagnostic_builder->create_invalid_operation_error("comparison", left_type, right_type, node.location());
                        }
                    }

                    if (result_type)
                    {
                        node.set_resolved_type(result_type);
                    }
                }
            }
        }
    }

    void TypeChecker::visit(UnaryExpressionNode &node)
    {
        // Visit the operand first
        if (node.operand())
        {
            node.operand()->accept(*this);

            Type *operand_type = node.operand()->has_resolved_type()
                                     ? node.operand()->get_resolved_type()
                                     : nullptr;

            if (operand_type)
            {
                TokenKind op = node.operator_token().kind();

                if (op == TokenKind::TK_AMP) // Address-of operator (&)
                {
                    // Special handling for arrays: &array should give pointer to element type
                    if (operand_type->kind() == TypeKind::Array)
                    {
                        auto array_type = static_cast<ArrayType *>(operand_type);
                        auto element_type = array_type->element_type();
                        if (element_type)
                        {
                            Type *pointer_type = _type_context.create_pointer_type(element_type.get());
                            LOG_DEBUG(Cryo::LogComponent::AST, "Array address-of: &{}[] = {}*", 
                                     element_type->to_string(), element_type->to_string());
                            node.set_resolved_type(pointer_type);
                        }
                        else
                        {
                            // Fallback to reference type
                            Type *reference_type = _type_context.create_reference_type(operand_type);
                            node.set_resolved_type(reference_type);
                        }
                    }
                    else
                    {
                        // Create a reference type to the operand type for non-arrays
                        Type *reference_type = _type_context.create_reference_type(operand_type);
                        node.set_resolved_type(reference_type);
                    }
                }
                else if (op == TokenKind::TK_STAR) // Dereference operator (*)
                {
                    // Operand should be a pointer or reference type
                    if (operand_type->kind() == TypeKind::Pointer)
                    {
                        // Get the pointee type
                        auto *ptr_type = static_cast<PointerType *>(operand_type);
                        node.set_resolved_type(ptr_type->pointee_type().get());
                    }
                    else if (operand_type->kind() == TypeKind::Reference)
                    {
                        // Get the referent type
                        auto *ref_type = static_cast<ReferenceType *>(operand_type);
                        node.set_resolved_type(ref_type->referent_type().get());
                    }
                    else
                    {
                        std::string type_name = operand_type ? operand_type->to_string() : "unknown";
                        report_error(TypeError::ErrorKind::InvalidOperation, node.location(),
                                     "Cannot dereference value of type '" + type_name + "'");
                        node.set_resolved_type(_type_context.get_unknown_type());
                    }
                }
                else if (op == TokenKind::TK_MINUS) // Unary minus
                {
                    if (is_numeric_type(operand_type))
                    {
                        node.set_resolved_type(operand_type);
                    }
                    else
                    {
                        std::string type_name = operand_type ? operand_type->to_string() : "unknown";
                        report_error(TypeError::ErrorKind::InvalidOperation, node.location(),
                                     "Cannot apply unary minus to type '" + type_name + "'");
                        node.set_resolved_type(_type_context.get_unknown_type());
                    }
                }
                else if (op == TokenKind::TK_EXCLAIM) // Logical NOT
                {
                    if (is_boolean_context_valid(operand_type) || operand_type->kind() == TypeKind::Unknown)
                    {
                        node.set_resolved_type(_type_context.get_boolean_type());
                    }
                    else
                    {
                        _diagnostic_builder->create_invalid_operation_error("logical NOT", operand_type, nullptr, node.location());
                    }
                }
                else if (op == TokenKind::TK_PLUSPLUS) // Increment operator (++ prefix or postfix)
                {
                    if (is_numeric_type(operand_type) || operand_type->kind() == TypeKind::Pointer)
                    {
                        // For both prefix and postfix increment, the result type is the same as operand type
                        node.set_resolved_type(operand_type);
                    }
                    else
                    {
                        _diagnostic_builder->create_invalid_operation_error("increment", operand_type, nullptr, node.location());
                    }
                }
                else if (op == TokenKind::TK_MINUSMINUS) // Decrement operator (-- prefix or postfix)
                {
                    if (is_numeric_type(operand_type) || operand_type->kind() == TypeKind::Pointer)
                    {
                        // For both prefix and postfix decrement, the result type is the same as operand type
                        node.set_resolved_type(operand_type);
                    }
                    else
                    {
                        _diagnostic_builder->create_invalid_operation_error("decrement", operand_type, nullptr, node.location());
                    }
                }
                else
                {
                    _diagnostic_builder->create_invalid_operation_error(node.operator_token().to_string(), operand_type, nullptr, node.location());
                }
            }
        }
    }

    void TypeChecker::visit(CallExpressionNode &node)
    {
        // Visit callee with call expression context
        if (node.callee())
        {
            bool prev_in_call = _in_call_expression;
            _in_call_expression = true;
            node.callee()->accept(*this);
            _in_call_expression = prev_in_call;
        }

        // Visit arguments
        std::vector<Type *> arg_types;
        for (const auto &arg : node.arguments())
        {
            if (arg)
            {
                arg->accept(*this);
                if (arg->has_resolved_type())
                {
                    Type *arg_type = arg->get_resolved_type();
                    arg_types.push_back(arg_type);
                }
                else
                {
                    arg_types.push_back(_type_context.get_unknown_type());
                }
            }
        }

        // Special handling for struct/class constructor calls (e.g., TestClass(20), TestStruct(25))
        // Constructor calls without 'new' are stack allocations and should be allowed
        // This check takes priority over function call logic
        if (node.callee() && node.callee()->kind() == NodeKind::Identifier)
        {
            auto identifier = dynamic_cast<IdentifierNode *>(node.callee());
            if (identifier)
            {
                const std::string &callee_name = identifier->name();
                bool has_resolved = node.callee()->has_resolved_type();
                Type *callee_resolved_type = has_resolved ? node.callee()->get_resolved_type() : nullptr;

                LOG_DEBUG(Cryo::LogComponent::AST, "CallExpression checking struct/class constructor for '{}', has_resolved_type: {}, type: {}",
                          callee_name, has_resolved, callee_resolved_type ? callee_resolved_type->to_string() : "none");

                // Check if the resolved type is unknown - this indicates an undefined identifier
                if (has_resolved && callee_resolved_type && callee_resolved_type->kind() == TypeKind::Unknown)
                {
                    // Check if it's not a known primitive type
                    if (!is_primitive_integer_type(callee_name) && callee_name != "f32" && callee_name != "f64" && callee_name != "float")
                    {
                        // The identifier was processed but not found - this is an undefined function call
                        // Report as undefined function and return to avoid cascading non-callable errors
                        _diagnostic_builder->create_undefined_symbol_error(callee_name, NodeKind::FunctionDeclaration, node.location());
                        node.set_resolved_type(_type_context.get_unknown_type());
                        return;
                    }
                }

                // Check if the resolved type is a struct or class type (constructor call)
                if (has_resolved && callee_resolved_type)
                {
                    if (callee_resolved_type->kind() == TypeKind::Struct)
                    {
                        // This is a valid stack allocation constructor call for a struct
                        // TODO: Add proper constructor argument validation here
                        LOG_DEBUG(Cryo::LogComponent::AST, "CallExpression: Recognized struct constructor call for '{}'", callee_name);
                        node.set_resolved_type(callee_resolved_type);
                        return;
                    }
                    else if (callee_resolved_type->kind() == TypeKind::Class)
                    {
                        // This is a valid stack allocation constructor call for a class
                        // TODO: Add proper constructor argument validation here
                        LOG_DEBUG(Cryo::LogComponent::AST, "CallExpression: Recognized class constructor call for '{}'", callee_name);
                        node.set_resolved_type(callee_resolved_type);
                        return;
                    }
                    else if (callee_resolved_type->kind() == TypeKind::Enum)
                    {
                        // This is a valid enum constructor call
                        LOG_DEBUG(Cryo::LogComponent::AST, "CallExpression: Recognized enum constructor call for '{}'", callee_name);
                        node.set_resolved_type(callee_resolved_type);
                        return;
                    }
                }

                // If not resolved, try to look up struct/class types manually
                if (!has_resolved)
                {
                    // Check for existing struct types first
                    Type *struct_type = _type_context.lookup_struct_type(callee_name);
                    if (struct_type && struct_type->kind() == TypeKind::Struct)
                    {
                        // This is a valid stack allocation constructor call for a struct
                        // TODO: Add proper constructor argument validation here
                        LOG_DEBUG(Cryo::LogComponent::AST, "CallExpression: Found unresolved struct constructor call for '{}'", callee_name);
                        node.set_resolved_type(struct_type);
                        return;
                    }

                    // Check for existing class types (consistent with resolve_type_with_generic_context)
                    Type *class_type = _type_context.get_class_type(callee_name);
                    if (class_type && class_type->kind() == TypeKind::Class)
                    {
                        // This is a valid stack allocation constructor call for a class
                        // TODO: Add proper constructor argument validation here
                        LOG_DEBUG(Cryo::LogComponent::AST, "CallExpression: Found unresolved class constructor call for '{}'", callee_name);
                        node.set_resolved_type(class_type);
                        return;
                    }

                    // Check if this is an existing enum type
                    Type *enum_type = _type_context.lookup_enum_type(callee_name);
                    if (enum_type && enum_type->kind() == TypeKind::Enum)
                    {
                        // This is a valid enum constructor call
                        LOG_DEBUG(Cryo::LogComponent::AST, "CallExpression: Found unresolved enum constructor call for '{}'", callee_name);
                        node.set_resolved_type(enum_type);
                        return;
                    }

                    // If we reach here and the callee is not resolved and it's not a known type,
                    // and it's not a primitive type, then it's likely an undefined function call
                    if (!is_primitive_integer_type(callee_name) && callee_name != "f32" && callee_name != "f64" && callee_name != "float")
                    {
                        // This is an undefined function call
                        _diagnostic_builder->create_undefined_symbol_error(callee_name, NodeKind::FunctionDeclaration, node.location());
                        node.set_resolved_type(_type_context.get_unknown_type());
                        return;
                    }
                }
            }
        }

        // Special handling for primitive type constructors (e.g., u64(0), i32(42))
        // Check for constructors regardless of resolved type status since primitive types may have unknown type
        if (node.callee() && node.callee()->kind() == NodeKind::Identifier)
        {
            auto identifier = dynamic_cast<IdentifierNode *>(node.callee());
            if (identifier)
            {
                const std::string &callee_name = identifier->name();
                bool has_resolved = node.callee()->has_resolved_type();
                Type *callee_resolved_type = has_resolved ? node.callee()->get_resolved_type() : nullptr;

                LOG_DEBUG(Cryo::LogComponent::AST, "CallExpression checking primitive constructor for '{}', has_resolved_type: {}, type: {}",
                          callee_name, has_resolved, callee_resolved_type ? callee_resolved_type->to_string() : "none");

                // Handle primitive constructors: either not resolved, resolved to unknown type, or resolved to the matching primitive type
                bool is_primitive_constructor = (is_primitive_integer_type(callee_name) || callee_name == "f32" || callee_name == "f64" || callee_name == "float");
                bool should_handle_as_constructor = (!has_resolved || 
                                                   (callee_resolved_type && callee_resolved_type->kind() == TypeKind::Unknown) ||
                                                   (is_primitive_constructor && callee_resolved_type && callee_resolved_type->name() == callee_name));
                
                if (should_handle_as_constructor && is_primitive_constructor)
                {
                    // This is a primitive type constructor call like u64(0)
                    Type *target_type = lookup_type_by_name(callee_name);
                    if (target_type)
                    {
                        // Validate that we have exactly one argument for primitive type constructors
                        if (arg_types.size() != 1)
                        {
                            _diagnostic_builder->create_too_many_args_error(callee_name + " constructor", 1, arg_types.size(), node.location());
                            node.set_resolved_type(_type_context.get_unknown_type());
                            return;
                        }

                        // Check if the argument can be converted to the target type
                        Type *arg_type = arg_types[0];
                        if (arg_type->is_numeric() || arg_type->kind() == TypeKind::Boolean)
                        {
                            // Allow numeric conversions and boolean to integer conversions
                            node.set_resolved_type(target_type);
                            return;
                        }
                        else
                        {
                            _diagnostic_builder->create_assignment_type_error(target_type, arg_type, node.location());
                            node.set_resolved_type(_type_context.get_unknown_type());
                            return;
                        }
                    }
                }
            }
        }

        // Check if callee is callable
        if (node.callee() && node.callee()->has_resolved_type())
        {
            // Get the callee's resolved type directly
            Type *callee_type = node.callee()->get_resolved_type();
            LOG_DEBUG(Cryo::LogComponent::AST, "CallExpression callee has resolved type: {} (kind={})",
                      callee_type ? callee_type->name() : "null",
                      callee_type ? static_cast<int>(callee_type->kind()) : -1);

            // Special handling for ScopeResolution callees
            if (node.callee()->kind() == NodeKind::ScopeResolution)
            {
                // For scope resolution, we may need special handling
                // For now, if it's a function type, extract return type
                if (callee_type && callee_type->kind() == TypeKind::Function)
                {
                    FunctionType *func_type = static_cast<FunctionType *>(callee_type);
                    node.set_resolved_type(func_type->return_type().get());
                    return;
                }
                else
                {
                    // This is likely an enum constructor or other type
                    node.set_resolved_type(callee_type ? callee_type : _type_context.get_unknown_type());
                    return;
                }
            }

            // Special handling for method calls (MemberAccess as callee)
            if (node.callee()->kind() == NodeKind::MemberAccess)
            {
                // For method calls, check if this is a function type
                if (callee_type && callee_type->kind() == TypeKind::Function)
                {
                    FunctionType *func_type = static_cast<FunctionType *>(callee_type);
                    node.set_resolved_type(func_type->return_type().get());
                }
                else
                {
                    // For non-function types, use the type directly
                    node.set_resolved_type(callee_type ? callee_type : _type_context.get_unknown_type());
                }
                return;
            }

            // If callee type is not resolved, fall back to unknown
            if (!callee_type)
            {
                _diagnostic_builder->create_non_callable_error(nullptr, node.location(), &node);
                node.set_resolved_type(_type_context.get_unknown_type());
                return;
            }

            // Check if this is a function type
            if (callee_type->kind() == TypeKind::Function)
            {
                // This is a function type - extract return type
                FunctionType *func_type = static_cast<FunctionType *>(callee_type);

                // Check function call compatibility and promote integer literals if needed
                if (check_function_call_compatibility(func_type, arg_types, node.location()))
                {
                    // Promote integer literals to match parameter types
                    const auto &param_types = func_type->parameter_types();
                    size_t fixed_params = func_type->is_variadic() ? param_types.size() - 1 : param_types.size();
                    
                    for (size_t i = 0; i < fixed_params && i < node.arguments().size(); ++i)
                    {
                        auto &arg = node.arguments()[i];
                        if (arg && arg->kind() == NodeKind::Literal)
                        {
                            LiteralNode *literal = static_cast<LiteralNode *>(arg.get());
                            Type *arg_type = arg->get_resolved_type();
                            Type *param_type = param_types[i].get();
                            
                            // Check if this is an integer literal (represented as 'int' type) that should be promoted
                            if (arg_type && param_type && 
                                arg_type->name() == "int" && 
                                param_type->kind() == TypeKind::Integer &&
                                literal->literal_kind() == TokenKind::TK_NUMERIC_CONSTANT)
                            {
                                // Check if the literal value doesn't have a type suffix
                                const std::string &value = literal->value();
                                if (!value.ends_with("u8") && !value.ends_with("u16") && !value.ends_with("u32") && 
                                    !value.ends_with("u64") && !value.ends_with("i8") && !value.ends_with("i16") && 
                                    !value.ends_with("i32") && !value.ends_with("i64") && !value.ends_with("f32") && 
                                    !value.ends_with("f64") && !value.ends_with("usize") && !value.ends_with("isize"))
                                {
                                    // Promote the literal to the parameter type
                                    LOG_DEBUG(Cryo::LogComponent::AST, "CallExpression: Promoting integer literal from 'int' to '{}' for parameter {}", 
                                             param_type->to_string(), i + 1);
                                    literal->set_resolved_type(param_type);
                                }
                            }
                        }
                    }
                    
                    Type *return_type = func_type->return_type().get();
                    if (return_type)
                    {
                        LOG_DEBUG(Cryo::LogComponent::AST, "CallExpression: Setting resolved type to function return type: {}", return_type->to_string());
                        node.set_resolved_type(return_type);
                    }
                    else
                    {
                        LOG_WARN(Cryo::LogComponent::AST, "CallExpression: Function return type is null, using unknown type");
                        node.set_resolved_type(_type_context.get_unknown_type());
                    }
                }
                else
                {
                    node.set_resolved_type(_type_context.get_unknown_type());
                }
                return;
            }

            // Not callable
            _diagnostic_builder->create_non_callable_error(callee_type, node.location(), &node);
            node.set_resolved_type(_type_context.get_unknown_type());
        }
        else
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "CallExpression callee does not have resolved type, setting to unknown");
            node.set_resolved_type(_type_context.get_unknown_type());
        }
    }

    void TypeChecker::visit(NewExpressionNode &node)
    {
        // Visit arguments first to ensure they are type-checked
        for (const auto &arg : node.arguments())
        {
            if (arg)
            {
                arg->accept(*this);
            }
        }

        // Build the full type name including generic arguments
        std::string type_name = node.type_name();

        // Handle generic types
        if (!node.generic_args().empty())
        {
            // Build the full generic type string for TypeRegistry resolution
            std::string full_type_name = node.type_name() + "<";
            for (size_t i = 0; i < node.generic_args().size(); ++i)
            {
                if (i > 0)
                    full_type_name += ", ";
                full_type_name += node.generic_args()[i];
            }
            full_type_name += ">";

            // Use TypeRegistry to resolve generic type instantiation
            auto resolved_type = resolve_generic_type(full_type_name);
            if (resolved_type)
            {
                // Successfully resolved generic type - use its name
                type_name = resolved_type->get_instantiated_name();
                node.set_type(type_name);

                // Track this instantiation for monomorphization
                track_instantiation(node.type_name(), node.generic_args(), full_type_name, node.location());
                return;
            }
            else
            {
                // Fallback to manual type name construction
                type_name = full_type_name;

                // Still track the instantiation even if TypeRegistry couldn't resolve it
                track_instantiation(node.type_name(), node.generic_args(), full_type_name, node.location());
            }
        }

        // Look for the type definition in the symbol table
        // First try the full instantiated name (e.g., "Array<int>")
        TypedSymbol *type_symbol = _symbol_table->lookup_symbol(type_name);

        // If not found and this is a generic type, try to find the base template
        if (!type_symbol && !node.generic_args().empty())
        {
            // Try to find the generic template (e.g., "Array" for "Array<int>")
            type_symbol = _symbol_table->lookup_symbol(node.type_name());
        }

        if (type_symbol)
        {
            // The type exists, so the new expression is valid
            node.set_type(type_name);
        }
        else
        {
            // Type not found - report error and set to unknown
            _diagnostic_builder->create_undefined_symbol_error(node.type_name(), NodeKind::ClassDeclaration, node.location());
            node.set_type("unknown");
        }

        // TODO: Add constructor argument validation
        // For now, we assume any struct/class can be constructed with any arguments
        // Later we can add proper constructor signature checking
    }

    void TypeChecker::visit(StructLiteralNode &node)
    {
        // Look up the struct type in the symbol table
        const std::string &struct_name = node.struct_type();
        TypedSymbol *type_symbol = _symbol_table->lookup_symbol(struct_name);

        if (!type_symbol)
        {
            // Struct type not found - report error
            _diagnostic_builder->create_undefined_symbol_error(struct_name, NodeKind::StructDeclaration, node.location());
            node.set_resolved_type(_type_context.get_unknown_type());
        }
        else
        {
            // Struct type exists - set the resolved type
            node.set_resolved_type(type_symbol->type);

            // TODO: Validate field initializers against struct definition
            // For now, assume all field initializers are valid
        }

        // Visit all field initializers for type checking
        for (const auto &field_init : node.field_initializers())
        {
            if (field_init && field_init->value())
            {
                field_init->value()->accept(*this);
            }
        }
    }

    void TypeChecker::visit(SizeofExpressionNode &node)
    {
        // sizeof is a compile-time operator that returns u64
        // We need to validate that the type exists

        std::string type_name = node.type_name();

        // First check if it's a primitive type
        if (type_name == "u8" || type_name == "i8" || type_name == "char")
        {
            node.set_type("u64");
            return;
        }
        else if (type_name == "u16" || type_name == "i16")
        {
            node.set_type("u64");
            return;
        }
        else if (type_name == "u32" || type_name == "i32" || type_name == "int")
        {
            node.set_type("u64");
            return;
        }
        else if (type_name == "u64" || type_name == "i64")
        {
            node.set_type("u64");
            return;
        }
        else if (type_name == "f32" || type_name == "f64" || type_name == "float")
        {
            node.set_type("u64");
            return;
        }
        else if (type_name == "boolean")
        {
            node.set_type("u64");
            return;
        }
        else if (type_name == "string")
        {
            node.set_type("u64");
            return;
        }

        // Check if we're in a generic context and this is a generic parameter
        if (is_in_generic_context() && is_generic_parameter(type_name))
        {
            // This is a valid generic parameter - sizeof is valid
            node.set_type("u64");
            return;
        }

        // Check if it's a user-defined type in the symbol table
        TypedSymbol *type_symbol = _symbol_table->lookup_symbol(type_name);

        if (type_symbol)
        {
            // The type exists, sizeof is valid
            node.set_type("u64");
        }
        else
        {
            // Type not found - report error and set to unknown
            _diagnostic_builder->create_undefined_variable_error(type_name, NodeKind::Declaration, node.location());
            node.set_type("u64"); // Still return u64 even on error
        }
    }

    // Helper function to substitute generic type parameters in a type string
    std::string substitute_generic_type(const std::string &type_str,
                                        const std::string &object_type,
                                        const std::string &base_type)
    {
        // Extract generic arguments from object_type (e.g., "GenericStruct<string>" -> ["string"])
        std::vector<std::string> generic_args;
        size_t start_pos = object_type.find('<');
        if (start_pos != std::string::npos)
        {
            size_t end_pos = object_type.rfind('>');
            if (end_pos != std::string::npos && end_pos > start_pos)
            {
                std::string args_str = object_type.substr(start_pos + 1, end_pos - start_pos - 1);

                // Simple parsing of comma-separated generic arguments
                std::stringstream ss(args_str);
                std::string arg;
                while (std::getline(ss, arg, ','))
                {
                    // Trim whitespace
                    size_t first = arg.find_first_not_of(' ');
                    if (first != std::string::npos)
                    {
                        size_t last = arg.find_last_not_of(' ');
                        generic_args.push_back(arg.substr(first, (last - first + 1)));
                    }
                }
            }
        }

        // For now, handle simple case of single generic parameter T -> first argument
        if (type_str == "T" && !generic_args.empty())
        {
            return generic_args[0];
        }

        // Handle U as second parameter for multi-parameter generics
        if (type_str == "U" && generic_args.size() >= 2)
        {
            return generic_args[1];
        }

        // Return original type if no substitution needed
        return type_str;
    }

    void TypeChecker::visit(MemberAccessNode &node)
    {
        LOG_DEBUG(Cryo::LogComponent::AST, "Visiting MemberAccessNode: member='{}'", node.member());

        // Visit the object expression first
        if (node.object())
        {
            node.object()->accept(*this);
        }

        // Get the Type* of the object being accessed
        Type *object_type = nullptr;
        if (node.object())
        {
            if (node.object()->has_resolved_type())
            {
                // Use the resolved type for proper chaining of member access
                object_type = node.object()->get_resolved_type();
                LOG_DEBUG(Cryo::LogComponent::AST, "Member '{}' - using resolved type: {}", node.member(),
                          object_type ? object_type->name() : "null");
            }
            else if (node.object()->type().has_value())
            {
                // Fallback to lookup by string name (only for initial resolution)
                std::string type_name = node.object()->type().value();
                object_type = lookup_type_by_name(type_name);
                LOG_DEBUG(Cryo::LogComponent::AST, "Member '{}' - looked up type '{}': {}",
                          node.member(), type_name, object_type ? object_type->name() : "null");
            }
        }

        if (!object_type)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Object type is null, cannot resolve member access");
            Type *expected_type = _type_context.get_struct_type(""); // Placeholder for "object type"
            Type *found_type = _type_context.get_unknown_type();
            _diagnostic_builder->create_assignment_type_error(expected_type, found_type, node.location());
            node.set_resolved_type(_type_context.get_unknown_type());
            return;
        }

        // Get the member name
        std::string member_name = node.member();

        // Handle pointer types - automatically dereference for member access
        Type *effective_type = object_type;
        bool is_pointer_access = false;

        LOG_DEBUG(Cryo::LogComponent::AST, "Object type name: '{}', kind: {}, checking if pointer",
                  object_type->name(), static_cast<int>(object_type->kind()));

        if (object_type->name().find("HeapBlock") != std::string::npos)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "HEAPBLOCK DEBUG: type='{}', kind={} (7=Pointer, 10=Struct, 4=Class)",
                      object_type->name(), static_cast<int>(object_type->kind()));
        }

        if (object_type->kind() == TypeKind::Pointer)
        {
            // This is a pointer type, get the pointee type
            const PointerType *ptr_type = static_cast<const PointerType *>(object_type);
            effective_type = ptr_type->pointee_type().get();
            is_pointer_access = true;
            LOG_DEBUG(Cryo::LogComponent::AST, "Dereferencing pointer type to access member '{}' on type '{}'",
                      member_name, effective_type->name());
        }
        else
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Object type '{}' is not a pointer, kind: {}",
                      object_type->name(), static_cast<int>(object_type->kind()));
        }

        // Handle array types
        if (effective_type->kind() == TypeKind::Array)
        {
            if (member_name == "length" || member_name == "size")
            {
                node.set_resolved_type(_type_context.get_u64_type());
                return;
            }
            _diagnostic_builder->create_undefined_variable_error(member_name, NodeKind::VariableDeclaration, node.location());
            node.set_resolved_type(_type_context.get_unknown_type());
            return;
        }

        // For non-struct/class/enum types, reject member access
        if (effective_type->kind() != TypeKind::Struct &&
            effective_type->kind() != TypeKind::Class &&
            effective_type->kind() != TypeKind::Enum &&
            effective_type->kind() != TypeKind::Generic)
        {
            std::string type_name = effective_type ? effective_type->to_string() : "unknown";
            report_error(TypeError::ErrorKind::UndefinedVariable, node.location(),
                         "No field '" + member_name + "' on type '" + type_name + "'", &node);
            node.set_resolved_type(_type_context.get_unknown_type());
            return;
        }

        // Look up field in struct field map using the type name
        std::string lookup_type_name = effective_type->name();
        LOG_DEBUG(Cryo::LogComponent::AST, "Looking up field '{}' in struct '{}'", member_name, lookup_type_name);
        auto struct_it = _struct_fields.find(lookup_type_name);
        if (struct_it != _struct_fields.end())
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Found struct '{}' in _struct_fields with {} fields", lookup_type_name, struct_it->second.size());
            auto field_it = struct_it->second.find(member_name);
            if (field_it != struct_it->second.end())
            {
                // Found the field - store the resolved Type*
                node.set_resolved_type(field_it->second);
                return;
            }
            else
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "Field '{}' NOT found in struct '{}'", member_name, lookup_type_name);
            }
        }
        else
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Struct '{}' NOT found in _struct_fields map", lookup_type_name);
        }

        // Look up method in struct method map
        LOG_DEBUG(Cryo::LogComponent::AST, "Starting method lookup for '{}' in struct '{}'", member_name, lookup_type_name);
        auto method_struct_it = _struct_methods.find(lookup_type_name);
        if (method_struct_it != _struct_methods.end())
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Found struct '{}' in _struct_methods with {} methods", lookup_type_name, method_struct_it->second.size());
            auto method_it = method_struct_it->second.find(member_name);
            if (method_it != method_struct_it->second.end())
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "Found public method '{}' in struct '{}'", member_name, lookup_type_name);
                // Found the method - store the resolved Type*
                node.set_resolved_type(method_it->second);
                return;
            }
            else
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "Method '{}' NOT found in public methods for struct '{}'", member_name, lookup_type_name);
            }
        }
        else
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Struct '{}' NOT found in _struct_methods map", lookup_type_name);
        }

        // Check for private methods - allow access from within the same class
        LOG_DEBUG(Cryo::LogComponent::AST, "Checking for private methods: lookup_type_name='{}', member_name='{}', current_struct='{}'", lookup_type_name, member_name, _current_struct_name);
        auto private_method_struct_it = _private_struct_methods.find(lookup_type_name);
        if (private_method_struct_it != _private_struct_methods.end())
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Found private methods for type '{}'", lookup_type_name);
            auto private_method_it = private_method_struct_it->second.find(member_name);
            if (private_method_it != private_method_struct_it->second.end())
            {
                // Check if we're accessing the private method from within the same class
                if (_current_struct_name == lookup_type_name)
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "Found private method '{}' and allowing access from within same class '{}'", member_name, lookup_type_name);
                    // Allow access to private method from within the same class
                    node.set_resolved_type(private_method_it->second);
                    return;
                }
                else
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "Found private method '{}' but access is forbidden from different class '{}' - reporting E0353_PRIVATE_ACCESS", member_name, _current_struct_name);
                    // This is a private method being accessed from outside the class - report error
                    _diagnostic_builder->create_private_member_access_error(member_name, lookup_type_name, node.location());
                    // Also add to TypeChecker's error list so has_errors() returns true
                    _errors.emplace_back(TypeError::ErrorKind::InvalidOperation, node.location(),
                                         "Cannot access private member '" + member_name + "' of type '" + lookup_type_name + "'");
                    node.set_resolved_type(_type_context.get_unknown_type());
                    return;
                }
            }
            else
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "Method '{}' not found in private methods for type '{}'", member_name, lookup_type_name);
            }
        }
        else
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "No private methods found for type '{}'", lookup_type_name);
        }

        // Handle built-in type methods (string, primitive types, etc.)
        if (effective_type->is_primitive())
        {
            // Check for string methods
            if (effective_type->name() == "string")
            {
                if (member_name == "length" || member_name == "size")
                {
                    node.set_resolved_type(_type_context.get_u64_type());
                    return;
                }
            }

            // Allow method access on enum types even though they're considered primitive
            if (effective_type->kind() == TypeKind::Enum)
            {
                // Continue to method lookup for enum types
                // Don't return here, let it fall through to method lookup
            }
            else
            {
                // For other primitive types, we can add support for methods as needed
                // For now, reject member access on non-enum primitive types
                std::string type_name = effective_type ? effective_type->to_string() : "unknown";
                report_error(TypeError::ErrorKind::UndefinedVariable, node.location(),
                             "No field '" + member_name + "' on type '" + type_name + "'", &node);
                node.set_resolved_type(_type_context.get_unknown_type());
                return;
            }
        }

        // If we get here, the member was not found
        LOG_DEBUG(Cryo::LogComponent::AST, "MEMBER ACCESS FAILED: member='{}', object_type='{}' (kind={}), effective_type='{}' (kind={})",
                  member_name,
                  object_type ? object_type->name() : "null",
                  object_type ? static_cast<int>(object_type->kind()) : -1,
                  effective_type ? effective_type->name() : "null",
                  effective_type ? static_cast<int>(effective_type->kind()) : -1);

        // Use enhanced diagnostic builder for field access errors
        std::string type_name = effective_type ? effective_type->to_string() : "unknown";
        report_error(TypeError::ErrorKind::UndefinedVariable, node.location(),
                     "No field '" + member_name + "' on type '" + type_name + "'", &node);
        node.set_resolved_type(_type_context.get_unknown_type());
    }

    void TypeChecker::visit(ScopeResolutionNode &node)
    {
        const std::string &scope_name = node.scope_name();
        const std::string &member_name = node.member_name();

        // Try namespace lookup first using our new namespace support with context
        if (_main_symbol_table)
        {
            Symbol *symbol = _main_symbol_table->lookup_namespaced_symbol_with_context(scope_name, member_name, _current_namespace);
            if (symbol)
            {
                // Found in namespace - this is what we want for imports like CryoTest::test_import_function
                if (symbol->data_type != nullptr)
                {
                    std::string function_signature = symbol->data_type->to_string();
                    node.set_type(function_signature);
                    // Resolved namespace symbol
                }
                else
                {
                    // For symbols without type information, use a generic type based on symbol kind
                    std::string type_name = "unknown";
                    if (symbol->kind == SymbolKind::Function)
                    {
                        type_name = "function"; // Or we could try to infer from the AST
                    }
                    node.set_type(type_name);
                    LOG_DEBUG(Cryo::LogComponent::AST, "Resolved namespace symbol: {}::{} with generic type: {} (context: {})",
                              scope_name, member_name, type_name, _current_namespace);
                }
                return;
            }

            // Check if this is a multi-level scope like "Syscall::IO" where we need to look up
            // the class "IO" within the namespace "Syscall" (with context resolution)
            size_t last_scope = scope_name.find_last_of(':');
            if (last_scope != std::string::npos && last_scope >= 1)
            {
                // Split "Syscall::IO" into namespace="Syscall" and class_name="IO"
                std::string namespace_part = scope_name.substr(0, last_scope - 1); // "Syscall"
                std::string class_name = scope_name.substr(last_scope + 1);        // "IO"

                // Trying multi-level resolution

                // Look up the class in the namespace (with context)
                Symbol *class_symbol = _main_symbol_table->lookup_namespaced_symbol_with_context(namespace_part, class_name, _current_namespace);
                if (class_symbol && class_symbol->kind == SymbolKind::Type)
                {
                    // Found class in namespace

                    // Now look for the method within the class
                    // For static methods, we assume they exist and have appropriate types
                    if (member_name == "write" && class_name == "IO")
                    {
                        node.set_type("i64"); // write returns i64
                        LOG_DEBUG(Cryo::LogComponent::AST, "Resolved static method: {}::{} -> i64", scope_name, member_name);
                        return;
                    }
                    else if (member_name == "read" && class_name == "IO")
                    {
                        node.set_type("i64"); // read returns i64
                        // Resolved static method IO::read -> i64
                        return;
                    }
                    else if (member_name == "open" && class_name == "IO")
                    {
                        node.set_type("int"); // open returns int
                        // Resolved static method IO::open -> int
                        return;
                    }
                    else if (member_name == "close" && class_name == "IO")
                    {
                        node.set_type("int"); // close returns int
                        LOG_DEBUG(Cryo::LogComponent::AST, "Resolved static method: {}::{} -> int", scope_name, member_name);
                        return;
                    }
                    else
                    {
                        // Generic static method - assume it exists
                        node.set_type("unknown");
                        LOG_DEBUG(Cryo::LogComponent::AST, "Resolved generic static method: {}::{} -> unknown", scope_name, member_name);
                        return;
                    }
                }
                else
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "Class {} not found in namespace {}", class_name, namespace_part);
                }
            }
        }

        // Fallback: try to look up the member name in main symbol table and check if it has the correct scope
        if (_main_symbol_table)
        {
            Symbol *symbol = _main_symbol_table->lookup_symbol(member_name);
            if (symbol && symbol->scope == scope_name)
            {
                // This is a scope-qualified function call like Std::Runtime::cryo_print_int
                node.set_type(symbol->data_type->to_string());
                return;
            }
        }

        // Check if this is a generic type parameter with trait bounds (e.g., T::get_default where T: Default)
        auto trait_bounds_it = _current_generic_trait_bounds.find(scope_name);
        if (trait_bounds_it != _current_generic_trait_bounds.end())
        {
            // scope_name is a generic type parameter with trait bounds
            const std::vector<std::string> &trait_names = trait_bounds_it->second;

            // For now, assume the static method exists if the trait bound is present
            // TODO: In a complete implementation, we would look up the trait definition
            // and verify that the member_name method exists in one of the bound traits

            // For static methods like get_default(), infer the return type as the generic parameter
            if (member_name == "get_default")
            {
                node.set_type(scope_name); // Return type is T
                LOG_DEBUG(Cryo::LogComponent::AST, "Resolved generic static method: {}::{} -> {}", scope_name, member_name, scope_name);
                return;
            }

            // For other static methods, use unknown type for now
            node.set_type("unknown");

            // Build trait bounds list for logging
            std::string trait_bounds;
            for (size_t i = 0; i < trait_names.size(); ++i)
            {
                if (i > 0)
                    trait_bounds += ", ";
                trait_bounds += trait_names[i];
            }
            LOG_DEBUG(Cryo::LogComponent::AST, "Resolved generic static method: {}::{} -> unknown (trait bounds: {})", scope_name, member_name, trait_bounds);
            return;
        }

        // If not found, try the old enum-based approach
        // For generic types like "Option<T>", extract the base type name
        std::string base_scope_name = scope_name;
        size_t generic_start = scope_name.find('<');
        if (generic_start != std::string::npos)
        {
            base_scope_name = scope_name.substr(0, generic_start);
        }

        // Look up the scope type (should be an enum type)
        LOG_DEBUG(Cryo::LogComponent::AST, "Looking up scope symbol '{}'", base_scope_name);

        // Check if it's a generic type pattern like Option<T>
        TypedSymbol *scope_symbol = nullptr;
        if (scope_name.find('<') != std::string::npos)
        {
            // For generic types, first try the base name
            scope_symbol = _symbol_table->lookup_symbol(base_scope_name);

            // If not found locally, try looking in all imported namespaces
            if (!scope_symbol)
            {
                scope_symbol = _symbol_table->lookup_symbol_in_any_namespace(base_scope_name);
            }

            if (!scope_symbol && _main_symbol_table)
            {
                // Try looking for the generic type in the main symbol table
                Symbol *main_symbol = _main_symbol_table->lookup_symbol(base_scope_name);
                if (main_symbol && main_symbol->data_type)
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "Found generic type '{}' in main symbol table", base_scope_name);
                    // For enum types, allow the scope resolution to proceed
                    return; // Found the type, proceed with resolution
                }

                // Also try main symbol table namespaces
                main_symbol = _main_symbol_table->lookup_symbol_in_any_namespace(base_scope_name);
                if (main_symbol && main_symbol->data_type)
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "Found generic type '{}' in main symbol table namespace", base_scope_name);
                    return; // Found the type, proceed with resolution
                }
            }

            // If not found yet, check if it's a registered generic type in our type registry
            if (!scope_symbol)
            {
                ParameterizedType *template_type = _type_registry->get_template(base_scope_name);
                if (template_type)
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "Found generic type '{}' in type registry", base_scope_name);
                    return; // Found the type, proceed with resolution
                }
            }

            // If still not found, this might be a forward reference within the same module
            // For common types like Option, Result etc, allow them to be resolved
            if (!scope_symbol && (base_scope_name == "Option" || base_scope_name == "Result" || base_scope_name == "Array"))
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "Allowing forward reference for common generic type '{}'", base_scope_name);
                return; // Allow the scope resolution to proceed
            }
        }
        else
        {
            scope_symbol = _symbol_table->lookup_symbol(base_scope_name);

            // If not found in local scope, try looking in all imported namespaces
            if (!scope_symbol)
            {
                scope_symbol = _symbol_table->lookup_symbol_in_any_namespace(base_scope_name);
            }

            // If still not found, check if it's a registered generic type in our type registry
            if (!scope_symbol)
            {
                ParameterizedType *template_type = _type_registry->get_template(base_scope_name);
                if (template_type)
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "Found generic type '{}' in type registry (non-generic case)", base_scope_name);
                    return; // Found the type, proceed with resolution
                }
            }
        }

        if (!scope_symbol)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Scope symbol '{}' not found", base_scope_name);
            std::string qualified_name = scope_name + "::" + member_name;
            _diagnostic_builder->create_undefined_symbol_error(qualified_name, NodeKind::Declaration, node.location());
            node.set_type(_type_context.get_unknown_type()->to_string());
            return;
        }

        Type *scope_type = scope_symbol->type;
        if (!scope_type)
        {
            _diagnostic_builder->create_undefined_symbol_error(base_scope_name, NodeKind::Declaration, node.location());
            node.set_type(_type_context.get_unknown_type()->to_string());
            return;
        }

        // Verify it's an enum type
        if (scope_type->kind() != TypeKind::Enum)
        {
            _diagnostic_builder->create_invalid_operation_error("scope resolution", scope_type, nullptr, node.location());
            node.set_type(_type_context.get_unknown_type()->to_string());
            return;
        }

        // For enum scope resolution like Shape::Circle or Option<T>::None,
        // the type should be the original parameterized type (e.g., "Option<T>")
        node.set_type(scope_name);

        // Resolved generic enum variant
    }

    //===----------------------------------------------------------------------===//
    // Struct/Class Declaration Visitors
    //===----------------------------------------------------------------------===//

    void TypeChecker::visit(StructDeclarationNode &node)
    {
        std::string struct_name = node.name();

        // Check for redefinition
        if (_symbol_table->lookup_symbol(struct_name))
        {
            _diagnostic_builder->create_redefined_symbol_error(struct_name, NodeKind::StructDeclaration, node.location());
            return;
        }

        // Register struct type in symbol table FIRST, before processing fields
        // This allows fields to reference this struct type or other struct types
        Type *struct_type = _type_context.get_struct_type(struct_name);
        _symbol_table->declare_symbol(struct_name, struct_type, node.location(), &node);

        // Save previous struct type and set current for 'this' keyword in methods
        Type *previous_struct_type = _current_struct_type;
        _current_struct_type = struct_type;

        // Save previous struct name and set current for field tracking
        std::string previous_struct_name = _current_struct_name;
        _current_struct_name = struct_name;

        // Enter struct scope
        enter_scope();

        // Process generic parameters if any
        for (const auto &generic_param : node.generic_parameters())
        {
            if (generic_param)
            {
                generic_param->accept(*this);
            }
        }

        // Process fields
        for (const auto &field : node.fields())
        {
            if (field)
            {
                field->accept(*this);
            }
        }

        // Process methods in two passes to handle forward references
        // Pass 1: Register all method signatures without type-checking bodies
        for (const auto &method : node.methods())
        {
            if (method)
            {
                // Register method signature only, skip body type-checking
                register_method_signature(*method);
            }
        }

        // Pass 2: Type-check all method bodies with all signatures available
        for (const auto &method : node.methods())
        {
            if (method)
            {
                method->accept(*this);
            }
        }

        // Exit struct scope
        exit_scope();

        // Restore previous struct type and struct name
        _current_struct_type = previous_struct_type;
        _current_struct_name = previous_struct_name;

        node.set_type(struct_name);
    }

    void TypeChecker::visit(ClassDeclarationNode &node)
    {
        std::string class_name = node.name();

        // Check for redefinition
        if (_symbol_table->lookup_symbol(class_name))
        {
            _diagnostic_builder->create_redefined_symbol_error(class_name, NodeKind::ClassDeclaration, node.location());
            return;
        }

        // Handle generic parameters
        std::vector<std::string> generic_param_names;
        for (const auto &generic_param : node.generic_parameters())
        {
            if (generic_param)
            {
                generic_param_names.push_back(generic_param->name());
            }
        }

        // Track if we entered a generic context
        bool entered_generic_context = false;

        // Enter generic context if this is a generic class
        if (!generic_param_names.empty())
        {
            enter_generic_context(class_name, generic_param_names, node.location());
            entered_generic_context = true;
        }

        // Register class type in symbol table FIRST, before processing fields
        // This allows fields to reference this class type or other class/struct types
        Type *class_type = _type_context.get_class_type(class_name);
        _symbol_table->declare_symbol(class_name, class_type, node.location(), &node);

        // Save previous class type and set current for 'this' keyword in methods
        Type *previous_struct_type = _current_struct_type;
        _current_struct_type = class_type;

        // Save previous class name and set current for field/method tracking
        std::string previous_struct_name = _current_struct_name;
        _current_struct_name = class_name;

        // Enter class scope
        enter_scope();

        // Process generic parameters if any
        for (const auto &generic_param : node.generic_parameters())
        {
            if (generic_param)
            {
                generic_param->accept(*this);
            }
        }

        // Process base class if any
        if (!node.base_class().empty())
        {
            std::string base_class_name = node.base_class();
            TypedSymbol *base_symbol = _symbol_table->lookup_symbol(base_class_name);
            if (!base_symbol)
            {
                _diagnostic_builder->create_undefined_symbol_error(base_class_name, NodeKind::ClassDeclaration, node.location());
            }
            else
            {
                Type *base_class_type = base_symbol->type;
                if (!base_class_type || base_class_type->kind() != TypeKind::Class)
                {
                    _diagnostic_builder->create_invalid_member_access_error("base class", base_class_type, node.location());
                }
            }
        }

        // Process fields
        for (const auto &field : node.fields())
        {
            if (field)
            {
                field->accept(*this);
            }
        }

        // Process methods in two passes to handle forward references
        // Pass 1: Register all method signatures without type-checking bodies
        for (const auto &method : node.methods())
        {
            if (method)
            {
                // Register method signature only, skip body type-checking
                register_method_signature(*method);
            }
        }

        // Pass 2: Type-check all method bodies with all signatures available
        for (const auto &method : node.methods())
        {
            if (method)
            {
                method->accept(*this);
            }
        }

        // Exit class scope
        exit_scope();

        // Restore previous struct type and struct name
        _current_struct_type = previous_struct_type;
        _current_struct_name = previous_struct_name;

        // Exit generic context if we entered one
        if (entered_generic_context)
        {
            exit_generic_context();
        }

        node.set_type(class_name);
    }

    void TypeChecker::visit(TraitDeclarationNode &node)
    {
        std::string trait_name = node.name();

        // Check for redefinition
        if (_symbol_table->lookup_symbol(trait_name))
        {
            _diagnostic_builder->create_redefined_symbol_error(trait_name, NodeKind::TraitDeclaration, node.location());
            return;
        }

        // Collect generic parameter names
        std::vector<std::string> generic_param_names;
        for (const auto &generic_param : node.generic_parameters())
        {
            if (generic_param)
            {
                generic_param_names.push_back(generic_param->name());
            }
        }

        // Track if we entered a generic context
        bool entered_generic_context = false;

        // Enter generic context if this is a generic trait
        if (!generic_param_names.empty())
        {
            enter_generic_context(trait_name, generic_param_names, node.location());
            entered_generic_context = true;
        }

        // Register trait type in symbol table
        Type *trait_type = _type_context.get_trait_type(trait_name);
        _symbol_table->declare_symbol(trait_name, trait_type, node.location(), &node);

        // Enter trait scope for processing methods
        enter_scope();

        // Process generic parameters if any
        for (const auto &generic_param : node.generic_parameters())
        {
            if (generic_param)
            {
                generic_param->accept(*this);
            }
        }

        // Process trait inheritance (parent traits)
        for (const auto &base_trait : node.base_traits())
        {
            // Validate that parent trait exists
            TypedSymbol *parent_symbol = _symbol_table->lookup_symbol(base_trait.name);
            if (!parent_symbol)
            {
                _diagnostic_builder->create_undefined_variable_error(base_trait.name, NodeKind::TraitDeclaration, node.location());
            }
            else if (parent_symbol->type->kind() != TypeKind::Trait)
            {
                _diagnostic_builder->create_assignment_type_error(_type_context.get_trait_type(base_trait.name), parent_symbol->type, node.location());
            }
        }

        // Process trait methods
        for (const auto &method : node.methods())
        {
            if (method)
            {
                method->accept(*this);
            }
        }

        // Exit trait scope
        exit_scope();

        // Exit generic context if we entered one
        if (entered_generic_context)
        {
            exit_generic_context();
        }

        // TraitDeclarationNode doesn't have set_type method, so we don't call it
    }

    void TypeChecker::visit(TypeAliasDeclarationNode &node)
    {
        std::string alias_name = node.alias_name();

        // Check for redefinition
        if (_symbol_table->lookup_symbol(alias_name))
        {
            // In stdlib compilation mode, be more lenient with type alias redefinitions
            if (_stdlib_compilation_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "Type alias '{}' already exists in stdlib compilation mode, skipping redeclaration", alias_name);
                return;
            }
            _diagnostic_builder->create_redefined_symbol_error(alias_name, NodeKind::TypeAliasDeclaration, node.location());
            return;
        }

        // Handle generic type aliases differently
        if (node.is_generic())
        {
            // For now, create a type alias that represents the generic template
            // In a full implementation, we'd need proper template system with type parameter substitution
            // LOG_DEBUG(Cryo::LogComponent::AST, "Generic type alias: {} = {}", alias_name, node.target_type());
            const auto &params = node.generic_params();

            // Build parameter list for logging
            std::string param_list;
            for (size_t i = 0; i < params.size(); ++i)
            {
                if (i > 0)
                    param_list += ", ";
                param_list += params[i];
            }
            LOG_DEBUG(Cryo::LogComponent::AST, "Generic type alias: {}<{}> = {}", alias_name, param_list, node.target_type());

            // Create a generic type alias - we'll use the full signature as the alias name for now
            std::string full_signature = alias_name + "<";
            for (size_t i = 0; i < params.size(); ++i)
            {
                if (i > 0)
                    full_signature += ", ";
                full_signature += params[i];
            }
            full_signature += ">";

            // Create a type alias that shows the generic nature
            Type *target_type = node.get_resolved_target_type();
            if (!target_type)
            {
                target_type = _type_context.get_unknown_type();
            }

            Type *generic_alias = _type_context.create_type_alias(full_signature, target_type);
            _symbol_table->declare_symbol(alias_name, generic_alias, node.location(), false);
            return;
        }

        Type *target_type = node.get_resolved_target_type();

        // Handle forward declarations (null target type)
        if (!target_type)
        {
            // Forward declaration - create TypeAlias with unknown target for now
            Type *forward_alias = _type_context.create_type_alias(alias_name, _type_context.get_unknown_type());
            _symbol_table->declare_symbol(alias_name, forward_alias, node.location(), false);
            return;
        }

        // Visit target type to ensure it's valid
        if (target_type && target_type->kind() != TypeKind::Unknown)
        {
            // Create TypeAlias wrapping the target type
            Type *alias_type = _type_context.create_type_alias(alias_name, target_type);
            _symbol_table->declare_symbol(alias_name, alias_type, node.location(), false);
        }
        else
        {
            _diagnostic_builder->create_undefined_symbol_error(alias_name, NodeKind::TypeAliasDeclaration, node.location());
        }
    }

    void TypeChecker::visit(EnumDeclarationNode &node)
    {
        std::string enum_name = node.name();
        LOG_DEBUG(Cryo::LogComponent::AST, "DEBUG: TypeChecker::visit(EnumDeclarationNode) called for '{}'", enum_name);
        LOG_DEBUG(Cryo::LogComponent::AST, "TypeChecker: Visiting enum declaration: {} - stdlib_compilation_mode={}", enum_name, _stdlib_compilation_mode);

        // Check if enum already exists in symbol table (from populate_symbol_table phase)
        TypedSymbol *existing_symbol = _symbol_table->lookup_symbol(enum_name);
        if (existing_symbol && existing_symbol->type != nullptr)
        {
            // Enum type was already created during symbol table population
            // Just handle variant registration for simple enums
            if (node.is_simple_enum())
            {
                int variant_value = 0;
                for (const auto &variant : node.variants())
                {
                    if (variant)
                    {
                        std::string variant_name = variant->name();
                        Type *int_type = _type_context.get_int_type();

                        // In stdlib compilation mode, always skip enum variant redeclarations
                        // since they're often processed multiple times during compilation
                        if (_stdlib_compilation_mode)
                        {
                            TypedSymbol *existing_variant = _symbol_table->lookup_symbol(variant_name);
                            if (existing_variant)
                            {
                                LOG_DEBUG(Cryo::LogComponent::AST, "Enum variant '{}' already exists in stdlib compilation mode, skipping redeclaration", variant_name);
                                variant_value++;
                                continue;
                            }
                        }

                        // Attempt to declare the variant
                        if (!_symbol_table->declare_symbol(variant_name, int_type, variant->location(), false))
                        {
                            // Declaration failed - only report error if not in stdlib mode
                            if (!_stdlib_compilation_mode)
                            {
                                // Use VariableDeclaration as NodeKind since enum variants are essentially constant variables
                                _diagnostic_builder->create_redefined_symbol_error(variant_name, NodeKind::VariableDeclaration, variant->location());
                            }
                            // In stdlib mode, silently skip all redefinition errors for enum variants
                        }
                        variant_value++;
                    }
                }
            }
            return;
        }

        // If not found or has null type, this is a fallback for cases where
        // populate_symbol_table didn't handle it properly
        if (existing_symbol)
        {
            // In stdlib compilation mode, be more lenient with enum redefinitions
            if (_stdlib_compilation_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "Enum '{}' already exists in stdlib compilation mode, skipping redeclaration", enum_name);
                return;
            }
            _diagnostic_builder->create_redefined_symbol_error(enum_name, NodeKind::EnumDeclaration, node.location());
            return;
        }

        // Collect generic parameter names for validation
        std::vector<std::string> generic_param_names;
        for (const auto &param : node.generic_parameters())
        {
            if (param)
            {
                generic_param_names.push_back(param->name());
            }
        }

        // Track if we entered a generic context
        bool entered_generic_context = false;

        // Enter generic context if this is a generic enum
        if (!generic_param_names.empty())
        {
            enter_generic_context(enum_name, generic_param_names, node.location());
            entered_generic_context = true;
        }

        // Collect variant information
        std::vector<std::string> variant_names;
        bool is_simple_enum = node.is_simple_enum();

        for (const auto &variant : node.variants())
        {
            if (variant)
            {
                variant_names.push_back(variant->name());
                // Pass generic parameters to variant validation
                validate_enum_variant(*variant, generic_param_names);
            }
        }

        // Create enum type and register it
        Type *enum_type = _type_context.get_enum_type(enum_name, variant_names, is_simple_enum);
        _symbol_table->declare_symbol(enum_name, enum_type, node.location(), false);

        // For simple enums (C-style), register each variant as a constant
        if (is_simple_enum)
        {
            int variant_value = 0;
            for (const auto &variant : node.variants())
            {
                if (variant)
                {
                    // Simple variants are essentially integer constants
                    Type *int_type = _type_context.get_int_type();
                    _symbol_table->declare_symbol(variant->name(), int_type, variant->location(), false);
                    variant_value++;
                }
            }
        }
        else
        {
            // For complex enums (Rust-style), variants are constructor functions
            for (const auto &variant : node.variants())
            {
                if (variant && !variant->is_simple_variant())
                {
                    // Create a function type for the variant constructor
                    std::vector<Type *> param_types;
                    for (const auto &type_str : variant->associated_types())
                    {
                        // Check if this is a generic parameter
                        if (std::find(generic_param_names.begin(), generic_param_names.end(), type_str) != generic_param_names.end())
                        {
                            // It's a generic parameter - create a generic type placeholder
                            Type *generic_type = _type_context.get_generic_type(type_str);
                            param_types.push_back(generic_type);
                        }
                        else
                        {
                            // Resolve the type string to a Type* object through TypeContext
                            Type *param_type = lookup_type_by_name(type_str);
                            if (param_type)
                            {
                                param_types.push_back(param_type);
                            }
                        }
                    }

                    Type *variant_constructor = _type_context.create_function_type(enum_type, param_types);
                    _symbol_table->declare_symbol(variant->name(), variant_constructor, variant->location(), false);
                }
            }
        }

        // Exit generic context if we entered one
        if (entered_generic_context)
        {
            exit_generic_context();
        }

        // EnumDeclarationNode is a DeclarationNode, not ExpressionNode, so no set_type call needed
    }

    void TypeChecker::visit(EnumVariantNode &node)
    {
        // Individual variant processing - validation is now done in EnumDeclarationNode
        // This method is kept for compatibility but actual validation happens in validate_enum_variant
    }

    void TypeChecker::validate_enum_variant(EnumVariantNode &node, const std::vector<std::string> &generic_param_names)
    {
        // Individual variant processing - validation with generic parameter context
        if (!node.is_simple_variant())
        {
            // Validate associated types for complex variants
            for (const auto &type_str : node.associated_types())
            {
                // Check if this is a generic parameter first
                if (std::find(generic_param_names.begin(), generic_param_names.end(), type_str) != generic_param_names.end())
                {
                    // It's a valid generic parameter - no further validation needed
                    continue;
                }

                // Otherwise, validate as a concrete type
                Type *type = lookup_type_by_name(type_str);
                if (!type || type->kind() == TypeKind::Unknown)
                {
                    _diagnostic_builder->create_undefined_symbol_error(type_str, NodeKind::Declaration, node.location());
                }
            }
        }
    }

    void TypeChecker::visit(ImplementationBlockNode &node)
    {
        // Get target type name as string
        std::string target_type_name = node.target_type();
        Type *target_type = nullptr;
        bool is_primitive_type = false;

        if (!target_type_name.empty())
        {
            // Handle generic types like "Option<T>" by extracting the base type name
            std::string base_type_name = target_type_name;
            size_t generic_start = target_type_name.find('<');
            if (generic_start != std::string::npos)
            {
                base_type_name = target_type_name.substr(0, generic_start);
            }

            // Check if it's a primitive type first
            if (base_type_name == "string" || base_type_name == "int" || base_type_name == "i8" ||
                base_type_name == "i16" || base_type_name == "i32" || base_type_name == "i64" ||
                base_type_name == "uint" || base_type_name == "u8" || base_type_name == "u16" ||
                base_type_name == "u32" || base_type_name == "u64" || base_type_name == "float" ||
                base_type_name == "f32" || base_type_name == "f64" || base_type_name == "double" ||
                base_type_name == "boolean" || base_type_name == "char" ||
                base_type_name == "void")
            {
                is_primitive_type = true;
                // Create a primitive type using type context
                target_type = lookup_type_by_name(base_type_name);
            }
            else
            {
                // Look up the base type in symbol table
                target_type = lookup_variable_type(base_type_name);
                if (!target_type)
                {
                    _diagnostic_builder->create_undefined_symbol_error(base_type_name, NodeKind::Declaration, node.location());
                    return;
                }

                // Verify it's a struct, class, enum, or trait type
                if (target_type->kind() != TypeKind::Struct &&
                    target_type->kind() != TypeKind::Class &&
                    target_type->kind() != TypeKind::Enum &&
                    target_type->kind() != TypeKind::Trait)
                {
                    _diagnostic_builder->create_invalid_operation_error("implementation block", target_type, nullptr, node.location());
                    return;
                }
            }
        }

        // Save previous struct type and struct name
        Type *previous_struct_type = _current_struct_type;
        std::string previous_struct_name = _current_struct_name;
        _current_struct_type = target_type;
        _current_struct_name = target_type_name;

        // Enter implementation scope
        enter_scope();

        // Process all methods in the implementation block
        for (const auto &method : node.method_implementations())
        {
            if (method)
            {
                method->accept(*this);
            }
        }

        // Exit implementation scope
        exit_scope();

        // Restore previous struct type and name
        _current_struct_type = previous_struct_type;
        _current_struct_name = previous_struct_name;
    }

    void TypeChecker::visit(ExternBlockNode &node)
    {
        // Process all function declarations in the extern block
        for (const auto &function : node.function_declarations())
        {
            if (function)
            {
                function->accept(*this);
            }
        }
    }

    void TypeChecker::visit(GenericParameterNode &node)
    {
        std::string param_name = node.name();

        // Check for redefinition within the same generic parameter list
        if (_symbol_table->lookup_symbol(param_name))
        {
            _diagnostic_builder->create_redefined_symbol_error(param_name, NodeKind::Declaration, node.location());
            return;
        }

        // Register generic parameter as a type parameter
        // For now, treat it as a placeholder type
        Type *generic_type = _type_context.get_generic_type(param_name);
        _symbol_table->declare_symbol(param_name, generic_type, node.location(), false);

        node.set_type("generic<" + param_name + ">");
    }

    void TypeChecker::visit(StructFieldNode &node)
    {
        std::string field_name = node.name();

        // Check for redefinition within the same struct
        if (_symbol_table->lookup_symbol(field_name))
        {
            _diagnostic_builder->create_redefined_symbol_error(field_name, NodeKind::VariableDeclaration, node.location());
            return;
        }

        // Get field type from resolved type (avoid deprecated string methods)
        Type *field_type = node.get_resolved_type();
        std::string field_type_str = "unknown";

        // CRITICAL: Safely get type name - the pointer might be corrupt
        if (field_type)
        {
            try
            {
                field_type_str = field_type->name();
            }
            catch (...)
            {
                LOG_ERROR(Cryo::LogComponent::AST, "ERROR: Corrupt type pointer for field '{}', attempting to re-resolve from annotation", field_name);
                field_type = nullptr; // Mark as needing resolution
            }
        }

        // If field type is unknown or corrupt, try to resolve it from the type annotation
        if (!field_type || field_type->kind() == TypeKind::Unknown || field_type_str.empty())
        {
            std::string type_annotation = node.type_annotation();
            LOG_DEBUG(Cryo::LogComponent::AST, "Re-resolving field '{}' type from annotation: {}", field_name, type_annotation);

            field_type = resolve_type_with_generic_context(type_annotation);
            if (field_type && field_type->kind() != TypeKind::Unknown)
            {
                node.set_resolved_type(field_type);
                field_type_str = field_type->name();
                LOG_DEBUG(Cryo::LogComponent::AST, "Successfully re-resolved field type to: {}", field_type_str);
            }
        }

        // Debug: Check generic context state when processing struct fields
        LOG_DEBUG(Cryo::LogComponent::AST, "Processing struct field '{}' with resolved type: {}, is_in_generic_context: {}, generic_context_stack_size: {}",
                  field_name, field_type_str, is_in_generic_context(), _generic_context_stack.size());

        if (field_type && field_type->kind() != TypeKind::Unknown)
        {
            // Register field in current scope (struct/class scope)
            _symbol_table->declare_symbol(field_name, field_type, node.location(), node.is_mutable());

            // Store field information for later member access resolution
            if (!_current_struct_name.empty())
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "StructField registration check for '{}': current_struct='{}', field_type={}, kind={}",
                          field_name, _current_struct_name,
                          field_type ? field_type->name() : "null",
                          field_type ? static_cast<int>(field_type->kind()) : -1);

                _struct_fields[_current_struct_name][field_name] = field_type;
                LOG_DEBUG(Cryo::LogComponent::AST, "Registered struct field '{}' of type '{}' in struct/class '{}'",
                          field_name, field_type->name(), _current_struct_name);
            }

            node.set_type(field_type_str);
        }
        else
        {
            _diagnostic_builder->create_undefined_symbol_error(field_type_str, NodeKind::Declaration, node.location());
            node.set_type("unknown");
        }
    }

    void TypeChecker::visit(StructMethodNode &node)
    {
        std::string method_name = node.name();

        // Check if this is a constructor (method name matches the current struct/class name)
        bool is_constructor = !_current_struct_name.empty() && method_name == _current_struct_name;

        // Check for redefinition within the same struct/class (but allow constructors)
        if (!is_constructor && _symbol_table->lookup_symbol(method_name))
        {
            // Check if this method was already registered during signature registration
            bool is_method_already_registered = false;
            LOG_DEBUG(Cryo::LogComponent::AST, "Found existing symbol '{}', current_struct_name='{}', checking method registries...", 
                     method_name, _current_struct_name);
            if (!_current_struct_name.empty())
            {
                // Check if this method was already registered in the method registries
                auto public_methods_it = _struct_methods.find(_current_struct_name);
                auto private_methods_it = _private_struct_methods.find(_current_struct_name);
                
                bool found_in_public = (public_methods_it != _struct_methods.end() && 
                                      public_methods_it->second.find(method_name) != public_methods_it->second.end());
                bool found_in_private = (private_methods_it != _private_struct_methods.end() && 
                                       private_methods_it->second.find(method_name) != private_methods_it->second.end());
                
                LOG_DEBUG(Cryo::LogComponent::AST, "StructMethod lookup for '{}': found_in_public={}, found_in_private={}", 
                         method_name, found_in_public, found_in_private);
                
                if (found_in_public || found_in_private)
                {
                    is_method_already_registered = true;
                    LOG_DEBUG(Cryo::LogComponent::AST, "Suppressing redefinition error for struct method '{}' in '{}' - already registered during signature phase", 
                             method_name, _current_struct_name);
                }
            }
            
            if (!is_method_already_registered)
            {
                _diagnostic_builder->create_redefined_symbol_error(method_name, NodeKind::FunctionDeclaration, node.location());
                return;
            }
        }

        // Special handling for constructors to avoid name conflicts with struct/class type
        if (is_constructor)
        {
            // Process constructor parameters and body manually to avoid registering
            // the constructor function with the same name as the struct/class type

            const std::string &func_name = node.name();

            // Parse return type from node annotation (constructors have void return typically)
            Type *return_type = node.get_resolved_return_type();
            const std::string &return_type_str = return_type ? return_type->to_string() : "void";

            if (!return_type)
            {
                _diagnostic_builder->create_undefined_symbol_error(return_type_str, NodeKind::Declaration, node.location());
                return_type = _type_context.get_unknown_type();
            }

            // Collect parameter types
            std::vector<Type *> param_types;
            for (const auto &param : node.parameters())
            {
                if (param)
                {
                    Type *param_type = param->get_resolved_type();
                    const std::string &param_type_str = param_type ? param_type->to_string() : "unknown";
                    if (!param_type)
                    {
                        _diagnostic_builder->create_undefined_symbol_error(param_type_str, NodeKind::Declaration, param->location());
                        param_type = _type_context.get_unknown_type();
                    }
                    param_types.push_back(param_type);
                }
            }

            // Create function type
            FunctionType *func_type = static_cast<FunctionType *>(
                _type_context.create_function_type(return_type, param_types));

            // DO NOT declare constructor function in symbol table to avoid name conflict
            // The struct/class type is already declared with this name

            // Enter function scope for parameter and body checking
            enter_scope();
            _in_function = true;
            _current_function_return_type = return_type;

            // Declare parameters in function scope
            for (const auto &param : node.parameters())
            {
                if (param)
                {
                    param->accept(*this);
                }
            }

            // Check function body
            if (node.body())
            {
                node.body()->accept(*this);
            }

            // Exit function scope
            _current_function_return_type = nullptr;
            _in_function = false;
            exit_scope();
        }
        else
        {
            // Delegate to FunctionDeclarationNode visitor for regular methods
            visit(static_cast<FunctionDeclarationNode &>(node));
        }

        // Store method information for member access resolution (if not already registered)
        if (!_current_struct_name.empty())
        {
            // Check if method is already registered (from signature registration pass)
            bool already_registered = false;
            if (node.visibility() == Visibility::Private)
            {
                auto struct_it = _private_struct_methods.find(_current_struct_name);
                if (struct_it != _private_struct_methods.end())
                {
                    auto method_it = struct_it->second.find(method_name);
                    already_registered = (method_it != struct_it->second.end());
                }
            }
            else
            {
                auto struct_it = _struct_methods.find(_current_struct_name);
                if (struct_it != _struct_methods.end())
                {
                    auto method_it = struct_it->second.find(method_name);
                    already_registered = (method_it != struct_it->second.end());
                }
            }

            if (!already_registered)
            {
                // Get the return type from the node
                Type *return_type = node.get_resolved_return_type();
                std::string return_type_str = return_type ? return_type->to_string() : "";
                if (return_type_str.empty() && is_constructor)
                {
                    return_type_str = "void"; // Constructors typically return void
                }

                if (return_type)
                {
                    // Build full function signature for method type checking
                    std::vector<Type *> param_types;
                    for (const auto &param : node.parameters())
                    {
                        if (param)
                        {
                            Type *param_type = param->get_resolved_type();
                            const std::string &param_type_str = param_type ? param_type->to_string() : "unknown";
                            if (param_type)
                            {
                                param_types.push_back(param_type);
                            }
                        }
                    }

                    // Create function type with full signature
                    FunctionType *func_type = static_cast<FunctionType *>(
                        _type_context.create_function_type(return_type, param_types));

                    // Register in appropriate registry based on visibility
                    LOG_DEBUG(Cryo::LogComponent::AST, "Method '{}' in struct '{}' has visibility: {}", method_name, _current_struct_name,
                              (node.visibility() == Visibility::Private ? "Private" : node.visibility() == Visibility::Public ? "Public"
                                                                                                                              : "Unknown"));
                    if (node.visibility() == Visibility::Private)
                    {
                        LOG_DEBUG(Cryo::LogComponent::AST, "Registering private method '{}' for struct '{}'", method_name, _current_struct_name);
                        _private_struct_methods[_current_struct_name][method_name] = func_type;
                    }
                    else
                    {
                        LOG_DEBUG(Cryo::LogComponent::AST, "Registering public method '{}' for struct '{}'", method_name, _current_struct_name);
                        _struct_methods[_current_struct_name][method_name] = func_type;
                    }
                }
            }
            else
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "Method '{}' in struct '{}' already registered, skipping duplicate registration", method_name, _current_struct_name);
            }
        }
    }

    //===----------------------------------------------------------------------===//
    // Type Inference Helpers
    //===----------------------------------------------------------------------===//

    Type *TypeChecker::infer_literal_type(const LiteralNode &node)
    {
        switch (node.literal_kind())
        {
        case TokenKind::TK_KW_TRUE:
        case TokenKind::TK_KW_FALSE:
            return _type_context.get_boolean_type();

        case TokenKind::TK_NUMERIC_CONSTANT:
        {
            const std::string &value = node.value();

            // Check for type suffixes first
            if (value.ends_with("u8"))
            {
                return _type_context.get_u8_type();
            }
            else if (value.ends_with("u16"))
            {
                return _type_context.get_u16_type();
            }
            else if (value.ends_with("u32"))
            {
                return _type_context.get_u32_type();
            }
            else if (value.ends_with("u64"))
            {
                return _type_context.get_u64_type();
            }
            else if (value.ends_with("i8"))
            {
                return _type_context.get_i8_type();
            }
            else if (value.ends_with("i16"))
            {
                return _type_context.get_i16_type();
            }
            else if (value.ends_with("i32"))
            {
                return _type_context.get_i32_type();
            }
            else if (value.ends_with("i64"))
            {
                return _type_context.get_i64_type();
            }
            else if (value.ends_with("f32"))
            {
                return _type_context.get_f32_type();
            }
            else if (value.ends_with("f64"))
            {
                return _type_context.get_f64_type();
            }
            else if (value.ends_with("usize"))
            {
                // Map usize to u64 for now
                return _type_context.get_u64_type();
            }
            else if (value.ends_with("isize"))
            {
                // Map isize to i64 for now
                return _type_context.get_i64_type();
            }

            // No suffix - use heuristic based on content
            if (value.find('.') != std::string::npos)
            {
                return _type_context.get_default_float_type();
            }
            else
            {
                return _type_context.get_int_type();
            }
        }

        case TokenKind::TK_STRING_LITERAL:
            return _type_context.get_string_type();

        case TokenKind::TK_CHAR_CONSTANT:
            return _type_context.get_char_type();

        case TokenKind::TK_KW_NULL:
            return _type_context.get_null_type(); // We need to implement this

        default:
            return _type_context.get_unknown_type();
        }
    }

    Type *TypeChecker::infer_binary_expression_type(const BinaryExpressionNode &node)
    {
        // Get operand types
        Type *lhs_type = nullptr;
        Type *rhs_type = nullptr;

        if (node.left() && node.left()->has_resolved_type())
        {
            lhs_type = node.left()->get_resolved_type();
        }

        if (node.right() && node.right()->has_resolved_type())
        {
            rhs_type = node.right()->get_resolved_type();
        }

        if (!lhs_type || !rhs_type)
            return _type_context.get_unknown_type();

        TokenKind op = node.operator_token().kind();

        // Comparison operators always return boolean
        if (op == TokenKind::TK_EQUALEQUAL || op == TokenKind::TK_EXCLAIMEQUAL ||
            op == TokenKind::TK_L_ANGLE || op == TokenKind::TK_R_ANGLE ||
            op == TokenKind::TK_LESSEQUAL || op == TokenKind::TK_GREATEREQUAL)
        {
            return _type_context.get_boolean_type();
        }

        // Logical operators
        if (op == TokenKind::TK_AMPAMP || op == TokenKind::TK_PIPEPIPE)
        {
            return _type_context.get_boolean_type();
        }

        // Arithmetic operators - return common type
        if (op == TokenKind::TK_PLUS || op == TokenKind::TK_MINUS ||
            op == TokenKind::TK_STAR || op == TokenKind::TK_SLASH ||
            op == TokenKind::TK_PERCENT)
        {
            return _type_context.get_common_type(lhs_type, rhs_type);
        }

        return _type_context.get_unknown_type();
    }

    //===----------------------------------------------------------------------===//
    // Type Compatibility Checking
    //===----------------------------------------------------------------------===//

    bool TypeChecker::check_assignment_compatibility(Type *lhs_type, Type *rhs_type, SourceLocation loc)
    {
        if (!lhs_type || !rhs_type)
            return false;

        // Early string-based compatibility checks for migration issues
        std::string lhs_str = lhs_type->to_string();
        std::string rhs_str = rhs_type->to_string();

        LOG_DEBUG(Cryo::LogComponent::AST, "Assignment compatibility check: {} = {} (TypeKinds: {} vs {})",
                  lhs_str, rhs_str,
                  std::to_string(static_cast<int>(lhs_type->kind())),
                  std::to_string(static_cast<int>(rhs_type->kind())));

        // SPECIAL CASE: Class/Struct assignment compatibility
        // Allow assignment between same class/struct types (e.g., HeapManager = HeapManager())
        if ((lhs_type->kind() == TypeKind::Class || lhs_type->kind() == TypeKind::Struct) &&
            (rhs_type->kind() == TypeKind::Class || rhs_type->kind() == TypeKind::Struct))
        {
            if (lhs_str == rhs_str)
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "Allowing class/struct assignment: {} = {}", lhs_str, rhs_str);
                return true;
            }
        }

        // SPECIAL CASE: Integer literal promotion to larger integer types
        // Allow integer literals (represented as 'int' type) to promote to larger integer types
        if (lhs_type->is_integral() && rhs_type->is_integral())
        {
            auto lhs_int = static_cast<IntegerType *>(lhs_type);
            auto rhs_int = static_cast<IntegerType *>(rhs_type);
            
            // Check if RHS is the default 'int' type (likely from a literal)
            // and LHS is a larger integer type - allow safe promotion
            if (rhs_str == "int" && lhs_int->size_bytes() >= rhs_int->size_bytes())
            {
                // Additional check: ensure we're not going from signed to unsigned
                // unless the target type is larger
                if (rhs_int->is_signed() == lhs_int->is_signed() || 
                    lhs_int->size_bytes() > rhs_int->size_bytes())
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "Allowing integer literal promotion: {} -> {}", rhs_str, lhs_str);
                    return true;
                }
            }
        }

        // Check if both are pointer types of the same base type (HeapBlock* = HeapBlock*)
        if (lhs_str.find("*") != std::string::npos && rhs_str.find("*") != std::string::npos)
        {
            // Extract base types
            std::string lhs_base = lhs_str.substr(0, lhs_str.find("*"));
            std::string rhs_base = rhs_str.substr(0, rhs_str.find("*"));

            LOG_DEBUG(Cryo::LogComponent::AST, "Checking pointer assignment: {} = {} (bases: '{}' vs '{}')", lhs_str, rhs_str, lhs_base, rhs_base);

            if (lhs_base == rhs_base)
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "Allowing assignment between same pointer types: {} = {}", lhs_str, rhs_str);
                return true;
            }
        }

        // Check float-to-float assignments (string fallback for migration issues)
        if ((lhs_str == "f64" && (rhs_str == "float" || rhs_str == "f32" || rhs_str == "f64")) ||
            (lhs_str == "f32" && (rhs_str == "float" || rhs_str == "f32" || rhs_str == "f64")) ||
            (lhs_str == "float" && (rhs_str == "f64" || rhs_str == "f32" || rhs_str == "float")))
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Allowing float assignment: {} = {} (string fallback)", lhs_str, rhs_str);
            return true;
        }

        // Check if RHS is void* and LHS is any pointer type
        if (rhs_str == "void*" && lhs_str.find("*") != std::string::npos)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Allowing implicit conversion from void* to {} (string fallback)", lhs_str);
            return true;
        }

        // Special case: null can be assigned to any nullable type (pointers, optionals, etc.)
        if (rhs_type->kind() == TypeKind::Null && lhs_type->is_nullable())
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Allowing null assignment to nullable type {}", lhs_type->to_string());
            return true;
        }

        // Special case: Also check string-based null detection as fallback for migration issues
        if (rhs_type->to_string() == "null" && (lhs_type->is_nullable() || lhs_type->to_string().find("*") != std::string::npos))
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Allowing null assignment to pointer type {} (string fallback)", lhs_type->to_string());
            return true;
        }

        // Special case: &T[] (reference to array) can be assigned to T* (pointer to element type)
        if (lhs_type->kind() == TypeKind::Pointer && rhs_type->kind() == TypeKind::Reference)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Checking reference-to-pointer conversion: {} = {}", lhs_str, rhs_str);
            auto lhs_ptr = static_cast<PointerType *>(lhs_type);
            auto rhs_ref = static_cast<ReferenceType *>(rhs_type);
            
            auto referenced_type = rhs_ref->referent_type();
            auto pointer_pointee = lhs_ptr->pointee_type();
            
            // Check if RHS is a reference to an array type (special case for array-to-pointer)
            if (referenced_type && referenced_type->kind() == TypeKind::Array)
            {
                auto array_type = static_cast<ArrayType *>(referenced_type.get());
                auto element_type = array_type->element_type();
                
                // Check if the pointer's pointee type matches the array's element type
                if (element_type && pointer_pointee && 
                    element_type->to_string() == pointer_pointee->to_string())
                {
                    LOG_DEBUG(Cryo::LogComponent::AST, "Allowing array-to-pointer conversion: &{}[] = {}*", 
                             element_type->to_string(), pointer_pointee->to_string());
                    return true;
                }
            }
            // Check if RHS is a reference to the same type as the pointer points to (general case)
            else if (referenced_type && pointer_pointee && 
                     referenced_type->to_string() == pointer_pointee->to_string())
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "Allowing reference-to-pointer conversion: &{} = {}*", 
                         referenced_type->to_string(), pointer_pointee->to_string());
                return true;
            }
        }

        // Special case: void* can be implicitly converted to any pointer type
        if (rhs_type->kind() == TypeKind::Pointer && lhs_type->kind() == TypeKind::Pointer)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Both types are pointers, checking for void* conversion");
            auto rhs_ptr = static_cast<PointerType *>(rhs_type);
            auto lhs_ptr = static_cast<PointerType *>(lhs_type);

            // Check if RHS is void* (void pointer can convert to any pointer)
            auto rhs_pointee = rhs_ptr->pointee_type();
            LOG_DEBUG(Cryo::LogComponent::AST, "RHS pointee type: {} (kind={})",
                      (rhs_pointee ? rhs_pointee->to_string() : "NULL"),
                      (rhs_pointee ? std::to_string(static_cast<int>(rhs_pointee->kind())) : "N/A"));

            if (rhs_pointee && rhs_pointee->kind() == TypeKind::Void)
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "Allowing implicit conversion from void* to {}", lhs_type->to_string());
                return true;
            }
            else
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "RHS is not void*, cannot do implicit conversion");
            }
        }

        // Check if the assignment is valid
        bool is_assignable = lhs_type->is_assignable_from(*rhs_type);

        // If direct assignment is not allowed, check for explicit conversions with warnings
        if (!is_assignable && lhs_type->is_integral() && rhs_type->is_integral())
        {
            auto lhs_int = static_cast<IntegerType *>(lhs_type);
            auto rhs_int = static_cast<IntegerType *>(rhs_type);

            Type::ConversionSafety safety = rhs_int->get_conversion_safety(*lhs_int);

            switch (safety)
            {
            case Type::ConversionSafety::Safe:
                // This should have been caught by is_assignable_from, but allow anyway
                return true;

            case Type::ConversionSafety::Warning:
            {
                // Emit warning but allow the conversion
                size_t lhs_size = lhs_type->size_bytes();
                size_t rhs_size = rhs_type->size_bytes();
                bool lhs_signed = lhs_type->is_signed();
                bool rhs_signed = rhs_type->is_signed();

                if (lhs_size < rhs_size)
                {
                    // Narrowing conversion
                    report_conversion_warning(
                        TypeWarning::WarningKind::PotentialDataLoss, loc,
                        "Narrowing conversion may lose data",
                        rhs_type, lhs_type);
                }
                else if (lhs_signed != rhs_signed)
                {
                    // Sign conversion
                    report_conversion_warning(
                        TypeWarning::WarningKind::SignConversion, loc,
                        "Converting between signed and unsigned integers",
                        rhs_type, lhs_type);
                }
                return true; // Allow with warning
            }

            case Type::ConversionSafety::Unsafe:
                // Mixed sign and size changes - emit warning but still allow
                report_conversion_warning(
                    TypeWarning::WarningKind::UnsafeConversion, loc,
                    "Unsafe conversion: mixed sign and size change",
                    rhs_type, lhs_type);
                return true; // Allow with warning

            case Type::ConversionSafety::Impossible:
                // Fall through to return false
                break;
            }
        }

        // Check for float-to-int and int-to-float conversions
        if (!is_assignable &&
            ((lhs_type->is_integral() && rhs_type->kind() == TypeKind::Float) ||
             (lhs_type->kind() == TypeKind::Float && rhs_type->is_integral()) ||
             (lhs_type->kind() == TypeKind::Float && rhs_type->kind() == TypeKind::Float)))
        {
            // Allow float-to-int and int-to-float conversions with warnings
            if (lhs_type->is_integral() && rhs_type->kind() == TypeKind::Float)
            {
                // float to int - may lose precision
                report_conversion_warning(
                    TypeWarning::WarningKind::PotentialDataLoss, loc,
                    "Converting float to int may lose precision and fractional part",
                    rhs_type, lhs_type);
                return true;
            }
            else if (lhs_type->kind() == TypeKind::Float && rhs_type->is_integral())
            {
                // int to float - generally safe but may lose precision for very large integers
                if (rhs_type->size_bytes() > 4) // int64 to float32 could lose precision
                {
                    report_conversion_warning(
                        TypeWarning::WarningKind::PotentialDataLoss, loc,
                        "Converting large integer to float may lose precision",
                        rhs_type, lhs_type);
                }
                return true;
            }
            else if (lhs_type->kind() == TypeKind::Float && rhs_type->kind() == TypeKind::Float)
            {
                // float to float conversion - allow different precision levels
                // Check if we need to cast default float (0.0) to f64
                if (auto lhs_float = dynamic_cast<FloatType *>(lhs_type))
                {
                    if (auto rhs_float = dynamic_cast<FloatType *>(rhs_type))
                    {
                        // Allow default float literals to be assigned to f64
                        if ((lhs_float->float_kind() == FloatKind::F64 && rhs_float->float_kind() == FloatKind::Float) ||
                            (lhs_float->float_kind() == FloatKind::Float && rhs_float->float_kind() == FloatKind::F64) ||
                            (lhs_float->float_kind() == rhs_float->float_kind()))
                        {
                            return true;
                        }
                    }
                }
                return true;
            }
        }

        return is_assignable;
    }

    bool TypeChecker::check_binary_operation_compatibility(TokenKind op, Type *lhs_type, Type *rhs_type, SourceLocation loc)
    {
        if (!lhs_type || !rhs_type)
            return false;

        // Comparison operators
        if (op == TokenKind::TK_EQUALEQUAL || op == TokenKind::TK_EXCLAIMEQUAL)
        {
            return _type_context.are_types_compatible(lhs_type, rhs_type);
        }

        if (op == TokenKind::TK_L_ANGLE || op == TokenKind::TK_R_ANGLE ||
            op == TokenKind::TK_LESSEQUAL || op == TokenKind::TK_GREATEREQUAL)
        {
            return is_comparable_type(lhs_type) && is_comparable_type(rhs_type) &&
                   _type_context.are_types_compatible(lhs_type, rhs_type);
        }

        // Logical operators
        if (op == TokenKind::TK_AMPAMP || op == TokenKind::TK_PIPEPIPE)
        {
            return is_boolean_context_valid(lhs_type) && is_boolean_context_valid(rhs_type);
        }

        // Arithmetic operators
        if (op == TokenKind::TK_PLUS || op == TokenKind::TK_MINUS ||
            op == TokenKind::TK_STAR || op == TokenKind::TK_SLASH)
        {
            return is_numeric_type(lhs_type) && is_numeric_type(rhs_type);
        }

        // Modulo operator - integers only
        if (op == TokenKind::TK_PERCENT)
        {
            return is_integral_type(lhs_type) && is_integral_type(rhs_type);
        }

        return false;
    }

    bool TypeChecker::check_function_call_compatibility(FunctionType *func_type,
                                                        const std::vector<Type *> &arg_types,
                                                        SourceLocation loc)
    {
        std::string func_name = func_type ? func_type->name() : "unknown";
        bool is_variadic = func_type ? func_type->is_variadic() : false;

        LOG_DEBUG(Cryo::LogComponent::AST, "check_function_call_compatibility: function={}, is_variadic={}, arg_count={}",
                  func_name, is_variadic, arg_types.size());

        // Special debug output for printf
        if (func_name.find("printf") != std::string::npos || func_name == "(string, ...) -> i32")
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "PRINTF DEBUG: func_type->is_variadic()={}, param_types.size()={}",
                      is_variadic, func_type ? func_type->parameter_types().size() : 0);
        }

        if (!func_type)
            return false;

        const auto &param_types = func_type->parameter_types();
        LOG_DEBUG(Cryo::LogComponent::AST, "check_function_call_compatibility: param_types.size()={}", param_types.size()); // For variadic functions, we need at least the required (non-variadic) parameters
        size_t required_params = param_types.size();
        if (func_type->is_variadic() && required_params > 0)
        {
            // For variadic functions, the last parameter represents the variadic part
            // So we need at least (param_types.size() - 1) arguments for the fixed parameters
            // But we allow equal to param_types.size() for the case where there's one variadic arg
            required_params = param_types.size() - 1;
        }

        // Check minimum argument count
        LOG_DEBUG(Cryo::LogComponent::AST, "check_function_call_compatibility: required_params={}, arg_types.size()={}", required_params, arg_types.size());
        if (arg_types.size() < required_params)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "check_function_call_compatibility: FAILED - too few arguments");
            if (_diagnostic_manager)
            {
                SourceRange range(loc);
                _diagnostic_manager->create_error(ErrorCode::E0216_TOO_FEW_ARGS, range, _source_file);
            }
            else
            {
                _diagnostic_builder->create_too_many_args_error("function", required_params, arg_types.size(), loc);
            }
            return false;
        }

        // Check maximum argument count for non-variadic functions
        if (arg_types.size() > param_types.size() && !func_type->is_variadic())
        {
            if (_diagnostic_manager)
            {
                SourceRange range(loc);
                _diagnostic_manager->create_error(ErrorCode::E0215_TOO_MANY_ARGS, range, _source_file);
            }
            else
            {
                _diagnostic_builder->create_too_many_args_error("function", param_types.size(), arg_types.size(), loc);
            }
            return false;
        }

        // Check argument types for the fixed parameters
        size_t fixed_params = func_type->is_variadic() ? param_types.size() - 1 : param_types.size();
        for (size_t i = 0; i < fixed_params && i < arg_types.size(); ++i)
        {
            if (!check_assignment_compatibility(param_types[i].get(), arg_types[i], loc))
            {
                report_type_mismatch(loc, param_types[i].get(), arg_types[i],
                                     "function argument " + std::to_string(i + 1));
                return false;
            }
        }

        // For variadic functions, the remaining arguments are not type-checked against specific parameter types
        // They will be handled at the call site (usually by the runtime or specific intrinsic handling)

        LOG_DEBUG(Cryo::LogComponent::AST, "check_function_call_compatibility: SUCCESS - all checks passed");
        return true;
    }

    //===----------------------------------------------------------------------===//
    // Error Reporting
    //===----------------------------------------------------------------------===//

    void TypeChecker::report_error(TypeError::ErrorKind kind, SourceLocation loc, const std::string &message)
    {
        // If we have a diagnostic manager, use it with enhanced error reporting
        if (_diagnostic_manager)
        {
            ErrorCode error_code = ErrorCode::E0805_INTERNAL_ERROR;

            // Map TypeError::ErrorKind to ErrorCode for enhanced reporting
            switch (kind)
            {
            case TypeError::ErrorKind::TypeMismatch:
                error_code = ErrorCode::E0200_TYPE_MISMATCH;
                break;
            case TypeError::ErrorKind::UndefinedVariable:
                error_code = ErrorCode::E0201_UNDEFINED_VARIABLE;
                break;
            case TypeError::ErrorKind::UndefinedFunction:
                error_code = ErrorCode::E0202_UNDEFINED_FUNCTION;
                break;
            case TypeError::ErrorKind::RedefinedSymbol:
                error_code = ErrorCode::E0205_REDEFINED_SYMBOL;
                break;
            case TypeError::ErrorKind::InvalidOperation:
                error_code = ErrorCode::E0209_INVALID_OPERATION;
                break;
            case TypeError::ErrorKind::InvalidAssignment:
                error_code = ErrorCode::E0210_INVALID_ASSIGNMENT;
                break;
            case TypeError::ErrorKind::InvalidCast:
                error_code = ErrorCode::E0208_INVALID_CAST;
                break;
            case TypeError::ErrorKind::IncompatibleTypes:
                error_code = ErrorCode::E0211_INCOMPATIBLE_TYPES;
                break;
            case TypeError::ErrorKind::TooManyArguments:
                error_code = ErrorCode::E0215_TOO_MANY_ARGS;
                break;
            case TypeError::ErrorKind::TooFewArguments:
                error_code = ErrorCode::E0216_TOO_FEW_ARGS;
                break;
            case TypeError::ErrorKind::NonCallableType:
                error_code = ErrorCode::E0213_NON_CALLABLE;
                break;
            case TypeError::ErrorKind::VoidValueUsage:
                error_code = ErrorCode::E0212_VOID_VALUE_USED;
                break;
            default:
                error_code = ErrorCode::E0805_INTERNAL_ERROR;
                break;
            }

            SourceRange range(loc);
            _diagnostic_manager->create_error(error_code, range, _source_file, message);
            // Also add to internal errors for has_errors() check
            _errors.emplace_back(kind, loc, message);
        }
        else
        {
            // Fallback to old behavior if no diagnostic manager
            _errors.emplace_back(kind, loc, message);
        }
    }

    void TypeChecker::report_error(TypeError::ErrorKind kind, SourceLocation loc, const std::string &message, ASTNode *node)
    {
        // Check if this node already has an error reported to prevent duplicates
        if (node && node->has_error())
        {
            return; // Skip duplicate error reporting
        }

        // Mark this node as having an error
        if (node)
        {
            node->mark_error();
        }

        // Delegate to the original report_error method
        report_error(kind, loc, message);
    }

    void TypeChecker::report_warning(TypeWarning::WarningKind kind, SourceLocation loc, const std::string &message)
    {
        _warnings.emplace_back(kind, loc, message);
    }

    void TypeChecker::report_conversion_warning(TypeWarning::WarningKind kind, SourceLocation loc,
                                                const std::string &message, Type *from, Type *to)
    {
        _warnings.emplace_back(kind, loc, message, from, to);
    }

    void TypeChecker::report_type_mismatch(SourceLocation loc, Type *expected, Type *actual, const std::string &context)
    {
        if (_diagnostic_manager)
        {
            // ENHANCED: Create sophisticated multi-span diagnostics like Rust
            SourceRange range(loc);

            // Get the actual type name for error reporting and fallback estimation
            std::string actual_name = actual ? actual->to_string() : "unknown";

            // Get the actual source text to determine accurate token length
            size_t token_width = 1;

            // Try to get the actual source line and extract the token at the location
            try
            {
                const SourceManager &source_mgr = _diagnostic_manager->source_manager();
                std::string source_line = source_mgr.get_line_text(_source_file, loc.line());

                if (!source_line.empty() && loc.column() <= source_line.length())
                {
                    size_t start_col = loc.column() - 1; // Convert to 0-based indexing

                    // Find the end of the token at this position
                    if (start_col < source_line.length())
                    {
                        char start_char = source_line[start_col];
                        size_t end_col = start_col;

                        if (start_char == '"')
                        {
                            // String literal - find closing quote
                            end_col = start_col + 1;
                            while (end_col < source_line.length() && source_line[end_col] != '"')
                            {
                                if (source_line[end_col] == '\\' && end_col + 1 < source_line.length())
                                {
                                    end_col += 2; // Skip escaped character
                                }
                                else
                                {
                                    end_col++;
                                }
                            }
                            if (end_col < source_line.length())
                                end_col++; // Include closing quote
                        }
                        else if (std::isalnum(start_char) || start_char == '_')
                        {
                            // Identifier or number - continue until non-alphanumeric
                            while (end_col < source_line.length() &&
                                   (std::isalnum(source_line[end_col]) || source_line[end_col] == '_' || source_line[end_col] == '.'))
                            {
                                end_col++;
                            }
                        }
                        else
                        {
                            // Single character token
                            end_col = start_col + 1;
                        }

                        token_width = end_col - start_col;
                    }
                }
            }
            catch (...)
            {
                // Fall back to simple estimation if source reading fails
                if (actual_name == "string")
                {
                    token_width = 8; // Approximate for string literal
                }
                else if (actual_name == "int" || actual_name == "float")
                {
                    token_width = 3; // Approximate for numeric literals
                }
                else if (actual_name == "boolean")
                {
                    token_width = 4; // "true" or "false"
                }
                else if (actual_name.find("Config") != std::string::npos)
                {
                    token_width = 6; // "config"
                }
                else
                {
                    token_width = std::max(static_cast<size_t>(3), actual_name.length());
                }
            }

            // Ensure minimum width of 1
            token_width = std::max(static_cast<size_t>(1), token_width);

            // Create primary span with accurate width for proper underlining
            SourceLocation end_loc(loc.line(), loc.column() + token_width - 1);
            SourceSpan value_span(loc, end_loc, _source_file, true);

            // Generate sophisticated inline labels
            std::string expected_name = expected ? expected->to_string() : "unknown";

            std::string inline_label = "expected `" + expected_name + "`, found `" + actual_name + "`";
            value_span.set_label(inline_label);

            // Create enhanced diagnostic with proper message and type context
            std::string message = "type mismatch in " + context;

            // Create context for structured payload
            std::any type_context = std::make_pair(expected, actual);

            auto &diagnostic = _diagnostic_manager->create_diagnostic(ErrorCode::E0200_TYPE_MISMATCH,
                                                                      value_span.to_source_range(),
                                                                      _source_file,
                                                                      message,
                                                                      type_context);

            // Add primary span with the smart label (this preserves the label information)
            diagnostic.with_primary_span(value_span);

            // For variable declarations, create a better secondary span for type annotation
            if (context == "variable initialization")
            {
                // For now, skip the secondary span since we don't have accurate parser info
                // The primary span with the clear error message is sufficient
                // TODO: Add proper type annotation span tracking in the parser
            }

            // Add context-aware suggestions based on the types - TEMPORARILY DISABLED
            /*
            if (expected_name == "int" && actual_name == "string")
            {
                diagnostic.add_help("if you meant to create a string variable, remove the type annotation");
                diagnostic.add_help("if you want to convert string to integer, try parsing");
                diagnostic.add_help("string literals cannot be implicitly converted to integers");
            }
            else if (expected_name == "string" && actual_name == "int")
            {
                diagnostic.add_help("try `value.to_string()` to convert the integer");
                diagnostic.add_help("use string interpolation: `\"${value}\"`");
            }
            else if (expected_name == "boolean" && (actual_name == "int" || actual_name == "float"))
            {
                diagnostic.add_help("use comparison operators like `value != 0` or `value > 0`");
                diagnostic.add_help("numeric types don't implicitly convert to boolean in Cryo");
            }
            else if (expected_name.find("*") != std::string::npos && actual_name.find("*") == std::string::npos)
            {
                diagnostic.add_help("use address-of operator `&` to get a pointer");
                diagnostic.add_help("pointer and value types are not directly compatible");
            }
            else if (expected_name.find("*") == std::string::npos && actual_name.find("*") != std::string::npos)
            {
                diagnostic.add_help("use dereference operator `*` to get the value");
                diagnostic.add_help("use address-of (&) or dereference (*) operators as appropriate");
            }
            */

            // Diagnostic is automatically stored by create_error, no emit() needed
        }
        else
        {
            // Fallback to old behavior
            std::string expected_type = expected ? expected->to_string() : "unknown";
            std::string actual_type = actual ? actual->to_string() : "unknown";

            std::string message = "Type mismatch in " + context + "\n" +
                                  "Cannot assign '" + actual_type + "' to '" + expected_type + "'";
            _errors.emplace_back(TypeError::ErrorKind::TypeMismatch, loc, message, expected, actual);
        }
    }

    void TypeChecker::report_undefined_symbol(SourceLocation loc, const std::string &symbol_name)
    {
        std::string message = "Undefined symbol '" + symbol_name + "'";
        _errors.emplace_back(TypeError::ErrorKind::UndefinedVariable, loc, message);
        LOG_DEBUG(Cryo::LogComponent::AST, "TypeChecker::report_undefined_symbol - Added error for '{}', total errors: {}", symbol_name, _errors.size());
    }

    void TypeChecker::report_redefined_symbol(SourceLocation loc, const std::string &symbol_name)
    {
        std::string message = "Symbol '" + symbol_name + "' is already defined";
        _errors.emplace_back(TypeError::ErrorKind::RedefinedSymbol, loc, message);
    }

    //===----------------------------------------------------------------------===//
    // Symbol Table Helpers
    //===----------------------------------------------------------------------===//

    void TypeChecker::enter_scope()
    {
        _symbol_table = _symbol_table->enter_scope();
    }

    void TypeChecker::exit_scope()
    {
        _symbol_table = _symbol_table->exit_scope();
    }

    bool TypeChecker::declare_variable(const std::string &name, Type *type, SourceLocation loc, bool is_mutable)
    {
        return _symbol_table->declare_symbol(name, type, loc, is_mutable);
    }

    bool TypeChecker::declare_function(const std::string &name, Type *type, SourceLocation loc, FunctionDeclarationNode *function_node)
    {
        return _symbol_table->declare_symbol(name, type, loc, function_node);
    }

    Type *TypeChecker::lookup_variable_type(const std::string &name)
    {
        TypedSymbol *symbol = _symbol_table->lookup_symbol(name);
        return symbol ? symbol->type : nullptr;
    }

    Type *TypeChecker::lookup_type_by_name(const std::string &type_name)
    {
        // Handle basic types using TypeContext specific methods
        if (type_name == "void")
            return _type_context.get_void_type();
        if (type_name == "boolean")
            return _type_context.get_boolean_type();
        if (type_name == "char")
            return _type_context.get_char_type();
        if (type_name == "string")
            return _type_context.get_string_type();

        // Integer types
        if (type_name == "i8")
            return _type_context.get_i8_type();
        if (type_name == "i16")
            return _type_context.get_i16_type();
        if (type_name == "i32")
            return _type_context.get_i32_type();
        if (type_name == "i64")
            return _type_context.get_i64_type();
        if (type_name == "int")
            return _type_context.get_int_type();

        // Unsigned integer types
        if (type_name == "u8")
            return _type_context.get_u8_type();
        if (type_name == "u16")
            return _type_context.get_u16_type();
        if (type_name == "u32")
            return _type_context.get_u32_type();
        if (type_name == "u64")
            return _type_context.get_u64_type();

        // Float types
        if (type_name == "f32")
            return _type_context.get_f32_type();
        if (type_name == "f64")
            return _type_context.get_f64_type();
        if (type_name == "float")
            return _type_context.get_default_float_type();
        if (type_name == "double")
            return _type_context.get_f64_type();

        // Check for pointer types (ends with '*')
        if (type_name.back() == '*')
        {
            std::string pointee_type = type_name.substr(0, type_name.length() - 1);
            Type *pointee = lookup_type_by_name(pointee_type);
            if (pointee)
            {
                return _type_context.create_pointer_type(pointee);
            }
        }

        // Check for reference types (ends with '&')
        if (type_name.back() == '&')
        {
            std::string referent_type = type_name.substr(0, type_name.length() - 1);
            Type *referent = lookup_type_by_name(referent_type);
            if (referent)
            {
                return _type_context.create_reference_type(referent);
            }
        }

        // Look up user-defined types (struct, class, enum)
        // First try as class type
        Type *class_type = _type_context.get_class_type(type_name);
        if (class_type && class_type->kind() != TypeKind::Unknown)
        {
            return class_type;
        }

        // Then try as struct type
        Type *struct_type = _type_context.get_struct_type(type_name);
        if (struct_type && struct_type->kind() != TypeKind::Unknown)
        {
            return struct_type;
        }

        // Fall back to symbol table lookup for backwards compatibility
        Type *user_type = lookup_variable_type(type_name);
        if (user_type)
        {
            return user_type;
        }

        // Try looking up as enum type
        Type *enum_type = _type_context.lookup_enum_type(type_name);
        if (enum_type)
        {
            return enum_type;
        }

        // Try looking up as type alias
        Type *alias_type = _type_context.lookup_type_alias(type_name);
        if (alias_type)
        {
            return alias_type;
        }

        // Fall back to unknown type
        return _type_context.get_unknown_type();
    }

    void TypeChecker::register_method_signature(StructMethodNode &node)
    {
        // Extract method information
        std::string method_name = node.name();
        bool is_constructor = node.is_constructor();
        
        LOG_DEBUG(Cryo::LogComponent::AST, "register_method_signature: method_name='{}', struct='{}', is_constructor={}", 
                  method_name, _current_struct_name, is_constructor);

        // Get return type or use struct name for constructors
        Type *return_type = node.get_resolved_return_type();

        if (!return_type && is_constructor)
        {
            // Constructors return void or their class type
            return_type = _type_context.get_void_type();
        }

        if (!return_type)
        {
            // Try to resolve return type from annotation
            std::string return_type_annotation = node.return_type_annotation();
            if (!return_type_annotation.empty())
            {
                return_type = resolve_type_with_generic_context(return_type_annotation);
                if (return_type)
                {
                    node.set_resolved_return_type(return_type);
                }
            }
        }

        if (!return_type)
        {
            // Default to void if no return type can be determined
            return_type = _type_context.get_void_type();
        }

        // Build parameter types for function signature
        std::vector<Type *> param_types;
        for (const auto &param : node.parameters())
        {
            if (param)
            {
                Type *param_type = param->get_resolved_type();
                if (!param_type)
                {
                    // Try to resolve parameter type from annotation
                    std::string param_type_annotation = param->type_annotation();
                    if (!param_type_annotation.empty())
                    {
                        param_type = resolve_type_with_generic_context(param_type_annotation);
                        if (param_type)
                        {
                            param->set_resolved_type(param_type);
                        }
                    }
                }

                if (param_type)
                {
                    param_types.push_back(param_type);
                }
            }
        }

        // Create function type
        FunctionType *func_type = static_cast<FunctionType *>(
            _type_context.create_function_type(return_type, param_types));

        // Register in appropriate registry based on visibility
        LOG_DEBUG(Cryo::LogComponent::AST, "Registering signature for method '{}' in struct '{}' with visibility: {}",
                  method_name, _current_struct_name,
                  (node.visibility() == Visibility::Private ? "Private" : node.visibility() == Visibility::Public ? "Public"
                                                                                                                  : "Unknown"));

        if (node.visibility() == Visibility::Private)
        {
            // Check if method already exists
            auto struct_it = _private_struct_methods.find(_current_struct_name);
            if (struct_it != _private_struct_methods.end()) {
                auto method_it = struct_it->second.find(method_name);
                if (method_it != struct_it->second.end()) {
                    LOG_DEBUG(Cryo::LogComponent::AST, "WARNING: Private method '{}' already exists for struct '{}' - DUPLICATE REGISTRATION", 
                              method_name, _current_struct_name);
                }
            }
            
            LOG_DEBUG(Cryo::LogComponent::AST, "Registering private method signature '{}' for struct '{}'",
                      method_name, _current_struct_name);
            _private_struct_methods[_current_struct_name][method_name] = func_type;
        }
        else
        {
            // Check if method already exists
            auto struct_it = _struct_methods.find(_current_struct_name);
            if (struct_it != _struct_methods.end()) {
                auto method_it = struct_it->second.find(method_name);
                if (method_it != struct_it->second.end()) {
                    LOG_DEBUG(Cryo::LogComponent::AST, "WARNING: Public method '{}' already exists for struct '{}' - DUPLICATE REGISTRATION", 
                              method_name, _current_struct_name);
                }
            }
            
            LOG_DEBUG(Cryo::LogComponent::AST, "Registering public method signature '{}' for struct '{}'",
                      method_name, _current_struct_name);
            _struct_methods[_current_struct_name][method_name] = func_type;
        }
    }

    //===----------------------------------------------------------------------===//
    // Utility Methods
    //===----------------------------------------------------------------------===//

    bool TypeChecker::is_numeric_type(Type *type)
    {
        return type && type->is_numeric();
    }

    bool TypeChecker::is_comparable_type(Type *type)
    {
        return type && (type->is_numeric() || type->kind() == TypeKind::String ||
                        type->kind() == TypeKind::Char);
    }

    bool TypeChecker::is_integral_type(Type *type)
    {
        return type && type->is_integral();
    }

    bool TypeChecker::is_boolean_context_valid(Type *type)
    {
        return type && (type->kind() == TypeKind::Boolean ||
                        type->is_numeric() ||
                        type->kind() == TypeKind::Pointer);
    }

    std::string TypeChecker::format_type_error(const std::string &context, Type *expected, Type *actual)
    {
        std::ostringstream oss;
        oss << "Type error in " << context << ": expected '"
            << (expected ? expected->to_string() : "unknown") << "', got '"
            << (actual ? actual->to_string() : "unknown") << "'";
        return oss.str();
    }

    //===----------------------------------------------------------------------===//
    // Match Statement Type Checking
    //===----------------------------------------------------------------------===//

    void TypeChecker::visit(MatchStatementNode &node)
    {
        // Visit the expression to match on
        if (node.expr())
        {
            node.expr()->accept(*this);
        }

        // Visit all match arms
        for (const auto &arm : node.arms())
        {
            if (arm)
            {
                arm->accept(*this);
            }
        }
    }

    void TypeChecker::visit(MatchArmNode &node)
    {
        // Visit the pattern
        if (node.pattern())
        {
            node.pattern()->accept(*this);
        }

        // Enter new scope for the match arm body
        enter_scope();

        // Visit the body
        if (node.body())
        {
            node.body()->accept(*this);
        }

        // Exit the scope
        exit_scope();
    }

    void TypeChecker::visit(PatternNode &node)
    {
        // Pattern type checking would go here
        // For now, just visit sub-patterns if they exist
        // Pattern matching type checking would involve:
        // - Checking pattern compatibility with matched expression type
        // - Binding pattern variables to their inferred types
        // - Ensuring exhaustiveness of match patterns
    }

    void TypeChecker::visit(EnumPatternNode &node)
    {
        // Enum pattern type checking
        const std::string &enum_name = node.enum_name();
        const std::string &variant_name = node.variant_name();

        // Look up the enum type
        TypedSymbol *enum_symbol = _symbol_table->lookup_symbol(enum_name);
        if (!enum_symbol)
        {
            _diagnostic_builder->create_undefined_symbol_error(enum_name, NodeKind::EnumDeclaration, node.location());
            return;
        }

        Type *enum_type = enum_symbol->type;
        if (!enum_type || enum_type->kind() != TypeKind::Enum)
        {
            _diagnostic_builder->create_assignment_type_error(_type_context.get_enum_type(enum_name, {}, false), enum_type, node.location());
            return;
        }

        // TODO: In a more complete implementation, we would:
        // - Verify that variant_name is a valid variant of the enum
        // - Get the parameter types for this specific variant

        // For now, bind pattern variables with inferred types based on common enum patterns
        const auto &bound_vars = node.bound_variables();

        for (const auto &var_name : bound_vars)
        {
            if (!var_name.empty())
            {
                // For enum patterns, we need to infer the type based on the enum variant
                // For now, we'll use float for most numeric patterns (Circle(radius), Rectangle(width, height), etc.)
                // TODO: Get actual parameter types from enum variant definition
                Type *var_type = _type_context.get_default_float_type();

                // Bind the variable in the current scope
                _symbol_table->declare_symbol(var_name, var_type, node.location(), false);
            }
        }
    }

    bool TypeChecker::is_primitive_integer_type(const std::string &type_name)
    {
        static const std::unordered_set<std::string> integer_types = {
            "i8", "i16", "i32", "i64", "int",
            "u8", "u16", "u32", "u64", "uint"};

        return integer_types.find(type_name) != integer_types.end();
    }
}
